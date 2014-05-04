AC_INIT(
  [ep],
  [1.2.0],
  [sforbes41@gmail.com],
  [ep],
  [https://github.com/natediddy/ep],
)

AC_MSG_NOTICE([Configuring $PACKAGE_NAME $PACKAGE_VERSION])

AC_CONFIG_SRCDIR([ep.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([-Wall no-define foreign])
AC_CONFIG_FILES([Makefile])

AM_MAINTAINER_MODE

AC_PROG_CC

AC_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

AC_ARG_ENABLE(
  [debug],
  [AS_HELP_STRING([--enable-debug], [Enable debugging])
AS_HELP_STRING([--disable-debug], [Disable debugging (DEFAULT)])]
)

AS_IF(
  [test x$enable_debug = xyes],
  [AC_DEFINE([DEBUG], [1], [Define if debugging is enabled])],
  []
)

AC_CHECK_LIB(curl, curl_easy_init, [LIBS="$LIBS -lcurl"],
             [AC_ERROR([libcurl is required to build ep, if you do not
                        have it, you can get it from
                        http://curl.haxx.se/download.html/ or from your
                        system package manager (if you have one)])])

AC_OUTPUT