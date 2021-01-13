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
