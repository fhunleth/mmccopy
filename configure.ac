AC_INIT([mmccopy], [1.0.0], [fhunleth@troodon-software.com])
AC_CONFIG_SRCDIR([mmccopy.c])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([-Wall -Werror foreign])

# Checks for programs.
AC_PROG_INSTALL

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h sys/mount.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_SYS_LARGEFILE

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([strdup strstr strtoul])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
