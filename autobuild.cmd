#!/bin/bash

JAVA_HOME=/home1/irteam/app/jdk/jdk6

. ./autogen.sh

./configure --prefix=$HOME/deploy

make

make install

if [ $? = 0 ] 
  then
    echo "build success"
    exit 0
  else
    echo "build error"
    exit -1 
fi
