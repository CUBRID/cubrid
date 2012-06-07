#!/bin/bash
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
#
#   This program is free software; you can redistribute it and/or modify 
#   it under the terms of the GNU General Public License as published by 
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version. 
#
#  This program is distributed in the hope that it will be useful, 
#  but WITHOUT ANY WARRANTY; without even the implied warranty of 
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
#  GNU General Public License for more details. 
#
#  You should have received a copy of the GNU General Public License 
#  along with this program; if not, write to the Free Software 
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
APP_NAME=$0

function show_usage ()
{
  echo "Usage: $APP_NAME [OPTIONS] [LOCALE]"
  echo " OPTIONS"
  echo "  -t arg  Set target machine (32(i386) or 64(x86_64)); [default: 32]"
  echo "  -m arg  Set build mode(release or debug); [default: release]"
  echo "          Values for arg: i386, x86, 32, 32bit, x86_64, x64, 64, 64bit"
  echo "  -? | -h Show this help message and exit"
  echo "  LOCALE  The locale name for which to build the library (de_DE, fr_FR etc.)"
  echo "          (Ommit param to build all configured locales)"
  echo " EXAMPLES"
  echo " $APP_NAME                         # Build and pack all locales (32/release)"
  echo " $APP_NAME -t 32bit de_DE          # 32bit release library for de_DE (German) locale"
  echo " $APP_NAME -t x64 -m debug         # Create 64bit debug mode library with all locales"
  echo " $APP_NAME -t 64bit -m debug de_DE # Create 64bit debug mode library for de_DE locale"
  echo ""
}

BUILD_TARGET=.
BUILD_MODE=.
SELECTED_LOCALE=
LOCALE_PARAM=


  while getopts ":t:m:h" opt; do
    case $opt in
      t ) if [ "$BUILD_TARGET" == "." ]; then
			BUILD_TARGET="$OPTARG"
		  else
		    GOTO error_target
		  fi			
	  ;;
      m ) if [ "$BUILD_MODE" == "." ]; then
			BUILD_MODE="$OPTARG"
		  else
		    GOTO error_build_mode
		  fi
	   ;;
      h|\?|* ) show_usage; echo "Invalid parameter."; exit 1;;
    esac
  done
  shift $(($OPTIND - 1))

if [ $# -gt 1 ]; then
show_usage
echo "Invalid number of parameters."
exit 1
else
if [ ".$1" == "." ]; then
SELECTED_LOCALE=all_locales
else
LOCALE_PARAM=$1
SELECTED_LOCALE=$1
fi
fi

  case $BUILD_TARGET in
    i386|x86|32|32bit|.) BUILD_TARGET=32bit;;
    x86_64|x64|64|64bit) BUILD_TARGET=64bit;;
    *) 
	  show_usage
	  echo "Target [$BUILD_TARGET] is not valid target"
	  exit 1
	;;
  esac

  case $BUILD_MODE in
    release|.) BUILD_MODE="release";;
	debug);;
    *) 
	  show_usage
	  echo "Mode [$BUILD_MODE] is not valid mode"
	  exit 1
	;;
  esac

echo " Running $APP_NAME with parameters:"
echo "         BUILD_TARGET    = $BUILD_TARGET"
echo "         BUILD_MODE      = $BUILD_MODE"
echo "         SELECTED_LOCALE = $SELECTED_LOCALE"
echo ""


echo "Generating locale C file for $SELECTED_LOCALE"

PS=$(cubrid genlocale $LOCALE_PARAM 2>&1)
if [ "$PS" != "" ]; then
	echo $PS
	echo "Error: Command cubrid genlocale $LOCALE_PARAM failed!"
	exit 1
fi


current_dir=$CD
cd $CUBRID/locales/loclib
echo "Compiling locale library"
sh build_locale.sh $BUILD_TARGET $BUILD_MODE $SELECTED_LOCALE
if [ ! -e $CUBRID/locales/loclib/libcubrid_$SELECTED_LOCALE.so ]
then 
echo "Compile failed! Run \"$CUBRID/locales/loclib/build_locale.sh $1 $2\" for more details"
cd $current_dir
exit 1
else
echo "Done."
cd $current_dir
fi

echo "Copying $CUBRID/locales/loclib/libcubrid_$SELECTED_LOCALE.so to $CUBRID\lib"
cp $CUBRID/locales/loclib/libcubrid_$SELECTED_LOCALE.so $CUBRID/lib/libcubrid_$SELECTED_LOCALE.so
echo "Done."

echo "Removing shared object $CUBRID/locales/loclib/libcubrid_$SELECTED_LOCALE.so"
rm -f $CUBRID/locales/loclib/libcubrid_$SELECTED_LOCALE.so
echo "Done."

echo "Removing locale C source file $CUBRID/locales/loclib/locale.c"
rm -f $CUBRID/locales/loclib/locale.c
echo "Done."

echo "The library for $SELECTED_LOCALE has been created."
echo "SUCCESS!"
echo "Locale library for $SELECTED_LOCALE can be found at $CUBRID/lib/libcubrid_$SELECTED_LOCALE.so"
echo "Edit $CUBRID/conf/cubrid_locales.txt to perform complete integration with CUBRID"
exit 0

:error_target
show_usage
echo ""
echo "Target already set to $BUILD_TARGET"
exit 1

:error_build_mode
show_usage
echo ""
echo "Build mode already set to $BUILD_MODE"
exit 1

:error_param
show_usage
echo "Invalid parameter "
exit 1

