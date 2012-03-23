#!/bin/bash

CURR_DIR=$(dirname $0)
source $CURR_DIR/../common/common.sh

#################################################################################
# program variables
#################################################################################
prog_name='ha_check_script.sh'

ha_temp_home=
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
	echo "    -o [output_file]"
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
	fi
	
	expect_home=$ha_temp_home/expect
}

#################################################################################
# main function
#################################################################################
while getopts "t:o:" option
do
	case "$option" in
		"t") ha_temp_home="${OPTARG}";;
		"o") output_file="${OPTARG}";;
		"?") print_usage;;
		":") print_usage;;
		*) print_usage;;
	esac
done

check_args

echo $(hostname) > $output_file