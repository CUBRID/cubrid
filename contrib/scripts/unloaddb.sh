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
target_dir=$(pwd)
from_file=0
num_args_remain=0

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

         echo ""
         echo " EXAMPLES"
         echo "  $0 -t 4 -v demodb          # unload all tables in demodb"
         echo "  $0 -i file demodb          # unload tables listed in file in demodb"
         echo "  $0 -u user1 -D /tmp -i file -t 4 -v demodb"
         echo ""
}

#
# Kill unloaddb processes in progress and delete the directory, and delete incomplete files
# Directories/files created by the normally terminated unloaddb process is not deleted.
#
function cleanup ()
{
        local i
        local pid
        local exit_stat

	echo "interrupted"

        for ((i = 0; i < $num_proc; i++))
        do
                if [ -f $database.$i/unloaddb_$i.pid ];then
                        pid=$(cat $database.$i/unloaddb_$i.pid)

                        kill -0 $pid 2> /dev/null
                        if [ $? -eq 0 ];then
                                kill -9 $pid
                                rm -rf $database.$i
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

        dba_groups=$(csql $user $pass -l -c "$qry_dba_grp" $database | grep -w $USERNAME | wc -l)
        if [ $USERNAME != "DBA" ] && [ $dba_groups -eq 0 ];then
                echo "User '$dbuser' is not a member of DBA group"
                exit 2
        fi
}

function get_options ()
{
         while getopts ":D:u:i:t:v" opt; do
                case $opt in
                        u ) user="-u $OPTARG" ;;
                        i ) filename="$OPTARG" ;from_file=1 ;;
                        t ) num_proc="$OPTARG" ;;
                        D ) target_dir="$OPTARG" ;;
                        v ) verbose="yes" ;;
                esac
        done

        shift $(($OPTIND - 1))

        num_args_remain=$#
        database=$*
}

function check_database ()
{
        local db=$database

        # check the database server is running

        db=${database%%@*}

        retcode=$(ps -ef | grep cub_server | grep $db | wc -l)
        if [ $retcode -eq 0 ];then
                echo "Database server '$database' is not running"
              exit 1
        fi
}

function silent_cd ()
{
        cd $* > /dev/null
}

function get_table_size ()
{
        local num_rows=0
        local table_name=$1
        local Avg_rec_len=0
        local table_size=0

        num_rows=$(csql $user $pass -l -c "show heap capacity of $table_name" $db | grep Num_recs | awk '{print $3}')
        Avg_rec_len=$(csql $user $pass -l -c "show heap capacity of $table_name" $db | grep Avg_rec_len | awk '{print $3}')

        let "table_size = num_rows * Avg_rec_len"

        echo $table_size
}

function find_slot ()
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
        local file="Tables_unloaded_$database.$slot_num"
        local num_tables_in_slot=0
        local msg="Success"
        local pid
        local buf

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

        echo "process $slot_num: starting" > unloaddb_$slot_num.status

	cubrid unloaddb $user $pass --input-class-only --input-class-file $file $database &
        pid=$!

        echo $pid > unloaddb_$slot_num.pid

        wait $pid

        if [ $? -ne 0 ];then
                msg="Failed"
                echo "process $slot_num: failed" > unloaddb_$slot_num.status
        else
                echo "process $slot_num: success" > unloaddb_$slot_num.status
        fi

        if [ $verbose = "yes" ];then
                echo "Finished Proc $slot_num ($msg)"
        fi
}

function analyze_table_info ()
{
        local found=0
        local line
        local idx=0
        local table_name
        local db=$database
        local query="show tables"

        # Get all table names if $from_file equals 0, otherwise from input file
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
                if [ $from_file -eq 0 ] && [ ${token:0:1} != "'" ];then
                        break
                fi

                table_selected[idx]="$token"

                if [ $from_file -eq 0 ];then        # remove single quota from csql output
                        table_selected[idx]=$(echo ${table_selected[idx]} | sed "s/[\']//g")
                fi

                let "idx++"

        done

        # Get the size of all tables
        n=1
        for ((i = 0; i < ${#table_selected[@]}; i++))
        do
                table_name=${table_selected[i]}
                this_table_size=$(get_table_size $table_name)

                if [ -z $this_table_size ] || [ $this_table_size -eq 0 ];then
                        echo "'$table_name': unknown table"
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
else
        silent_cd $target_dir
fi

check_database $database

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
        this_slot=$(find_slot)
        tbl=${table_selected[i]}
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
# Do unloaddb for each process
#
# check and create sub-directories for each process
for ((i = 0; i < $num_proc; i++))
do
	if [ ${num_tables_slot[i]} -ne 0 ];then
		if [ -f $database.$i ] || [ -d $database.$i ];then
			echo "$database.$i: File or directory already exist."
			exit 1
		fi

		mkdir $database.$i
	fi
done

#
# RUN unloaddb process cuncurrently
#
for ((i = 0; i < $num_proc; i++))
do
        if [ ${slot_size[i]} -gt 0 ];then
	       (silent_cd $database.$i; do_unloaddb $i) &
        fi
done

wait

echo "Completed."
