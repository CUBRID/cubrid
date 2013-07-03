#!/bin/sh

opts_libgcrypt="--prefix=$PWD/.. --with-gpg-error-prefix=$PWD/.. --disable-shared --enable-static"
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
      opts_libgcrypt="$opts_libgcrypt '$1'"
      srcdiropt=`echo $1 | sed 's/--srcdir=//'`
      srcdir=`readlink -f $srcdiropt`
	  opts_gpg_error="$opts_gpg_error --srcdir=${srcdir}/../libgpg-error-1.11/"
      shift
      ;;
    --enable-64bit)
      opts_libgcrypt="$opts_libgcrypt $fpic_flags"
      opts_gpg_error="$opts_gpg_error $fpic_flags"
	  shift
      ;; 	  
    *)
      shift
      ;;
  esac
done

srcdir_gpg_error="${srcdir}/../libgpg-error-1.11/"

current_dir=`pwd`

current_gpg_dir="$current_dir/../libgpg-error-1.11/"

opts_libgcrypt="$opts_libgcrypt --disable-asm"

if test -f config.status; then
  echo "configured already. skip $PWD"
else
  echo "configure: start to build libgpg-error for libgcrypt!"
  if test ! -d $current_gpg_dir
  then
      echo "configure: mkdir $current_gpg_dir"
      mkdir $current_gpg_dir
  fi
  eval "cd $current_gpg_dir"
  #echo "configure: call ${srcdir_gpg_error}/autogen.sh"
  #eval "${srcdir_gpg_error}/autogen.sh"
  echo "configure: call ${srcdir_gpg_error}/configure $opts_gpg_error"
  eval "${srcdir_gpg_error}/configure $opts_gpg_error" 
  echo "configure: call make for libgpg-error"  
  eval "make"
  echo "configure: call make install for libgpg-error"
  eval "make install"
  echo "configure: complete to build libgpg-error for libgcrypt!"
  eval "cd $current_dir"
  eval "$srcdir/configure $opts_libgcrypt"
  #sed "/^GPG_ERROR_LIBS/c \GPC_ERROR_LIBS=${lib_gpg_error}" Makefile > Makefile.tmp
  #mv -f Makefile.tmp Makefile
fi
