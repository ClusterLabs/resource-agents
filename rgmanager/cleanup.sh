#!/bin/bash

make maintainer-clean
rm -rf autom4te* aclocal.m4 configure config.* install-sh mkinstalldirs missing
find . -name "Makefile" | xargs rm -f
find . -name "Makefile.in" | xargs rm -f
find . -type f -name "*~" | xargs rm -f
rm -f stamp-h1 depcomp
rm -f include/config.h.*
rm -f examples/clumanager.spec

