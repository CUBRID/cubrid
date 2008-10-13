#!/bin/bash

PWD=`pwd`
CMD=$0
PREPATH=${CMD%/*}
EXTERNAL=${PWD%/*}
XDBMS=`cd ${EXTERNAL%/*} && pwd`
ARGS="CC=\"$CC\" CXX=\"$CXX\""

while [ -n "$(echo $1)" ]; do
	case $1 in
		--enable-64bit ) ;;
	esac
	shift
done

case $SYSTEM_TYPE in
	*linux*) 
		if test "$BIT_MODEL"="-m32"
		then
			TARGET="linux-gcc-x86-static"
		else
			TARGET="linux-gcc-$MACHING_TYPE-static"
		fi
		;;
	*)       TARGET="" ;;
esac

LIBHOARD=libhoard.a
#echo "$CONFIGURE $ARGS"
if [ -f $LIBHOARD ]; then
	TMP=""
else
	HOARD=$(readlink -f $PREPATH)
	(cd $HOARD/src; find . -type f) | while read F; do
		mkdir -p `dirname $F`
		rm -f $F; ln -s $HOARD/src/$F $F
	done
	eval make $TARGET
	eval mkdir -p $EXTERNAL/lib
	eval cp -f $LIBHOARD $EXTERNAL/lib
fi
