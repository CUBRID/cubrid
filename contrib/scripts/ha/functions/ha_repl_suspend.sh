#!/bin/bash

CURR_DIR=$(dirname $0)
source $CURR_DIR/../common/common.sh

#################################################################################
# program variables
#################################################################################
prog_name='ha_repl_suspend.sh'

log_home=
db_name=
host=
output_file=

log_path=
current_host=$(uname -n)

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
	if [ -z $log_home ]; then
		print_usage
		error "log_home is not specified."
	elif [ -z "$db_name" ]; then
		print_usage
		error "db_name is not specified."
	elif [ -z "$host" ]; then
		print_usage
		error "host is not specified."
	elif [ -z "$output_file" ]; then
		print_usage
		error "output_file is not specified."
	fi
	
	rm -f $output_file && touch $output_file
	
	log_home=$(readlink -f $log_home)
	log_path=${log_home}/${db_name}_${host}
	if [ ! -d "$log_path" ]; then
		error "log_path($log_path) is not a directory"
	fi
}

function suspend_copylogdb()
{
	local i=0
	
	ps=$(ps -f -U $(whoami) | grep "cub_admin copylogdb" | grep "${db_name}_${host}" | grep -v grep)
	
	arg=$(echo $ps | cut -d ' ' -f 8-)	       
	pid=$(echo $ps | cut -d ' ' -f 2)

	arg_list=($arg)		
	for ((; $i < ${#arg_list[@]}; i++)); do
		if [ "${arg_list[$i]}" == "-L" ]; then
		ps_log_path=${arg_list[(($i + 1))]}
			break
		fi
	done

	ps_log_path=$(readlink -f $ps_log_path)

	if [ "$ps_log_path" == "$log_path" ]; then
		echo "$current_host ]$ cubrid heartbeat deregister $pid" 
		echo "suspend: ($pid) $arg"
		cubrid heartbeat deregister $pid >/dev/null 2>&1
		
		[ -n "$output_file" ] && echo $arg >> $output_file
		
		return
	fi
		
	echo -e "\033[38mCannot find the copylogdb process.\033[39m"
	exit 1
}

function suspend_applylogdb()
{
	local i=0
	
	ps=$(ps -f -U $(whoami) | grep "cub_admin applylogdb" | grep "${db_name}_${host}" | grep -v grep)
	
	arg=$(echo $ps | cut -d ' ' -f 8-)	       
	pid=$(echo $ps | cut -d ' ' -f 2)

	arg_list=($arg)		
	for ((; $i < ${#arg_list[@]}; i++)); do
		if [ "${arg_list[$i]}" == "-L" ]; then
			ps_log_path=${arg_list[(($i + 1))]}
			break
		fi
	done

	ps_log_path=$(readlink -f $ps_log_path)

	if [ "$ps_log_path" == "$log_path" ]; then
		echo "$current_host ]$ cubrid heartbeat deregister $pid" 
		echo "suspend: ($pid) $arg"
		cubrid heartbeat deregister $pid >/dev/null 2>&1
		
		[ -n "$output_file" ] && echo $arg >> $output_file
		
		return
	fi
	
	echo -e "\033[38mCannot find the applylogdb process.\033[39m"
	exit 1
}

function ha_repl_suspend()
{
	rm -f $output_file
	
	suspend_copylogdb 
	suspend_applylogdb

	sleep 1

	echo -ne "\n\n3. heartbeat status on $current_host(master).\n\n"
	echo "$current_host ]$ cubrid heartbeat list" 
	cubrid heartbeat list
	echo -ne "\n\n"
}

#################################################################################
# main function
#################################################################################
while getopts "l:d:h:o:" optname
do
        case "$optname" in
                "l") log_home="${OPTARG}";;
                "d") db_name="${OPTARG}";;
                "h") host="${OPTARG}";;
                "o") output_file="${OPTARG}";;
                "?") print_usage;;
                ":") print_usage;;
                *) print_usage;;
        esac
done

check_args

ha_repl_suspend

sleep 1

exit 0

