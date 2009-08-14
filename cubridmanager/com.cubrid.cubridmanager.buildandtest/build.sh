#!/bin/sh

export ECLIPSE_HOME=~/eclipse
export JAVA_HOME=~/jdk1.5.0_11

export ANT_HOME=`ls -d ${ECLIPSE_HOME}/plugins/org.apache.ant_*`

echo %ANT_HOME%

ant