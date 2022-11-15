#!/bin/bash

#
# Copyright (C) 2008 Search Solution Corporation.
# Copyright (c) 2016 CUBRID Corporation.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
#
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# - Neither the name of the <ORGANIZATION> nor the names of its contributors
#   may be used to endorse or promote products derived from this software without
#   specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
# OF SUCH DAMAGE.
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
