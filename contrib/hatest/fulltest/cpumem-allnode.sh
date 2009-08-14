#!/bin/sh

THISDIR=`dirname $0`
. $THISDIR/hatest.env

NOW=`date +%Y%m%d%H%M%S`

mkdir -p $THISDIR/cpumem.report

# See how we were called.
case "$1" in
  start)
#	$THISDIR/cpumem-ssh.sh hatest d8g674 >> $THISDIR/d8g674.cpumem.$NOW &
#	$THISDIR/cpumem-ssh.sh hatest d8g675 >> $THISDIR/d8g675.cpumem.$NOW &
#	$THISDIR/cpumem-ssh.sh hatest d8g676 >> $THISDIR/d8g676.cpumem.$NOW &
#	$THISDIR/cpumem-ssh.sh hatest d8g677 >> $THISDIR/d8g677.cpumem.$NOW &

	$THISDIR/cpumem-ssh-collectl.sh $HA_USER $MASTER_HOST 		>> $THISDIR/cpumem.report/$MASTER_HOST.cpumem.$NOW &
	$THISDIR/cpumem-ssh-collectl.sh $HA_USER $SLAVE_HOST 		>> $THISDIR/cpumem.report/$SLAVE_HOST.cpumem.$NOW &
	$THISDIR/cpumem-ssh-collectl.sh $HA_USER $BROKER_PRI_HOST 	>> $THISDIR/cpumem.report/$BROKER_PRI_HOST.cpumem.$NOW &
	$THISDIR/cpumem-ssh-collectl.sh $HA_USER $BROKER_SEC_HOST 	>> $THISDIR/cpumem.report/$BROKER_SEC_HOST.cpumem.$NOW &
	$THISDIR/cpumem-ssh-collectl.sh $HA_USER $APPS_HOST 		>> $THISDIR/cpumem.report/$APPS_HOST.cpumem.$NOW &

#	$THISDIR/cpumem-ssh-collectl.sh hatest cdnt12v1.cub >> $THISDIR/cpumem.report/cdnt12v1.cub.cpumem.$NOW &
#	$THISDIR/cpumem-ssh-collectl.sh hatest cdnt13v1.cub >> $THISDIR/cpumem.report/cdnt13v1.cub.cpumem.$NOW &
#	$THISDIR/cpumem-ssh-collectl.sh hatest cdnt12v2.cub >> $THISDIR/cpumem.report/cdnt12v2.cub.cpumem.$NOW &
#	$THISDIR/cpumem-ssh-collectl.sh hatest cdnt13v2.cub >> $THISDIR/cpumem.report/cdnt13v2.cub.cpumem.$NOW &
#	$THISDIR/cpumem-ssh-collectl.sh hatest cdbs001.cub >> $THISDIR/cpumem.report/cdbs001.cpumem.$NOW &



	;;
  stop)
	pkill -f "cpumem-ssh-collectl.sh $HA_USER"
	;;
  status)
	pgrep -f "cpumem-ssh-collectl.sh $HA_USER"
	;;
  delete)
	rm -f $THISDIR/cpumem.report/*
	;;

  *)
	echo $"Usage: $0 {start|stop|status|delete}"
   	exit 1
esac

exit 0
