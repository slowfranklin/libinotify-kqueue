/*******************************************************************************
  Copyright (c) 2011 Dmitry Matveev <me@dmitrymatveev.co.uk>

  Copyright 2008, 2013, 2014
      The Board of Trustees of the Leland Stanford Junior University
  Copyright (c) 2004, 2005, 2006
      by Internet Systems Consortium, Inc. ("ISC")
  Copyright (c) 1991, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
      2002, 2003 by The Internet Software Consortium and Rich Salz

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*******************************************************************************/

#include <unistd.h> /* read, write */
#include <errno.h>  /* EINTR */
#include <stdlib.h> /* malloc */
#include <string.h> /* strlen */
#include <fcntl.h> /* fcntl */
#include <stdio.h>
#include <assert.h>
#include <stdarg.h> /* va_list */

#include <sys/types.h>
#include <sys/stat.h>  /* fstat */
#include <sys/uio.h>   /* writev */

#include "sys/inotify.h"
#include "utils.h"

#include "config.h"

/**
 * Create a file path using its name and a path to its directory.
 *
 * @param[in] dir  A path to a file directory. May end with a '/'.
 * @param[in] file File name.
 * @return A concatenated path. Should be freed with free().
 **/
char*
path_concat (const char *dir, const char *file)
{
    size_t dir_len = strlen (dir);
    size_t file_len = strlen (file);
    size_t alloc_sz = dir_len + file_len + 2;

    char *path = malloc (alloc_sz);
    if (path == NULL) {
        perror_msg ("Failed to allocate memory (%d bytes) "
                    "for path concatenation",
                    alloc_sz);
        return NULL;
    }

    strlcpy (path, dir, alloc_sz);

    if (dir[dir_len - 1] != '/') {
        ++dir_len;
        path[dir_len - 1] = '/';
    }

    strlcpy (path + dir_len, file, file_len + 1);
    return path;
}

/**
 * Create a new inotify event.
 *
 * @param[in] wd     An associated watch's id.
 * @param[in] mask   An inotify watch mask.
 * @param[in] cookie Event cookie.
 * @param[in] name   File name (may be NULL).
 * @param[out] event_len The length of the created event, in bytes.
 * @return A pointer to a created event on NULL on a failure.
 **/
struct inotify_event*
create_inotify_event (int         wd,
                      uint32_t    mask,
                      uint32_t    cookie,
                      const char *name,
                      size_t     *event_len)
{
    struct inotify_event *event = NULL;
    size_t name_len = name ? strlen (name) + 1 : 0;
    *event_len = sizeof (struct inotify_event) + name_len;
    event = calloc (1, *event_len);

    if (event == NULL) {
        perror_msg ("Failed to allocate a new inotify event [%s, %X]",
                    name,
                    mask);
        return NULL;
    }

    event->wd = wd;
    event->mask = mask;
    event->cookie = cookie;
    event->len = name_len;

    if (name) {
        strlcpy (event->name, name, name_len);
    }

    return event;
}


#define SAFE_GENERIC_OP(fcn, fd, data, size)    \
    size_t total = 0;                           \
    if (fd == -1) {                             \
        return -1;                              \
    }                                           \
    while (size > 0) {                          \
        ssize_t retval = fcn (fd, data, size);  \
        if (retval == -1) {                     \
            if (errno == EINTR) {               \
                continue;                       \
            } else {                            \
                return -1;                      \
            }                                   \
        }                                       \
        total += retval;                        \
        size -= retval;                         \
        data = (char *)data + retval;           \
    }                                           \
    return (ssize_t) total;

/**
 * EINTR-ready version of read().
 *
 * @param[in]  fd   A file descriptor to read from.
 * @param[out] data A receiving buffer.
 * @param[in]  size The number of bytes to read.
 * @return Number of bytes which were read on success, -1 on failure.
 **/
ssize_t
safe_read (int fd, void *data, size_t size)
{
    SAFE_GENERIC_OP (read, fd, data, size);
}

/**
 * EINTR-ready version of write().
 *
 * @param[in] fd   A file descriptor to write to.
 * @param[in] data A buffer to wtite.
 * @param[in] size The number of bytes to write.
 * @return Number of bytes which were written on success, -1 on failure.
 **/
ssize_t
safe_write (int fd, const void *data, size_t size)
{
    SAFE_GENERIC_OP (write, fd, data, size);
}

/**
 * EINTR-ready version of writev().
 * The canonical version of this routine is maintained in the rra-c-util,
 * which can be found at <http://www.eyrie.org/~eagle/software/rra-c-util/>.
 *
 * @param[in] fd     A file descriptor to write to.
 * @param[in] iov    An array of iovec buffers to wtite.
 * @param[in] iovcnt A number of iovec buffers to write.
 * @return Number of bytes which were written on success, -1 on failure.
 **/
ssize_t
safe_writev (int fd, const struct iovec iov[], int iovcnt)
{
    ssize_t total, status = 0;
    size_t left, offset;
    int iovleft, i, count;
    struct iovec *tmpiov;

    /*
     * Bounds-check the iovcnt argument.  This is just for our safety.  The
     * system will probably impose a lower limit on iovcnt, causing the later
     * writev to fail with an error we'll return.
     */
    if (iovcnt == 0)
        return 0;
    if (iovcnt < 0 || (size_t) iovcnt > SIZE_MAX / sizeof(struct iovec)) {
        errno = EINVAL;
        return -1;
    }

    /* Get a count of the total number of bytes in the iov array. */
    for (total = 0, i = 0; i < iovcnt; i++)
        total += iov[i].iov_len;
    if (total == 0)
        return 0;

    /*
     * First, try just writing it all out.  Most of the time this will succeed
     * and save us lots of work.  Abort the write if we try ten times with no
     * forward progress.
     */
    count = 0;
    do {
        if (++count > 10)
            break;
        status = writev(fd, iov, iovcnt);
        if (status > 0)
            count = 0;
    } while (status < 0 && errno == EINTR);
    if (status < 0)
        return -1;
    if (status == total)
        return total;

    /*
     * If we fell through to here, the first write partially succeeded.
     * Figure out how far through the iov array we got, and then duplicate the
     * rest of it so that we can modify it to reflect how much we manage to
     * write on successive tries.
     */
    offset = status;
    left = total - offset;
    for (i = 0; offset >= (size_t) iov[i].iov_len; i++)
        offset -= iov[i].iov_len;
    iovleft = iovcnt - i;
    assert(iovleft > 0);
    tmpiov = calloc(iovleft, sizeof(struct iovec));
    if (tmpiov == NULL)
        return -1;
    memcpy(tmpiov, iov + i, iovleft * sizeof(struct iovec));

    /*
     * status now contains the offset into the first iovec struct in tmpiov.
     * Go into the write loop, trying to write out everything remaining at
     * each point.  At the top of the loop, status will contain a count of
     * bytes written out at the beginning of the set of iovec structs.
     */
    i = 0;
    do {
        if (++count > 10)
            break;

        /* Skip any leading data that has been written out. */
        for (; offset >= (size_t) tmpiov[i].iov_len && iovleft > 0; i++) {
            offset -= tmpiov[i].iov_len;
            iovleft--;
        }
        tmpiov[i].iov_base = (char *) tmpiov[i].iov_base + offset;
        tmpiov[i].iov_len -= offset;

        /* Write out what's left and return success if it's all written. */
        status = writev(fd, tmpiov + i, iovleft);
        if (status <= 0)
            offset = 0;
        else {
            offset = status;
            left -= offset;
            count = 0;
        }
    } while (left > 0 && (status >= 0 || errno == EINTR));

    /* We're either done or got an error; if we're done, left is now 0. */
    free(tmpiov);
    return (left == 0) ? total : -1;
}

/**
 * Check if the specified file descriptor is still opened.
 *
 * @param[in] fd A file descriptor to check.
 * @return 1 if still opened, 0 if closed or an error has occured.
 **/
int
is_opened (int fd)
{
    int ret = (fcntl (fd, F_GETFL) != -1);
    return ret;
}

/**
 * Check if the file referenced by specified descriptor is deleted.
 *
 * @param[in] fd A file descriptor to check.
 * @return 1 if deleted or error occured, 0 if hardlinks to file still exist.
 **/
int
is_deleted (int fd)
{
    struct stat st;

    if (fstat (fd, &st) == -1) {
        perror_msg ("fstat %d failed", fd);
        return 1;
    }

    return (st.st_nlink == 0);
}

/**
 * Print an error message, if allowed.
 *
 * The function uses perror, so the errno-based error description will
 * be printed too.
 * The library should be built with --enable-perrors configure option.
 *
 * @param[in] msg A message format to print.
 * @param[in] ... A set of parameters to include in the message, according
 *      to the format string.
 **/
void
perror_msg (const char *msg, ...)
{
#ifdef ENABLE_PERRORS
    const int msgsz = 2048; /* should be enough */
    char buf[msgsz];
    va_list vl;

    va_start (vl, msg);
    vsnprintf (buf, msgsz, msg, vl);
    va_end (vl);

    perror (buf);
#else
    (void) msg;
#endif
}
