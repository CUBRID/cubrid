#!/bin/bash
#
#  Copyright 2008 Search Solution Corporation
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
#

show_usage ()
{
	echo "USAGE: sh build_tz.sh 32bit|64bit debug|release"
	echo ""
}

echo Building shared object for platform "$1", in $2 mode with tag "timezone"

if [ "$2" = "debug" ]; then
	DBG_PARAM=-g
elif [ "$2" = "release" ]; then
	DBG_PARAM=
else
	show_usage
	exit 1
fi

SYS=`uname -s`
if [ "x$SYS" = "xAIX" ]; then
	GCC32="gcc -maix32"
	GCC64="gcc -maix64"
else
	GCC32="gcc -m32"
	GCC64="gcc -m64"
fi

if [ "$1" = "64bit" ]; then
	$GCC64 $DBG_PARAM -Wall -fPIC -c timezones.c
	$GCC64 -shared -o libcubrid_timezones.so timezones.o
elif [ "$1" = "32bit" ]; then
	$GCC32 $DBG_PARAM -Wall -fPIC -c timezones.c
	$GCC32 -shared -o libcubrid_timezones.so timezones.o
else
	show_usage
	exit 1
fi

if [ ! -e "libcubrid_timezones.so" ]; then
	echo "Failed to generate libcubrid_timezones.so"
	exit 1
fi

if [ "x$SYS" = "xAIX" ]; then
	MAJOR_NUMBER=`cubrid --version | perl -ne 'if( /(\d+)\.\d+\.\d+\.\d+/ ) {print "$1"}'`
	
	mv libcubrid_timezones.so libcubrid_timezones.so.$MAJOR_NUMBER
	
	if [ "$1" = "64bit" ]; then
		ar -X64 cru libcubrid_timezones.a libcubrid_timezones.so.$MAJOR_NUMBER
	else
		ar -X32 cru libcubrid_timezones.a libcubrid_timezones.so.$MAJOR_NUMBER
	fi
fi

rm -f timezones.o
