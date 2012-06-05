#!/bin/sh

opts="--prefix=$PWD/.. --disable-shared --enable-widec"
srdrir=''

while test $# -ge 1; do
  case "$1" in
    -h | --help)
      echo 'configure script for external package'
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
    --enable-64bit)
      opts="$opts --with-pic"
      shift
      ;;
    *)
      opts="$opts '$1'"
      shift
      ;;
  esac
done

if test -f config.status; then
  echo "configured already. skip $PWD"
else
  eval "$srcdir/configure $opts"
fi
