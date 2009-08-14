#!/bin/sh

THISDIR=`dirname $0`
. $THISDIR/hatest.env

USER_NAME="$HA_USER"
SERVER_HOSTS="$MASTER_HOST $SLAVE_HOST"
BROKER_HOSTS="$BROKER_PRI_HOST $BROKER_SEC_HOST" 
NBENCH_HOSTS='localhost'

COLLECTED_LOG_DIR='logs'

# cubrid server logs
CUBRID_SERVER_LOGS='$CUBRID/log/server/*.err*'

# cubrid broker logs
CUBRID_BROKER_LOGS='$CUBRID/log/broker/*.* $CUBRID/log/broker/error_log/*.err* $CUBRID/log/broker/sql_log/*.log*'

# cubrid error logs
CUBRID_ERROR_LOGS='$CUBRID/log/*.err* $CUBRID/log/*.log*'

# system logs
SYSTEM_LOGS='/var/log/messages'

# heartbeat logs
HEARTBEAT_LOGS='/var/lib/heartbeat/cores/root/*'

# core files
CORE_FILES='/var/lib/heartbeat/cores/root/core* $CUBRID/bin/core*'

# nbench logs
NBENCH_LOGS='/home1/nbd/ha/nbench/benchmark/run/*.log* /home1/nbd/ha/nbench/benchmark/run/log/*.log*'

#echo $CUBRID_SERVER_LOGS
#echo $CUBRID_BROKER_LOGS
#echo $CUBRID_ERROR_LOGS
#echo $SYSTEM_LOGS
#echo $HEARTBEAT_LOGS
#echo $CORE_FILES
#echo $NBENCH_LOGS

SERVER_NODE_LOGS="$CUBRID_SERVER_LOGS $CUBRID_ERROR_LOGS $HEARTBEAT_LOGS $CORE_FILES $SYSTEM_LOGS"
BROKER_NODE_LOGS="$CUBRID_BROKER_LOGS $CUBRID_ERROR_LOGS $CORE_FILES $SYSTEM_LOGS"
NBENCH_NODE_LOGS="$NBENCH_LOGS"

COLLECT_TIME=$(date +%Y%m%d_%H%M%S)

collect_logs ()
{
	LOG_DIR=$1
	for node in $SERVER_HOSTS
	do
		echo "Collect log from $node"
	#	ssh -t $USER_NAME@$node ". .bash_profile; sudo mkdir /tmp/${node}_logs_$COLLECT_TIME && cp --parent $SERVER_NODE_LOGS /tmp/${node}_logs_$COLLECT_TIME && tar cz -C /tmp -f /tmp/${node}_logs_$COLLECT_TIME.tar.gz ${node}_logs_$COLLECT_TIME && rm -rf /tmp/${node}_logs_$COLLECT_TIME"
		ssh -t $USER_NAME@$node ". .bash_profile; sudo tar c -zf /tmp/${node}_logs_$COLLECT_TIME.tar.gz $SERVER_NODE_LOGS 2> /dev/null"
		scp $USER_NAME@$node:/tmp/${node}_logs_$COLLECT_TIME.tar.gz $LOG_DIR
	done

	for node in $BROKER_HOSTS
	do
		echo "Collect log from $node"
		ssh -t $USER_NAME@$node ". .bash_profile; sudo tar c -zf /tmp/${node}_logs_$COLLECT_TIME.tar.gz $BROKER_NODE_LOGS 2> /dev/null"
		scp $USER_NAME@$node:/tmp/${node}_logs_$COLLECT_TIME.tar.gz $LOG_DIR/
	done

	for node in $NBENCH_HOSTS
	do
		echo "Collect log from $node"
		if [ "$node" = "localhost" ]
		then
			tar c -zf $LOG_DIR/${HOSTNAME}_logs_$COLLECT_TIME.tar.gz $NBENCH_NODE_LOGS 2> /dev/null
		else
			ssh -t $USER_NAME@$node "tar c -zf /tmp/${node}_logs_$COLLECT_TIME.tar.gz $NBENCH_NODE_LOGS 2> /dev/null"
			scp $USER_NAME@$node:/tmp/${node}_logs_$COLLECT_TIME.tar.gz $LOG_DIR/
		fi
	done
}

remove_logs ()
{
#	echo "*** CAUTION: All CUBRID processes are must be NOT running ***"
#	echo -n "Are you sure to remove cubrid log and system logs? [y|N] "
#	read yn
#	case $yn in
#		"y"|"Y")
#			;;
#		*)
#			echo "Abort."
#			exit 0
#	esac

	for node in $SERVER_HOSTS
	do
		echo "Remove log from $node"
		ssh -t $USER_NAME@$node ". .bash_profile; sudo rm -f $CUBRID_SERVER_LOGS $CUBRID_ERROR_LOGS $HEARTBEAT_LOGS $CORE_FILES; sudo /usr/sbin/logrotate -f /etc/logrotate.conf; sudo rm -f /tmp/${node}_logs_*.tar.gz"
	done

	for node in $BROKER_HOSTS
	do
		echo "Remove log from $node"
		ssh -t $USER_NAME@$node ". .bash_profile; sudo rm -f $CUBRID_BROKER_LOGS $CUBRID_ERROR_LOGS $CORE_FILES; sudo /usr/sbin/logrotate -f /etc/logrotate.conf; sudo rm -f /tmp/${node}_logs_*.tar.gz"
	done

	for node in $NBENCH_HOSTS
	do
		echo "Remove log from $node"
		if [ "$node" = "localhost" ]
		then
		rm -f $NBENCH_NODE_LOGS
		else
		ssh -t $USER_NAME@$node "rm -f $NBENCH_NODE_LOGS"
		fi
	done
}

usage ()
{
	echo "Usage: $0 <cp|rm> [log path]"
	echo "   cp : copy logs from nodes"
	echo "   mp : remove logs at nodes"
	echo "   log path : collected logs path"
}

if [ $# -lt 1 ]
then
	usage
	exit 1
fi

if [ $# -lt 2 ]
then
	COLLECTED_LOG_DIR="$COLLECTED_LOG_DIR/$COLLECT_TIME"
else
	COLLECTED_LOG_DIR=$2
fi

case $1 in
	"cp")
		mkdir -p $COLLECTED_LOG_DIR
		collect_logs $COLLECTED_LOG_DIR
		;;
	"rm")
		remove_logs $COLLECTED_LOG_DIR
		;;
	*)
		usage
		exit 1
		;;
esac

echo "Done."
