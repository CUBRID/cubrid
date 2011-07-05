#!/bin/bash

################################################################################
# 
prog_name='ha_repl_copylog.sh'

repl_log_path=
db_name=
host=

now=$(date +"%Y%m%d_%H%M%S")
current_host=$(uname -n)
################################################################################

function print_usage()
{
	echo ""
	echo "Usage: $prog_name [options]"
	echo ""
	echo "    -r [repl_log_path]"
	echo "    -d [db_name]"
	echo "    -h [host]"
	echo ""
}

function is_invalid_args()
{
	[ -z "$repl_log_path" ] && echo " << ERROR >>  repl_log_path is not specified" && return 0
	[ -z "$db_name" ] && echo " << ERROR >>  db_name is not specified" && return 0
	[ -z "$host" ] && echo " << ERROR >> host is not specified" && return 0

	if [ -d "$repl_log_path" ]; then
		return 1
	else
		echo " << ERROR >> repl_log_path($log_path) is not a directory" 
		return 0
	fi
}

function ha_repl_copylog()
{
	# TODO : is it ok?
	# cubrid heartbeat deact >/dev/null 2>&1
	# sleep 5
	echo -ne "\n - cubrid service stop\n"
	echo "$current_host ]$ cubrid service stop >/dev/null 2>&1"
	cubrid service stop >/dev/null 2>&1
	sleep 5

	echo -ne "\n - start cub_master\n"
	echo "$current_host ]$ cub_master >/dev/null 2>&1"
	cub_master >/dev/null 2>&1
	# cubrid heartbeat act >/dev/null 2>&1

	echo -ne "\n - start copylogdb and wait until replication active log header to be initialized\n"
	echo "$current_host ]$ cub_admin copylogdb -L $repl_log_path -m 3 $db_name@$host >/dev/null 2>&1 &"
	eval cub_admin copylogdb -L $repl_log_path -m 3 $db_name@$host >/dev/null 2>&1 & 

	echo ""
	for i in `seq 1 60`
	do
	    cubrid applyinfo -L $repl_log_path $db_name | grep -wqs "DB name"
		ret=$?
		[ $ret -eq 0 ] && break

		echo -ne "."
		sleep 1
	done
	echo ""

	# TODO : is it ok?
	# cubrid heartbeat deact >/dev/null 2>&1
	# sleep 5
	echo -ne "\n - cubrid service stop\n"
	echo "$current_host ]$ cubrid service stop >/dev/null 2>&1"
	cubrid service stop >/dev/null 2>&1
	sleep 5

	echo -ne "\n - check copied active log header\n"
	echo "$current_host ]$  cubrid applyinfo -L $repl_log_path $db_name | grep -wqs \"DB name\""
	cubrid applyinfo -L $repl_log_path $db_name | grep -wqs "DB name"
	ret=$?
	return $ret
}

### main ##############################

while getopts "r:d:h:" optname
do
        case "$optname" in
                "r") repl_log_path="${OPTARG}";;
                "d") db_name="${OPTARG}";;
                "h") host="${OPTARG}";;
                "?") print_usage;;
                ":") print_usage;;
                *) print_usage;;
        esac
done

if is_invalid_args; then
	print_usage 
	exit 1
fi

ha_repl_copylog
ret=$?

exit $ret

