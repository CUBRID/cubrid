#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1

# load environment
. hatest.env

# start heartbeat and cubrid server
start_heartbeat()
{
	echo "start heartbeat at $1@$2"

	ssh -t $1@$2 "sudo /etc/init.d/heartbeat start" 
	[ $? -ne 0 ] && echo "Failed to start heartbeat at $1@$2" 
	echo "wait for heartbeat start" 
	sleep 10
}

# start cubrid broker
start_cubrid_broker()
{
	echo "start cubrid broker at $1@$2"
		
	ssh -t $1@$2 "sudo nohup su - $HA_USER -c '~/bin/run-cubrid-broker-rw.sh start'"
	sleep 1
    ssh -t $1@$2 "sudo nohup su - $HA_USER -c '~/bin/run-cubrid-broker-ro.sh start'"
	sleep 1
}

# 1) start heartbeat and cubrid server
start_heartbeat			$HA_USER $MASTER_HOST
start_heartbeat			$HA_USER $SLAVE_HOST

# 2) start cubrid broker  
start_cubrid_broker		$HA_USER $BROKER_HOST


