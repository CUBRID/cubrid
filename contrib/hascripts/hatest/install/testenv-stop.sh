#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1

# load environment
. hatest.env

# stop heartbeat and cubrid server
stop_heartbeat()
{
	echo "stop heartbeat at $1@$2"

	ssh -t $1@$2 "sudo /etc/init.d/heartbeat stop" 
	[ $? -ne 0 ] && echo "Failed to stop heartbeat at $1@$2" 
	echo "wait for heartbeat stop" 
	sleep 5
}

# stop cubrid broker
stop_cubrid_broker()
{
	echo "stop cubrid broker at $1@$2"
		
	ssh $1@$2 "~/bin/run-cubrid-broker-ro.sh stop"
	ssh $1@$2 "~/bin/run-cubrid-broker-rw.sh stop"
	sleep 5
}


# 1) stop cubrid broker  
stop_cubrid_broker		$HA_USER $BROKER_HOST

# 2) stop heartbeat and cubrid server
stop_heartbeat			$HA_USER $MASTER_HOST
stop_heartbeat			$HA_USER $SLAVE_HOST

