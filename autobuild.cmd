#!/bin/bash

if [ "x$JAVA_HOME" = "x" ]; then
	JAVA_HOME=/home1/irteam/app/jdk/jdk6
fi

. ./autogen.sh

if [ -d build ]; then
	rm -rf build
fi

mkdir -p build && cd build
../configure --prefix=`pwd`/../../cubrid --enable-coverage --enable-debug --enable-64bit 

make -j
make install
if [ $? = 0 ]
	then
	echo "build success"
	exit 0
else
	echo "build error"
	exit -1 
fi

