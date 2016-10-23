#!/bin/sh

RADIR=$1
PREFIX=$2
SUFFIX=$3

find "$RADIR" -type f -executable | while read -r file; do
    echo "${PREFIX}$(basename "$file")${SUFFIX}"
done
