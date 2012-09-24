#!/bin/sh

CUBRID=/opt/cubrid
CUBRID_DATABASES=$CUBRID/databases
CUBRID_LANG=en_US

LIB_PATH=`echo $LD_LIBRARY_PATH | grep -i cubrid`
if [ "$LIB_PATH" = "" ];
then
	LD_LIBRARY_PATH=$CUBRID/lib:$LD_LIBRARY_PATH
fi

BIN_PATH=`echo $PATH | grep -i cubrid`
if [ "$BIN_PATH" = "" ];
then
	PATH=$CUBRID/bin:$PATH
fi

export CUBRID CUBRID_DATABASES CUBRID_LANG LD_LIBRARY_PATH PATH
