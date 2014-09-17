#!/bin/sh
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
#
#   This program is free software; you can redistribute it and/or modify 
#   it under the terms of the GNU General Public License as published by 
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version. 
#
#  This program is distributed in the hope that it will be useful, 
#  but WITHOUT ANY WARRANTY; without even the implied warranty of 
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
#  GNU General Public License for more details. 
#
#  You should have received a copy of the GNU General Public License 
#  along with this program; if not, write to the Free Software 
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

show_usage ()
{
  echo "USAGE: sh build_tz.sh 32bit|64bit debug|release"
  echo ""
}

echo Building shared object for platform "$1", in $2 mode with tag "timezone"

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
$GCC64 $DBG_PARAM -Wall -fPIC -c timezones.c
$GCC64 -shared -o libcubrid_timezones.so timezones.o
else
if [ "$1" = "32bit" ]
then
$GCC32 $DBG_PARAM -m32 -Wall -fPIC -c timezones.c
$GCC32 -shared -o libcubrid_timezones.so timezones.o
else
show_usage
exit 1
fi
fi

rm -f timezones.o
