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

set path=($CUBRID/bin $path)

setenv SHLIB_PATH $LD_LIBRARY_PATH
setenv LIBPATH $LD_LIBRARY_PATH

set LIB=$CUBRID/lib

if ( -f /etc/redhat-release ) then
        set OS=`cat /etc/system-release-cpe | cut -d':' -f'3-3'`
else if ( -f /etc/os-release ) then
        set OS=`cat /etc/os-release | egrep "^ID=" | cut -d'=' -f2-2`
     endif
endif

switch ($OS)
  case "fedoraproject":
  case "centos":
  case "redhat":
  case "rocky":
    if ( ! -f /lib64/libncurses.so.5 && ! -f $LIB/libncurses.so.5 ) then
    	ln -s /lib64/libncurses.so.6 $LIB/libncurses.so.5
    	ln -s /lib64/libform.so.6 $LIB/libform.so.5
    	ln -s /lib64/libtinfo.so.6 $LIB/libtinfo.so.5
    endif
    breaksw
  case "ubuntu":
    if ( ! -f /lib/x86_64-linux-gnu/libncurses.so.5 && ! -f $LIB/libncurses.so.5 ) then
      ln -s /lib/x86_64-linux-gnu/libncurses.so.6 $LIB/libncurses.so.5
      ln -s /lib/x86_64-linux-gnu/libform.so.6 $LIB/libform.so.5
      ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 $LIB/libtinfo.so.5
    endif
    breaksw
  case "debian":
    if ( ! -f /lib/x86_64-linux-gnu/libncurses.so.5 && ! -f $LIB/libncurses.so.5 ) then
            ln -s /lib/x86_64-linux-gnu/libncurses.so.6 $LIB/libncurses.so.5
            ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 $LIB/libtinfo.so.5
            ln -s /usr/lib/x86_64-linux-gnu/libform.so.6 $LIB/libform.so.5
    endif
    breaksw
  default:
    echo "CUBRID requires the ncurses package. Make sure the ncurses package is installed"
    breaksw
endsw
