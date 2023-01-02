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

if [ $# != 1 ]; then
    echo 'usage: test-transpile-plcsql.sh <a test case or a directory containing test cases>'
    exit 1
fi

if [ ! -d $1 ] && [ ! -f $1 ]; then
    echo "$1 is neither a directory nor a file"
    exit 1
fi

JAR='jspserver.jar'
APP='com.cubrid.plcsql.handler.TestMain'

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

FILES=$(find $1 -type f -name '*.sql' | sort)
#echo $FILES
java -ea -cp $JAR $APP $FILES

