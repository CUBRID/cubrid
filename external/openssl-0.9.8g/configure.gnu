#!/bin/bash

PWD=`pwd`
CMD=$0
PREPATH=${CMD%/*}
EXTERNAL=${PWD%/*}
CONFIGURE=$PREPATH/config
ARGS="-m32 --prefix=$EXTERNAL no-asm no-shared"

while [ -n "$(echo $1)" ]; do
	case $1 in
		--enable-64bit ) ARGS=${ARGS#"-m32"} ;;
	esac
	shift
done

#echo "$CONFIGURE $ARGS"
if [ -f Makefile ]; then
	TMP=""
else
	OPENSSL=$(readlink -f $PREPATH)
	if [ "$PWD" != "$OPENSSL" ]
	then
		(cd $OPENSSL; find . -type f) | while read F; do
			mkdir -p `dirname $F`
			rm -f $F; ln -s $OPENSSL/$F $F
		done
	fi
	eval $CONFIGURE $ARGS
	eval make
	eval make install
fi
