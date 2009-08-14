#!/bin/sh

THISDIR=`dirname $0`
. $THISDIR/hatest.env

CUR_DIR="$THISDIR/logs/current"

# make new 
mkdir -p $CUR_DIR


###############################################################################
# T4
# 	

# 1) check post heartbeat status
echo "*** check post heartbeat status ***"
ssh -t $HA_USER@$MASTER_HOST "sudo su - root -c crm_mon" 	> $CUR_DIR/crm_mon.master.post
ssh -t $HA_USER@$SLAVE_HOST "sudo su - root -c crm_mon" 	> $CUR_DIR/crm_mon.slave.post

# 2) check post cubrid status (replication, sync, ...)
echo "*** check post cubrid status***"
$THISDIR/status_check.sh 5 2 	> $CUR_DIR/cubrid-status.post

# 3) check post process IDs
echo "*** check post process IDs ***"
$THISDIR/psall-cub.sh post; 	mv $THISDIR/pscub.*.post $CUR_DIR/


###############################################################################
# T5
#

# 1) stop all test environnment
echo "*** stop all test environnment ***"
$THISDIR/testenv-stop.sh
sleep 10 

# 2) stop resource monitor
echo "*** stop resource monitor ***"
$THISDIR/cpumem-allnode.sh stop
mv $THISDIR/cpumem.report $CUR_DIR/cpumem.report
sleep 10

# 3) log collector
echo "*** log collector ***"
$THISDIR/logcollector.sh cp $CUR_DIR/

mv ./csql.err* ./*.log $CUR_DIR/

mkdir -p $CUR_DIR/$MASTER_HOST		
tar -C $CUR_DIR/$MASTER_HOST -xzf $CUR_DIR/$MASTER_HOST*.tar.gz

mkdir -p $CUR_DIR/$SLAVE_HOST
tar -C $CUR_DIR/$SLAVE_HOST -xzf $CUR_DIR/$SLAVE_HOST*.tar.gz

mkdir -p $CUR_DIR/$BROKER_PRI_HOST
tar -C $CUR_DIR/$BROKER_PRI_HOST -xzf $CUR_DIR/$BROKER_PRI_HOST*.tar.gz

mkdir -p $CUR_DIR/$BROKER_SEC_HOST
tar -C $CUR_DIR/$BROKER_SEC_HOST -xzf $CUR_DIR/$BROKER_SEC_HOST*.tar.gz

mkdir -p $CUR_DIR/$APPS_HOST
tar -C $CUR_DIR/$APPS_HOST -xzf $CUR_DIR/$APPS_HOST*.tar.gz

echo "*** NBench report ***"
(cd $NBENCH_HOME;./report.sh all) > $CUR_DIR/nbench.report 2>&1
echo ""

echo "*** log deletor ***"
$THISDIR/logcollector.sh rm

echo "*** post script done ***"

echo "Check crm_mon.*.post cubrid-status.post and  pscub.*.post files and logs in $CUR_DIR"
