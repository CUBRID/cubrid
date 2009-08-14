#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1

THISDIR=`dirname $0`
# load environment
. $THISDIR/hatest.env

# start heartbeat and cubrid server
start_heartbeat()
{
	echo "start heartbeat at $1@$2"

	ssh -t $1@$2 ". .bash_profile; cubrid_rel" 
	ssh -t $1@$2 "sudo /etc/init.d/heartbeat start" 
	[ $? -ne 0 ] && echo "Failed to start heartbeat at $1@$2" 
	echo "wait for heartbeat start" 
	sleep 10
}

# start cubrid broker
start_cubrid_broker()
{
	echo "start cubrid broker at $1@$2"

	ssh -t $1@$2 ". .bash_profile; cubrid_rel" 
	ssh -t $1@$2 "sudo nohup su - $HA_USER -c '$CUBRID_HOME/bin/cubrid broker start'"
}

# 1) start heartbeat and cubrid server
start_heartbeat			$HA_USER $MASTER_HOST
start_heartbeat			$HA_USER $SLAVE_HOST

# 2) start cubrid broker pri  
start_cubrid_broker		$HA_USER $BROKER_PRI_HOST

# 2-2) start cubrid broker sec
start_cubrid_broker		$HA_USER $BROKER_SEC_HOST



