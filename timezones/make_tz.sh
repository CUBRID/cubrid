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
  echo "    -d arg  Set the database name used when in extend mode"
  echo "    -? | -h Show this help message and exit"
  echo ""
  echo " EXAMPLES"
  echo "   $APP_NAME                  # Build and pack timezone data (32bit/release/new)"
  echo "   $APP_NAME -m debug         # Create debug mode library with timezone data"
  echo "   $APP_NAME -m debug -g update   # Update existing timezone library (in debug mode)"
  echo "   $APP_NAME -t x64 -m debug  # Build and pack timezone data (64bit/release/new)"
  echo ""
}

error_database_name()
{
	show_usage
	echo ""
	echo "When using extend we must have a database"
	exit 1
}

BUILD_TARGET=32bit
BUILD_MODE=release
TZ_GEN_MODE=new
DATABASE_NAME=""

while getopts ":t:m:g:d:h" opt; do
	case $opt in
		t ) BUILD_TARGET="$OPTARG";;
		m ) BUILD_MODE="$OPTARG";;
		g ) TZ_GEN_MODE="$OPTARG";;
		d ) DATABASE_NAME="$OPTARG";;
		h|\?|* ) show_usage; exit 1;;
	esac
done
shift $(($OPTIND - 1))

if [[ "$DATABASE_NAME" = "" && "$TZ_GEN_MODE" = "extend" ]]; then
	error_database_name
fi

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
	debug) ;;
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
if [ "$TZ_GEN_MODE" = "extend" ]; then
	echo "         DATABASE_NAME  = $DATABASE_NAME"
fi
echo ""

echo "Generating timezone C file in mode $TZ_GEN_MODE"

PS=$(cubrid gen_tz -g $TZ_GEN_MODE $DATABASE_NAME 2>&1)
if ! [ "$?" -eq 0 ] ; then
	echo $PS
	echo "Error: Command cubrid gen_tz -g $TZ_GEN_MODE failed!"
	exit 1
fi

echo $PS | grep -o 'Could not make all the data backward compatible!'

current_dir=$CD
cd $CUBRID/timezones/tzlib
echo "Compiling timezone library"
sh build_tz.sh $BUILD_TARGET $BUILD_MODE

LIB_TZ_NAME=""
SYS=`uname -s`
if [ "x$SYS" = "xAIX" ]; then
	LIB_TZ_NAME="libcubrid_timezones.a"
else
	LIB_TZ_NAME="libcubrid_timezones.so"
fi

if [ ! -e $CUBRID/timezones/tzlib/$LIB_TZ_NAME ]; then 
	echo "Compile failed! Run \"$CUBRID/timezones/tzlib/build_tz.sh $BUILD_TARGET $BUILD_MODE\" for more details"
	cd $current_dir
	exit 1
else
	echo "Done."
	cd $current_dir
fi

echo "Moving $CUBRID/timezones/tzlib/$LIB_TZ_NAME to $CUBRID/lib"
mv -f $CUBRID/timezones/tzlib/$LIB_TZ_NAME $CUBRID/lib/$LIB_TZ_NAME
echo "Done."

echo "Removing locale C source file $CUBRID/timezones/tzlib/timezones.c"
rm -f $CUBRID/timezones/tzlib/timezones.c
echo "Done."

echo "The timezone library has been created at $CUBRID/lib/$LIB_TZ_NAME"
exit 0
