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
  echo "USAGE: sh build_locale.sh 32bit|64bit debug|release <locale_lib_tag>"
  echo "locale_so_tag = string value used to decorate shred object name."
  echo ""
}

echo Building shared object for platform "$1", in $2 mode, with tag "$3"

if [ "$2" = "debug" ]; then
DBG_PARAM=-g
else
if [ "$2" = "release" ]; then
DBG_PARAM=
else
show_usage
exit 1
fi
fi

SYS=`uname -s`
if [ "x$SYS" = "xAIX" ]; then
GCC32="gcc -maix32"
GCC64="gcc -maix64"
else
GCC32="gcc -m32"
GCC64="gcc -m64"
fi

if [ "$1" = "64bit" ]
then
$GCC64 $DBG_PARAM -Wall -fPIC -c locale.c
$GCC64 -shared -o libcubrid_$3.so locale.o
else
if [ "$1" = "32bit" ]
then
$GCC32 $DBG_PARAM -Wall -fPIC -c locale.c
$GCC32 -shared -o libcubrid_$3.so locale.o
else
show_usage
exit 1
fi
fi

rm -f locale.o
