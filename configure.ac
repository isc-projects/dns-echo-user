#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([dns-echo], [0.1], [bind9-bugs@isc.org])
AM_INIT_AUTOMAKE([no-define foreign])
AM_SILENT_RULES([yes])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

# Checks for programs.
AC_PROG_CC

# Checks for compiler flags.
DESIRED_FLAGS="-std=c99"
for flag in $DESIRED_FLAGS; do
  AS_COMPILER_FLAG([$flag], [CFLAGS="$CFLAGS $flag"])
done

# Checks for libraries.
CPPFLAGS="$AM_CPPFLAGS $CPPFLAGS"
CFLAGS="$AM_CFLAGS $CFLAGS"
LDFLAGS="$AM_LDFLAGS $LDFLAGS"
AC_CHECK_LIB([event], [event_base_new])
AC_CHECK_LIB([pthread], [pthread_setaffinity_np])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h net/netmap.h net/netmap_user.h netdb.h stdlib.h string.h sys/socket.h unistd.h])

AC_CHECK_HEADERS([linux/if_packet.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SSIZE_T

# Checks for standard library functions.
AC_CHECK_FUNCS([socket poll select])

# Enable extra warnings
WARNING_FLAGS="-fdiagnostics-show-option -Wall -Wextra -Wno-unused-parameter -Wstrict-prototypes -Wundef -Wformat=2 -Wmissing-include-dirs -Wformat-nonliteral -Wold-style-definition -Wpointer-arith -Waggregate-return -Winit-self -Wpacked -Wc++-compat -Wno-long-long -Wno-overlength-strings -Wdeclaration-after-statement -Wredundant-decls -Wmissing-noreturn -Wshadow -Wendif-labels -Wcast-qual -Wwrite-strings -fno-common"
for flag in $WARNING_FLAGS; do
  AS_COMPILER_FLAG([$flag], [CFLAGS="$CFLAGS $flag"])
done

AC_OUTPUT
