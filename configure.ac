
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])

AC_INIT([resource-agents], 
	m4_esyscmd([make/git-version-gen .tarball-version]),
	[linux-cluster@redhat.com])

AM_INIT_AUTOMAKE([-Wno-portability dist-bzip2 dist-xz])

AC_CONFIG_SRCDIR([rgmanager/src/resources/ip.sh])

# Sanitize path

if test "$prefix" = "NONE"; then
	prefix="/usr"
	if test "$localstatedir" = "\${prefix}/var"; then
		localstatedir="/var"
	fi
	if test "$sysconfdir" = "\${prefix}/etc"; then
		sysconfdir="/etc"
	fi
	if test "$libdir" = "\${exec_prefix}/lib"; then
		if test -e /usr/lib64; then
			libdir="/usr/lib64"
		else
			libdir="/usr/lib"
		fi
	fi
fi

case $exec_prefix in
  NONE)   exec_prefix=$prefix;;
  prefix) exec_prefix=$prefix;;
esac

# Checks for programs.

# check stolen from gnulib/m4/gnu-make.m4
if ! ${MAKE-make} --version /cannot/make/this >/dev/null 2>&1; then
	AC_MSG_ERROR([you don't seem to have GNU make; it is required])
fi

AC_PROG_AWK
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# local options
AC_ARG_ENABLE([debug],
	[  --enable-debug          enable debug build. ],
	[ default="no" ])

## random vars

LOGDIR=${localstatedir}/log/cluster
CLUSTERDATA=${datadir}/cluster

## do subst

AC_SUBST([LOGDIR])
AC_SUBST([CLUSTERDATA])

AC_CONFIG_FILES([Makefile
		 rgmanager/Makefile
		 rgmanager/src/Makefile
		 rgmanager/src/resources/Makefile
		 rgmanager/src/resources/utils/Makefile
		 doc/Makefile
		 ])

AC_OUTPUT
