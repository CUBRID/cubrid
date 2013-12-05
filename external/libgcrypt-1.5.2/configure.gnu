#!/bin/sh

opts_libgcrypt="--prefix=$PWD/.. --disable-shared --enable-static --with-pic"

srcdir=''

build_len="32"

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
      opts_libgcrypt="$opts_libgcrypt '$1'"
      srcdiropt=`echo $1 | sed 's/--srcdir=//'`
      srcdir=`readlink -f $srcdiropt`
      shift
      ;;
    --enable-64bit)
      opts_libgcrypt="$opts_libgcrypt"
	  build_len="64"
	  shift
      ;; 	  
    *)
      shift
      ;;
  esac
done

SYS=`uname -a`
echo "SYS=$SYS"
case $SYS in
  *x86_64* | *i386* | *i486* | *i586* | *i686*)
    CFLAGS="$CFLAGS -fPIC"
    opts_libgcrypt="$opts_libgcrypt CFLAGS=\"$CFLAGS\""
    if test "$build_len" = "32"
	then
	  opts_libgcrypt="$opts_libgcrypt --build=x86"
	else
	  opts_libgcrypt="$opts_libgcrypt --build=x86_64"
	fi
    ;;
  *)
    opts_libgcrypt="$opts_libgcrypt --disable-asm"
    ;;
esac 

if test -f config.status; then
  echo "configured already. skip $PWD"
else
  eval "$srcdir/configure $opts_libgcrypt"
fi

