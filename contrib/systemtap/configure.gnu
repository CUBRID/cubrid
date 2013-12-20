#!/bin/sh

opts="--prefix=$PWD/.. --disable-shared --with-pic"
srdrir=''

while test $# -ge 1; do
  case "$1" in
    -h | --help)
      echo 'configure script for systemtap support'
      exit 0
      ;;
    --prefix=*)
      shift
      ;;
    --srcdir=*)
      opts="$opts '$1'"
      srcdiropt=`echo $1 | sed 's/--srcdir=//'`
      srcdir=`readlink -f $srcdiropt`
      shift
      ;;
    *)
      opts="$opts '$1'"
      shift
      ;;
  esac
done

dtrace -C -h -s $srcdir/probes.d -o probes.h
dtrace -C -G -s $srcdir/probes.d -o probes.o
