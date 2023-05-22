#!/bin/bash
#
#  Copyright 2008 Search Solution Corporation
#  Copyright 2016 CUBRID Corporation
# 
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
# 
#       http://www.apache.org/licenses/LICENSE-2.0
# 
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
# 
#
APP_NAME=$0

show_usage ()
{
  echo "Usage: $APP_NAME [OPTIONS] [LOCALE]"
  echo " OPTIONS"
  echo "    -t arg  Set target machine (64(x86_64)); [default: 64]"
  echo "            Values for arg: x86_64, x64, 64, 64bit"
  echo "    -m arg  Set build mode(release or debug); [default: release]"
  echo "    -? | -h Show this help message and exit"
  echo ""
  echo " LOCALE  The locale name for which to build the library (de_DE, fr_FR etc.)"
  echo "         (Ommit param to build all configured locales)"
  echo " EXAMPLES"
  echo "   $APP_NAME                         # Build and pack all locales (64/release)"
  echo "   $APP_NAME -t x64 -m debug         # Create 64bit debug mode library with all locales"
  echo "   $APP_NAME -t 64bit -m debug de_DE # Create 64bit debug mode library for de_DE locale"
  echo ""
}

BUILD_TARGET=.
BUILD_MODE=.
SELECTED_LOCALE=
LOCALE_PARAM=


  while getopts ":t:m:h" opt; do
    case $opt in
      t ) if [ "$BUILD_TARGET" = "." ]; then
			BUILD_TARGET="$OPTARG"
		  else
		    GOTO error_target
		  fi			
	  ;;
      m ) if [ "$BUILD_MODE" = "." ]; then
			BUILD_MODE="$OPTARG"
		  else
		    GOTO error_build_mode
		  fi
	   ;;
      h|\?|* ) show_usage; exit 1;;
    esac
  done
  shift $(($OPTIND - 1))

if [ $# -gt 1 ]; then
show_usage
echo "Invalid number of parameters."
exit 1
else
if [ ".$1" = "." ]; then
SELECTED_LOCALE=all_locales
else
LOCALE_PARAM=$1
SELECTED_LOCALE=$1
fi
fi

  case $BUILD_TARGET in
    i386|x86|32|32bit) BUILD_TARGET=32bit;;
    x86_64|x64|64|64bit|.) BUILD_TARGET=64bit;;
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
if ! [ "$?" -eq 0 ] ; then
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

echo "Moving $CUBRID/locales/loclib/libcubrid_$SELECTED_LOCALE.so to $CUBRID/lib"
mv -f $CUBRID/locales/loclib/libcubrid_$SELECTED_LOCALE.so $CUBRID/lib/libcubrid_$SELECTED_LOCALE.so
echo "Done."

echo "Removing locale C source file $CUBRID/locales/loclib/locale.c"
rm -f $CUBRID/locales/loclib/locale.c
echo "Done."

echo "The library for selected locale(s) has been created at $CUBRID/lib/libcubrid_$SELECTED_LOCALE.so"
echo "To check compatibility and synchronize your existing databases, run:"
echo "	cubrid synccolldb <database-name>"
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

