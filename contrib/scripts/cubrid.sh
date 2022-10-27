#
#
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

export CUBRID=/opt/cubrid
export CUBRID_DATABASES=$CUBRID/databases

LD_LIBRARY_PATH=$CUBRID/lib:$CUBRID/cci/lib:$LD_LIBRARY_PATH
SHLIB_PATH=$LD_LIBRARY_PATH
LIBPATH=$LD_LIBRARY_PATH
PATH=$CUBRID/bin:/usr/sbin:$PATH
export LD_LIBRARY_PATH SHLIB_PATH LIBPATH PATH

is_ncurses5=$(ldconfig -p | grep libncurses | grep "so.5" | wc -l)

if [ $is_ncurses5 -eq 0 ] && [ ! -f $CUBRID/lib/libncurses.so.5 ];then
  for lib in libncurses libform libtinfo
  do
    curses_lib=$(ldconfig -p | grep $lib.so | grep -v "so.[1-4]" | sort -h | tail -1 | awk '{print $4}')
    if [ -z "$curses_lib" ];then
      echo "$lib.so: CUBRID requires the ncurses package. Make sure the ncurses package is installed"
      break
    fi
    ln -s $curses_lib $CUBRID/lib/$lib.so.5
  done
fi
