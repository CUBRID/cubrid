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
num_proc=8
table_size=()
table_selected=()
num_tables=0
slot_selected=0
slot_size=(0 0 0 0 0 0 0 0)
num_tables_slot=(0 0 0 0 0 0 0 0)
database=
user="-u dba"
pass=""
password=""
total_pages=0
from_file=""
target_dir=$(pwd)

slot=()

function show_usage ()
{
         echo "Usage: $0 [OPTIONS] [TARGET]"
         echo " OPTIONS"
         echo "  -t arg  Set number of parallel process; default 8"
         echo "  -i arg  input FILE of table names; default: dump all classes"
         echo "  -u arg  Set database user name; default dba"
         echo "  -p arg  Set dbuser password"
         echo "  -D arg  Set directory for unloaddb output dir/files"
         echo "  -v      Set verbose mode on"

         echo ""
         echo " EXAMPLES"
         echo "  $0 -t 4 -v demodb          # unload all tables in demodb"
         echo "  $0 -i file demodb          # unload tables listed in file in demodb"
         echo "  $0 -u user1 -p 1234 -i file -t 4 -v demodb"
         echo ""
}

function cleanup ()
{
	echo "Catch interrupt"
	exit
}

function veryfy_user_pass ()
{
         local msg
         msg=$(csql $user $pass -c "select 1" $database)

         if [ $? -ne 0 ];then
                echo "Invalid user/pass: please check user and password"
                exit
         fi
}

function get_options ()
{
         while getopts ":D:u:p:i:t:v" opt; do
                case $opt in
                        u ) user="-u $OPTARG" ;;
                        p ) pass="-p $OPTARG" ;;
                        i ) from_file="$OPTARG" ;;
                        t ) num_proc="$OPTARG" ;;
                        D ) target_dir="$OPTARG" ;;
                        v ) verbose="yes" ;;
                esac
        done
}

function silent_cd ()
{
        cd $* > /dev/null
}

function extract_db_name ()
{
        local nelem=$#
        local i

        # extract last argument & set as database name

        if [ $nelem -eq 0 ];then
                show_usage
                exit
        fi

        database=$(echo ${@:$nelem:1})
}

function get_table_size ()
{
        local num_rows=0
        local table_name=$1
        local row_size=0
        local table_size=0
        local j="2147483647"

        num_rows=$(csql $user $pass -l -c "show heap capacity of $table_name" $db | grep Num_recs | awk '{print $3}')
        row_size=$(get_schema_size $table_name)
        let "table_size = num_rows * row_size"

        echo $table_size
}

function get_schema_size ()
{
        local table_name=$1
        local size=0
        local query=" select CAST(SUM(CASE \
         WHEN "data_type" = 'BIGINT' THEN 8.0 \
         WHEN "data_type" = 'INTEGER' THEN 4.0 \
         WHEN "data_type" = 'SMALLINT' THEN 2.0 \
         WHEN "data_type" = 'FLOAT' THEN 4.0 \
         WHEN "data_type" = 'DOUBLE' THEN 8.0 \
         WHEN "data_type" = 'MONETARY' THEN 12.0 \
         WHEN "data_type" = 'STRING' THEN prec \
         WHEN "data_type" = 'VARCHAR' THEN prec \
         WHEN "data_type" = 'NVARCHAR' THEN prec \
         WHEN "data_type" = 'CHAR' THEN prec \
         WHEN "data_type" = 'NCHAR' THEN prec \
         WHEN "data_type" = 'TIMESTAMP' THEN 8.0 \
         WHEN "data_type" = 'DATE' THEN 4.0 \
         WHEN "data_type" = 'TIME' THEN 4.0 \
         WHEN "data_type" = 'DATETIME' THEN 4.0 \
         WHEN "data_type" = 'BIT' THEN FLOOR(prec / 8.0) \
         WHEN "data_type" = 'BIT VARYING' THEN FLOOR(prec / 8.0) \
         ELSE 0 \
     END) as BIGINT)  AS [size] \
 from db_attribute where class_name = '$table_name';"

        if [ $# -eq 0 ];then
                echo "0"
                return
        fi

        size=$(csql $user $pass -l -c "$query" $db | grep "^<0000" | awk '{print $3}')
        echo $size
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

        for ((i = 0; i < $num_tables; i++))
        do
                if [ ${slot[i]} -eq $slot_num ];then
                        let "num_tables_in_slot++"
                        echo "${table_selected[i]}" >> $file
                fi
        done

        if [ $verbose = "yes" ];then
                echo "Proc $slot_num: num tables: $num_tables_in_slot, ${slot_size[$slot_num]} bytes"
        fi

	  cubrid unloaddb $user $pass --input-class-only --input-class-file $file $database

        if [ $? -ne 0 ];then
                msg="Failed"
        fi

        if [ $verbose = "yes" ];then
                echo "Finished Proc $slot_num ($msg)"
        fi
}

function get_table_name ()
{
        local found=0
        local line
        local idx=0
        local table_name
        local db=$database

        # Get all table names from CATALOG

        if [ X$from_file != X"" ];then  # Read table name from file
                result=$(cat $from_file)
        else
                result=$(csql $user $pass -c "select class_name from db_class where is_system_class = 'NO' AND class_type = 'CLASS' order by class_name" $db)
        fi

        for token in $result
        do
                if  [ X$from_file = X"" ] && [ $found -eq 0 ];then
                        str=$(echo $token | grep "====") # skip until we found table names in csql
                        if [ $? -eq 0 ];then
                                found=1
                        fi
                        continue
                fi

                if [ X$from_file = X"" ] && [ ${token:0:1} != "'" ];then
                        break
                fi

                table_selected[idx]="$token"

                if [ X$from_file = X"" ];then        # remove single quota from csql output
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

                if [ -z $this_table_size ];then
                        echo "Unknown table: $table_name"
                        exit
                fi

                table_size[$num_tables]="$this_table_size"
                let "num_tables++"
        done
}

# MAIN

trap "cleanup; exit" SIGHUP SIGINT SIGTERM

extract_db_name $*
get_options "$@"

if [ ! -d $target_dir ];then
        echo "$target_dir: directory not exists or permission denied"
        exit
else
   silent_cd $target_dir
fi

veryfy_user_pass

get_table_name $*

if [ $num_tables -eq 0 ];then
        echo "No Table SELECTED."
        exit
fi

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
for ((i = 0; i < $num_proc; i++))
do
        if [ ${num_tables_slot[i]} -ne 0 ];then
                if [ -f $database.$i ] || [ -d $database.$i ];then
                        echo "$database.$i: File or directory already exist."
                        continue
                fi
                mkdir $database.$i
                (silent_cd $database.$i; do_unloaddb $i) &
        fi
done
wait

echo "Completed."
