AC_INIT([package], [version])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_LIBTOOL
AC_PROG_CXX
AC_CONFIG_FILES([Makefile])

case "$host_os" in
*linux*)
   OS=Linux
   ;;
*freebsd*)
   OS=FreeBSD
   ;;
esac

AM_CONDITIONAL(LINUX, [test "$OS" = "Linux"])
AM_CONDITIONAL(FREEBSD, [test "$OS" = "FreeBSD"])


AC_ARG_ENABLE([perrors],
    AS_HELP_STRING([--enable-perrors], [enable error messages in the library]),
    AC_DEFINE([ENABLE_PERRORS],[1],[Enable error messages])
)


kqueue_support=no
AC_CHECK_HEADERS([sys/event.h],
[
    AC_CHECK_FUNCS(kqueue,,AC_MSG_ERROR(No kqueue detected in your system!))
    AC_CHECK_FUNCS(kevent,,AC_MSG_ERROR(No kevent detected in your system!))
    kqueue_support=yes
],
[
    if [test "$OS" = "Linux"]; then
        echo "Host system in GNU/Linux, enabling target \"test\" only"
        kqueue_support=no
    else
        AC_MSG_ERROR(No sys/kqueue.h found in your system!)
    fi
])
AM_CONDITIONAL(BUILD_LIBRARY, [test "$kqueue_support" = "yes"])


AC_MSG_CHECKING(for pthread_barrier)
AC_COMPILE_IFELSE(
[
    AC_LANG_PROGRAM(
    [
        @%:@include <pthread.h>
    ],
    [
        pthread_barrier_t barrier;
        pthread_barrier_init(&barrier, NULL, 1);
        pthread_barrier_wait(&barrier);
        pthread_barrier_destroy(&barrier);
        return 0;
    ])
],
[
    AC_DEFINE([HAVE_PTHREAD_BARRIER],[1],[Define to 1 if the system has pthread_barrier])
    AC_MSG_RESULT(yes)
],
    AC_MSG_RESULT(no)
)


AC_OUTPUT
