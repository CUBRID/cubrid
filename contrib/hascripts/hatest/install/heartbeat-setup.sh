#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1

INSTALL_TMP_DIR=~/.hatest.tmp

NOW=`date +%Y%m%d-%H%M%S`

# load environment
. hatest.env

###############################################################################
#
# NOTE : 
# 		how to install and activate heartbeat 
#		
#		[master] 
#				1) copy_heartbeat_config
# 				2) change_heartbeat_config
#				3) change_heartbeat_cib
# 				4) install_heartbeat_config
# 				5) startup_heartbeat
# 				5) apply_heartbeat_cib
#
#		[slave] 
#				1) copy_heartbeat_config
# 				2) change_heartbeat_config
# 				3) install_heartbeat_config
# 				4) startup_heartbeat
#
###############################################################################


# copy heartbeat configuration
copy_heartbeat_config()
{
	# make dir 
	ssh $1@$2 "mkdir -p $INSTALL_TMP_DIR" 
	# backup 
	ssh $1@$2 "[ -d $INSTALL_TMP_DIR/heartbeat ] && mv -f $INSTALL_TMP_DIR/heartbeat $INSTALL_TMP_DIR/heartbeat.bak.$NOW"
	ssh $1@$2 "[ -d $INSTALL_TMP_DIR/ocf ] && mv -f $INSTALL_TMP_DIR/ocf $INSTALL_TMP_DIR/ocf.bak.$NOW"
	
	# copy 
	scp -r ./files/heartbeat 	$1@$2:$INSTALL_TMP_DIR
	scp -r ./files/ocf 			$1@$2:$INSTALL_TMP_DIR
}


# change heartbeat config
change_heartbeat_config()
{
	echo "setup heartbeat config at $1@$2"

	cmd="ssh $1@$2 'sed -i \"s/HB_NIC/$HB_NIC/g\" $INSTALL_TMP_DIR/heartbeat/ha.cf'"; 			eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/A1_NODE/$MASTER_HOST/g\" $INSTALL_TMP_DIR/heartbeat/ha.cf'"; 	eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/S1_NODE/$SLAVE_HOST/g\" $INSTALL_TMP_DIR/heartbeat/ha.cf'"; 		eval $cmd
}

# change heartbeat cib 
change_heartbeat_cib()
{
	echo "setup heartbeat cib at $1@$2"

	cmd="ssh $1@$2 'sed -i \"s/CUBRID_USER/$USER/g\" $INSTALL_TMP_DIR/heartbeat/*.xml'"; 		eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/DB_1/$DB_1/g\" $INSTALL_TMP_DIR/heartbeat/*.xml'"; 				eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/DB_2/$DB_2/g\" $INSTALL_TMP_DIR/heartbeat/*.xml'"; 				eval $cmd 	
	cmd="ssh $1@$2 'sed -i \"s/DB_3/$DB_3/g\" $INSTALL_TMP_DIR/heartbeat/*.xml'"; 				eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/DB_4/$DB_4/g\" $INSTALL_TMP_DIR/heartbeat/*.xml'"; 				eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/CUBRID_HOME/$CUBRID_HOME/g\" $INSTALL_TMP_DIR/heartbeat/*.xml'"; 			eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/CUBRID_DATABASES/$CUBRID_DATABASES/g\" $INSTALL_TMP_DIR/heartbeat/*.xml'"; 	eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/A1_NODE/$MASTER_HOST/g\" $INSTALL_TMP_DIR/heartbeat/*.xml'"; 				eval $cmd
	cmd="ssh $1@$2 'sed -i \"s/S1_NODE/$SLAVE_HOST/g\" $INSTALL_TMP_DIR/heartbeat/*.xml'"; 					eval $cmd
}



# install_heartbeat_config
install_heartbeat_config()
{
	echo "install heartbeat config at $1@$2"

	ssh -t $1@$2 "sudo cp -f $INSTALL_TMP_DIR/heartbeat/ha.cf		/etc/ha.d/ha.cf"
	ssh -t $1@$2 "sudo cp -f $INSTALL_TMP_DIR/heartbeat/authkeys 	/etc/ha.d/authkeys"
	ssh -t $1@$2 "sudo cp -f $INSTALL_TMP_DIR/heartbeat/logd.cf 	/etc/logd.cf"

	ssh -t $1@$2 "sudo cp -f $INSTALL_TMP_DIR/ocf/* 				/usr/lib/ocf/resource.d/heartbeat/"
}

# startup heartbeat 
startup_heartbeat_on_master()
{
	echo "startup heartbeat at $1@$2"

	# backup old cib 
	ssh -t $1@$2 "sudo su - hacluster -c 'mkdir -p /var/lib/heartbeat/crm/bak.$NOW'" 
	ssh -t $1@$2 "sudo [ -f /var/lib/heartbeat/crm/cib.xml ] && sudo su - hacluster -c 'mv -f /var/lib/heartbeat/crm/cib.xml* /var/lib/heartbeat/crm/bak.$NOW/'" 

	ssh -t $1@$2 "sudo /etc/init.d/heartbeat start" 
	[ $? -ne 0 ] && echo "Failed to start heartbeat at $1@$2" 
	echo "wait for heartbeat warming up" 
	 
	sleep 15
	while : ; do
		nodenum=`ssh -t $1@$2 /usr/sbin/crmadmin -N | wc -l`
		echo "nodenum : $nodenum"
		[ $nodenum -ge 1 ] && break 
		sleep 5
	done
	sleep 30
}

startup_heartbeat_on_slave()
{
#echo "startup heartbeat at $1@$2"
#
#ssh -t $1@$2 "sudo /etc/init.d/heartbeat start" 
#[ $? -ne 0 ] && echo "Failed to start heartbeat at $1@$2" 
#echo "wait for heartbeat warming up" 
# 
#sleep 15
#while : ; do
#	nodenum=`ssh -t $1@$2 /usr/sbin/crmadmin -N | wc -l`
#	echo "nodenum : $nodenum"
#	[ $nodenum -ge 1 ] && break 
#	sleep 5
#done
#sleep 30
	startup_heartbeat_on_master $1 $2
}


# apply heartbeat cib 
apply_heartbeat_cib()
{
	echo "update heartbeat crm_config at $1@$2"
	ssh -t $1@$2 "sudo /usr/sbin/cibadmin -V -o crm_config -U -x $INSTALL_TMP_DIR/heartbeat/crm-config-comment_test-init.xml" 
	for i in 50; do 
		echo -n "--$i%"
		sleep 5
	done 
	echo "--100%! done" 
	echo ""

	echo "replace heartbeat constraints at $1@$2"
	ssh -t $1@$2 "sudo /usr/sbin/cibadmin -V -o constraints -R -x $INSTALL_TMP_DIR/heartbeat/constraints-comment_test-init.xml" 
	for i in 10 20 30 40 50 60 70 80 90 ; do 
		echo -n "--$i%"
		sleep 2
	done 
	echo "--100%! done" 
	echo ""

	echo "replace heartbeat resources at $1@$2"
	ssh -t $1@$2 "sudo /usr/sbin/cibadmin -V -o resources -R -x $INSTALL_TMP_DIR/heartbeat/resources-comment_test-init.xml"
	for i in 10 20 30 40 50 60 70 80 90 ; do 
		echo -n "--$i%"
		sleep 2
	done 
	echo "--100%! done" 
	echo ""

	echo "startup heartbeat resources at $1@$2"
	ssh -t $1@$2 "sudo /usr/sbin/cibadmin -V -o resources -U -x $INSTALL_TMP_DIR/heartbeat/resources-comment_test-start.xml"
	for i in 10 20 30 40 50 60 70 80 90 ; do 
		echo -n "--$i%"
		sleep 2
	done 
	echo "--100%! done" 
	echo ""
}

exec_heartbeat_on_master()
{
	copy_heartbeat_config 		$HA_USER $MASTER_HOST
	change_heartbeat_config		$HA_USER $MASTER_HOST	
	change_heartbeat_cib		$HA_USER $MASTER_HOST
	install_heartbeat_config	$HA_USER $MASTER_HOST
	startup_heartbeat_on_master	$HA_USER $MASTER_HOST
	apply_heartbeat_cib			$HA_USER $MASTER_HOST	
}

exec_heartbeat_on_slave()
{
	copy_heartbeat_config		$HA_USER $SLAVE_HOST
	change_heartbeat_config		$HA_USER $SLAVE_HOST
	install_heartbeat_config	$HA_USER $SLAVE_HOST
	startup_heartbeat_on_slave	$HA_USER $SLAVE_HOST
}

