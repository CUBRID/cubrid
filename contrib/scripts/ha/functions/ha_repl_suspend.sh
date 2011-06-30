#!/bin/bash

################################################################################
# 
prog_name='ha_repl_suspend.sh'

log_home=
db_name=
host=
output_file=

log_path=
current_host=$(uname -n)
################################################################################

function print_usage()
{
	echo ""
	echo "Usage: $prog_name [options]"
	echo ""
	echo "    -l [log_home]"
	echo "    -d [db_name]"
	echo "    -h [host]"
	echo "    -o [output_file]"
	echo ""
}

function is_invalid_args()
{
	[ -z "$log_home" ] && echo " << ERROR >> log_home is not specified" && return 0
	[ -z "$db_name" ] && echo " << ERROR >> db_name is not specified" && return 0
	[ -z "$host" ] && echo " << ERROR >> host is not specified" && return 0

	log_path=${log_home}/${db_name}_${host}
	if [ -d "$log_path" ]; then
		return 1
	else
		echo "[ERROR] log_path($log_path) is not a directory" 
		return 0
	fi
}

function is_utils_not_running()
{
	ps_copylogdb=$(ps -ef | grep -v grep | grep "cub_admin copylogdb -L $log_path")
	ps_applylogdb=$(ps -ef | grep -v grep | grep "cub_admin applylogdb -L $log_path")
	[ -z "$ps_copylogdb" -a -z "$ps_applylogdb" ] && return 0

	return 1
}



function suspend_copylogdb()
{
	line=$(ps -ef | grep -v grep | grep "cub_admin copylogdb -L $log_path")
	pid=$(echo $line | cut -d ' ' -f 2)
	args=$(echo $line | cut -d ' ' -f 8-)

	echo -ne "\n\n1. deregister copylogdb on $current_host(master).\n\n"
	if [ -n "$pid" -a -n "$args" ]; then
		echo "$current_host ]$ cubrid heartbeat deregister $pid" 
		echo "suspend: ($pid) $args"
		cubrid heartbeat deregister $pid >/dev/null 2>&1
		[ -n "$output_file" ] && echo $args >> $output_file
	fi
}

function suspend_applylogdb()
{
	line=$(ps -ef | grep -v grep | grep "cub_admin applylogdb -L $log_path")
	pid=$(echo $line | cut -d ' ' -f 2)
	args=$(echo $line | cut -d ' ' -f 8-)

	echo -ne "\n\n2. deregister applylogdb on $current_host(master).\n\n"
	if [ -n "$pid" -a -n "$args" ]; then
		echo "$current_host ]$ cubrid heartbeat deregister $pid" 
		echo "suspend: ($pid) $args"
		cubrid heartbeat deregister $pid >/dev/null 2>&1
		[ -n "$output_file" ] && echo $args >> $output_file
	fi
}

function ha_repl_suspend()
{
	suspend_copylogdb 
	suspend_applylogdb

	sleep 1

	echo -ne "\n\n3. heartbeat status on $current_host(master).\n\n"
	echo "$current_host ]$ cubrid heartbeat list" 
	cubrid heartbeat list
	echo -ne "\n\n"
}


### main ##############################

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

if is_invalid_args; then
	print_usage 
	exit 1
fi

# 
# if is_utils_not_running; then
#	exit 0
# fi

[ -n "$output_file" ] && rm -f $output_file && touch $output_file

ha_repl_suspend

sleep 1

exit 0

