# Process this file with autoconf to produce a configure script.
AC_INIT(libgfs2, 1.0.0, rpeterso@redhat.com)

echo "-------------------------------------------"
echo "Configuring libgfs2 libraries and utilities"
echo "-------------------------------------------"

AC_CONFIG_SRCDIR(buf.c)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_ENABLE_SHARED
AC_PROG_LIBTOOL
AM_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([gfs2_disk_hash.h libgfs2.h iddev.h linux_endian.h ondisk.h osi_list.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME

# Checks for system libraries
AC_CHECK_LIB(dl, dlopen)

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([])

# This makes sure "@VERSION@" can be used in Makefile.am's for things like
# pod2man. I've noticed that some versions of autoconf (or automake?) don't
# define this, the following trick seems to fix that.
VERSION=$PACKAGE_VERSION
AC_SUBST(VERSION)

# Declare all the SSL-related stuff
TOP_LEVEL_SSL

#AC_CONFIG_FILES([Makefile])

AC_OUTPUT([Makefile])
