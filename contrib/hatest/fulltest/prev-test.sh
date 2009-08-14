#!/bin/sh

THISDIR=`dirname $0`

. $THISDIR/hatest.env

CTIME=`date +%Y%m%d-%H%M%S`
CUR_DIR="$THISDIR/logs/current"
LAST_DIR="$THISDIR/logs/$CTIME"

# backup old 
[ -d $CUR_DIR -o -f $CUR_DIR ] && mv -f $CUR_DIR $LAST_DIR 

# make new 
mkdir -p $CUR_DIR


###############################################################################
# T1
# 	

# 1) stop all test environnment
#echo "*** stop all test environnment ***"
#$THISDIR/testenv-stop.sh
#sleep 5 

# 2) start all test environment
echo "*** start all test environment ***"
$THISDIR/testenv-start.sh
sleep 10 

# 3) start resource monitor
echo "*** start resource monitor ***"
$THISDIR/cpumem-allnode.sh stop
$THISDIR/cpumem-allnode.sh delete
$THISDIR/cpumem-allnode.sh start
sleep 10

# 4) wait a moment
echo "*** wait a moment ***"
#sleep 180		# 3 minute
sleep 120		# 2 minute


###############################################################################
# T2
#

#	1) check prev heartbeat status
echo "*** check prev heartbeat status ***"
ssh -t $HA_USER@$MASTER_HOST "sudo su - root -c 'crm_mon -1'" 	> $CUR_DIR/crm_mon.master.prev
ssh -t $HA_USER@$SLAVE_HOST "sudo su - root -c 'crm_mon -1'" 	> $CUR_DIR/crm_mon.slave.prev

# 	2) check prev cubrid status (replication, sync, ...)
echo "*** check prev cubrid status ***"
$THISDIR/status_check.sh 5 2 	> $CUR_DIR/cubrid-status.prev

#	3) check prev process IDs
echo "*** check prev process IDs ***"
$THISDIR/psall-cub.sh prev; 	mv $THISDIR/pscub.*.prev $CUR_DIR


echo "*** pre script done. ***"

echo "Check crm_mon.*.prev cubrid-status.prev and  pscub.*.prev files in $CUR_DIR"
