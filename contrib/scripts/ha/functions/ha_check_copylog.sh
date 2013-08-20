#!/bin/bash

CURR_DIR=$(dirname $0)
source $CURR_DIR/../common/common.sh

#################################################################################
# program variables
#################################################################################
prog_name='ha_check_copylog.sh'

ha_temp_home=
db_name=
repl_log_path=
dba_password=
output_file=

#################################################################################
# program function
#################################################################################
function print_usage()
{
	echo ""
	echo "Usage: $prog_name [options]"
	echo ""
	echo "    -t [ha_temp_home]"
	echo "    -d [db_name]"
	echo "    -r [repl_log_path]"
	echo "    -P [dba_password]"
	echo "    -o [output_file]"
	echo ""
}

function check_args()
{
	if [ -z $ha_temp_home ]; then
		print_usage
		error "Invalid ha_temp_home."
	elif [ -z $db_name ]; then
		print_usage
		error "Invalid db_name."
	elif [ -z $repl_log_path ]; then
		print_usage
		error "Invalid repl_log_path."
	elif [ -z $output_file ]; then
		print_usage
		error "Invalid output_file."
	fi
	
	repl_log_path=$(readlink -f $repl_log_path)
	expect_home=$ha_temp_home/expect
}

#################################################################################
# main function
#################################################################################
while getopts "t:d:r:P:o:" option
do
	case "$option" in
		"t") ha_temp_home="${OPTARG}";;
		"d") db_name="${OPTARG}";;
		"r") repl_log_path="${OPTARG}";;
		"P") dba_password="${OPTARG}";;
		"o") output_file="${OPTARG}";;
		"?") print_usage;;
		":") print_usage;;
		*) print_usage;;
	esac
done

check_args

if [ ! -d $repl_log_path ]; then
	if [ -z $dba_password ]; then
		pw_option=""
	else
		pw_option="-p '$dba_password'"
	fi
	csql -S -u DBA $pw_option --sysadm $db_name -c "select * from db_ha_apply_info where db_name='$db_name' and copied_log_path='$repl_log_path'" | grep "There are no results."
	if [ $? -eq $SUCCESS ]; then
		echo $(hostname) > $output_file
	fi
fi
