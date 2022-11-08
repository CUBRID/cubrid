#!/bin/bash

if [ $# != 1 ]; then
    echo 'usage: test-transpile-plcsql.sh <a test case or a directory containing test cases>'
    exit 1
fi

if [ ! -d $1 ] && [ ! -f $1 ]; then
    echo "$1 is neither a directory nor a file"
    exit 1
fi

JAR='jspserver.jar'
APP='com.cubrid.plcsql.driver.TestDriver'

echo "trying test cases contained in the directory:"
echo "  $1"

if [ -d ./pt ]; then
    if [ -d ./pt-back0 ]; then
        if [ -d ./pt-back1 ]; then
            rm -rf ./pt-back1
        fi
        mv  ./pt-back0 ./pt-back1
    fi
    mv ./pt ./pt-back0
fi
mkdir ./pt

n=0
find $1 -type f | sort | while read line; do
    if [ "$line" != "${line%.sql}" ]; then
        echo "file #$n: $line"

        if java -ea -cp $JAR $APP $line $n; then
            echo ' - success'
        else
            echo ' - failure'
        fi
        n=$((n+1))
    fi
done
