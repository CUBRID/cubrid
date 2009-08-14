#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1

THISDIR=`dirname $0`
# load environment
. $THISDIR/hatest.env

# stop heartbeat and cubrid server
stop_heartbeat()
{
	echo "stop heartbeat at $1@$2"

	ssh -t $1@$2 "sudo /etc/init.d/heartbeat stop" 
	[ $? -ne 0 ] && echo "Failed to stop heartbeat at $1@$2" 
	echo "wait for heartbeat stop" 
	sleep 5
	ssh -t $1@$2 "sudo nohup su - $HA_USER -c 'cubrid service stop'"
}

# stop cubrid broker
stop_cubrid_broker()
{
	echo "stop cubrid broker at $1@$2"
		
	#ssh $1@$2 "~/bin/run-cubrid-broker.sh stop"
	#ssh -t $1@$2 "sudo nohup su - $HA_USER -c 'cubrid broker stop'"
	ssh -t $1@$2 "sudo nohup su - $HA_USER -c '$CUBRID_HOME/bin/cubrid broker stop'"
	sleep 5
}


# 1) stop cubrid broker pri  
stop_cubrid_broker		$HA_USER $BROKER_PRI_HOST

# 1-2) stop cubrid broker sec
stop_cubrid_broker		$HA_USER $BROKER_SEC_HOST

# 2) stop heartbeat and cubrid server
stop_heartbeat			$HA_USER $SLAVE_HOST
stop_heartbeat			$HA_USER $MASTER_HOST

