#!/bin/bash

################################################################################
# 
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
################################################################################

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

function is_opt_invalid()
{
	[ -z "$repl_log_path" ] && echo " << ERROR >> repl_log_path is not specified" && return 0
	[ -z "$db_backup_output_file" ] && echo " << ERROR >> db_backup_output_file is not specified" && return 0
	return 1
}

function is_ha_apply_info_invalid()
{
	[ -z "$db_name" ] && echo " << ERROR >> db_name in $db_backup_output_file is invalid" && return 0
	[ -z "$db_creation" ] && echo " << ERROR >> db_creation in $db_backup_output_file is invalid" && return 0
	[ -z "$pageid" ] && echo " << ERROR >> pageid in $db_backup_output_file is invalid" && return 0
	[ -z "$offset" ] && echo " << ERROR >> offset in $db_backup_output_file is invalid" && return 0
	return 1
}

function print_ha_apply_info()
{
	echo ""
	echo "*********************************************************************************"
	echo "*  db_ha_apply_info                                                              "
	echo "*         - db_name : $db_name                                                  "
	echo "*         - db_creation : $db_creation                                          " 
	echo "*         - log_path : $repl_log_path                                           "
	echo "*         - pageid : $pageid                                                    "
	echo "*         - offset : $offset                                                    "
	echo "*                                                                               "
	echo "*********************************************************************************"
	return 0
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
	-1, -1, \
	NULL, \
	NULL, \
	0, \
	0, \
	0, \
	0, \
	0, \
	0, \
	0, \
	$pageid, \
	NULL \
)"
}

function print_old_ha_apply_info()
{
	echo -ne "\n\n"
	echo -ne "2. select old db_ha_apply_info. \n\n"
	if [ -z "$dba_password" ]; then
		cmd_select="csql -u dba -S $db_name -l -c \"SELECT db_name, db_creation_time, copied_log_path, page_id, offset, required_page_id FROM db_ha_apply_info WHERE db_name='$db_name'\"" 

		echo "$current_host ]$ $cmd_select"
		eval $cmd_select
		echo ""

	else
		cmd_select="csql -u dba -p '$dba_password' -S $db_name -l -c \"SELECT db_name, db_creation_time, copied_log_path, page_id, offset, required_page_id FROM db_ha_apply_info WHERE db_name='$db_name'\"" 

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
		cmd_delete="csql --sysadm -u dba -S $db_name -c \"DELETE FROM db_ha_apply_info WHERE db_name='$db_name'\""
		cmd_insert="csql --sysadm -u dba -S $db_name -c \"$csql_cmd\""

		echo "$current_host ]$ $cmd_delete"
		eval $cmd_delete
		echo "$current_host ]$ $cmd_insert"
		eval $cmd_insert

		cmd_select="csql -u dba -S $db_name -l -c \"SELECT db_name, db_creation_time, copied_log_path, page_id, offset, required_page_id FROM db_ha_apply_info WHERE db_name='$db_name'\"" 
		echo "$current_host ]$ $cmd_select"
		eval $cmd_select

	else
		cmd_delete="csql --sysadm -u dba -p '$dba_password' -S $db_name -c \"DELETE FROM db_ha_apply_info WHERE db_name='$db_name'\""
		cmd_insert="csql --sysadm -u dba -p '$dba_password' -S $db_name -c \"$csql_cmd\""

		echo "$current_host ]$ $cmd_delete"
		eval $cmd_delete
		echo "$current_host ]$ $cmd_insert"
		eval $cmd_insert

		cmd_select="csql -u dba -p '$dba_password' -S $db_name -l -c \"SELECT db_name, db_creation_time, copied_log_path, page_id, offset, required_page_id FROM db_ha_apply_info WHERE db_name='$db_name'\"" 
		echo "$current_host ]$ $cmd_select"
		eval $cmd_select
	fi
}


### main ##############################

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

if is_opt_invalid; then
	print_usage
	exit 1
fi

get_ha_apply_info

if is_ha_apply_info_invalid; then
	exit 1
fi

print_old_ha_apply_info

# print_ha_apply_info 

make_csql_cmd
insert_new_ha_apply_info

exit 0
