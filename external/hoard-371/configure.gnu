#!/bin/sh

srcdir=''
TARGET='linux-gcc-x86-static'

while test $# -ge 1; do
  case "$1" in
    -h | --help)
      echo 'configure script for external package'
      exit 0
      ;;
    --srcdir=*)
      srcdiropt=`echo $1 | sed 's/--srcdir=//'`
      srcdir=`readlink -f $srcdiropt`
      shift
      ;;
    --enable-64bit)
      TARGET='linux-gcc-x86-64-static'
      shift
      ;;
    *)
      shift
      ;;
  esac
done

case $SYSTEM_TYPE in
  *linux*) 
    echo "building hoard with target $TARGET"
    ;;
  *)
    TARGET=""
    ;;
esac

LIBHOARD=libhoard.a
if [ -f $LIBHOARD ]; then
	echo "built already. skip $PWD"
else
	HOARD=$srcdir
	(cd $HOARD/src; find . -type f | grep -vw ".svn") | while read F; do
		mkdir -p `dirname $F`
		rm -f $F; ln -s $HOARD/src/$F $F
	done
	eval make -j $TARGET
fi
