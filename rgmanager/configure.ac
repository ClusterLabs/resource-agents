#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT(rgmanager, 1.9.0, lhh@redhat.com)
AM_INIT_AUTOMAKE

# Define default prefix
AC_PREFIX_DEFAULT(/usr)
AM_CONFIG_HEADER([include/config.h])

#
# See below for assignment to REAL CFLAGS var.
#
cflags="-Wall -Wstrict-prototypes -Wshadow -O2 -fPIC -D_GNU_SOURCE -ggdb"

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_RANLIB

AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(void *)

#
# Check for ccs header.
#
AC_CHECK_HEADERS([ccs.h], ,
	     AC_MSG_ERROR([Unusable/nonexistent ccs.h!  Aborting.]))

#
# Check for libccs.  Special case; we also want to be able to use static
# libs here.
#
AC_CHECK_LIB([ccs], [ccs_connect], 
	[CCSLIB="-lccs"],
	echo -n "checking for static libccs.a... "
	if test -f "/usr/lib/libccs.a" -o -f "/lib/libccs.a"; then
		echo "yes"
		CCSLIB="-lccs"
	else
		AC_MSG_ERROR([Can not find usable libccs!  Aborting.])
	fi)
AC_SUBST(CCSLIB)

#
# Save our build-wide libraries.
#
libs_SAVE=${LIBS}
LIBS=""

AC_CHECK_HEADERS([magma.h magmamsg.h], ,
	     AC_MSG_ERROR([Unusable/nonexistent Magma headers!  Aborting.]))
#
# Magma needs: dl, magma, magmamsg, and pthread
# These are all dynamic; so this is straightforward.
#
AC_CHECK_LIB([dl], [dlopen], , 
	     AC_MSG_ERROR([Can not find usable libpthread!  Aborting.]))
AC_CHECK_LIB([pthread], [pthread_create], ,
	     AC_MSG_ERROR([Can not find usable libpthread!  Aborting.]))
AC_CHECK_LIB([magma], [clu_null], ,
	     AC_MSG_ERROR([Can not find usable libmagma!  Aborting.]))
AC_CHECK_LIB([magmamsg], [msg_update], ,
	     AC_MSG_ERROR([Can not find usable libmagmamsg!  Aborting.]))
MAGMALIB=${LIBS}
AC_SUBST(MAGMALIB)
#
# Restore LIBS value.
#
LIBS=${libs_SAVE}

#
# Save our build-wide libraries.
#
libs_SAVE=${LIBS}
LIBS=""
#
# clustat needs ncurses.
#
AC_CHECK_LIB([ncurses], [tputs], ,
	     AC_MSG_ERROR([Can not find usable libncurses!  Aborting.]))
NCURSESLIB=${LIBS}
AC_SUBST(NCURSESLIB)
# Restore LIBS value.
LIBS=${libs_SAVE}

#
# cfmanager/rgmanager needs libxml2
#
libs_SAVE=${LIBS}
LIBS=""
AC_CHECK_LIB([xml2], [xmlParseFile],
	[XMLLIB="-lxml2"
	echo -n "checking for libxml2 CFLAGS... "
	if test -x "/usr/bin/xml2-config"; then
		XMLCFLAGS="`/usr/bin/xml2-config --cflags`"
		echo ${XMLCFLAGS}
	else
		AC_MSG_ERROR([Can not find libxml2!  Aborting.])
	fi
	],
	     AC_MSG_ERROR([Can not find libxml2!  Aborting.]))

AC_SUBST(XMLLIB)
AC_SUBST(XMLCFLAGS)
LIBS=${libs_SAVE}


# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h malloc.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/file.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h syslog.h termios.h unistd.h])
       
# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM
AC_TYPE_UID_T

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_CHECK_FUNCS([bzero gethostbyaddr gethostbyname memset mkdir select socket strcasecmp strchr strdup strerror strrchr strstr strtol])

#
# If we define this at the top, we're doomed.
#
CFLAGS=${cflags}
AC_SUBST(CFLAGS)


AC_CONFIG_FILES([Makefile
                 include/Makefile
                 src/Makefile
                 src/clulib/Makefile
                 src/daemons/Makefile
                 src/resources/Makefile
                 src/utils/Makefile])

AC_OUTPUT([examples/rgmanager.spec])
