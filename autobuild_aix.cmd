#!/bin/bash

# Here you can specify it
OPTS="--prefix=$HOME/cubrid --enable-64bit --enable-debug"

# You'd better set JAVA_HOME
if [ "x$JAVA_HOME" = "x" ]; then
	JAVA_HOME=/home1/irteam/app/jdk/jdk6
fi

# default values
MAKE="make"
BIT_MODEL="32"

for opt in $OPTS
do
  case "$opt" in
    --enable-64bit)
      BIT_MODEL="64"
      shift
      ;;
    *)
      shift
      ;;
  esac
done

SYS=`uname -s`
if test "x$SYS" = "xAIX"
then
	MAKE="gmake"
	OPTS="$OPTS --with-mysql=no --with-mysql51=no --with-oracle=no"
	CC="gcc -maix$BIT_MODEL"
	CXX="g++ -maix$BIT_MODEL"
	OBJECT_MODE="$BIT_MODEL"

	export CC CXX OBJECT_MODE
fi

. ./autogen.sh

if [ -d build ]; then
	rm -rf build
fi

mkdir -p build && cd build
../configure $OPTS && $MAKE && $MAKE install

if [ $? = 0 ]
then
	echo "build success"
	exit 0
else
	echo "build error"
	exit -1
fi

