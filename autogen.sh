#!/bin/bash

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

run ()
{
  echo "** Running $1"
  (cd $srcdir && eval $*)
  if test $? -ne 0 ; then
    echo "** ERROR: while running '$*'"
    return 1
  fi
}

{
  run libtoolize --automake --copy
} &&
{
  run aclocal -I config
} &&
{
  run autoheader
} &&
{
  run autoconf --force
} &&
{
  run automake --add-missing --copy -Wall
}

if test $? -eq 0 ; then
  echo "** SUCCESS: $0 completed"

  if test $# -eq 0 ; then
    echo
    echo "** Please run $srcdir/configure and make to build"
  else
    echo "** Running $srcdir/configure $@"
    $srcdir/configure "$@"
    if test $? -eq 0 ; then
      echo
      echo "** Please run make to compile"
    else
      echo
      echo "** ERROR: while running 'configure'"
      exit 1
    fi
  fi
else
  echo "** ERROR: $0 failed"
  exit 1
fi

