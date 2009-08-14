#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1

# load environment
. hatest.env

# remove cubrid broker log
rem_broker_log()
{
	#ssh $1@$2 "mkdir -p $CUBRID_HOME.ro/log/broker/old" 
	#ssh $1@$2 "mv -f $CUBRID_HOME.ro/log/broker/*_log $CUBRID_HOME.ro/log/broker/old" 

	#ssh $1@$2 "mkdir -p $CUBRID_HOME.rw/log/broker/old" 
	#ssh $1@$2 "mv -f $CUBRID_HOME.rw/log/broker/*_log $CUBRID_HOME.rw/log/broker/old" 
	ssh $1@$2 "rm -rf $CUBRID_HOME/log/broker/old;mkdir -p $CUBRID_HOME/log/broker/old" 
	ssh $1@$2 "mv -f $CUBRID_HOME/log/broker/*_log $CUBRID_HOME/log/broker/old" 
}

# remove cubrid server log
rem_server_log()
{
	ssh $1@$2 "mkdir -p $CUBRID_HOME/log/server/old" 
	ssh $1@$2 "mv -f $CUBRID_HOME/log/server/*.err $CUBRID_HOME/log/server/old"
}

# 1) remove cubrid broker log
rem_broker_log			$HA_USER $BROKER_HOST

# 2) remove cubrid server log 
rem_server_log			$HA_USER $MASTER_HOST
rem_server_log			$HA_USER $SLAVE_HOST


