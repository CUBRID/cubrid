#!/bin/bash
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
# DESCRIPTION:
#
# This script retrieves the function name of the given memory address.
# The shared object must be inspected with gdb.
#

pid=$(ps x | grep "cub_server $1" | grep -v grep | cut -f 2 -d ' ')

lib_start_addr=$(cat /proc/$pid/maps | grep libcubrid.so | grep 00000000 | cut -f 1 -d ' ' | cut -f 1 -d '-')

calltrace_off=$(($2-0x$lib_start_addr))
calltrace_off=$(printf "%x\n" $calltrace_off)

line_nr=$(addr2line -e $CUBRID"/lib/libcubrid.so" $calltrace_off)
echo $line_nr
