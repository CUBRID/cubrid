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
table=()
table_size=()
table_selected=()
num_tables=0
slot_selected=0
slot_size=(0 0 0 0 0 0 0 0)
num_tables_slot=(0 0 0 0 0 0 0 0)
database=
user="dba"
total_pages=0
from_file=""

slot=()

function get_options ()
{
         while getopts ":u:i:t:v" opt; do
                case $opt in
                        u ) user="$OPTARG" ;;
                        i ) from_file="$OPTARG" ;;
                        t ) num_proc="$OPTARG" ;;
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
                return
        fi

        database=$(echo ${@:$nelem:1})
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
        local file="Table_list_$database.$slot_num"
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
                echo "Proc $slot_num: num tables: $num_tables_in_slot, page size: ${slot_size[$slot_num]}"
        fi

	cubrid unloaddb --input-class-only --input-class-file $file $database

        if [ $? -ne 0 ];then
                msg="Failed"
        fi

        if [ $verbose = "yes" ];then
                echo "Finished Proc $slot_num ($msg)"
        fi
}

function get_table_name ()
{
        local file=$HOME/.unload_temp_file
        local db=${database:-hdb1}
        local found=0
        local line
        local idx=0
        local table_name

        # Get all table names from CATALOG

        if [ X$from_file != X"" ];then  # Read table name from file
                result=$(cat $from_file)
        else
                result=$(csql -u $user -c "select class_name from db_class where is_system_class = 'NO' AND class_type = 'CLASS' order by class_name" $db)
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
                this_table_size=$(csql -u dba -l -c "show heap capacity of $table_name" $db | grep Num_pages | awk '{print $3}')

                if [ -z $this_table_size ];then
                        echo "Unknown table: $table_name"
                        exit
                fi

                table_size[$num_tables]="$this_table_size"
                let "num_tables++"
        done
}

# MAIN

extract_db_name $*
get_options "$@"

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
                echo "[$index:${slot[i]}] ${table_selected[i]}: pages = ${table_size[i]}"
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
