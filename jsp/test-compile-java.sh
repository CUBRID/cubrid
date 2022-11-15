#!/bin/bash

#
#
#  Copyright 2016 CUBRID Corporation
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#

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
