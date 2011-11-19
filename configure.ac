dnl
dnl autoconf for Agents
dnl
dnl License: GNU General Public License (GPL)

dnl ===============================================
dnl Bootstrap 
dnl ===============================================
AC_PREREQ(2.63)

dnl Suggested structure:
dnl     information on the package
dnl     checks for programs
dnl     checks for libraries
dnl     checks for header files
dnl     checks for types
dnl     checks for structures
dnl     checks for compiler characteristics
dnl     checks for library functions
dnl     checks for system services

AC_INIT([resource-agents], 
	m4_esyscmd([make/git-version-gen .tarball-version]),
	[to_be_defined@foobar.org])

AC_USE_SYSTEM_EXTENSIONS

CRM_DTD_VERSION="1.0"

PKG_FEATURES=""

AC_CONFIG_AUX_DIR(.)
AC_CANONICAL_HOST

dnl Where #defines go (e.g. `AC_CHECK_HEADERS' below)
dnl
dnl Internal header: include/config.h
dnl   - Contains ALL defines
dnl   - include/config.h.in is generated automatically by autoheader
dnl   - NOT to be included in any header files except lha_internal.h
dnl     (which is also not to be included in any other header files)
dnl
dnl External header: include/agent_config.h
dnl   - Contains a subset of defines checked here
dnl   - Manually edit include/agent_config.h.in to have configure include new defines
dnl   - Should not include HAVE_* defines
dnl   - Safe to include anywhere
AM_CONFIG_HEADER(include/config.h include/agent_config.h)
ALL_LINGUAS="en fr"

AC_ARG_WITH(version,
    [  --with-version=version   Override package version (if you're a packager needing to pretend) ],
    [ PACKAGE_VERSION="$withval" ])

AC_ARG_WITH(pkg-name,
    [  --with-pkg-name=name     Override package name (if you're a packager needing to pretend) ],
    [ PACKAGE_NAME="$withval" ])

dnl 
dnl AM_INIT_AUTOMAKE([1.11.1 foreign dist-bzip2 dist-xz])
dnl

AM_INIT_AUTOMAKE([1.10.1 foreign dist-bzip2])

AC_DEFINE_UNQUOTED(AGENTS_VERSION, "$PACKAGE_VERSION", Current agents version)

CC_IN_CONFIGURE=yes
export CC_IN_CONFIGURE

LDD=ldd

dnl ========================================================================
dnl Compiler characteristics
dnl ========================================================================

# check stolen from gnulib/m4/gnu-make.m4
if ! ${MAKE-make} --version /cannot/make/this >/dev/null 2>&1; then
	AC_MSG_ERROR([you don't seem to have GNU make; it is required])
fi

AC_PROG_CC dnl Can force other with environment variable "CC".
AM_PROG_CC_C_O
AC_PROG_CC_STDC
AC_PROG_AWK
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_C_STRINGIZE
AC_C_INLINE

AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UID_T
AC_TYPE_UINT16_T
AC_TYPE_UINT8_T
AC_TYPE_UINT32_T

AC_CHECK_SIZEOF(char)
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(long long)
AC_STRUCT_TIMEZONE

dnl ===============================================
dnl Helpers 
dnl ===============================================
cc_supports_flag() {
         local CFLAGS="$@"
         AC_MSG_CHECKING(whether $CC supports "$@")
         AC_COMPILE_IFELSE([int main(){return 0;}] ,[RC=0; AC_MSG_RESULT(yes)],[RC=1; AC_MSG_RESULT(no)])
         return $RC
}

extract_header_define() {
	  AC_MSG_CHECKING(for $2 in $1)
	  Cfile=/tmp/extract_define.$2.${$}
	  printf "#include <stdio.h>\n" > ${Cfile}.c
	  printf "#include <%s>\n" $1 >> ${Cfile}.c
	  printf "int main(int argc, char **argv) { printf(\"%%s\", %s); return 0; }\n" $2 >> ${Cfile}.c
	  $CC $CFLAGS ${Cfile}.c -o ${Cfile}
	  value=`${Cfile}`
	  AC_MSG_RESULT($value)
	  printf $value
	  rm -f ${Cfile}.c ${Cfile}
	}

dnl ===============================================
dnl Configure Options
dnl ===============================================

dnl Some systems, like Solaris require a custom package name
AC_ARG_WITH(pkgname,
    [  --with-pkgname=name     name for pkg (typically for Solaris) ],
    [ PKGNAME="$withval" ],
    [ PKGNAME="LXHAhb" ],
  )
AC_SUBST(PKGNAME)

AC_ARG_ENABLE([ansi],
[  --enable-ansi force GCC to compile to ANSI/ANSI standard for older compilers.
     [default=yes]])

AC_ARG_ENABLE([fatal-warnings],
[  --enable-fatal-warnings very pedantic and fatal warnings for gcc
     [default=yes]])

INITDIR=""
AC_ARG_WITH(initdir,
    [  --with-initdir=DIR      directory for init (rc) scripts [${INITDIR}]],
    [ INITDIR="$withval" ])

OCF_ROOT_DIR="/usr/lib/ocf"
AC_ARG_WITH(ocf-root,
    [  --with-ocf-root=DIR      directory for OCF scripts [${OCF_ROOT_DIR}]],
    [ if test x"$withval" = xprefix; then OCF_ROOT_DIR=${prefix}; else
	 OCF_ROOT_DIR="$withval"; fi ])
HA_RSCTMPDIR=${localstatedir}/run/resource-agents
AC_ARG_WITH(rsctmpdir,
    [  --with-rsctmpdir=DIR      directory for resource agents state files [${HA_RSCTMPDIR}]],
    [ if test x"$withval" = xprefix; then HA_RSCTMPDIR=${prefix}; else
	 HA_RSCTMPDIR="$withval"; fi ])

AC_ARG_ENABLE([libnet],
 [  --enable-libnet 	Use libnet for ARP based funcationality, [default=try]], 
 [enable_libnet="$enableval"], [enable_libnet=try])

BUILD_RGMANAGER=0
BUILD_LINUX_HA=0

RASSET=all
AC_ARG_WITH(ras-set,
    [  --with-ras-set=SET     build/install only linux-ha or rgmanager resource-agents [default: all]],
    [ RASSET="$withval" ])

if test x$RASSET = xyes || test x$RASSET = xall ; then
	BUILD_RGMANAGER=1
	BUILD_LINUX_HA=1
fi

if test x$RASSET = xlinux-ha; then
	BUILD_LINUX_HA=1
fi

if test x$RASSET = xrgmanager; then
	BUILD_RGMANAGER=1
fi

if test $BUILD_LINUX_HA -eq 0 && test $BUILD_RGMANAGER -eq 0; then
	AC_MSG_ERROR([Are you really sure you want this package?])
	exit 1
fi

AM_CONDITIONAL(BUILD_LINUX_HA, test $BUILD_LINUX_HA -eq 1)
AM_CONDITIONAL(BUILD_RGMANAGER, test $BUILD_RGMANAGER -eq 1)

dnl ===============================================
dnl General Processing
dnl ===============================================

INIT_EXT=""
echo Our Host OS: $host_os/$host

AC_MSG_NOTICE(Sanitizing prefix: ${prefix})
case $prefix in
  NONE)	prefix=/usr;;
esac

AC_MSG_NOTICE(Sanitizing exec_prefix: ${exec_prefix})
case $exec_prefix in
  dnl For consistency with Heartbeat, map NONE->$prefix
  NONE)	  exec_prefix=$prefix;;
  prefix) exec_prefix=$prefix;;
esac

AC_MSG_NOTICE(Sanitizing INITDIR: ${INITDIR})
case $INITDIR in
  prefix) INITDIR=$prefix;;
  "")
    AC_MSG_CHECKING(which init (rc) directory to use)
      for initdir in /etc/init.d /etc/rc.d/init.d /sbin/init.d	\
	   /usr/local/etc/rc.d /etc/rc.d
      do
        if
          test -d $initdir
        then
          INITDIR=$initdir
          break
        fi
      done
      if 
      	 test -z $INITDIR
      then
          INITDIR=${sysconfdir}/init.d
      fi
      AC_MSG_RESULT($INITDIR);;
esac
AC_SUBST(INITDIR)

if test "${prefix}" = "/usr"; then
 INITDIRPREFIX="$INITDIR" 
else
 INITDIRPREFIX="${prefix}/$INITDIR"
fi

AC_SUBST(INITDIRPREFIX)

AC_MSG_NOTICE(Sanitizing libdir: ${libdir})
case $libdir in
  dnl For consistency with Heartbeat, map NONE->$prefix
  *prefix*|NONE)
    AC_MSG_CHECKING(which lib directory to use)
    for aDir in lib64 lib
    do
      trydir="${exec_prefix}/${aDir}"
      if
        test -d ${trydir}
      then
        libdir=${trydir}
        break
      fi
    done
    AC_MSG_RESULT($libdir);
    ;;
esac

dnl Expand autoconf variables so that we dont end up with '${prefix}' 
dnl in #defines and python scripts
dnl NOTE: Autoconf deliberately leaves them unexpanded to allow
dnl    make exec_prefix=/foo install
dnl No longer being able to do this seems like no great loss to me...

eval prefix="`eval echo ${prefix}`"
eval exec_prefix="`eval echo ${exec_prefix}`"
eval bindir="`eval echo ${bindir}`"
eval sbindir="`eval echo ${sbindir}`"
eval libexecdir="`eval echo ${libexecdir}`"
eval datadir="`eval echo ${datadir}`"
eval sysconfdir="`eval echo ${sysconfdir}`"
eval sharedstatedir="`eval echo ${sharedstatedir}`"
eval localstatedir="`eval echo ${localstatedir}`"
eval libdir="`eval echo ${libdir}`"
eval includedir="`eval echo ${includedir}`"
eval oldincludedir="`eval echo ${oldincludedir}`"
eval infodir="`eval echo ${infodir}`"
eval mandir="`eval echo ${mandir}`"

dnl docdir is a recent addition to autotools
eval docdir="`eval echo ${docdir}`"
if test "x$docdir" = "x"; then
   docdir="`eval echo ${datadir}/doc`"
fi
AC_SUBST(docdir)

dnl Home-grown variables
eval INITDIR="${INITDIR}"

for j in prefix exec_prefix bindir sbindir libexecdir datadir sysconfdir \
    sharedstatedir localstatedir libdir includedir oldincludedir infodir \
    mandir INITDIR docdir
do 
  dirname=`eval echo '${'${j}'}'`
  if
    test ! -d "$dirname"
  then
    AC_MSG_WARN([$j directory ($dirname) does not exist!])
  fi
done

dnl This OS-based decision-making is poor autotools practice;
dnl feature-based mechanisms are strongly preferred.
dnl
dnl So keep this section to a bare minimum; regard as a "necessary evil". 

REBOOT_OPTIONS="-f"
POWEROFF_OPTIONS="-f"

case "$host_os" in
*bsd*)		LIBS="-L/usr/local/lib"
		CPPFLAGS="$CPPFLAGS -I/usr/local/include"
		INIT_EXT=".sh"
		;;
*solaris*)
		REBOOT_OPTIONS="-n"
		POWEROFF_OPTIONS="-n"
		;;
*linux*)	
		AC_DEFINE_UNQUOTED(ON_LINUX, 1, Compiling for Linux platform)
		POWEROFF_OPTIONS="-nf"
		REBOOT_OPTIONS="-nf"
 		;;
darwin*)	
		AC_DEFINE_UNQUOTED(ON_DARWIN, 1, Compiling for Darwin platform)
  		LIBS="$LIBS -L${prefix}/lib"
  		CFLAGS="$CFLAGS -I${prefix}/include"
		;;
esac

AC_SUBST(INIT_EXT)
AC_DEFINE_UNQUOTED(HA_LOG_FACILITY, LOG_DAEMON, Default logging facility)

AC_MSG_NOTICE(Host CPU: $host_cpu)

case "$host_cpu" in
  ppc64|powerpc64)
    case $CFLAGS in
     *powerpc64*)			;;
     *)	if test "$GCC" = yes; then
	  CFLAGS="$CFLAGS -m64"
	fi				;;
    esac
esac

AC_MSG_CHECKING(which format is needed to print uint64_t)
case "$host_cpu" in
  s390x)U64T="%lu";;
  *64*) U64T="%lu";;
  *)    U64T="%llu";;
esac
AC_MSG_RESULT($U64T)
AC_DEFINE_UNQUOTED(U64T, "$U64T", Correct printf format for logging uint64_t)


dnl Variables needed for substitution

AC_CHECK_HEADERS(heartbeat/glue_config.h)

if test "$ac_cv_header_heartbeat_glue_config_h" = "yes"; then
  OCF_ROOT_DIR=`extract_header_define heartbeat/glue_config.h OCF_ROOT_DIR`
else
  enable_libnet=no
fi

AC_DEFINE_UNQUOTED(OCF_ROOT_DIR,"$OCF_ROOT_DIR", OCF root directory - specified by the OCF standard)
AC_SUBST(OCF_ROOT_DIR)

GLUE_STATE_DIR=${localstatedir}/run
AC_DEFINE_UNQUOTED(GLUE_STATE_DIR,"$GLUE_STATE_DIR", Where to keep state files and sockets)
AC_SUBST(GLUE_STATE_DIR)

AC_DEFINE_UNQUOTED(HA_VARRUNDIR,"$GLUE_STATE_DIR", Where Heartbeat keeps state files and sockets - old name)
HA_VARRUNDIR="$GLUE_STATE_DIR"
AC_SUBST(HA_VARRUNDIR)

# Expand $prefix
eval HA_RSCTMPDIR="`eval echo ${HA_RSCTMPDIR}`"
AC_DEFINE_UNQUOTED(HA_RSCTMPDIR,"$HA_RSCTMPDIR", Where Resouce agents keep state files)
AC_SUBST(HA_RSCTMPDIR)

dnl Eventually move out of the heartbeat dir tree and create symlinks when needed
HA_VARLIBHBDIR=${localstatedir}/lib/heartbeat
AC_DEFINE_UNQUOTED(HA_VARLIBHBDIR,"$HA_VARLIBHBDIR", Whatever this used to mean)
AC_SUBST(HA_VARLIBHBDIR)

OCF_RA_DIR="${OCF_ROOT_DIR}/resource.d/"
AC_DEFINE_UNQUOTED(OCF_RA_DIR,"$OCF_RA_DIR", Location for OCF RAs)
AC_SUBST(OCF_RA_DIR)

if test "${prefix}" = "/usr"; then
 OCF_RA_DIR_PREFIX="$OCF_RA_DIR"
else
 OCF_RA_DIR_PREFIX="${prefix}/$OCF_RA_DIR"
fi
AC_SUBST(OCF_RA_DIR_PREFIX)

OCF_LIB_DIR="${OCF_ROOT_DIR}/lib/"
AC_DEFINE_UNQUOTED(OCF_LIB_DIR,"$OCF_LIB_DIR", Location for shared code for OCF RAs)
AC_SUBST(OCF_LIB_DIR)

if test "${prefix}" = "/usr"; then
 OCF_LIB_DIR_PREFIX="$OCF_LIB_DIR"
else
 OCF_LIB_DIR_PREFIX="${prefix}/$OCF_LIB_DIR"
fi
AC_SUBST(OCF_LIB_DIR_PREFIX)

dnl ===============================================
dnl rgmanager ras bits
dnl ===============================================

LOGDIR=${localstatedir}/log/cluster
CLUSTERDATA=${datadir}/cluster

AC_SUBST([LOGDIR])
AC_SUBST([CLUSTERDATA])

dnl ===============================================
dnl Program Paths
dnl ===============================================

PATH="$PATH:/sbin:/usr/sbin:/usr/local/sbin:/usr/local/bin"
export PATH

AM_PATH_PYTHON
AC_CHECK_PROGS(MAKE, gmake make)
AC_PATH_PROGS(SSH, ssh, /usr/bin/ssh)
AC_PATH_PROGS(SCP, scp, /usr/bin/scp)
AC_PATH_PROGS(TAR, tar)
AC_PATH_PROGS(MD5, md5)
AC_PATH_PROGS(TEST, test)
AC_PATH_PROGS(PING, ping, /bin/ping)
AC_PATH_PROGS(IFCONFIG, ifconfig, /sbin/ifconfig)
AC_PATH_PROGS(MAILCMD, mailx mail, mail)
AC_PATH_PROGS(EGREP, egrep)
AC_PATH_PROGS(PKGCONFIG, pkg-config)

AC_SUBST(MAILCMD)
AC_SUBST(EGREP)
AC_SUBST(SHELL)
AC_SUBST(PING)
AC_SUBST(TEST)

AC_PATH_PROGS(ROUTE, route)
AC_DEFINE_UNQUOTED(ROUTE, "$ROUTE", path to route command)

AC_MSG_CHECKING(ifconfig option to list interfaces)
for IFCONFIG_A_OPT in  "-A" "-a" ""
do
  $IFCONFIG $IFCONFIG_A_OPT > /dev/null 2>&1
  if
    test "$?" = 0
  then
    AC_DEFINE_UNQUOTED(IFCONFIG_A_OPT, "$IFCONFIG_A_OPT", option for ifconfig command)
    AC_MSG_RESULT($IFCONFIG_A_OPT)
    break
  fi
done

AC_SUBST(IFCONFIG_A_OPT)

if test x"${MAKE}" = x""; then
   AC_MSG_ERROR(You need (g)make installed in order to build ${PACKAGE})
fi

dnl ===============================================
dnl Libraries
dnl ===============================================
AC_CHECK_LIB(socket, socket)			
AC_CHECK_LIB(gnugetopt, getopt_long)		dnl if available

if test x"${PKGCONFIG}" = x""; then
   AC_MSG_ERROR(You need pkgconfig installed in order to build ${PACKAGE})
fi

if test "x${enable_thread_safe}" = "xyes"; then
        GPKGNAME="gthread-2.0"
else
        GPKGNAME="glib-2.0"
fi

if 
   $PKGCONFIG --exists $GPKGNAME
then
	GLIBCONFIG="$PKGCONFIG $GPKGNAME"
else
	set -x
        echo PKG_CONFIG_PATH=$PKG_CONFIG_PATH
	$PKGCONFIG --exists $GPKGNAME; echo $?
	$PKGCONFIG --cflags $GPKGNAME; echo $?
	$PKGCONFIG $GPKGNAME; echo $?
	set +x
        
	AC_MSG_ERROR(You need glib2-devel installed in order to build ${PACKAGE})
fi
AC_MSG_RESULT(using $GLIBCONFIG)

if test "X$GLIBCONFIG" != X; then
	AC_MSG_CHECKING(for special glib includes: )
	GLIBHEAD=`$GLIBCONFIG --cflags`
	AC_MSG_RESULT($GLIBHEAD)
	CPPFLAGS="$CPPFLAGS $GLIBHEAD"

	AC_MSG_CHECKING(for glib library flags)
	GLIBLIB=`$GLIBCONFIG --libs`
	AC_MSG_RESULT($GLIBLIB)
	LIBS="$LIBS $GLIBLIB"
fi

dnl ========================================================================
dnl Headers
dnl ========================================================================

AC_HEADER_STDC
AC_CHECK_HEADERS(sys/socket.h)
AC_CHECK_HEADERS(sys/sockio.h)
AC_CHECK_HEADERS([arpa/inet.h])
AC_CHECK_HEADERS([fcntl.h])
AC_CHECK_HEADERS([limits.h])
AC_CHECK_HEADERS([malloc.h])
AC_CHECK_HEADERS([netdb.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([sys/file.h])
AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADERS([sys/param.h])
AC_CHECK_HEADERS([sys/time.h])
AC_CHECK_HEADERS([syslog.h])

dnl ========================================================================
dnl Functions
dnl ========================================================================

AC_FUNC_FORK
AC_FUNC_STRNLEN
AC_CHECK_FUNCS([alarm gettimeofday inet_ntoa memset mkdir socket uname])
AC_CHECK_FUNCS([strcasecmp strchr strdup strerror strrchr strspn strstr strtol strtoul])

dnl 'reboot()' system call: one argument (e.g. Linux) or two (e.g. Solaris)?
dnl
AC_CACHE_CHECK([number of arguments in reboot system call],
  ac_cv_REBOOT_ARGS,[
    AC_TRY_COMPILE(
      [#include <sys/reboot.h>],
      [(void)reboot(0);],
      ac_cv_REBOOT_ARGS=1,
      [AC_TRY_COMPILE(
        [#include <sys/reboot.h>],
        [(void)reboot(0,(void *)0);],
        ac_cv_REBOOT_ARGS=2,
        ac_cv_REBOOT_ARGS=0
      )],
      ac_cv_REBOOT_ARGS=0
    )
  ]
)
dnl Argument count of 0 suggests no known 'reboot()' call.
if test "$ac_cv_REBOOT_ARGS" -ge "1"; then
  AC_DEFINE_UNQUOTED(REBOOT_ARGS,$ac_cv_REBOOT_ARGS,[number of arguments for reboot system call])
fi

AC_PATH_PROGS(REBOOT, reboot, /sbin/reboot)
AC_SUBST(REBOOT)
AC_SUBST(REBOOT_OPTIONS)
AC_DEFINE_UNQUOTED(REBOOT, "$REBOOT", path to the reboot command)
AC_DEFINE_UNQUOTED(REBOOT_OPTIONS, "$REBOOT_OPTIONS", reboot options)

AC_PATH_PROGS(POWEROFF_CMD, poweroff, /sbin/poweroff)
AC_SUBST(POWEROFF_CMD)
AC_SUBST(POWEROFF_OPTIONS)
AC_DEFINE_UNQUOTED(POWEROFF_CMD, "$POWEROFF_CMD", path to the poweroff command)
AC_DEFINE_UNQUOTED(POWEROFF_OPTIONS, "$POWEROFF_OPTIONS", poweroff options)

AC_PATH_PROGS(XSLTPROC, xsltproc)
AM_CONDITIONAL(BUILD_DOC, test "x$XSLTPROC" != "x" )
if test "x$XSLTPROC" = "x"; then
   AC_MSG_WARN([xsltproc not installed, unable to (re-)build manual pages])
fi
AC_SUBST(XSLTPROC)

AC_PATH_PROGS(POD2MAN, pod2man)
AM_CONDITIONAL(BUILD_POD_DOC, test "x$POD2MAN" != "x" )
if test "x$POD2MAN" = "x"; then
   AC_MSG_WARN([pod2man not installed, unable to (re-)build ldirector manual page])
fi
AC_SUBST(POD2MAN)

dnl ========================================================================
dnl Functions
dnl ========================================================================

AC_CHECK_FUNCS(getopt, AC_DEFINE(HAVE_DECL_GETOPT,  1, [Have getopt function]))

dnl ========================================================================
dnl   sfex
dnl ========================================================================

build_sfex=no
case $host_os in
    *Linux*|*linux*) 
	if test "$ac_cv_header_heartbeat_glue_config_h" = "yes"; then
	    build_sfex=yes
	fi
	;;
esac

AM_CONDITIONAL(BUILD_SFEX, test "$build_sfex" = "yes" )


dnl ========================================================================
dnl   tickle (needs port to BSD platforms)
dnl ========================================================================

AC_CHECK_MEMBERS([struct iphdr.saddr],,,[[#include <netinet/ip.h>]])
AM_CONDITIONAL(BUILD_TICKLE, test "$ac_cv_member_struct_iphdr_saddr" = "yes" )

dnl ========================================================================
dnl   libnet
dnl ========================================================================

libnet=""
libnet_version="none"
LIBNETLIBS=""
LIBNETDEFINES=""

AC_MSG_CHECKING(if libnet is required)
libnet_fatal=$enable_libnet
case $enable_libnet in
     no) ;;
     yes|libnet10|libnet11|10|11) libnet_fatal=yes;;
     try)
	case $host_os in
	     *Linux*|*linux*) libnet_fatal=no;;
	     *) libnet_fatal=yes;; dnl legacy behavior
	esac
	;;
     *) libnet_fatal=yes; enable_libnet=try;;
esac
AC_MSG_RESULT($libnet_fatal)

if test "x$enable_libnet" != "xno"; then
   AC_PATH_PROGS(LIBNETCONFIG, libnet-config)

   AC_CHECK_LIB(nsl, t_open)			dnl -lnsl
   AC_CHECK_LIB(socket, socket)			dnl -lsocket
   AC_CHECK_LIB(net, libnet_get_hwaddr, LIBNETLIBS=" -lnet", [])
 fi
 
AC_MSG_CHECKING(for libnet)
if test "x$LIBNETLIBS" != "x" -o "x$enable_libnet" = "xlibnet11"; then
      LIBNETDEFINES=""
      if test "$ac_cv_lib_nsl_t_open" = yes; then
         LIBNETLIBS="-lnsl $LIBNETLIBS"
      fi
      if test "$ac_cv_lib_socket_socket" = yes; then
         LIBNETLIBS="-lsocket $LIBNETLIBS"
      fi

      libnet=net
      libnet_version="libnet1.1"
fi

if test "x$enable_libnet" = "xtry" -o "x$enable_libnet" = "xlibnet10"; then
   if test "x$LIBNETLIBS" = x -a "x${LIBNETCONFIG}" != "x" ; then
      LIBNETDEFINES="`$LIBNETCONFIG --defines` `$LIBNETCONFIG --cflags`";
      LIBNETLIBS="`$LIBNETCONFIG --libs`";
      libnet_version="libnet1.0 (old)"
      case $LIBNETLIBS in
        *-l*)	libnet=`echo $LIBNETLIBS | sed 's%.*-l%%'`;;
        *)	libnet_version=none;;
      esac

      CPPFLAGS="$CPPFLAGS $LIBNETDEFINES"

      AC_CHECK_HEADERS(libnet.h)
      if test "$ac_cv_header_libnet_h" = no; then
	libnet_version=none
      fi
   fi
fi
AC_MSG_RESULT(found $libnet_version)

if test "$libnet_version" = none; then
   LIBNETLIBS=""
   LIBNETDEFINES=""
   if test $libnet_fatal = yes; then
        AC_MSG_ERROR(libnet not found)
   fi

else
   AC_CHECK_LIB($libnet,libnet_init,
      [new_libnet=yes; AC_DEFINE(HAVE_LIBNET_1_1_API, 1, Libnet 1.1 API)],
      [new_libnet=no; AC_DEFINE(HAVE_LIBNET_1_0_API, 1, Libnet 1.0 API)],$LIBNETLIBS)
   AC_SUBST(LIBNETLIBS)
fi

if test "$new_libnet" = yes; then
   AC_MSG_CHECKING(for libnet API 1.1.4: )
   save_CFLAGS="$CFLAGS"
   CFLAGS="$CFLAGS -fgnu89-inline -Wall -Werror"
   AC_COMPILE_IFELSE([#include <libnet.h>
   int main(){libnet_t *l=NULL; libnet_pblock_record_ip_offset(l, l->total_size); return(0); }],
      [AC_MSG_RESULT(no)],
      [AC_DEFINE(HAVE_LIBNET_1_1_4_API, 1, Libnet 1.1.4 API) AC_MSG_RESULT(yes)])
   CFLAGS="$save_CFLAGS"
fi

sendarp_linux=0
case $host_os in
     *Linux*|*linux*) sendarp_linux=1;;
esac

AC_SUBST(LIBNETLIBS)
AC_SUBST(LIBNETDEFINES)

AM_CONDITIONAL(SENDARP_LINUX, test $sendarp_linux = 1 )
AM_CONDITIONAL(USE_LIBNET, test "x$libnet_version" != "xnone" )

dnl ************************************************************************
dnl * Check for netinet/icmp6.h to enable the IPv6addr resource agent
AC_CHECK_HEADERS(netinet/icmp6.h,[],[],[#include <sys/types.h>])
AM_CONDITIONAL(USE_IPV6ADDR, test "$ac_cv_header_netinet_icmp6_h" = yes )

dnl ========================================================================
dnl Compiler flags
dnl ========================================================================

dnl Make sure that CFLAGS is not exported. If the user did
dnl not have CFLAGS in their environment then this should have
dnl no effect. However if CFLAGS was exported from the user's
dnl environment, then the new CFLAGS will also be exported
dnl to sub processes.

CC_ERRORS=""
CC_EXTRAS=""

if export | fgrep " CFLAGS=" > /dev/null; then
	export -n CFLAGS || true # We don't want to bomb out if this fails
	SAVED_CFLAGS="$CFLAGS"
	unset CFLAGS
	CFLAGS="$SAVED_CFLAGS"
	unset SAVED_CFLAGS
fi

if test "$GCC" != yes; then
        CFLAGS="$CFLAGS -g"
	enable_fatal_warnings=no
else
        CFLAGS="$CFLAGS -ggdb3"

	# We had to eliminate -Wnested-externs because of libtool changes
	# Also remove -Waggregate-return because we use one libnet
	# call which returns a struct
        EXTRA_FLAGS="-fgnu89-inline
		-fstack-protector-all
		-Wall
		-Wbad-function-cast 
		-Wcast-qual 
		-Wcast-align 
		-Wdeclaration-after-statement
		-Wendif-labels
		-Wfloat-equal
		-Wformat=2
		-Wformat-security
		-Wformat-nonliteral
		-Winline
		-Wmissing-prototypes 
		-Wmissing-declarations 
		-Wmissing-format-attribute
		-Wnested-externs
		-Wno-long-long
		-Wno-strict-aliasing
		-Wpointer-arith 
		-Wstrict-prototypes
    		-Wunsigned-char
		-Wwrite-strings"

# Additional warnings it might be nice to enable one day
#		-Wshadow
#		-Wunreachable-code

	for j in $EXTRA_FLAGS
	do
	  if
	    cc_supports_flag $j
	  then
	    CC_EXTRAS="$CC_EXTRAS $j"
	  fi
	done

dnl In lib/ais/Makefile.am there's a gcc option available as of v4.x

	GCC_MAJOR=`gcc -v 2>&1 | awk 'END{print $3}' | sed 's/[.].*//'`
	AM_CONDITIONAL(GCC_4, test "${GCC_MAJOR}" = 4)

dnl System specific options

	case "$host_os" in
  	*linux*|*bsd*)
		if test "${enable_fatal_warnings}" = "unknown"; then
        		enable_fatal_warnings=yes
        	fi
          	;;
	esac

	if test "x${enable_fatal_warnings}" != xno && cc_supports_flag -Werror ; then
	   enable_fatal_warnings=yes
	else
	   enable_fatal_warnings=no
        fi

	if test "x${enable_ansi}" != xno && cc_supports_flag -std=iso9899:199409 ; then
	  AC_MSG_NOTICE(Enabling ANSI Compatibility)
	  CC_EXTRAS="$CC_EXTRAS -ansi -D_GNU_SOURCE -DANSI_ONLY"
	fi

  	AC_MSG_NOTICE(Activated additional gcc flags: ${CC_EXTRAS})
fi

CFLAGS="$CFLAGS $CC_EXTRAS"

NON_FATAL_CFLAGS="$CFLAGS"
AC_SUBST(NON_FATAL_CFLAGS)

dnl
dnl We reset CFLAGS to include our warnings *after* all function
dnl checking goes on, so that our warning flags don't keep the
dnl AC_*FUNCS() calls above from working.  In particular, -Werror will
dnl *always* cause us troubles if we set it before here.
dnl
dnl
if test "x${enable_fatal_warnings}" = xyes ; then
   AC_MSG_NOTICE(Enabling Fatal Warnings)
   CFLAGS="$CFLAGS -Werror"
fi
AC_SUBST(CFLAGS)

dnl This is useful for use in Makefiles that need to remove one specific flag
CFLAGS_COPY="$CFLAGS"
AC_SUBST(CFLAGS_COPY)

AC_SUBST(LOCALE)

AC_SUBST(CC)
AC_SUBST(MAKE)

dnl The Makefiles and shell scripts we output
AC_CONFIG_FILES(Makefile				        \
include/Makefile						\
heartbeat/Makefile						\
   heartbeat/ocf-binaries					\
   heartbeat/ocf-directories					\
   heartbeat/ocf-shellfuncs					\
   heartbeat/shellfuncs						\
tools/Makefile							\
   tools/ocf-tester						\
   tools/ocft/Makefile						\
   tools/ocft/ocft						\
   tools/ocft/caselib						\
   tools/ocft/README						\
   tools/ocft/README.zh_CN					\
ldirectord/Makefile						\
ldirectord/ldirectord   					\
	ldirectord/init.d/Makefile				\
	ldirectord/init.d/ldirectord   				\
	ldirectord/init.d/ldirectord.debian			\
	ldirectord/init.d/ldirectord.debian.default		\
	ldirectord/logrotate.d/Makefile				\
	ldirectord/OCF/Makefile					\
	ldirectord/OCF/ldirectord				\
doc/Makefile							\
	doc/man/Makefile					\
rgmanager/Makefile						\
	rgmanager/src/Makefile					\
	rgmanager/src/resources/Makefile			\
	rgmanager/src/resources/utils/Makefile			\
		)

dnl Now process the entire list of files added by previous 
dnl  calls to AC_CONFIG_FILES()
AC_OUTPUT()

dnl *****************
dnl Configure summary
dnl *****************

AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE configuration:])
AC_MSG_RESULT([  Version                  = ${VERSION}])
AC_MSG_RESULT([  Build Version            = $Format:%H$])
AC_MSG_RESULT([  Features                 =${PKG_FEATURES}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  Prefix                   = ${prefix}])
AC_MSG_RESULT([  Executables              = ${sbindir}])
AC_MSG_RESULT([  Man pages                = ${mandir}])
AC_MSG_RESULT([  Libraries                = ${libdir}])
AC_MSG_RESULT([  Header files             = ${includedir}])
AC_MSG_RESULT([  Arch-independent files   = ${datadir}])
AC_MSG_RESULT([  Documentation            = ${docdir}])
AC_MSG_RESULT([  State information        = ${localstatedir}])
AC_MSG_RESULT([  System configuration     = ${sysconfdir}])
AC_MSG_RESULT([  RA state files           = ${HA_RSCTMPDIR}])
AC_MSG_RESULT([  AIS Plugins              = ${LCRSODIR}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  CFLAGS                   = ${CFLAGS}])
AC_MSG_RESULT([  Libraries                = ${LIBS}])
AC_MSG_RESULT([  Stack Libraries          = ${CLUSTERLIBS}])

