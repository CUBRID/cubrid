#!/bin/sh

opts="--prefix=$PWD/.."
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
    *)
      opts="$opts '$1'"
      shift
      ;;
  esac
done

if test -f config.status; then
  echo "configured already. skip $PWD"
else
  eval "touch $srcdir/src/parse-gram.[ch]; $srcdir/configure $opts"
fi
