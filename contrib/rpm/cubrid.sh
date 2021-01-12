
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

centos=$(cat /etc/redhat-release | awk '{print $4}' | cut -c 1)
if [ "$centos"x = "8x" ] && [ "CUBRID"x != "x" ] && [ ! -f $CUBRID/lib/libncurses.so.5 ];then
        ln -s /lib64/libncurses.so.6 $CUBRID/lib/libncurses.so.5
        ln -s /lib64/libtinfo.so.6 $CUBRID/lib/libtinfo.so.5
        ln -s /lib64/libform.so.6 $CUBRID/lib/libform.so.5
fi
