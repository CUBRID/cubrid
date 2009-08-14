#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1

INSTALL_TMP_DIR=~/.hatest.tmp

NOW=`date +%Y%m%d-%H%M%S`

# load environment
. hatest.env

# copy cubrid bin & config
copy_cubrid()
{
	echo "copy cubrid env at $1@$2"

	# make dir 
	ssh $1@$2 "mkdir -p $INSTALL_TMP_DIR"
	ssh $1@$2 "mkdir -p ~/bin"
	# backup
	ssh $1@$2 "[ -d $INSTALL_TMP_DIR/cubrid ] && mv -f $INSTALL_TMP_DIR/cubrid $INSTALL_TMP_DIR/cubrid.bak.$NOW"
	ssh $1@$2 "[ -d $INSTALL_TMP_DIR/cubrid.misc ] && mv -f $INSTALL_TMP_DIR/cubrid.misc $INSTALL_TMP_DIR/cubrid.misc.bak.$NOW"

	# copy 
	scp -r ./files/cubrid 			$1@$2:$INSTALL_TMP_DIR
	scp -r ./files/cubrid.misc 		$1@$2:$INSTALL_TMP_DIR	
}

# change cubrid env
change_cubrid_env()
{
	echo "change cubrid env at $1@$2"
	
	# change cubrid env 
	cmd="ssh $1@$2 'sed -i \"s/CUB_HOME/$CUBRID_HOME/g\" $INSTALL_TMP_DIR/cubrid.misc/cubridenv'"; 				eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/CUB_DATABASES/$CUBRID_DATABASES/g\" $INSTALL_TMP_DIR/cubrid.misc/cubridenv'"; 	eval $cmd
}

# setup cubrid server
setup_cubrid_server()
{
	echo "setup cubrid server at $1@$2"
	
	# scp	
	copy_cubrid $1 $2

	# change
	change_cubrid_env $1 $2

	# backup
	cmd="ssh $1@$2 \"[ -d $CUBRID_HOME -o -f $CUBRID_HOME ] && mv -f $CUBRID_HOME $CUBRID_HOME.bak.$NOW\"" 		
	eval $cmd 
	cmd="ssh $1@$2 \"[ -d $CUBRID_DATABASES -o -f $CUBRID_DATABASES ] && mv -f $CUBRID_DATABASES $CUBRID_DATABASES.bak.$NOW\""	
	eval $cmd

	# make dir 
	cmd="ssh $1@$2 \"cp -R $INSTALL_TMP_DIR/cubrid $CUBRID_HOME\""		
	eval $cmd
	cmd="ssh $1@$2 \"mkdir -p $CUBRID_DATABASES\""
	eval $cmd
	
	# copy  config
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/cubrid.conf.ha $CUBRID_HOME/conf/cubrid.conf"
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/cubrid_broker.conf.ha $CUBRID_HOME/conf/cubrid_broker.conf"
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/cubridenv ~/bin/cubridenv" 

	# set env 	
	ssh $1@$2 "grep cubridenv ~/.bash_profile" || ssh $1@$2 'echo "[ -f ~/bin/cubridenv ] && . ~/bin/cubridenv" >> ~/.bash_profile'		

	# create db
	ssh $1@$2 "mkdir -p $CUBRID_DATABASES/$DB_1"
	ssh $1@$2 "mkdir -p $CUBRID_DATABASES/$DB_2"
	ssh $1@$2 "mkdir -p $CUBRID_DATABASES/$DB_3"
	ssh $1@$2 "mkdir -p $CUBRID_DATABASES/$DB_4"
	
	ssh $1@$2 ". .bash_profile && cubrid createdb -F $CUBRID_DATABASES/$DB_1 $DB_1" 
	ssh $1@$2 ". .bash_profile && cubrid createdb -F $CUBRID_DATABASES/$DB_2 $DB_2" 
	ssh $1@$2 ". .bash_profile && cubrid createdb -F $CUBRID_DATABASES/$DB_3 $DB_3" 
	ssh $1@$2 ". .bash_profile && cubrid createdb -F $CUBRID_DATABASES/$DB_4 $DB_4" 
	
	ssh $1@$2 ". .bash_profile && csql -S -u dba -c \"create user $DB_USER password '$DB_PASSWD'\" $DB_1"
	ssh $1@$2 ". .bash_profile && csql -S -u dba -c \"create user $DB_USER password '$DB_PASSWD'\" $DB_2"
	ssh $1@$2 ". .bash_profile && csql -S -u dba -c \"create user $DB_USER password '$DB_PASSWD'\" $DB_3"
	ssh $1@$2 ". .bash_profile && csql -S -u dba -c \"create user $DB_USER password '$DB_PASSWD'\" $DB_4"
	
	cmd="ssh $1@$2 'sed -i \"s/localhost/$MASTER_HOST:$SLAVE_HOST/g\" $CUBRID_DATABASES/databases.txt'";		eval $cmd
}

# setup cubrid broker ext
setup_cubrid_broker_ext()
{
	echo "setup cubrid broker ext at $1@$2 $3"

	# scp	
	copy_cubrid $1 $2

	# backup
	cmd="ssh $1@$2 \"[ -d $CUBRID_HOME -o -f $CUBRID_HOME ] && mv -f $CUBRID_HOME $CUBRID_HOME.bak.$NOW\"" 		
	eval $cmd 
	cmd="ssh $1@$2 \"[ -d $CUBRID_DATABASES.$3 -o -f $CUBRID_DATABASES.$3 ] && mv -f $CUBRID_DATABASES.$3 $CUBRID_DATABASES.$3.bak.$NOW\""	
	eval $cmd

	# make dir 
	cmd="ssh $1@$2 \"cp -R $INSTALL_TMP_DIR/cubrid $CUBRID_HOME\""		
	eval $cmd
	cmd="ssh $1@$2 \"mkdir -p $CUBRID_DATABASES.$3\""
	eval $cmd

	# copy  config
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/cubrid.conf.ha $CUBRID_HOME/conf/cubrid.conf"
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/cubrid_broker.conf.ha.$3 $CUBRID_HOME/conf/cubrid_broker.conf"
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/cubrid_broker.conf.ha.$3 $CUBRID_DATABASES.$3/cubrid_broker.conf"
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/run-cubrid-broker.sh ~/bin/run-cubrid-broker-$3.sh" 

	# change cubrid env 
	cmd="ssh $1@$2 'sed -i \"s/CUB_HOME/$CUBRID_HOME/g\" 				~/bin/run-cubrid-broker-$3.sh'"
	eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/CUB_USER/$HA_USER/g\" 					~/bin/run-cubrid-broker-$3.sh'"
	eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/CUB_DATABASES/$CUBRID_DATABASES.$3/g\"	~/bin/run-cubrid-broker-$3.sh'"
	eval $cmd
	cmd="ssh $1@$2 'chmod 755 ~/bin/run-cubrid-broker-$3.sh'"
	eval $cmd

	RETVAL=0
	rm -f ./files/tmp/databases.txt 
	scp $HA_USER@$MASTER_HOST:$CUBRID_DATABASES/databases.txt ./files/tmp/databases.txt
	scp ./files/tmp/databases.txt $1@$2:$CUBRID_DATABASES.$3/databases.txt
	RETVAL=$?
	
	if [ $RETVAL -ne 0 ] ; then
		echo "[error] cannot copy databases.txt from master"  
		exit 2
	fi
	
	if [ "$3" = "ro" ] ; then
		cmd="ssh $1@$2 'sed -i \"s/$MASTER_HOST:$SLAVE_HOST/$SLAVE_HOST:$MASTER_HOST/g\" $CUBRID_DATABASES.$3/databases.txt'"
		eval $cmd
	fi 
}

# setup cubrid broker
setup_cubrid_broker()
{
	echo "setup cubrid broker at $1@$2"

	setup_cubrid_broker_ext $1 $2 "rw"
	setup_cubrid_broker_ext $1 $2 "ro" 
}

# setup cubrid apps
setup_cubrid_apps()
{
	echo "setup cubrid apps at $1@$2"
	
	# scp	
	copy_cubrid $1 $2

	# change
	change_cubrid_env $1 $2

	# backup
	cmd="ssh $1@$2 \"[ -d $CUBRID_HOME -o -f $CUBRID_HOME ] && mv -f $CUBRID_HOME $CUBRID_HOME.bak.$NOW\"" 		
	eval $cmd 
	cmd="ssh $1@$2 \"[ -d $CUBRID_DATABASES -o -f $CUBRID_DATABASES ] && mv -f $CUBRID_DATABASES $CUBRID_DATABASES.bak.$NOW\""	
	eval $cmd

	# make dir 
	cmd="ssh $1@$2 \"cp -R $INSTALL_TMP_DIR/cubrid $CUBRID_HOME\""		
	eval $cmd
	cmd="ssh $1@$2 \"mkdir -p $CUBRID_DATABASES\""
	eval $cmd

	# copy  config
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/cubrid.conf.ha $CUBRID_HOME/conf/cubrid.conf"
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/cubrid_broker.conf.ha $CUBRID_HOME/conf/cubrid_broker.conf"
	ssh $1@$2 "cp $INSTALL_TMP_DIR/cubrid.misc/cubridenv ~/bin/cubridenv" 

	# set env 	
	ssh $1@$2 "grep cubridenv ~/.bash_profile" || ssh $1@$2 'echo "[ -f ~/bin/cubridenv ] && . ~/bin/cubridenv" >> ~/.bash_profile'		

	RETVAL=0
	rm -f ./files/tmp/databases.txt 
	scp $HA_USER@$MASTER_HOST:$CUBRID_DATABASES/databases.txt ./files/tmp/databases.txt
	scp ./files/tmp/databases.txt $1@$2:$CUBRID_DATABASES/databases.txt
	RETVAL=$?
	
	if [ $RETVAL -ne 0 ]; then
		echo "[error] cannot copy databases.txt from master"  
		exit 2
	fi
}


# startup cubrid broker
startup_cubrid_broker()
{
	ssh -t $1@$2 "sudo nohup su - $HA_USER -c '~/bin/run-cubrid-broker-rw.sh start'"
	ssh -t $1@$2 "sudo nohup su - $HA_USER -c '~/bin/run-cubrid-broker-ro.sh start'"
}

