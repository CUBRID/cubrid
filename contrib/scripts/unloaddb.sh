#!/bin/bash
#
#
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

verbose="no"   # set 'yes' for verbose mode
max_num_proc=16
num_proc=8
table_size=()
table_selected=()
num_tables=0
slot_selected=0
# following two variable are depend on max_num_proc
slot_size=(0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0)
num_tables_slot=(0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0)
database=""
user="-u dba"
pass=""
total_pages=0
filename=""
from_file=0
num_args_remain=0
logdir=""
current_dir=
cwd=$(pwd)
target_dir=$cwd
opt_schema=0
opt_data=0

slot=()

function show_usage ()
{
	echo "Usage: $0 [OPTIONS] [database]"
	echo " OPTIONS"
	echo "  -t arg  Set number of parallel process; default 8, max 16"
	echo "  -i arg  input FILE of table names; default: dump all classes"
	echo "  -u arg  Set database user name; default dba"
	echo "  -D arg  Set directory for unloaddb output dir/files"
	echo "  -v      Set verbose mode on"
	echo "  -d      dump objects only; default: schema and objects"
	echo "  -s      dump schema only; default: schema and objects"

	echo ""
	echo " EXAMPLES"
	echo "  $0 -t 4 -v demodb          # unload all tables in demodb"
	echo "  $0 -i file demodb          # unload tables listed in file in demodb"
	echo "  $0 -u user1 -D /tmp -i file -t 4 -v demodb"
	echo ""
}

function get_options ()
{
	   while getopts ":D:u:i:t:sdv" opt; do
		    case $opt in
				u ) user="-u $OPTARG" ;;
				i ) filename="$OPTARG" ;from_file=1 ;;
				t ) num_proc="$OPTARG" ;;
				D ) target_dir="$OPTARG" ;;
				v ) verbose="yes" ;;
				s ) opt_schema=1 ;;
				d ) opt_data=1 ;;
				\? ) echo "unknown option -$OPTARG"; show_usage; exit 1 ;;
		    esac
	  done

	  shift $(($OPTIND - 1))

	  num_args_remain=$#
	  database=$*
}

#
# Kill unloaddb processes in progress and delete the directory, and delete incomplete files
# Directories/files created by the normally terminated unloaddb process is not deleted.
#
function cleanup ()
{
	local i
	local pid
	local pid_file

	echo "interrupted"

	for ((i = 0; i < $num_proc; i++))
	do
		pid_file=$logdir/"$database"_$i.pid

		if [ -f $pid_file ];then
			pid=$(cat $pid_file)

			kill -0 $pid 2> /dev/null
			if [ $? -eq 0 ];then
				kill -9 $pid
				rm -f "$database"_$i"_objects"
			fi
		fi
	done
}

function get_password ()
{
	local password

	read -sp "Enter Password : " password
	echo $password
	echo "" > /dev/tty
}

function verify_user_pass ()
{
	local msg
	local USERNAME
	local dbuser
	local passwd
	local qry_dba_grp="SELECT u.name FROM db_user AS u, TABLE(u.groups) AS g(x) where x.name = 'DBA'"

	dbuser=$(echo ${user} | cut -d' ' -f2-2)
	USERNAME=$(echo ${dbuser} | tr [:lower:] [:upper:])

	# Try with NULL password
	passwd=$(csql $user --password="" -c "SELECT 1" $database 2> /dev/null)

	if [ $? -ne 0 ];then
		passwd=$(get_password)
		pass="-p $passwd"

		passwd=$(csql $user $pass -c "SELECT 1" $database 2> /dev/null)
		if [ $? -ne 0 ];then
			echo "$dbuser: Incorrect or missing password"
			exit 1
		fi

	fi

	# check whether this user is a member of DBA groups
	#
	dba_groups=$(csql $user $pass -l -c "$qry_dba_grp" $database | grep -w $USERNAME | wc -l)
	if [ $USERNAME != "DBA" ] && [ $dba_groups -eq 0 ];then
		echo "User '$dbuser' is not a member of DBA group"
		exit 2
	fi
}

#
# exit, if subject db server is not running
#
function is_db_server_running ()
{
	local db=$database

	db=${database%%@*}

	retcode=$(ps -ef | grep cub_server | grep $db | wc -l)
	if [ $retcode -eq 0 ];then
		echo "Database server '$database' is not running"
		exit 1
	fi
}

#
# when doing a cd, cd display current working directory.
# skip it.
#
function silent_cd ()
{
	cd $* > /dev/null
}

#
# run csql -u dba -l -c "show heap capacity of code" demodb
# extract Num_recs, Avg_rec_len for the table
# size = Num_recs * Avg_rec_len
#
function get_table_size ()
{
	local Num_recs=0
	local table_name=$1
	local Avg_rec_len=0
	local table_size=0
	local csql_output

	csql_output=$(csql $user $pass -l -c "show heap capacity of $table_name" $db)

	if [ $? -ne 0 ];then
		table_size=-1
	else
		Avg_rec_len=$(echo ${csql_output##*Avg_rec_len} | awk '{print $2}')
		Num_recs=$(echo ${csql_output##*Num_recs} | awk '{print $2}')
		let "table_size = Num_recs * Avg_rec_len"
	fi

	echo $table_size
}

#
# Find the process with the least load.
#
function find_least_loaded_proc ()
{
	local selected=0
	local i
	local size=${slot_size[0]}

	for ((i = 0; i < $num_proc; i++))
	do
		if [ ${slot_size[i]} -lt $size ];then
			size=${slot_size[i]}
			selected=$i
		fi
	done

	echo $selected
}

function do_unloaddb ()
{
	local slot_num=$1
	local i
	local num_tables_in_slot=0
	local msg="Success"
	local pid
	local buf
	local prefix="$database"_$slot_num
	local log_prefix=$logdir/$prefix
	local unloaddb_log=$prefix"_unloaddb.log"
	local current_dir=$(pwd)
	local file=$log_prefix".files"
	local unloaddb_opts="$user $pass --output-prefix=$prefix -d --input-class-only"
	local opts="$user $pass -s "
	local do_schema_unload=0
	local do_data_unload=0

	for ((i = 0; i < $num_tables; i++))
	do
		if [ ${slot[i]} -eq $slot_num ];then
			let "num_tables_in_slot++"
			echo "${table_selected[i]}" >> $file
		fi
	done

	if [ $verbose = "yes" ];then
		buf=$(printf %3d $num_tables_in_slot)
		echo "Proc $slot_num: num tables: $buf, ${slot_size[$slot_num]} bytes"
	fi

	echo "process $slot_num: starting" > $log_prefix.status

	#
	# unload schema files if the following two conditions are satisfied.
	# 1. it is 0th process
	# 2. None of the -s -d options were given, or -s option is given

	if [ $opt_schema -eq 1 ] || [ $opt_schema -eq 0 -a $opt_data -eq 0 ];then
		if [ $slot_num -eq 0 ];then
			do_schema_unload=1
		fi
	fi

	if [ $opt_data -eq 1 ] || [ $opt_schema -eq 0 -a $opt_data -eq 0 ];then
		do_data_unload=1
	fi

	if [ $do_schema_unload -eq 1 ];then
		if [ $from_file -eq 1 ];then
			opts=$opts" --input-class-only --input-class-file $filename"
		fi
		(silent_cd $target_dir;cubrid unloaddb $opts $database) &
	fi

	if [ $do_data_unload -eq 0 ];then
		return
	fi

	(silent_cd $target_dir; cubrid unloaddb $unloaddb_opts --input-class-file $file $database) &
	pid=$!

	echo $pid > $log_prefix.pid

	wait $pid

	if [ $? -ne 0 ];then
		msg="Failed"
		echo "process $slot_num: failed" > $log_prefix.status
	else
		echo "process $slot_num: success" > $log_prefix.status
	fi

	# move unloaddb logfile to $logdir
	if [ -f $target_dir/$unloaddb_log ];then
		mv $target_dir/$unloaddb_log $logdir
	fi

	if [ $verbose = "yes" ];then
		echo "Finished Proc $slot_num ($msg)"
	fi
}

#
# output: 2 string array, table_selected [], table_size []
# Phase1: get all table names, if -i option is not given. Fill it to table_selected []
# Phase2: for all tables ${table_selected}, calculate the size of the table, and
# 	    fill it to table_size
#
function analyze_table_info ()
{
	local found=0
	local line
	local idx=0
	local table_name
	local db=$database
	local query="show tables"

	# Phase1: Get all table names if $from_file equals 0, otherwise from input file
	# and fill it to array table_selected []

	if [ $from_file -eq 1 ];then  # Read table name from file
		result=$(cat $filename)
	else
		result=$(csql $user $pass -c "$query" $db)
	fi

	for token in $result
	do
		if  [ $from_file -eq 0 ] && [ $found -eq 0 ];then
			str=$(echo $token | grep "====") # skip until we found table names in csql

			if [ $? -eq 0 ];then
				found=1
			fi

			continue
		fi

		# if table name comes from csql it is the pattern of 'code'
		# and end with string like this
		# "\n16 rows selected. (0.009125 sec) Committed."
		# If it comes from file it is the pattern of code
		#

		if [ $from_file -eq 0 ] && [ ${token:0:1} != "'" ];then
			break
		fi

		table_selected[idx]="$token"

		if [ $from_file -eq 0 ];then        # remove single quota from csql output
			table_selected[idx]=$(echo ${table_selected[idx]} | sed "s/[\']//g")
		fi

		let "idx++"
	done

	# 
	# Phase2: calculate all table size in array table_selected[]
	# calculate the table size to array table_size []
	#

	for ((i = 0; i < ${#table_selected[@]}; i++))
	do
		table_name=${table_selected[i]}
		this_table_size=$(get_table_size $table_name)

		if [ -z $this_table_size ] || [ $this_table_size -lt 0 ];then
			echo "'$table_name': unknown table ($this_table_size)"
			exit
		fi

		table_size[$num_tables]="$this_table_size"
		let "num_tables++"
	done
}

# MAIN
#
trap "cleanup" SIGHUP SIGINT SIGTERM
set -o monitor

get_options "$@"

if [ $num_args_remain -ne 1 ] || [ -z $database ];then
	show_usage
	exit 1
fi

if [ $num_proc -gt $max_num_proc ];then
	echo "Num Proc exeed Max Proc. Force set num proc to $max_num_proc"
	num_proc=$max_num_proc
fi

if [ ! -d $target_dir ];then
	echo "$target_dir: directory not exists or permission denied"
	exit
fi

# make full path from "/"
if [ ! -z $filename ] && [ ${filename:0:1} != "/" ];then
	filename="$cwd/"$filename
fi

is_db_server_running $database

logdir=$cwd"/"$database"_"unloaddb.log
if [ -d $logdir ];then
	rm -rf $logdir
fi

mkdir $logdir

verify_user_pass

echo -n "Analyzing table spacace ..."

analyze_table_info

if [ $num_tables -eq 0 ];then
	echo "No Table SELECTED."
	exit
fi

echo ""

# Assign all tables to unloaddb slot
# Assign and calculate total table size & num tables assigned in each slot
for ((i = 0; i < $num_tables; i++))
do
	this_slot=$(find_least_loaded_proc)
	slot[i]=$this_slot

	let "slot_size[$this_slot]+=${table_size[$i]}"
	let "num_tables_slot[$this_slot]++"
	let "total_pages+=${table_size[$i]}"

	if [ $verbose = "yes" ];then
		index=$(printf "%3d" $i)
		echo "[$index:${slot[i]}] ${table_selected[i]}: ${table_size[i]} bytes"
	fi
done

echo "Total $num_tables tables, $total_pages pages"

#
# RUN unloaddb process cuncurrently
#
for ((i = 0; i < $num_proc; i++))
do
	if [ ${slot_size[i]} -gt 0 ];then
		do_unloaddb $i &
	fi
done

wait

echo "Completed."
