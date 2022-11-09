#!/bin/bash

TARGET=''
if [ $# == 0 ]; then
    TARGET='.'
elif [ $# == 1 ]; then
    TARGET="$1"
else
    echo 'usage: test-compile-java.sh <a test case or a directory containing test cases>'
    exit 1
fi

if [ ! -d $TARGET ] && [ ! -f $TARGET ]; then
    echo "$TARGET is neither a directory nor a file"
    exit 1
fi

JARDIR=$(realpath $(dirname $0))
SPLIB=$JARDIR/splib.jar

n=0
find $TARGET -type f | sort | while read line; do
    if [ "$line" != "${line%.java}" ]; then
        echo "# $n. compiling $line"
        log=${line%.java}.log
        if javac -cp $SPLIB $line >& $log; then
            echo " - success"
        else
            echo " - failure"
            mv $log $log.fail
        fi
        n=$((n+1))
    fi
done;
