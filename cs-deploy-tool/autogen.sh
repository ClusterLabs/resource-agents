#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

THEDIR=`pwd`
cd $srcdir

AUTOMAKE=automake-1.7
ACLOCAL=aclocal-1.7
DIE=0

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have autoconf installed to compile imlib."
	echo "Download the appropriate package for your distribution,"
	echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
	DIE=1
}

if automake-1.7 --version < /dev/null > /dev/null 2>&1 ; then
     echo "Aren't you lucky You have automake 1.7"
     AUTOMAKE=automake-1.7
     ACLOCAL=aclocal-1.7
else
         echo
         echo "You must have automake 1.7.x installed to compile $PROJECT."
         echo "Install the appropriate package for your distribution,"
         echo "or get the source tarball at http://ftp.gnu.org/gnu/automake/"
         DIE=1
fi

if test "$DIE" -eq 1; then
	exit 1
fi

if test -z "$*"; then
	echo "I am going to run ./configure with no arguments - if you wish "
        echo "to pass any to it, please specify them on the $0 command line."
fi

case $CC in
xlc )
    am_opt=--include-deps;;
esac


glib-gettextize --copy --force
intltoolize --copy -f --automake


autoconf
aclocal-1.7 -I . $ACLOCAL_FLAGS
aclocal-1.7
automake-1.7 --add-missing $am_opt
autoconf
cd $THEDIR

$srcdir/configure --enable-maintainer-mode "$@"

echo 
echo "Now type 'make' to compile Red Hat Cluster Suite - Deployment Tool."
