#!/bin/bash

. ./autogen.sh
                                                                                                                                                             
if [ -d build ]; then
    rm -rf build
fi

mkdir -p build && cd build
../configure --prefix=`pwd`/../../cubrid --enable-coverage --enable-debug --enable-64bit

make -j

if [ $? = 0 ]
then
    make install
    echo "build success"
    exit 0
else
    echo "build error"
    exit -1
fi

