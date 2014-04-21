#!/bin/bash

CURR_DIR=$(dirname $0)
source $CURR_DIR/../common/common.sh

#################################################################################
# program variables
#################################################################################
prog_name='ha_check_environment.sh'

ha_temp_home=
output_file=
cubrid_path=
cubrid_db_path=
repl_log_path=
is_slave=$NO

#################################################################################
# program function
#################################################################################
function print_usage()
{
	echo ""
	echo "Usage: $prog_name [options]"
	echo ""
	echo "    -t [ha_temp_home]"
	echo "    -o [output_file]"
	echo "    -c [cubrid_path]"
	echo "    -d [cubrid_db_path]"
	echo "    -r [repl_log_path]"
	echo "    -s"
	echo ""
}

function check_args()
{
	if [ -z $ha_temp_home ]; then
		print_usage
		error "Invalid ha_temp_home."
	elif [ -z $output_file ]; then
		print_usage
		error "Invalid output_file."
	elif [ -z $cubrid_path ]; then
		print_usage
		error "Invalid cubrid_path."
	elif [ -z $cubrid_db_path ]; then
		print_usage
		error "Invalid cubrid_db_path."
	elif [ -z $repl_log_path ]; then
		print_usage
		error "Invalid repl_log_path."
	fi
	
	expect_home=$ha_temp_home/expect
}

#################################################################################
# main function
#################################################################################
while getopts "t:o:c:d:r:l:s" option
do
	case "$option" in
		"t") ha_temp_home="${OPTARG}";;
		"o") output_file="${OPTARG}";;
		"c") cubrid_path="${OPTARG}";;
		"d") cubrid_db_path="${OPTARG}";;
		"r") repl_log_path="${OPTARG}";;
		"s") is_slave=$YES;;
		"?") print_usage;;
		":") print_usage;;
		*) print_usage;;
	esac
done

check_args

if [ "$CUBRID" != "$cubrid_path" ]; then
	exit 1
fi
if [ "$CUBRID_DATABASES" != "$cubrid_db_path" ]; then
	exit 1
fi
if [ ! -d $repl_log_path ]; then
        if [ $is_slave -eq $YES ]; then
	        mkdir -p $repl_log_path
	else
		exit 1
	fi
fi

echo $(hostname) > $output_file
