AC_INIT(
        [copy],
        [1.1.6],
        [sforbes41@gmail.com],
        [copy],
        [https://github.com/natediddy/copy],
)

AC_MSG_NOTICE([Configuring $PACKAGE_NAME $PACKAGE_VERSION])

AC_CONFIG_SRCDIR([copy.c])
AC_CONFIG_HEADERS([copy-config.h])
AM_INIT_AUTOMAKE([-Wall no-define foreign])
AC_CONFIG_FILES([Makefile])

AM_MAINTAINER_MODE
AC_PROG_CC

AC_ARG_ENABLE([sound],
[AS_HELP_STRING([--enable-sound], [Enable sound notification])
AS_HELP_STRING([--disable-sound], [Disable sound notification (DEFAULT)])
])

if test "$enable_sound" = "yes"; then
  AC_DEFINE([ENABLE_SOUND], [1], [Define if notification sound enabled])
fi

AM_CONDITIONAL([ENABLE_SOUND], [test "$enable_sound" = "yes"])

LIBS="$LIBS -lm"
if test "$enable_sound" = "yes"; then
  AC_CHECK_LIB([SDL], [SDL_Init],
               [LIBS="$LIBS -lSDL"],
               [AC_ERROR([Sound support was requested but the SDL library
                          could not be found])])
  AC_CHECK_LIB([SDL_sound], [Sound_Init],
               [LIBS="$LIBS -lSDL_sound"],
               [AC_ERROR([Sound support was requested but the SDL_sound
                          library could not found])])
fi

AC_OUTPUT
