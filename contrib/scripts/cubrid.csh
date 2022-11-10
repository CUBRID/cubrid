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

setenv CUBRID /opt/cubrid
setenv CUBRID_DATABASES $CUBRID/databases

if ( ${?LD_LIBRARY_PATH} ) then
	setenv LD_LIBRARY_PATH $CUBRID/lib:$CUBRID/cci/lib:$LD_LIBRARY_PATH
else
	setenv LD_LIBRARY_PATH $CUBRID/lib:$CUBRID/cci/lib
endif

set path=($CUBRID/bin /usr/sbin $path)
set curses_lib=

setenv SHLIB_PATH $LD_LIBRARY_PATH
setenv LIBPATH $LD_LIBRARY_PATH

set is_ncurses5=`ldconfig -p`
if ( $status != 0 ) then
  echo "ldconfig: Command not found or permission denied, please check it."
  exit 1
endif

set is_ncurses5=`ldconfig -p | grep libncurses.so.5 | wc -l`

if ( $is_ncurses5 == 0 && ! -f $CUBRID/lib/libncurses.so.5 ) then
  foreach lib ( libncurses libform libtinfo )
    set curses_lib=`ldconfig -p | grep $lib.so | grep -v "so.[1-4]" | sort -h | tail -1 | awk '{print $4}'`
    if ( -z "$curses_lib" ) then
      echo "$lib.so: CUBRID requires the ncurses package. Make sure the ncurses package is installed"
      break
    endif
    ln -s $curses_lib $CUBRID/lib/$lib.so.5
  end
endif
