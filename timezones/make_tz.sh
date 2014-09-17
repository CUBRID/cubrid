#!/bin/sh

APP_NAME=$0

show_usage ()
{
  echo "Usage: $APP_NAME [OPTIONS]"
  echo "Build timezone shared library for CUBRID"
  echo " OPTIONS"
  echo "    -t arg  Set target machine (32(i386) or 64(x86_64)); [default: 32]"
  echo "            Values for arg: i386, x86, 32, 32bit, x86_64, x64, 64, 64bit"
  echo "    -m arg  Set build mode(release or debug); [default: release]"
  echo "    -g arg  Set generation mode(new, update or extend); [default: new]"
  echo "            See detailed description below for each flag."
  echo "        new    = build timezone library from scratch; also generates a"
  echo "                 C file containing all timezone names (for developers)"
  echo "        update = for timezones encoded into CUBRID, update GMT offset"
  echo "                 information and daylight saving rules from the files"
  echo "                 in the input folder (no timezone C file is generated)"
  echo "        extend = build timezone library using the data in the input"
  echo "                 folder; timezone IDs encoded into CUBRID are preserved;"
  echo "                 new timezones are added; GMT offset and daylight saving"
  echo "                 information is updated (or added, for new timezones);"
  echo "                 timezone removal is not allowed (if a timezone encoded"
  echo "                 into CUBRID is missing from the input files, the"
  echo "                 associated data is imported from the old timezone library."
  echo "                 a new C file containing all timezone names is generated,"
  echo "                 and it must be included in CUBRID src before the new"
  echo "                 timezone library can be used."
  echo "    -? | -h Show this help message and exit"
  echo ""
  echo " EXAMPLES"
  echo "   $APP_NAME                  # Build and pack timezone data (32bit/release/new)"
  echo "   $APP_NAME -m debug         # Create debug mode library with timezone data"
  echo "   $APP_NAME -m debug -g update   # Update existing timezone library (in debug mode)"
  echo "   $APP_NAME -t x64 -m debug  # Build and pack timezone data (64bit/release/new)"
  echo ""
}

BUILD_TARGET=32bit
BUILD_MODE=debug
TZ_GEN_MODE=new

  while getopts ":t:m:g:h" opt; do
    case $opt in
      t ) if [ "$BUILD_TARGET" = "32bit" ]; then
			BUILD_TARGET="$OPTARG"
		  else
		    GOTO error_target
		  fi			
	  ;;
      m ) if [ "$BUILD_MODE" = "debug" ]; then
			BUILD_MODE="$OPTARG"
		  else
		    GOTO error_build_mode
		  fi
	   ;;
	  g ) if [ "$TZ_GEN_MODE" = "new" ]; then
			TZ_GEN_MODE="$OPTARG"
		  else
		    GOTO error_tz_gen_mode
		  fi
	   ;;
      h|\?|* ) show_usage; exit 1;;
    esac
  done
  shift $(($OPTIND - 1))

if [ $# -gt 0 ]; then
show_usage
echo "Parameter count must be zero."
exit 1
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
  
  case $TZ_GEN_MODE in
    new) TZ_GEN_MODE=new;;
	update) TZ_GEN_MODE=update;;
	extend) TZ_GEN_MODE=extend;;
    *) 
	  show_usage
	  echo "Generation mode [$TZ_GEN_MODE] is not valid"
	  exit 1
	;;
  esac

echo " Running $APP_NAME with parameters:"
echo "         BUILD_TARGET = $BUILD_TARGET"
echo "         BUILD_MODE   = $BUILD_MODE"
echo "         TZ_GEN_MODE  = $TZ_GEN_MODE"
echo ""


echo "Generating timezone C file in mode $TZ_GEN_MODE"

PS=$(cubrid gen_tz -g $GEN_TZ_MODE 2>&1)
if ! [ "$?" -eq 0 ] ; then
	echo $PS
	echo "Error: Command cubrid gen_tz -g $GEN_TZ_MODE failed!"
	exit 1
fi


current_dir=$CD
cd $CUBRID/timezones/tzlib
echo "Compiling timezone library"
sh build_tz.sh $BUILD_TARGET $BUILD_MODE
if [ ! -e $CUBRID/timezones/tzlib/libcubrid_timezones.so ]
then 
echo "Compile failed! Run \"$CUBRID/timezones/tzlib/build_tz.sh $BUILD_TARGET $BUILD_MODE\" for more details"
cd $current_dir
exit 1
else
echo "Done."
cd $current_dir
fi

echo "Moving $CUBRID/timezones/tzlib/libcubrid_timezones.so to $CUBRID/lib"
mv -f $CUBRID/timezones/tzlib/libcubrid_timezones.so $CUBRID/lib/libcubrid_timezones.so
echo "Done."

echo "Removing locale C source file $CUBRID/timezones/tzlib/timezones.c"
rm -f $CUBRID/timezones/tzlib/timezones.c
echo "Done."

echo "The timezone library has been created at $CUBRID/lib/libcubrid_timezones.so"
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

:error_tz_gen_mode
show_usage
echo ""
echo "Generation mode already set to $TZ_GEN_MODE"
exit 1


:error_param
show_usage
echo "Invalid parameter "
exit 1

