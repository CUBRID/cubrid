#!/bin/bash

CURR_DIR=$(dirname $0)
source $CURR_DIR/../common/common.sh

#################################################################################
# program variables
#################################################################################
prog_name='ha_set_apply_info.sh'

dba_password=
repl_log_path=
db_backup_output_file=

db_name=
db_creation=
pageid=
offset=

ha_info_prefix='HA apply info:'

csql_cmd=

current_host=$(uname -n)

#################################################################################
# program function
#################################################################################
function print_usage()
{
	echo ""
	echo "Usage: $prog_name [options]"
	echo ""
	echo "    -r [repl_log_path]"
	echo "    -o [backup_output_file]"
	echo "    -p [dba_password]"
	echo ""
}

function check_args()
{
	if [ -z "$repl_log_path" ]; then
		print_usage
		error "repl_log_path is not specified."
	elif [ -z "$db_backup_output_file" ]; then
		print_usage
		error "db_backup_output_file is not specified."
	fi
	
	repl_log_path=$(readlink -f $repl_log_path)
}

function get_ha_apply_info()
{
	ha_info=$(grep -m 1 "$ha_info_prefix" $db_backup_output_file)
	ret=$?
	if [ $ret -ne 0 ]; then
		return $ret 
	fi

	db_name=$(echo $ha_info | cut -d ' ' -f 5)
	db_creation=$(echo $ha_info | cut -d ' ' -f 6)
	pageid=$(echo $ha_info | cut -d ' ' -f 7)
	offset=$(echo $ha_info | cut -d ' ' -f 8)
	
	if [ -z "$db_name" ]; then
		error "db_name in $db_backup_output_file is invalid."
	elif [ -z "$db_creation" ]; then
		error "db_creation in $db_backup_output_file is invalid."
	elif [ -z "$pageid" ]; then
		error "pageid in $db_backup_output_file is invalid."
	elif [ -z "$offset" ]; then
		error "offset in $db_backup_output_file is invalid."
	fi

	echo -ne "\n\n"
	echo -ne "1. get db_ha_apply_info from backup output($db_backup_output_file). \n\n"
	echo -ne " - dn_name       : $db_name\n"
	echo -ne " - db_creation   : $db_creation\n"
	echo -ne " - pageid        : $pageid\n"
	echo -ne " - offset        : $offset\n"
	echo -ne " - log_path      : $repl_log_path\n"
	echo -ne "\n"
	return 0
}

function make_csql_cmd()
{
	export DB_CREATION=$db_creation
	local_db_creation=`awk 'BEGIN { print strftime("%m/%d/%Y %H:%M:%S", ENVIRON["DB_CREATION"]) }'`

	csql_cmd="\
INSERT INTO \
	db_ha_apply_info \
VALUES \
( \
	'$db_name', \
	datetime '$local_db_creation', \
	'$repl_log_path', \
	$pageid, $offset, \
	$pageid, $offset, \
	$pageid, $offset, \
	$pageid, $offset, \
	$pageid, $offset, \
	$pageid, $offset, \
	NULL, \
	NULL, \
	NULL, \
	0, \
	0, \
	0, \
	0, \
	0, \
	0, \
	0, \
	NULL \
)"
}

function print_old_ha_apply_info()
{
	echo -ne "\n\n"
	echo -ne "2. select old db_ha_apply_info. \n\n"
	if [ -z "$dba_password" ]; then
		cmd_select="csql -u DBA -S $db_name -l -c \"SELECT db_name, db_creation_time, copied_log_path, committed_lsa_pageid, committed_lsa_offset, committed_rep_pageid, committed_rep_offset, required_lsa_pageid, required_lsa_offset FROM db_ha_apply_info WHERE db_name='$db_name'\"" 

		echo "$current_host ]$ $cmd_select"
		eval $cmd_select
		echo ""

	else
		cmd_select="csql -u DBA -p '$dba_password' -S $db_name -l -c \"SELECT db_name, db_creation_time, copied_log_path, committed_lsa_pageid, committed_lsa_offset, committed_rep_pageid, committed_rep_offset, required_lsa_pageid, required_lsa_offset FROM db_ha_apply_info WHERE db_name='$db_name'\"" 

		echo "$current_host ]$ $cmd_select"
		eval $cmd_select
		echo ""
	fi
}

function insert_new_ha_apply_info()
{
	echo -ne "\n\n"
	echo -ne "3. insert new db_ha_apply_info on slave. \n\n"

	if [ -z "$dba_password" ]; then
		cmd_delete="csql --sysadm -u DBA -S $db_name -c \"DELETE FROM db_ha_apply_info WHERE db_name='$db_name'\""
		cmd_insert="csql --sysadm -u DBA -S $db_name -c \"$csql_cmd\""

		echo "$current_host ]$ $cmd_delete"
		eval $cmd_delete
		echo "$current_host ]$ $cmd_insert"
		eval $cmd_insert

		cmd_select="csql -u DBA -S $db_name -l -c \"SELECT db_name, db_creation_time, copied_log_path, committed_lsa_pageid, committed_lsa_offset, committed_rep_pageid, committed_rep_offset, required_lsa_pageid, required_lsa_offset FROM db_ha_apply_info WHERE db_name='$db_name'\"" 
		echo "$current_host ]$ $cmd_select"
		eval $cmd_select

	else
		cmd_delete="csql --sysadm -u DBA -p '$dba_password' -S $db_name -c \"DELETE FROM db_ha_apply_info WHERE db_name='$db_name'\""
		cmd_insert="csql --sysadm -u DBA -p '$dba_password' -S $db_name -c \"$csql_cmd\""

		echo "$current_host ]$ $cmd_delete"
		eval $cmd_delete
		echo "$current_host ]$ $cmd_insert"
		eval $cmd_insert

		cmd_select="csql -u DBA -p '$dba_password' -S $db_name -l -c \"SELECT db_name, db_creation_time, copied_log_path, committed_lsa_pageid, committed_lsa_offset, committed_rep_pageid, committed_rep_offset, required_lsa_pageid, required_lsa_offset FROM db_ha_apply_info WHERE db_name='$db_name'\"" 
		echo "$current_host ]$ $cmd_select"
		eval $cmd_select
	fi
}

#################################################################################
# main function
#################################################################################
while getopts "p:r:o:" optname
do
	case "$optname" in
		"p") dba_password="${OPTARG}";;
		"r") repl_log_path="${OPTARG}";;
		"o") db_backup_output_file="${OPTARG}";;
		"?") print_usage;;
		":") print_usage;;
		*) print_usage;;
	esac
done

check_args

get_ha_apply_info

print_old_ha_apply_info

make_csql_cmd
insert_new_ha_apply_info

exit 0
