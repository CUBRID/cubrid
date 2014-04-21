#!/bin/bash

CURR_DIR=$(dirname $0)
source $CURR_DIR/../common/common.sh

#################################################################################
# program variables
#################################################################################
prog_name='ha_repl_resume.sh'
current_host=$(uname -n)

input_file=

#################################################################################
# program function
#################################################################################
function print_usage()
{
	echo ""
	echo "Usage: $prog_name [options]"
	echo ""
	echo "    -i [input_file]"
	echo ""
}

function check_args()
{
	if [ -z $input_file ]; then
		print_usage
		error "Invalid input_file."
	fi
}

function ha_repl_resume()
{
	while read line
	do
		echo "$current_host ]$ $line >/dev/null 2>&1 &"
		echo "resume: $line"
		eval "$line >/dev/null 2>&1 &"
	done < $input_file

	sleep 1

	echo -ne "\n"
	echo -ne " - check heartbeat list on $current_host(master).\n\n"
	echo "$current_host ]$ cubrid heartbeat list" 
	cubrid heartbeat list
	echo -ne "\n\n"
}

#################################################################################
# main function
#################################################################################
while getopts "i:" option
do
	case "$option" in
		"i") input_file="${OPTARG}";;
		"?") print_usage;;
		":") print_usage;;
		*) print_usage;;
	esac
done

check_args

ha_repl_resume

exit 0
