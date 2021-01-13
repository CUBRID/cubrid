
CUBRID=/opt/cubrid
CUBRID_DATABASES=$CUBRID/databases

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

export CUBRID CUBRID_DATABASES LD_LIBRARY_PATH PATH

VERSION=${VERSION:-0}
LIB=$CUBRID/lib

if [ -f /etc/redhat-release ];then
	OS=$(cat /etc/system-release-cpe | cut -d':' -f'3-3')
	if [ $OS = "centos" ];then
		VERSION=$(cat /etc/system-release-cpe | cut -d':' -f'5-5')
	else
		OS=$(cat /etc/system-release-cpe | cut -d':' -f'4-4')
		if [ $OS = "fedora" ];then
			VERSION=$(cat /etc/system-release-cpe | cut -d':' -f'5-5')
		fi
	fi
elif [ -f /etc/os-release ];then
	OS=$(cat /etc/os-release | egrep "^ID=" | cut -d'=' -f2-2)
	VERSION=$(cat /etc/os-release | grep VERSION_ID | cut -d'=' -f2-2)
	VERSION=${VERSION#\"}
	VERSION=${VERSION%\"}
	if [ $OS = "ubuntu" ];then
		VERSION=$(echo  $VERSION | cut -d'.' -f1-1)
	fi
fi


case $OS in
	fedora)
		if [ $VERSION -ge 24 ] && [ ! -f $LIB/libncurses.so.5 ] ;then
			ln -s /lib64/libncurses.so.6 $LIB/libncurses.so.5
			ln -s /lib64/libform.so.6 $LIB/libform.so.5
			ln -s /lib64/libtinfo.so.6 $LIB/libtinfo.so.5
		fi
		;;
	centos)
		if [ $VERSION -ge 8 ] && [ ! -f $LIB/libncurses.so.5 ] ;then
			ln -s /lib64/libncurses.so.6 $LIB/libncurses.so.5
			ln -s /lib64/libform.so.6 $LIB/libform.so.5
			ln -s /lib64/libtinfo.so.6 $LIB/libtinfo.so.5
		fi
		;;
	ubuntu)
		if [ $VERSION -ge 20 ] && [ ! -f $LIB/libncurses.so.5 ] ;then
			ln -s /lib/x86_64-linux-gnu/libncurses.so.6 $LIB/libncurses.so.5
			ln -s /lib/x86_64-linux-gnu/libform.so.6 $LIB/libform.so.5
			ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 $LIB/libtinfo.so.5
		fi
		;;
	debian)
		if [ $VERSION -ge 10 ] && [ ! -f $LIB/libncurses.so.5 ] ;then
			ln -s /lib/x86_64-linux-gnu/libncurses.so.6 $LIB/libncurses.so.5
			ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 $LIB/libtinfo.so.5
			ln -s /usr/lib/x86_64-linux-gnu/libform.so.6 $LIB/libform.so.5
		fi
		;;
esac
