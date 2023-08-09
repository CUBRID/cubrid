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


# declare read only variable
declare -r -a key_titles=(
    "Max_num_ovf_page_a_key"
    "Avg_num_ovf_page_per_key"
    "Num_ovf_page"    
)

declare -r sort_option_page=1
declare -r sort_option_owner=2
declare -r sort_option_table=3
declare -r sort_option_index=4
declare -r -a  sort_titles=(
    "pages" "pages"       $sort_option_page
    "owner" "owner-name"  $sort_option_owner
    "table" "table-name"  $sort_option_table
    "index" "index-name"  $sort_option_index
)

# global variable
database=""
user="-u dba"
pass=""
target_owner=""
target_table=""
connection_mode="-C"

match_key="${key_titles[1]}"
declare -i match_key_idx=1
declare -i sort_field=${sort_titles[2]}
declare -i threshold_value=1000
declare -i info_map_st_sz=6
declare -a info_map  # array ( pages, owner-name, table-name, index-name, partition, invisible )
declare IFS_backup=$IFS
declare -i verbose_mode=0

current_dir=""
work_dir=""

######################################################################
# functions
#
function echo_stderr()
{
#   echo "$@" 1>&2;
#   cat <<< "$@" 1>2&;
    printf "%s\n" "$*" 1>&2;
}

function echo_verbose()
{
        if [ ${verbose_mode} -eq 1 ]; then
             printf "%s\n" "$*"
        fi
}

function show_usage ()
{
	echo "Usage: $0 [OPTIONS] [database]"
	echo " OPTIONS"
	echo "  -C client-server mode execution, default"
	echo "  -S standalone mode execution"
	echo "  -u, --user=ARG      Set database user name(only DBA or DBA group member); default dba"
	echo "  -p, --password=ARG  Set password of databases user name; default NULL"
	echo "  -o, --owner=ARG     Set target owner name; default: NULL"
	echo "  -c, --class=ARG     Set target class name; default: NULL"
	echo "  -k, --key=ARG       Set the name of the key field to be checked; default: 1 (Max_num_ovf_page_a_key)"
        echo "                      Choose one of the following numbers."
        echo "                        1 : Max_num_ovf_page_a_key (default)"
        echo "                        2 : Avg_num_ovf_page_per_key"
        echo "                        3 : Num_ovf_page"        
	echo "  -t, --threshold=ARG Set threshold (show only indexes exceed this value); default: 1000"    
	echo "  -s, --sort=ARG      Set sort field name(pages, owner, table, index); default pages"
        echo "  -v, --verbose       Set verbose mode; default disable" 
	echo ""
	echo " EXAMPLES"
	echo "  $0 -S -o user1 demodb"
	echo "  $0 -C -c game -t 100 demodb"
	echo "  $0 -C -o user1 --key=1 --sort=table demodb"
	echo ""
	exit 1
}

function get_options()
{
	local sa_mode=0
	local tsort=""

	args=$(getopt -o :u:p:o:c:t:k:s:CSv  -l user:,password:,owner:,class:,threshold:,key:,sort:,--verbose   -- "$@" )
	eval set -- "$args"
	while true
	do
		case "$1" in
			-C ) (( sa_mode++ ));  connection_mode="-C"  ;;
			-S ) (( sa_mode++ ));  connection_mode="-S" ;;
			-u | --user)  user="-u $2";       shift  ;;
			-p | --password ) pass="-p $2";   shift ;;
			-o | --owner) target_owner="$2";  shift ;;
			-c | --class) target_table="$2";  shift ;;
			-t | --threshold) threshold_value=$2; shift ;;
			-k | --key)
                                match_key_idx=$2
                                if [ $2 -ge 1 ] && [ $2 -le  ${#key_titles[@]} ]; then
                                   match_key="${key_titles[${match_key_idx} - 1]}"                                    
                                else
                                    show_usage
				fi
				shift ;;
				
			-s | --sort)
				tsort=${2,,} # to lowercase
				for (( i =0; i < ${#sort_titles[@]}; i+=3 ))
				do
					if [ x"$tsort" == x"${sort_titles[${i}]}" ] || [ x"$tsort" == x"${sort_titles[${i} + 1]}" ]; then
						sort_field=${sort_titles[${i} + 2]}
						break
					fi
				done
				if [ $i -gt ${#sort_titles[@]} ]; then
					show_usage
				fi
				shift ;;
                         -v | --verbose)
                                verbose_mode=1 ;;                               
			-- ) shift;  break ;;
			* )  echo "unknown option"; show_usage ;;
		esac
		shift
	done

	if [ $# -ne 1 ]; then
		show_usage
	elif [ $sa_mode -gt 1 ]; then
		show_usage
	fi

	database="$@"
}

function print_args()
{
#    echo arguments
	echo_verbose "Args list..."
	echo_verbose "connection_mode=${connection_mode}"
	echo_verbose "user=${user}"
	echo_verbose "pass=${pass}"
	echo_verbose "owner=${target_owner}"
	echo_verbose "class=${target_table}"
	echo_verbose "key=${match_key_idx} (${match_key})"
	echo_verbose "threshold=${threshold_value}"
	echo_verbose "sort=${sort_titles[${sort_field}-1*3]}"
	echo_verbose "database=${database}"
	echo_verbose "***************************************"
	echo_verbose ""
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
	passwd=$(csql ${connection_mode} ${user} --password="" -c "SELECT 1" $database 2> /dev/null)

	if [ $? -ne 0 ];then
		passwd=$(get_password)
		pass="-p $passwd"

		passwd=$(csql ${connection_mode} ${user} ${pass} -c "SELECT 1" $database 2> /dev/null)
		if [ $? -ne 0 ];then
			echo "$dbuser: Incorrect or missing password"
			exit 1
		fi
	fi

	# check whether this user is a member of DBA groups
	#
	dba_groups=$(csql  ${connection_mode}  ${user} ${pass} -l -c "$qry_dba_grp" $database | grep -w $USERNAME | wc -l)
	if [ $USERNAME != "DBA" ] && [ $dba_groups -eq 0 ];then
		echo "User '$dbuser' is not a member of DBA group"
		exit 2
	fi
}

# exit, if subject db server is not running
function check_db_server_running ()
{
	local db=$1
	local mode=$2

	db=${database%%@*}

	retcode=$(ps -ef | grep cub_server | grep $db | wc -l)
	if [ $retcode -eq 0 ];then
		if [ x"${mode}" == x"-C" ]; then
			echo "Database server '$database' is not running"
			exit 1
		fi
	else
		if [ x"${mode}" != x"-C" ]; then
			echo "Database server '$database' is running"
			exit 1
		fi
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


function IFS_cleanup() 
{       
        IFS=
}

function IFS_restore()
{
        IFS=${IFS_backup}
}

function do_get_index_name_list()
{
	local is_title_line=1
	local res_buf=""
	local csql_options=""
	local i_qry=""
	local csql_qry=""

	i_qry="${i_qry} SELECT CAST (c.owner.name AS VARCHAR(255)) AS owner_name,"
	i_qry="${i_qry}         c.class_name AS class_name,"
	i_qry="${i_qry}         i.index_name AS index_name,"
	i_qry="${i_qry}         NVL( (SELECT CASE WHEN pname IS NULL THEN 'P' ELSE 'S' END"
	i_qry="${i_qry}               FROM _db_partition p WHERE p.class_of =c), 'X') AS partitioned,"
	i_qry="${i_qry}         CASE i.status WHEN 2 THEN 'YES' ELSE 'NO' END AS invisible"
	i_qry="${i_qry} FROM _db_index i, _db_class c"
	i_qry="${i_qry} WHERE i.class_of = c"
	i_qry="${i_qry}       AND c.class_type=0 AND (MOD(c.is_system_class, 2) != 1)"
	i_qry="${i_qry}       AND i.is_unique=0  AND (i.status=1 /* NORMAL */ OR i.status=2 /* INVISIBLE */)"

	csql_qry="SELECT owner_name || '.' || class_name || '.' ||  index_name || '.' || partitioned || '.' || invisible  "
	csql_qry="${csql_qry} FROM ( ${i_qry} ) AS t"
	csql_qry="${csql_qry} WHERE 1=1"

	if [ x"${target_owner}" != x"" ]; then
		# For comparison of owner_name, change it to uppercase and enter it.
		csql_qry="${csql_qry} AND t.owner_name='${target_owner^^}'"
	fi

	if [ x"${target_table}" != x"" ]; then
		csql_qry="${csql_qry} AND ( t.class_name='${target_table}'"
		csql_qry="${csql_qry}        OR (t.class_name LIKE '${target_table}__p__%' AND t.partitioned = 'S'))"
	fi

	csql_qry="${csql_qry} ORDER BY owner_name, class_name, partitioned, index_name"

    # send query
	csql_options="${connection_mode} ${user} ${pass} -t"
	res_buf=$(csql ${csql_options} -c "${csql_qry}" $database)

        IFS_cleanup

	# build info_map
	info_map=()
	is_title_line=1
	idx=0
	is_title_line=1
	while  read full_name || [ -n "${full_line}" ]
	do
		if [ $is_title_line -eq 1 ]; then
			is_title_line=0
		elif [ x"${full_name}" != x"" ]; then
			info_map+=("0")
			info_map+=("$(echo $full_name | cut -d '.' -f1)") # owner
			info_map+=("$(echo $full_name | cut -d '.' -f2)") # class
			info_map+=("$(echo $full_name | cut -d '.' -f3)") # index
			info_map+=("$(echo $full_name | cut -d '.' -f4)") # partitioned
			info_map+=("$(echo $full_name | cut -d '.' -f5)") # invisible
		fi
	done <<< ${res_buf}
        
        IFS_restore	
}

function do_get_capacity()
{
	if [ $# -ne 1 ]; then
		return
	fi

	local res=""
	local idx=$(($1 * ${info_map_st_sz}))
	local capacity_query="SHOW INDEX CAPACITY OF [${info_map[${idx} + 1]}].[${info_map[${idx} + 2]}].[${info_map[${idx} + 3]}]"

	if [ x"${info_map[${idx} + 4]}" == x"P" ]; then
		info_map[$idx]=0  # partition table
	else		
		res=$(csql ${connection_mode} ${user} ${pass} -l -c "${capacity_query}" $database)
		
		if [ $? -ne 0 ] || [ x"$res" == x"" ] ; then
			info_map[$idx]=0
			echo "Error: csql query=\"${capacity_query}\"" 
			return
		fi
		info_map[$idx]=$(echo ${res#*${match_key}} | awk '{print $2}')
	fi
}

function do_sort()
{
	local tmp_fnm="${1}.tmp"
	local -i i
	local -i k

	touch "${tmp_fnm}"

        IFS_cleanup

	for (( i = 0; i < ${#info_map[@]}; i+=${info_map_st_sz} ))
	do
		if [ ${info_map[${i}]} -ge ${threshold_value} ]; then
			for (( k=0; k < ${info_map_st_sz}; k++ ))
			do
				if [ $k -eq 0 ]; then
					echo -n "${info_map[$i]}"
				else
					echo -n ".${info_map[$i+$k]}"
				fi
			done  >> "${tmp_fnm}"
			echo "" >> "${tmp_fnm}"
		fi
	done

        IFS_restore

	# sorting
	if [ ${sort_field} -eq ${sort_option_page} ]; then
		sort -n -r --key=${sort_field} --field-separator='.' ${tmp_fnm} > "${1}"
	else
		sort --key=${sort_field} --field-separator='.' ${tmp_fnm} > "${1}"
	fi
	rm "${tmp_fnm}"
}

function do_print()
{
	local partitioned_nm
	local -a format_sz=()
	local -r -a title_nm=( "pages" "owner-name"  "class-name" "index-name" "partitioned"  "invisible" )
	local i

	# initialize	
	for (( i=0; i < ${info_map_st_sz}; i++ ))
	do	    
		format_sz+=(${#title_nm[$i]})
	done

        IFS_cleanup

	# get formatted size
	while read line || [ -n "${line}" ]
	do
		for (( i=1; i <= ${info_map_st_sz}; i++ ))
		do
			name="$(echo $line | cut -d '.' -f${i})"
			len=${#name}
			if [ ${format_sz[$i - 1]} -lt ${len} ]; then
				format_sz[$i - 1]=${len}
			fi
		done
	done < "${1}"

	echo ""
	echo ""
        echo "  ----------------------------------------------------------------------------------------------------------------------"
	echo "  *** Key=${match_key} Threahols=${threshold_value}"
	echo ""

	printf "  %-${format_sz[1]}s\t" "${title_nm[1]}"
	printf "%-${format_sz[2]}s\t" "${title_nm[2]}"
	printf "%-${format_sz[3]}s\t" "${title_nm[3]}"
	printf "%-${format_sz[0]}s\t" "${title_nm[0]}"
	printf "%-${format_sz[4]}s\t" "${title_nm[4]}"
	printf "%-${format_sz[5]}s\n" "${title_nm[5]}"
	echo "  ======================================================================================================================"
	while read line || [ -n "${line}" ]
	do
		printf "  %-${format_sz[1]}s\t" "'$(echo $line | cut -d '.' -f2)'"
		printf "%-${format_sz[2]}s\t" "'$(echo $line | cut -d '.' -f3)'"
		printf "%-${format_sz[3]}s\t" "'$(echo $line | cut -d '.' -f4)'"

		if [ x"$(echo $line | cut -d '.' -f5)" == x"P" ]; then
			printf "%${format_sz[0]}s\t" "-"
		else
			printf "%${format_sz[0]}s\t" $(echo $line | cut -d '.' -f1)
		fi

		case "$(echo $line | cut -d '.' -f5)" in
			P) partitioned_nm="MAIN"  ;;
			S) partitioned_nm="SUB"  ;;
			*) partitioned_nm="NO"  ;;
		esac

		printf "%-${format_sz[4]}s\t" "'${partitioned_nm}'"
		printf "%-${format_sz[4]}s\n" "'$(echo $line | cut -d '.' -f6)'"
	done < "${1}"        
        echo "  ----------------------------------------------------------------------------------------------------------------------"
        echo ""

        IFS_restore
}


function cleanup()
{
        IFS=$IFS_restore # restore IFS

	silent_cd ${current_dir}

	if [ x"$work_dir" != x"" ]; then		
		echo_verbose "delete temp directory...[$work_dir]"
		rm -rf ${work_dir}
	fi
}

#################################################################################
# MAIN
#
current_dir="$(pwd)"
work_dir=""

set -o monitor

get_options "$@" # arguments check
print_args # show arguments

if [ x"$database" == x""  ]; then
	show_usage
elif [ x"$user" == x""  ]; then
	show_usage
fi

check_db_server_running ${database} ${connection_mode}

verify_user_pass

# set interrupt
trap "cleanup; exit 1;" HUP INT TERM

silent_cd ${current_dir}
work_dir=$(mktemp -u -d tmp_cub_XXXXX)
echo_verbose "make temp directory...[$work_dir]"
mkdir $work_dir
silent_cd ${work_dir}

info_map=()
echo_verbose "get index name list..."
do_get_index_name_list

if [ ${#info_map[@]} -le 0 ];then
	echo "There are no suitable indexes to check up."
else
	echo_verbose "get capacity info..."
	index_counter=0
	for (( i=0; i < ${#info_map[@]}; i+=${info_map_st_sz} ))
	do
		echo_verbose "    [${info_map[$i + 1]}].[${info_map[$i + 2]}].[${info_map[$i + 3]}]"

		do_get_capacity $index_counter
		(( index_counter++ ))
	done

	result_fnm="result.txt"
	# sort
	do_sort "${result_fnm}"
	#print
	do_print "${result_fnm}"
fi


#clear temp files
cleanup

echo_verbose "Completed."


