#!/bin/bash

DIR=$1

if [[ ! -d $DIR ]]; then
    echo "'$DIR' is not a folder, abort."
    exit 1
fi

for dir in `find $1 -type d`; do
    (cd $dir; md5sum -s -c md5sum.txt)
    if (( $? != 0 )); then
        echo "MD5 checksum failure: $dir"
        exit 1
    fi
done
