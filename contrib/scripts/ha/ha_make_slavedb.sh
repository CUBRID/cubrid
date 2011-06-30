#!/bin/bash

################################################################################

# user specific options 
# mandatory
master_host=
db_name=
repl_log_home=

# conditional
dba_password=

# optional
backup_dest_path=
backup_option=
restore_option=
scp_option="-l 131072"	# default - limit : 16M*1024*8=131072


# automatic - DO NOT CHANGE !!!
slave_host=$(uname -n)
cubrid_user=$(whoami)
now=$(date +"%Y%m%d_%H%M%S")

# temp - DO NOT CHANGE !!!
prog_name="ha_make_slavedb.sh"
ret=0
ha_temp_home=$HOME/.ha
script_home=$ha_temp_home/scripts
repl_util_output=$ha_temp_home/repl_utils.output
backupdb_output=
db_vol_path=
db_log_path=
################################################################################


function print_usage()
{
	echo ""
	echo "Usage: $prog_name"
	echo ""
}


function ssh_cubrid()
{
	[ "$#" -ne 1 ] && return

	ssh $cubrid_user@$master_host "export PATH=$PATH; export LD_LIBRARY_PATH=$LD_LIBRARY_PATH; export CUBRID=$CUBRID; export CUBRID_DATABASES=$CUBRID_DATABASES; sh -lc \"$1\""
}


function scp_cubrid_to_master()
{
	[ "$#" -ne 2 ] && return

	scp $scp_option -r $1 $cubrid_user@$master_host:$2 
}

function scp_cubrid_to_slave()
{
	[ "$#" -ne 2 ] && return

	scp $scp_option -r $cubrid_user@$master_host:$1 $2
}

function is_invalid_option()
{
	[ -z "$master_host" ] && echo " << ERROR >> Invalid master_host($master_host)." && return 0
	[ -z "$db_name" ] && echo " << ERROR >> Invalid db_name($db_name)." && return 0
	[ -z "$repl_log_home" ] && echo " << ERROR >> Invalid repl_log_home($repl_log_home)." && return 0
	[ -z "$slave_host" ] && echo " << ERROR >> Invalid slave_host($slave_host)." && return 0
	[ -z "$cubrid_user" ] && echo " << ERROR >> Invalid cubrid_user($cubrid_user)." && return 0

	[ -z "$backup_dest_path" ] && backup_dest_path=$ha_temp_home/backup

	return 1
}


# step 1
function step_1()
{
	echo ""
	echo "##### step 1 ###################################################################"	
	echo "#                                                                               "
	echo "#  $prog_name is script for making slave database more easily                   "
	echo "#                                                                               "
	echo "#  * environment                                                                "
	echo "#   - master_host       : $master_host                                          "
	echo "#   - db_name           : $db_name                                              "
	echo "#   - repl_log_home     : $repl_log_home                                        "
	echo "#                                                                               "
	echo "#   - slave_host        : $slave_host                                           "
	echo "#   - cubrid_user       : $cubrid_user                                          "
	echo "#                                                                               "
	echo "#   - dba_password      : $dba_password                                         "
	echo "#   - backup_dest_path  : $backup_dest_path                                     "
	echo "#   - backup_option     : $backup_option                                        "
	echo "#   - restore_option    : $restore_option                                       "
	echo "#   - scp_option        : $scp_option                                           "
	echo "#                                                                               "
	echo "#  * warning !!!                                                                "
	echo "#   - environment on slave must be same as master                               "
	echo "#   - database and replication log on slave will be deleted                     "
	echo "#                                                                               "
	echo "################################################################################"

	echo ""
	echo -ne "\n                              - Please enter the master_host >> "
	read in_master_host
	if [ $master_host != "$in_master_host" ]; then
		echo -ne "\n << ERROR >> $in_master_host is different from $master_host. \n\n" 
		return 1
	fi

	echo -ne "\n                              - Please enter the db_name     >> "
	read in_db_name
	if [ $db_name != "$in_db_name" ]; then
		echo -ne "\n << ERROR >> $in_db_name is different from $db_name. \n\n" 
		return 1
	fi

	echo ""
	echo -ne "\n                                          continue ? (yes/no/skip): "
	read yesno
	if [ $yesno = "skip" ]; then
		echo -ne "\n                              ............................ skipped .......\n"
		return 0
	elif [ $yesno != "yes" ]; then 
		return 1
	fi


	echo -ne "\n\n"
	return 0
}

# step 2
function step_2()
{
	echo ""
	echo "##### step 2 ###################################################################"	
	echo "#                                                                               "
	echo "#  copy scripts to master node                                                  "
	echo "#                                                                               "
	echo "#  * details                                                                    "
	echo "#   - scp scripts to '~/.ha' on $master_host(master)                            "
	echo "#                                                                               "
	echo "################################################################################"
	echo -ne "\n                                          continue ? (yes/no/skip): "

	read yesno
	if [ $yesno = "skip" ]; then
		echo -ne "\n                              ............................ skipped .......\n"
		return 0
	elif [ $yesno != "yes" ]; then 
		return 1
	fi

	# make temporary directory 
	echo -ne "\n\n"
	echo -ne "1. scp scripts to '~/.ha' on $master_host.\n\n"

	ssh_cubrid "ls $ha_temp_home" >/dev/null 2>&1
	ret=$?
	if [ $ret -eq 0 ]; then   

		echo "$master_host ]$ rm -rf $ha_temp_home"
		ssh_cubrid "rm -rf $ha_temp_home"
	fi 

	echo "$master_host ]$ mkdir -p $ha_temp_home"
	ssh_cubrid "mkdir -p $ha_temp_home" 

	echo "$master_host ]$ mkdir -p $script_home"
	ssh_cubrid "mkdir -p $script_home" 

	echo "$master_host ]$ mkdir -p $script_home/functions"
	ssh_cubrid "mkdir -p $script_home/functions" 

	echo "$master_host ]$ mkdir -p $backup_dest_path"
	ssh_cubrid "mkdir -p $backup_dest_path" 

	# copy scripts to master node
	echo -ne "\n"
	echo "$slave_host ]$ scp *.sh $cubrid_user@$master_host:$script_home"
	scp_cubrid_to_master "*.sh" "$script_home"
	echo "$slave_host ]$ scp functions/*.sh $cubrid_user@$master_host:$script_home/functions"
	scp_cubrid_to_master "functions/*.sh" "$script_home/functions"

	echo -ne "\n\n"
	return 0
}


# step 3
function step_3()
{
	echo ""
	echo "##### step 3 ###################################################################"	
	echo "#                                                                               "
	echo "#  suspend copylogdb/applylogdb on master if running                            "
	echo "#                                                                               "
	echo "#  * details                                                                    "
	echo "#   - deregister copylogdb/applylogdb on $master_host(master)                   "
	echo "#                                                                               "
	echo "################################################################################"
	echo -ne "\n                                          continue ? (yes/no/skip): "

	read yesno
	if [ $yesno = "skip" ]; then
		echo -ne "\n                              ............................ skipped .......\n"
		return 0
	elif [ $yesno != "yes" ]; then 
		return 1
	fi

	# suspend
	ssh_cubrid "$script_home/functions/ha_repl_suspend.sh -l $repl_log_home -d $db_name -h $slave_host -o $repl_util_output"	
	ret=$?
	[ $ret -ne 0 ] && return 1

	echo -ne "\n\n"

	sleep 60

	return 0
}


# step 4
function step_4()
{
	echo ""
	echo "##### step 4 ###################################################################"	
	echo "#                                                                               "
	echo "#  online backup database $db_bame on master                                    "
	echo "#                                                                               "
	echo "#  * details                                                                    "
	echo "#   - run 'cubrid backupdb -C -D ... -o ... $db_name@localhost' on master       "
	echo "#                                                                               "
	echo "################################################################################"
	echo -ne "\n                                          continue ? (yes/no/skip): "

	read yesno
	if [ $yesno = "skip" ]; then
		echo -ne "\n                              ............................ skipped .......\n"
		return 0
	elif [ $yesno != "yes" ]; then 
		return 1
	fi

	# backupdb
	echo -ne "\n\n"
	echo -ne "1. online backup database $db_name on $master_host.\n\n"
	backupdb_output=$backup_dest_path/$db_name.bkup.output

	echo "$master_host ]$ cubrid backupdb -C -D $backup_dest_path -o $backupdb_output $backup_option $db_name@localhost"
	ssh_cubrid "cubrid backupdb -C -D $backup_dest_path -o $backupdb_output $backup_option $db_name@localhost"
	ret=$?
	[ $ret -ne 0 ] && return 1

	echo -ne "\n\n"
	echo -ne "2. check backup output file $backupdb_output.\n\n"
	echo -ne "$master_host ]$ cat $backupdb_output\n"
	ssh_cubrid "cat $backupdb_output"

	echo -ne "\n\n\n"
	return 0
}

function step_5()
{
	echo ""
	echo "##### step 5 ###################################################################"	
	echo "#                                                                               "
	echo "#  copy $db_name databases backup to slave                                      "
	echo "#                                                                               "
	echo "#  * details                                                                    "
	echo "#   - scp databases.txt from master if there's no $db_name info on slave        "
	echo "#   - remove old database and replication log if exist                          "
	echo "#   - make new database volume and replication path                             "
	echo "#   - scp $db_nama database backup to slave                                     "
	echo "#                                                                               "
	echo "################################################################################"
	echo -ne "\n                                          continue ? (yes/no/skip): "

	read yesno
	if [ $yesno = "skip" ]; then
		echo -ne "\n                              ............................ skipped .......\n"
		return 0
	elif [ $yesno != "yes" ]; then 
		return 1
	fi

	# prepare on slave 
	echo -ne "\n\n"
	echo -ne "1. scp databases.txt from $master_host if there's no $db_name info on slave.\n\n"
	line=$(grep "^$db_name" $CUBRID_DATABASES/databases.txt) 
	if [ -z "$line" ]; then
		echo "$slave_host ]$ mv -f $CUBRID_DATABASES/databases.txt $CUBRID_DATABASES/databases.txt.$now"
		mv -f $CUBRID_DATABASES/databases.txt $CUBRID_DATABASES/databases.txt.$now

		echo "$slave_host ]$ scp $cubrid_user@$master_host:$CUBRID_DATABASES/databases.txt $CUBRID_DATABASES/."
		scp_cubrid_to_slave "$CUBRID_DATABASES/databases.txt" "$CUBRID_DATABASES/."
	else
		echo " - thres's already $db_name information in $CUBRID_DATABASES/databases.txt" 
		echo "$slave_host ]$ grep $db_name $CUBRID_DATABASES/databases.txt" 
		echo "$line"
	fi
	
	line=$(grep "^$db_name" $CUBRID_DATABASES/databases.txt) 
	db_vol_path=$(echo $line | cut -d ' ' -f 2)
	db_log_path=$(echo $line | cut -d ' ' -f 4)
	[ -z "$db_vol_path" -o -z "$db_log_path" ] && return 1

	# TODO : remove? is it ok...?
	echo -ne "\n\n"
	echo -ne "2. remove old database and replication log.\n\n"

	echo "$slave_host ]$ rm -rf $db_log_path" 
	rm -rf $db_log_path
	echo "$slave_host ]$ rm -rf $db_vol_path"
	rm -rf $db_vol_path
	echo "$slave_host ]$ rm -rf $repl_log_home/${db_name}_*"
	rm -rf $repl_log_home/${db_name}_*


	echo -ne "\n\n"
	echo -ne "3. make new database volume and replication log directory.\n\n"

	echo "$slave_host ]$ mkdir -p $db_vol_path"
	mkdir -p $db_vol_path
	echo "$slave_host ]$ mkdir -p $db_log_path"
	mkdir -p $db_log_path

	echo "$slave_host ]$ mkdir -p $ha_temp_home"
	mkdir -p $ha_temp_home
	echo "$slave_host ]$ rm -rf $backup_dest_path"
	rm -rf $backup_dest_path
	echo "$slave_host ]$ mkdir -p $backup_dest_path"
	mkdir -p $backup_dest_path


	echo -ne "\n\n"
	echo -ne "4. scp $db_name database backup to slave.\n\n"
	# copy backup volume and log
	echo "$slave_host ]$ scp $cubrid_user@$master_host:$db_log_path/${db_name}_bkvinf $db_log_path"
	scp_cubrid_to_slave "$db_log_path/${db_name}_bkvinf" "$db_log_path"
	ret=$?
	[ $ret -ne 0 ] && return 1

	echo "$slave_host ]$ scp $cubrid_user@$master_host:$backup_dest_path/* $backup_dest_path/."
	scp_cubrid_to_slave "$backup_dest_path/*" "$backup_dest_path/."
	ret=$?
	[ $ret -ne 0 ] && return 1

	echo -ne "\n\n"
	return 0
}

function step_6()
{
	echo ""
	echo "##### step 6 ###################################################################"	
	echo "#                                                                               "
	echo "#  restore database $db_name on slave                                           "
	echo "#                                                                               "
	echo "#  * details                                                                    "
	echo "#   - cubrid restoredb -B ... $db_name on slave                                 "
	echo "#                                                                               "
	echo "################################################################################"
	echo -ne "\n                                          continue ? (yes/no/skip): "

	read yesno
	if [ $yesno = "skip" ]; then
		echo -ne "\n                              ............................ skipped .......\n"
		return 0
	elif [ $yesno != "yes" ]; then 
		return 1
	fi

	# restore database
	echo -ne "\n\n"
	echo -ne "1. restore database $db_name on slave.\n\n"
	echo "$slave_host ]$ cubrid restoredb -B $backup_dest_path $restore_option $db_name"
	cubrid restoredb -B $backup_dest_path $restore_option $db_name

	echo -ne "\n\n"
	return 0
}

function step_7()
{
	echo ""
	echo "##### step 7 ###################################################################"	
	echo "#                                                                               "
	echo "#  set db_ha_apply_info on slave                                                " 
	echo "#                                                                               "
	echo "#  * details                                                                    "
	echo "#   - insert db_ha_apply_info on slave                                          "
	echo "#                                                                               "
	echo "################################################################################"
	echo -ne "\n                                          continue ? (yes/no/skip): "

	read yesno
	if [ $yesno = "skip" ]; then
		echo -ne "\n                              ............................ skipped .......\n"
		return 0
	elif [ $yesno != "yes" ]; then 
		return 1
	fi

	# set db_ha_apply_info on slave 
	repl_log_path=$repl_log_home/${db_name}_${master_host}
	./functions/ha_set_apply_info.sh -r $repl_log_path -o $backupdb_output -p "$dba_password"

	echo -ne "\n\n"
	return 0
}


function step_8()
{
	echo ""
	echo "##### step 8 ###################################################################"	
	echo "#                                                                               "
	echo "#  make initial replication active log on slave, and copy archive logs from     "
	echo "#  master                                                                       " 
	echo "#                                                                               "
	echo "#  * details                                                                    "
	echo "#   - remove old replication log on slave if exist                              "
	echo "#   - start copylogdb to make replication active log                            "
	echo "#   - copy archive logs from master                                             "
	echo "#                                                                               "
	echo "################################################################################"
	echo -ne "\n                                          continue ? (yes/no/skip): "

	read yesno
	if [ $yesno = "skip" ]; then
		echo -ne "\n                              ............................ skipped .......\n"
		return 0
	elif [ $yesno != "yes" ]; then 
		return 1
	fi

	# prepare
	repl_log_path=$repl_log_home/${db_name}_${master_host}

	echo -ne "\n\n"
	echo -ne "1. remove old replicaton log on slave.\n\n"
	echo "$slave_host ]$ rm -rf $repl_log_path"
	rm -rf $repl_log_path
	echo "$slave_host ]$ mkdir -p $repl_log_path"
	mkdir -p $repl_log_path

	# copy replication log from master
	echo -ne "\n\n"
	echo -ne "2. start copylogdb to initiate active log on slave.\n\n"
	./functions/ha_repl_copylog.sh -r $repl_log_path -d $db_name -h $master_host


	echo -ne "\n\n"
	echo -ne "3. copy archive log from master.\n\n"
	echo "$slave_host ]$ scp $cubrid_user@$master_host:$db_log_path/${db_name}_lgar[0-9]* $repl_log_path/."
	scp_cubrid_to_slave "$db_log_path/${db_name}_lgar[0-9]*" "$repl_log_path/."	

	echo -ne "\n\n"
	return 0
}

function step_9()
{
	echo ""
	echo "##### step 9 ###################################################################"	
	echo "#                                                                               "
	echo "#  reset replication log and db_ha_apply_info, then restart copylogdb/applylogdb" 
	echo "#  on master                                                                    "
	echo "#                                                                               "
	echo "#  * details                                                                    "
	echo "#   - remove old replication log                                                "
	echo "#   - reset db_ha_apply_info                                                    "
	echo "#   - restart copylogdb/applylogdb                                              "
	echo "#                                                                               "
	echo "################################################################################"
	echo -ne "\n                                          continue ? (yes/no/skip): "

	read yesno
	if [ $yesno = "skip" ]; then
		echo -ne "\n                              ............................ skipped .......\n"
		return 0
	elif [ $yesno != "yes" ]; then 
		return 1
	fi

	# reset  
	if [ -n "$dba_password" ]; then
		ssh_cubrid "$script_home/functions/ha_repl_reset.sh -l $repl_log_home -d $db_name -h $slave_host -p $dba_password"	
	else
		ssh_cubrid "$script_home/functions/ha_repl_reset.sh -l $repl_log_home -d $db_name -h $slave_host"	
	fi
	ret=$?
	[ $ret -ne 0 ] && return 1

	# resume 
	echo -ne "\n\n3. restart copylogdb/applylogdb on master.\n\n"

	echo " - touch replication utils process arguments file"
	echo "$master_host ]$ touch $repl_util_output"
	ssh_cubrid "touch $repl_util_output"

	echo ""
	echo " - restart copylogdb/applylogdb on $master_host(master)"
	ssh_cubrid "$script_home/functions/ha_repl_resume.sh -i $repl_util_output"	
	ret=$?
	[ $ret -ne 0 ] && return 1
	
	echo -ne "\n\n\n"
	return 0
}


function step_10()
{
	echo ""
	echo "##### step 10 ##################################################################"	
	echo "#                                                                               "
	echo "#  completed                                                                    "
	echo "#                                                                               "
	echo "################################################################################"
	echo ""

	return 0
}




### main ##############################

clear

if is_invalid_option; then
	exit 1
fi

if ! step_1; then
	exit 1
fi

if ! step_2; then
	exit 1
fi

if ! step_3; then 
	exit 1
fi 

if ! step_4; then
	exit 1
fi

if ! step_5; then
	exit 1
fi

if ! step_6; then
	exit 1
fi

if ! step_7; then
	exit 1
fi

if ! step_8; then
	exit 1
fi

if ! step_9; then
	exit 1
fi

if ! step_10; then
	exit 1
fi

exit 0
