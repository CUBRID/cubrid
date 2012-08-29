#!/bin/bash

CURR_DIR=$(dirname $0)
WORK_DIR=$(pwd)
HA_DIR=$CURR_DIR/..
source $CURR_DIR/common/common.sh

#################################################################################
# user configuration
#################################################################################
# mandatory
target_host=
repl_log_home=$CUBRID_DATABASES

# optional
db_name=

backup_dest_path=
backup_option=
restore_option=
scp_option="-l 131072"		# default - limit : 16M*1024*8=131072

#################################################################################
# program variables
#################################################################################
# automatic - DO NOT CHANGE !!!
prog_name="ha_make_slavedb.sh"
repl_log_home_abs=
master_host=
slave_host=
replica_hosts=
current_host=$(uname -n)
current_state=
target_state=
cubrid_user=$(whoami)
now=$(date +"%Y%m%d_%H%M%S")

ha_temp_home=$HOME/.ha
function_home=$ha_temp_home/functions
expect_home=$ha_temp_home/expect
install_output=$ha_temp_home/install.output
env_output=$ha_temp_home/env.output
repl_util_output=$ha_temp_home/repl_utils.output
copylog_output=$ha_temp_home/copylog.output
backupdb_output=
db_vol_path=
db_log_path=

step_no=1
step_func=
step_func_slave_from_master=(
	"get_password"
	"show_environment"
	"copy_script_to_master"
	"copy_script_to_replica"
	"check_environment"
	"suspend_master_repl_util"
	"init_ha_info_on_master"
	"init_ha_info_on_replica"
	"online_backup_db"
	"copy_backup_db_from_target"
	"restore_db_to_current"
	"init_ha_info_on_slave"
	"copy_active_log_from_master"
	"restart_master_repl_util"
	"show_complete"
)
step_func_slave_from_replica=(
	"get_password"
	"show_environment"
	"copy_script_to_master"
	"copy_script_to_replica"
	"check_environment"
	"suspend_master_repl_util"
	"init_ha_info_on_master"
	"init_ha_info_on_replica"
	"online_backup_db"
	"copy_backup_db_from_target"
	"restore_db_to_current"
	"copy_active_log_from_master"
	"restart_master_repl_util"
	"show_complete"
)
step_func_replica_from_slave=(
	"get_password"
	"show_environment"
	"copy_script_to_master"
	"copy_script_to_slave"
	"check_environment"
	"online_backup_db"
	"copy_backup_db_from_target"
	"restore_db_to_current"
	"copy_active_log_from_master"
	"copy_active_log_from_slave"
	"show_complete"	
)
step_func_replica_from_replica=(
	"get_password"
	"show_environment"
	"copy_script_to_target"
	"check_environment"
	"online_backup_db"
	"copy_backup_db_from_target"
	"restore_db_to_current"
	"suspend_target_repl_util"
	"copy_active_log_from_target"
	"restart_target_repl_util"
	"show_complete"
)


#################################################################################
# program functions
#################################################################################
function execute()
{
	if [ $# -ne 1 ]; then
		error "Invalid execute call. $*"
	fi
	
	command=$1
	echo "[$cubrid_user@$current_host]$ $command"
	eval $command > /dev/null 2>&1
}

function ssh_cubrid()
{
	if [ $# -eq 2 ]; then
		verbose=true
	elif [ $# -lt 2 -o $# -gt 3 ]; then
		error "Invalid ssh_cubrid call. $*"
	else
		verbose=$3
	fi
	
	host=$1
	command=$2

	if $verbose; then
		echo "[$cubrid_user@$host]$ $command"
	fi
	ssh -t $cubrid_user@$host "export PATH=$PATH; export LD_LIBRARY_PATH=$LD_LIBRARY_PATH; export CUBRID=$CUBRID; export CUBRID_DATABASES=$CUBRID_DATABASES; $command"
}

function ssh_expect()
{
	if [ $# -lt 4 ]; then
		error "Invalid ssh_expect call. $*"
	fi
	
	user=$1
	password=$2
	host=$3
	
	command1=$4
	command2=$5
	command3=$6
	command4=$7
	command5=$8

	if [ ! -z "$command1" ]; then
		echo "[$user@$host]$ $command1"
	fi
	if [ ! -z "$command2" ]; then
		echo "[$user@$host]$ $command2"
	fi
	if [ ! -z "$command3" ]; then
		echo "[$user@$host]$ $command3"
	fi
	if [ ! -z "$command4" ]; then
		echo "[$user@$host]$ $command4"
	fi
	if [ ! -z "$command5" ]; then
		echo "[$user@$host]$ $command5"
	fi
	expect $CURR_DIR/expect/ssh.exp "$user" "$password" "$host" "$command1" "$command2" "$command3" "$command4" "$command5" >/dev/null 2>&1
}

function scp_cubrid_to()
{
	[ "$#" -ne 3 ] && return
	
	host=$1
	source=$2
	target=$3

	echo "[$cubrid_user@$current_host]$ scp $scp_option -r $source $cubrid_user@$host:$target"
	scp $scp_option -r $source $cubrid_user@$host:$target
}

function scp_to_expect()
{
	if [ $# -ne 5 ]; then
		error "Invalid scp_to_expect call. $*"
	fi
	
	user=$1
	password=$2
	source=$3
	host=$4
	target=$5
	
	echo "[$user@$current_host]$ scp $scp_option -r $source $user@$host:$target"
	expect $CURR_DIR/expect/scp_to.exp "$user" "$password" "$source" "$host" "$target" >/dev/null 2>&1
}

function scp_from_expect()
{
	if [ $# -ne 5 ]; then
		error "Invalid scp_from_expect call. $*"
	fi
	
	user=$1
	password=$2
	source=$3
	host=$4
	target=$5
	
	echo "[$user@$current_host]$ scp $scp_option -r $user@$host:$source $target"
	expect $CURR_DIR/expect/scp_from.exp "$user" "$password" "$source" "$host" "$target" >/dev/null 2>&1
}

function scp_cubrid_from()
{
	if [ $# -ne 3 ]; then
		error "Invalid scp_cubrid_from call. $*"
	fi
	
	host=$1
	source=$2
	target=$3

	scp $scp_option -r $cubrid_user@$host:$source $target
}

function get_output_from_replica()
{
	if [ $# -ne 1 ]; then
		error "Invalid get_output_from_replica call. $*"
	fi
	
	output=$1
	
	rm -rf $output
	mkdir $output
	
	for replica_host in ${replica_hosts[@]}; do
		scp_from_expect "$cubrid_user" "$server_password" $output $replica_host $output/$replica_host 
	done
}

function check_args()
{
	if [ -z $target_host ]; then
		error "Invalid target_host."
	elif [ -z $repl_log_home ]; then
		error "Invalid repl_log_home."
	fi
}

function init_conf()
{
	# init path
	mkdir -p $ha_temp_home
	if [ -n $backup_dest_path ]; then 
		backup_dest_path=$ha_temp_home/backup
		if [ ! -d $backup_dest_path ]; then
			mkdir $backup_dest_path
		fi
	fi
	repl_log_home=${repl_log_home%%/}
	repl_log_home_abs=$(readlink -f $repl_log_home)
	backup_dest_path=${backup_dest_path%%/}
	backup_dest_path=$(readlink -f $backup_dest_path)
	
	# get conf from cubrid_ha.conf file
	if [ ! -f $CUBRID/conf/cubrid_ha.conf ]; then
		error "Not found cubrid_ha.conf on $CUBRID/conf."
	fi
	
	node_index=1
	while read line
	do
		if [[ "${line:0:1}" != "#" && "${line:0:1}" != "" ]]; then
			OFS=$IFS
			IFS="="
			conf=($line)
			IFS=$OFS
			case ${conf[0]} in
				"ha_node_list")
					hosts=$(echo ${conf[1]} | cut -d '@' -f 2)
					master_host=$(echo $hosts | cut -d ':' -f 1)
					slave_host=$(echo $hosts | cut -d ':' -f 2)
					if [ "$slave_host" == "$target_host" ]; then
						node_index=2
					fi
					;;
				"ha_replica_list") replica_hosts=$(echo ${conf[1]} | cut -d '@' -f 2);;
				"ha_db_list")
					if [ -z $db_name ]; then
						db_name=${conf[1]}
						db_name=$(echo $db_name | cut -d ',' -f 1)
					fi
					;;
			esac
		fi
	done < $CUBRID/conf/cubrid_ha.conf

	if [ -z $db_name ]; then
		error "The db_name is null."
	fi
	
	# check the master and slave host is valid
	cubrid changemode $db_name@$master_host 2>/dev/null | grep active >/dev/null
	if [ $? -ne $SUCCESS ]; then
		cubrid changemode $db_name@$slave_host 2>/dev/null | grep active >/dev/null
		if [ $? -ne $SUCCESS ]; then
			error "Both the master and slave is not active state."
		fi
		tmp_host=$master_host
		master_host=$slave_host
		slave_host=$tmp_host
	fi
	
	# get state of the current server (master / slave / replca)
	if [ $current_host == "$master_host" ]; then
		error "This script is not running on master server."
	elif [ $current_host == "$slave_host" ]; then
		current_state="slave"
	else
		current_state="replica"
	fi
	
	# get state of the target server (master / slave / replca)
	if [ "$target_host" == "$master_host" ]; then
		target_state="master"
	elif [ "$target_host" == "$slave_host" ]; then
		target_state="slave"
	elif [ "$(echo $replica_hosts | grep $target_host)" != "" ]; then
		target_state="replica"
	else
		error "Could not find the target server."
	fi
	
	# check the target server state and current server state is valid.
	case $current_state in
		"slave")
			case $target_state in
				"master") step_func=(${step_func_slave_from_master[@]});;
				"replica") step_func=(${step_func_slave_from_replica[@]});;
				*) error "Invalid target server state.";;
			esac;;
		"replica")
			case $target_state in
				"slave") step_func=(${step_func_replica_from_slave[@]});;
				"replica") step_func=(${step_func_replica_from_replica[@]});;
				*) error "Invalid target server state.";;
			esac;;
	esac
	
	# split replica_hosts to array.
	OFS=$IFS
	IFS=":"
	replica_hosts=($replica_hosts)
	IFS=$OFS
	
	if [ "$current_host" == "$target_host" ]; then
		error "The current host($current_host) and target host($target_host) is equal." 
	fi
	
	# check the db server is running on current host.
	cubrid changemode $db_name@localhost >/dev/null 2>&1
	if [ $? -eq $SUCCESS ]; then
		cubrid heartbeat list
		error "The db server is running on current host"
	fi
	
	# check the replica host is running.
	if [ "$replica_hosts" != "" ]; then
		for replica_host in ${replica_hosts[@]}; do
			if [ "$current_host" == "$replica_host" ]; then
				continue
			fi
				
			cubrid changemode $db_name@$replica_host 2>/dev/null | grep standby >/dev/null
			if [ $? -ne $SUCCESS ]; then
				error "$replica_host is not running."
			fi
		done
	fi
}

function get_yesno()
{
	echo ""
	for ((i = 0; i < 10; i++ )) do
		echo -ne "   continue ? ([y]es / [n]o / [s]kip) : "
		read yesno
		case $yesno in
			"yes"|"y"|"Y")
				STDIN=$YES
				break;;
			"skip"|"s"|"S")
				echo -ne "\n      ............................ skipped .......\n\n"
				STDIN=$SKIP
				break;;
			"no"|"n"|"N")
				exit $SUCCESS;;
		esac
	done
	echo ""
}

function get_password()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "# get HA/replica user password and DBA password"	
	echo "#"
	echo "#  * warning !!!"
	echo "#   - Because $prog_name use expect (ssh, scp) to control HA/replica node,"
	echo "#     the script has to know these passwords."
	echo "#"
	echo "################################################################################"
	get_yesno

	while true; do
		echo -ne "\nHA/replica $cubrid_user's password : "
		read -s server_password
		echo -ne "\nHA/replica $cubrid_user's password : "
		read -s re_server_password
		
		if [ "$server_password" == "$re_server_password" ]; then
			break
		else
			echo "Sorry, passwords do not match."
		fi
	done
	
	while true; do
		echo -ne "\n\n$db_name's DBA password : "
		read -s dba_password
		echo -ne "\nRetype $db_name's DBA password : "
		read -s re_dba_password
		
		if [ "$dba_password" == "$re_dba_password" ]; then
			break
		else
			echo "Sorry, passwords do not match."
		fi
	done
}

function show_environment()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  $prog_name is the script for making slave database more easily"
	echo "#"
	echo "#  * environment"
	echo "#   - db_name           : $db_name"
	echo "#"
	echo "#   - master_host       : $master_host"
	echo "#   - slave_host        : $slave_host"
	echo "#   - replica_hosts     : ${replica_hosts[@]}"
	echo "#"
	echo "#   - current_host      : $current_host"
	echo "#   - current_state     : $current_state"
	echo "#"
	echo "#   - target_host       : $target_host"
	echo "#   - target_state      : $target_state"
	echo "#"
	echo "#   - repl_log_home     : $repl_log_home"
	echo "#   - backup_dest_path  : $backup_dest_path"
	echo "#   - backup_option     : $backup_option"
	echo "#   - restore_option    : $restore_option"
	echo "#"
	echo "#  * warning !!!"
	echo "#   - environment on slave must be same as master"
	echo "#   - database and replication log on slave will be deleted"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
}

function copy_script_to_master()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  copy scripts to master node"
	echo "#"
	echo "#  * details"
	echo "#   - scp scripts to '~/.ha' on $master_host(master)".
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	cd $HA_DIR
	execute "tar -zcf ha.tgz ha"
	cd $WORK_DIR
	ssh_cubrid $master_host "rm -rf $ha_temp_home"
	scp_cubrid_to $master_host "$HA_DIR/ha.tgz" "$HOME"
	ssh_cubrid $master_host "tar -zxf ha.tgz"
	ssh_cubrid $master_host "mv ha $ha_temp_home"
	ssh_cubrid $master_host "mkdir $backup_dest_path"
}

function copy_script_to_slave()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  copy scripts to slave node"
	echo "#"
	echo "#  * details"
	echo "#   - scp scripts to '~/.ha' on $slave_host($slave_state)".
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	cd $HA_DIR
	execute "tar -zcf ha.tgz ha"
	cd $WORK_DIR
	ssh_cubrid $slave_host "rm -rf $ha_temp_home"
	scp_cubrid_to $slave_host "$HA_DIR/ha.tgz" "$HOME"
	ssh_cubrid $slave_host "tar -zxf ha.tgz"
	ssh_cubrid $slave_host "mv ha $ha_temp_home"
	ssh_cubrid $slave_host "mkdir $backup_dest_path"
}

function copy_script_to_target()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  copy scripts to target node"
	echo "#"
	echo "#  * details"
	echo "#   - scp scripts to '~/.ha' on $target_host($target_state)".
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	cd $HA_DIR
	execute "tar -zcf ha.tgz ha"
	cd $WORK_DIR
	ssh_cubrid $target_host "rm -rf $ha_temp_home"
	scp_cubrid_to $target_host "$HA_DIR/ha.tgz" "$HOME"
	ssh_cubrid $target_host "tar -zxf ha.tgz"
	ssh_cubrid $target_host "mv ha $ha_temp_home"
	ssh_cubrid $target_host "mkdir $backup_dest_path"
}

function copy_script_to_replica()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  copy scripts to replication node"
	echo "#"
	echo "#  * details"
	echo "#   - scp scripts to '~/.ha' on replication node".
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	if [ "$replica_hosts" == "" ]; then
		echo "There is no replication server to copy scripts."
	else
		cd $HA_DIR
		execute "tar -zcf ha.tgz ha"
		cd $WORK_DIR
		
		# 1. scp ha.tgz to replications.
		echo -ne "\n - 1. scp ha.tgz to replications.\n\n"
		for replica_host in ${replica_hosts[@]}; do
			if [ "$replica_host" != "$current_host" ]; then
				scp_to_expect "$cubrid_user" "$server_password" "$HA_DIR/ha.tgz" "$replica_host" "~"
			fi	
		done
		
		# 2. extract ha.tgz on replications and check if the script is copyied normally.
		echo -ne "\n - 2. extract ha.tgz on replications and check if the script is copyied normally.\n\n"
		command1="rm -rf ~/.ha"
		command2="tar -zxf ha.tgz"
		command3="mv ha $ha_temp_home"
		command4="mkdir $backup_dest_path"
		command5="sh $function_home/ha_check_script.sh -t $ha_temp_home -o $install_output"
		for replica_host in ${replica_hosts[@]}; do
			ssh_expect "$cubrid_user" "$server_password" $replica_host "$command1" "$command2" "$command3" "$command4" "$command5"
		done
		
		get_output_from_replica $install_output
		
		is_exist_error=false
		for replica_host in ${replica_hosts[@]}; do
			if [ ! -f $install_output/$replica_host ]; then
				error "The script is not properly installed on $replica_host." true
				is_exist_error=true
			fi
		done
		
		execute "rm -f $HA_DIR/ha.tgz"
		
		if $is_exist_error; then
			error "The script is not properly installed on some replications."
		fi
	fi
}

function check_environment()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  check environment of all ha node"
	echo "#"
	echo "#  * details"
	echo '#   - test $CUBRID == '"$CUBRID"
	echo '#   - test $CUBRID_DATABASES == '"$CUBRID_DATABASES"
	echo "#   - test -d $repl_log_home/$db_name"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	rm -rf $env_output
	mkdir $env_output
	for host in $master_host $slave_host ${replica_hosts[@]}; do
		if [ "$current_host" == "$host" ]; then
			echo "[$cubrid_user@$current_host]$ sh $CURR_DIR/functions/ha_check_environment.sh -t $ha_temp_home -o $env_output/$host -c $CUBRID -d $CUBRID_DATABASES -r $repl_log_home"			
			sh $CURR_DIR/functions/ha_check_environment.sh -t $ha_temp_home -o $env_output/$host -c $CUBRID -d $CUBRID_DATABASES -r $repl_log_home
		else
			ssh_expect $cubrid_user "$server_password" "$host" "sh $function_home/ha_check_environment.sh -t $ha_temp_home -o $env_output -c $CUBRID -d $CUBRID_DATABASES -r $repl_log_home"
			scp_from_expect $cubrid_user "$server_password" $env_output $host $env_output/$host
		fi
	done
	
	for host in $master_host $slave_host ${replica_hosts[@]}; do
		if [ ! -f $env_output/$host ]; then
			error "$host's environment is different others."
		fi
	done
}

function suspend_master_repl_util()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  suspend copylogdb/applylogdb on master if running"
	echo "#"
	echo "#  * details"
	echo "#   - deregister copylogdb/applylogdb on $master_host(master)."
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	ssh_cubrid $master_host "sh $function_home/ha_repl_suspend.sh -l $repl_log_home -d $db_name -h $slave_host -o $repl_util_output"
	if [ $? -ne $SUCCESS ]; then   
		error "Fail to suspend copylogdb/applylogdb." true
	fi
	echo "Wait for 60s to deregister coppylogdb/applylogdb."
	for (( i = 0; i < 60; i++ )); do
		echo -ne "."
		sleep 1
	done
}

function suspend_target_repl_util()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  suspend copylogdb/applylogdb on target server if running"
	echo "#"
	echo "#  * details"
	echo "#   - deregister copylogdb/applylogdb on $target_host($target_state)."
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	# 1. suspend master repl util
	echo -ne "\n - 1. suspend master repl util.\n\n" 
	ssh_cubrid $target_host "sh $function_home/ha_repl_suspend.sh -l $repl_log_home -d $db_name -h $master_host -o $repl_util_output.m"
	if [ $? -ne $SUCCESS ]; then   
		error "Fail to suspend copylogdb/applylogdb."
	fi
		
	# 2. suspend slave repl util
	echo -ne "\n - 2. suspend slave repl util.\n\n"
	ssh_cubrid $target_host "sh $function_home/ha_repl_suspend.sh -l $repl_log_home -d $db_name -h $slave_host -o $repl_util_output.s"
	if [ $? -ne $SUCCESS ]; then   
		error "Fail to suspend copylogdb/applylogdb."
	fi
	
	echo "Wait for 60s to deregister coppylogdb/applylogdb."
	for (( i = 0; i < 60; i++ )); do
		echo -ne "."
		sleep 1
	done
}

function online_backup_db()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  online backup database $db_bame on $target_state"
	echo "#"
	echo "#  * details"
	echo "#   - run 'cubrid backupdb -C -D ... -o ... $db_name@localhost' on $target_state"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	backupdb_output=$backup_dest_path/$db_name.bkup.output

	ssh_cubrid $target_host "cubrid backupdb $backup_option -C -D $backup_dest_path -o $backupdb_output $db_name@localhost"
	if [ $? -ne $SUCCESS ]; then
		error "Fail to backup database."
	fi
	ssh_cubrid $target_host "cat $backupdb_output"
}

function copy_backup_db_from_target()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  copy $db_name databases backup to current host"
	echo "#"
	echo "#  * details"
	echo "#   - scp databases.txt from target host if there's no $db_name info on current host"
	echo "#   - remove old database and replication log if exist"
	echo "#   - make new database volume and replication path"
	echo "#   - scp $db_nama database backup to current host"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	# 1. check if the databases information is already registered.
	echo -ne "\n - 1. check if the databases information is already registered.\n\n"
	line=$(grep "^$db_name" $CUBRID_DATABASES/databases.txt) 
	if [ -z "$line" ]; then
		execute "mv -f $CUBRID_DATABASES/databases.txt $CUBRID_DATABASES/databases.txt.$now"
		scp_cubrid_from $target_host "$CUBRID_DATABASES/databases.txt" "$CUBRID_DATABASES/."
	else
		echo -ne "\n - thres's already $db_name information in $CUBRID_DATABASES/databases.txt" 
		echo "[$current_host]$ grep $db_name $CUBRID_DATABASES/databases.txt" 
		echo "$line"
	fi
	
	# 2. get db_vol_path and db_log_path from databases.txt.
	echo -ne "\n - 2. get db_vol_path and db_log_path from databases.txt.\n\n"
	line=($(grep "^$db_name" $CUBRID_DATABASES/databases.txt))
	db_vol_path=${line[1]}
	db_log_path=${line[3]}
	if [ -z "$db_vol_path" -o -z "$db_log_path" ]; then
		error "Invalid db_vol_path/db_log_path."
	fi
	
	# 3. remove old database and replication log.
	echo -ne "\n - 3. remove old database and replication log.\n\n"
	execute "rm -rf $db_log_path"
	execute "rm -rf $db_vol_path"
	execute "rm -rf $repl_log_home/${db_name}_*"
	
	# 4. make new database volume and replication log directory.
	echo -ne "\n - 4. make new database volume and replication log directory.\n\n"
	execute "mkdir -p $db_vol_path"
	execute "mkdir -p $db_log_path"
	execute "mkdir -p $ha_temp_home"
	execute "rm -rf $backup_dest_path"
	execute "mkdir -p $backup_dest_path"
	
	# 5. copy backup volume and log from target host
	echo -ne "\n - 5. copy backup volume and log from target host\n\n"
	scp_cubrid_from $target_host "$db_log_path/${db_name}_bkvinf" "$db_log_path"
	if [ $? -ne $SUCCESS ]; then
		error "Fail to copy backup volume and log from target host."
	fi

	scp_cubrid_from $target_host "$backup_dest_path/*" "$backup_dest_path/."
	if [ $? -ne $SUCCESS ]; then
		error "Fail to copy backup volume and log from target host."
	fi
}

function restore_db_to_current()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  restore database $db_name on current host"
	echo "#"
	echo "#  * details"
	echo "#   - cubrid restoredb -B ... $db_name current host"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	execute "cubrid restoredb -B $backup_dest_path $restore_option $db_name"
}

function init_ha_info_on_master()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  remove old copy log of slave and init db_ha_apply_info on master" 
	echo "#"
	echo "#  * details"
	echo "#   - remove old copy log of slave"
	echo "#   - init db_ha_apply_info on master"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	# 1. remove old copy log.
	echo -ne "\n - 1. remove old copy log.\n\n"
	ssh_cubrid $master_host "rm -rf $CUBRID_DATABASES/$db_name\_$slave_host/*"
	
	# 2. init db_ha_apply_info.
	echo -ne "\n - 2. init db_ha_apply_info.\n\n"
	pw_option=""
	if [ -n "$dba_password" ]; then
		pw_option="-p $dba_password"
	fi
	ssh_cubrid $master_host "csql -C -u DBA $pw_option --sysadm $db_name@localhost -c \"delete from db_ha_apply_info where db_name='$db_name'\""
	ssh_cubrid $master_host "csql -C -u DBA $pw_option --sysadm $db_name@localhost -c \"select * from db_ha_apply_info where db_name='$db_name'\""
}
	
function init_ha_info_on_slave()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  set db_ha_apply_info on slave" 
	echo "#"
	echo "#  * details"
	echo "#   - insert db_ha_apply_info on slave"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi

	repl_log_path=$repl_log_home_abs/${db_name}_${master_host}
	
	pw_option=""
	if [ -n "$dba_password" ]; then
		pw_option="-p $dba_password"
	fi
	sh $CURR_DIR/functions/ha_set_apply_info.sh -r $repl_log_path -o $backupdb_output $pw_option
}

function init_ha_info_on_replica()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  remove old copy log of slave and init db_ha_apply_info on replications" 
	echo "#"
	echo "#  * details"
	echo "#   - remove old copy log of slave"
	echo "#   - init db_ha_apply_info on replications"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	if [ "$replica_hosts" == "" ]; then
		echo "There is no replication server to init ha_info"
	else
		repl_log_path="$repl_log_home_abs/${db_name}_${slave_host}"
		
		command1="cubrid heartbeat stop"
		command2="rm -rf $repl_log_path"
		pw_option=""
		if [ -n "$dba_password" ]; then
			pw_option="-p $dba_password"
		fi
		command3="csql -S -u DBA $pw_option --sysadm $db_name -c \"delete from db_ha_apply_info where db_name='$db_name' and copied_log_path='$repl_log_path'\""
		command4="sh $function_home/ha_check_copylog.sh -t $ha_temp_home -d $db_name -r $repl_log_path $pw_option -o $copylog_output"
		command5="cubrid heartbeat start"
		
		# 1. stop cubrid ha.
		# 2. remove old copy log and init db_ha_apply_info.
		# 3. check if the db_ha_apply_info is initialized.
		# 4. start cubrid ha.
		echo -ne "\n - 1. stop cubrid ha.\n\n"
		echo -ne "\n - 2. remove old copy log and init db_ha_apply_info.\n\n"
		echo -ne "\n - 3. check if the db_ha_apply_info is initialized.\n\n"
		echo -ne "\n - 4. start cubrid ha.\n\n"
		
		for replica_host in ${replica_hosts[@]}; do
			ssh_expect "$cubrid_user" "$server_password" $replica_host "$command1" "$command2" "$command3" "$command4" "$command5"
		done
		
		get_output_from_replica $copylog_output
		
		is_exist_error=false
		for replica_host in ${replica_hosts[@]}; do
			if [ ! -f $copylog_output/$replica_host ]; then
				error "The old copy log is not removed or db_ha_apply_info is not initialized on $replica_host." true
				is_exist_error=true
			fi
		done
		
		if $is_exist_error; then
			error "The old copy log is not removed or db_ha_apply_info is not initialized on some replications."
		fi
	fi
}

function copy_active_log_from_master()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  make initial replication active log on master, and copy archive logs from"
	echo "#  master" 
	echo "#"
	echo "#  * details"
	echo "#   - remove old replication log on master if exist"
	echo "#   - start copylogdb to make replication active log"
	echo "#   - copy archive logs from master"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	repl_log_path=$repl_log_home_abs/${db_name}_${master_host}

	# 1. remove old replicaton log.
	echo -ne "\n - 1. remove old replicaton log.\n\n"
	execute "rm -rf $repl_log_path"	
	execute "mkdir -p $repl_log_path"

	# 2. start copylogdb to initiate active log.
	echo -ne "\n - 2. start copylogdb to initiate active log.\n\n"
	sh $CURR_DIR/functions/ha_repl_copylog.sh -r $repl_log_path -d $db_name -h $master_host

	# 3. copy archive log from target.
	echo -ne "\n - 3. copy archive log from target.\n\n"
	if [ -z $db_log_path ]; then
		line=($(grep "^$db_name" $CUBRID_DATABASES/databases.txt))
		db_log_path=${line[3]}
	fi
	scp_cubrid_from $master_host "$db_log_path/${db_name}_lgar[0-9]*" "$repl_log_path/."	
}

function copy_active_log_from_slave()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  make initial replication active log on slave, and copy archive logs from"
	echo "#  slave" 
	echo "#"
	echo "#  * details"
	echo "#   - remove old replication log on slave if exist"
	echo "#   - start copylogdb to make replication active log"
	echo "#   - copy archive logs from slave"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	repl_log_path=$repl_log_home_abs/${db_name}_${slave_host}

	# 1. remove old replicaton log.
	echo -ne "\n - 1. remove old replicaton log.\n\n"
	execute "rm -rf $repl_log_path"	
	execute "mkdir -p $repl_log_path"

	# 2. start copylogdb to initiate active log.
	echo -ne "\n - 2. start copylogdb to initiate active log.\n\n"
	sh $CURR_DIR/functions/ha_repl_copylog.sh -r $repl_log_path -d $db_name -h $slave_host

	# 3. copy archive log from target.
	echo -ne "\n - 3. copy archive log from target.\n\n"
	if [ -z $db_log_path ]; then
		line=($(grep "^$db_name" $CUBRID_DATABASES/databases.txt))
		db_log_path=${line[3]}
	fi
	scp_cubrid_from $slave_host "$db_log_path/${db_name}_lgar[0-9]*" "$repl_log_path/."	
}

function copy_active_log_from_target()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  make initial replication active log on target, and copy archive logs from"
	echo "#  target" 
	echo "#"
	echo "#  * details"
	echo "#   - remove old replication log on target if exist"
	echo "#   - start copylogdb to make replication active log"
	echo "#   - copy archive logs from target"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	repl_log_path=$repl_log_home_abs/${db_name}_${slave_host}

	# 1. remove old replicaton log.
	echo -ne "\n - 1. remove old slave replicaton log.\n\n"
	execute "rm -rf $repl_log_path"	
	execute "mkdir -p $repl_log_path"

	# 2. start copylogdb to initiate active log.
	echo -ne "\n - 2. start copylogdb to initiate slave active log.\n\n"
	sh $CURR_DIR/functions/ha_repl_copylog.sh -r $repl_log_path -d $db_name -h $slave_host

	# 3. copy archive log from target.
	echo -ne "\n - 3. copy archive log from slave.\n\n"
	if [ -z $db_log_path ]; then
		line=($(grep "^$db_name" $CUBRID_DATABASES/databases.txt))
		db_log_path=${line[3]}
	fi
	scp_cubrid_from $slave_host "$db_log_path/${db_name}_lgar[0-9]*" "$repl_log_path/."
	
	repl_log_path=$repl_log_home_abs/${db_name}_${master_host}

	# 4. remove old replicaton log.
	echo -ne "\n - 4. remove old master replicaton log.\n\n"
	execute "rm -rf $repl_log_path"	
	execute "mkdir -p $repl_log_path"

	# 5. start copylogdb to initiate active log.
	echo -ne "\n - 5. start copylogdb to initiate master active log.\n\n"
	sh $CURR_DIR/functions/ha_repl_copylog.sh -r $repl_log_path -d $db_name -h $master_host

	# 6. copy archive log from target.
	echo -ne "\n - 6. copy archive log from master.\n\n"
	if [ -z $db_log_path ]; then
		line=($(grep "^$db_name" $CUBRID_DATABASES/databases.txt))
		db_log_path=${line[3]}
	fi
	scp_cubrid_from $master_host "$db_log_path/${db_name}_lgar[0-9]*" "$repl_log_path/."	
}

function restart_master_repl_util()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  restart copylogdb/applylogdb on master"
	echo "#"
	echo "#  * details"
	echo "#   - restart copylogdb/applylogdb"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	ssh_cubrid $master_host "sh $function_home/ha_repl_resume.sh -i $repl_util_output"
	if [ $? -ne $SUCCESS ]; then   
		error "Fail to restart copylogdb/applylogdb."
	fi
}

function restart_target_repl_util()
{
	echo ""
	echo "##### step $step_no ###################################################################"	
	echo "#"
	echo "#  restart copylogdb/applylogdb on target"
	echo "#"
	echo "#  * details"
	echo "#   - restart copylogdb/applylogdb"
	echo "#"
	echo "################################################################################"
	get_yesno
	if [ $STDIN -eq $SKIP ]; then
		return $SUCCESS
	fi
	
	# 1. restart master repl util
	echo -ne "\n - 1. restart master repl util.\n\n"
	ssh_cubrid $target_host "sh $function_home/ha_repl_resume.sh -i $repl_util_output.m"
	if [ $? -ne $SUCCESS ]; then   
		error "Fail to restart copylogdb/applylogdb."
	fi
	sleep 1
	
	# 2. restart slave repl util
	echo -ne "\n - 2. restart slave repl util.\n\n"
	ssh_cubrid $target_host "sh $function_home/ha_repl_resume.sh -i $repl_util_output.s"
	if [ $? -ne $SUCCESS ]; then   
		error "Fail to restart copylogdb/applylogdb."
	fi	
	ssh_cubrid $target_host "cubrid heartbeat list"
}

function show_complete()
{
	echo ""
	echo "##### step $step_no ##################################################################"	
	echo "#"
	echo "#  completed"
	echo "#"
	echo "################################################################################"
}

#################################################################################
# main function
#################################################################################
clear
check_args
init_conf

echo -ne "\n\n###### START $now ######\n" >> time.output
for ((n= 0, size = ${#step_func[@]}; n < size; n++)) do
	step_no=$(($n + 1))
	
	start_time=$(date +%s)
	eval ${step_func[$n]}
	end_time=$(date +%s)
	elapsed_time=$[ $end_time - $start_time ]
	echo "[$step_no] ${step_func[$n]} : $elapsed_time" >> time.output
	
	echo -ne "\n\n"
done
echo -ne "###### END ######\n\n" >> time.output
