#!/bin/sh

opts="--prefix=$PWD/.. --with-gpg-error-prefix=$PWD/.. --disable-shared --enable-static"
opts_gpg_error="--prefix=$PWD/.. --disable-shared --enable-static"

fpic_flags="CFLAGS=\"-fPIC\" CXXFLAGS=\"-fPIC\""

lib_gpg_error="-L${PWD}/../lib libgpg-error.a"

srcdir=''

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
      opts="$opts $fpic_flags"
      opts_gpg_error="$opts $fpic_flags"
	  shift
      ;; 	  
    *)
      shift
      ;;
  esac
done

srcdir_gpg_error="${srcdir}/../libgpg-error-1.11/"

current_dir=`pwd`

if test -f config.status; then
  echo "configured already. skip $PWD"
else
  eval "cd $srcdir_gpg_error"
  eval "./configure $opts_gpg_error"  
  eval "make"
  eval "make install"
  eval "cd $current_dir"
  eval "$srcdir/configure $opts"
  #sed "/^GPG_ERROR_LIBS/c \GPC_ERROR_LIBS=${lib_gpg_error}" Makefile > Makefile.tmp
  #mv -f Makefile.tmp Makefile
fi