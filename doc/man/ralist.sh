#!/bin/sh

RADIR=$1
PREFIX=$2
SUFFIX=$3

for f in `find $RADIR -type f -executable`; do
    echo ${PREFIX}`basename $f`${SUFFIX}
done
