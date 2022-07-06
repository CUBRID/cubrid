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

export LD_LIBRARY_PATH=$CUBRID/lib:$CUBRID/cci/lib:$LD_LIBRARY_PATH
export PATH=$CUBRID/bin:$PATH

export SHLIB_PATH=$LD_LIBRARY_PATH
export LIBPATH=$LD_LIBRARY_PATH


LIB=$CUBRID/lib

if [ -f /etc/redhat-release ];then
	OS=$(cat /etc/system-release-cpe | cut -d':' -f'3-3')
elif [ -f /etc/os-release ];then
	OS=$(cat /etc/os-release | egrep "^ID=" | cut -d'=' -f2-2)
fi

case $OS in
	fedoraproject | centos | redhat | rocky)
		if [ ! -h /lib64/libncurses.so.5 ] && [ ! -h $LIB/libncurses.so.5 ];then
			ln -s /lib64/libncurses.so.6 $LIB/libncurses.so.5
			ln -s /lib64/libform.so.6 $LIB/libform.so.5
			ln -s /lib64/libtinfo.so.6 $LIB/libtinfo.so.5
		fi
		;;
	ubuntu)
		if [ ! -h /lib/x86_64-linux-gnu/libncurses.so.5 ] && [ ! -h $LIB/libncurses.so.5 ];then
			ln -s /lib/x86_64-linux-gnu/libncurses.so.6 $LIB/libncurses.so.5
			ln -s /lib/x86_64-linux-gnu/libform.so.6 $LIB/libform.so.5
			ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 $LIB/libtinfo.so.5
		fi
		;;
	debian)
		if [ ! -h /lib/x86_64-linux-gnu/libncurses.so.5 ] && [ ! -h $LIB/libncurses.so.5 ];then
			ln -s /lib/x86_64-linux-gnu/libncurses.so.6 $LIB/libncurses.so.5
			ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 $LIB/libtinfo.so.5
			ln -s /usr/lib/x86_64-linux-gnu/libform.so.6 $LIB/libform.so.5
		fi
		;;
	*)
		echo "CUBRID requires the ncurses package. Make sure the ncurses package is installed"
		;;
esac
