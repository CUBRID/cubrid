#!/bin/sh

############################################################
# record vm stat 
# 
# timestamp swapd free buff cache us sys id wa st
#
############################################################

#HA_USER=hatest
#MASTER_HOST=d8g674
#SLAVE_HOST=d8g675
#BROKER_PRI_HOST=d8g676
#BROKER_SEC_HOST=d8g677
#MASTER_HOST=cdnt12v1.cub
#SLAVE_HOST=cdnt13v1.cub
#BROKER_PRI_HOST=cdnt12v2.cub
#BROKER_SEC_HOST=cdnt13v2.cub

THISDIR=`dirname $0`
. $THISDIR/hatest.env

usage()
{
	echo "usage : ps-cub.sh"
}

NOW=`date +%Y%m%d%H%M%S`
FNAME="$THISDIR/pscub.$NOW.$1"

echo "[MASTER : $HA_USER@$MASTER_HOST ] "					> 	$FNAME
ssh $HA_USER@$MASTER_HOST "ps -e -o pid,ppid,size,start_time,command --sort cmd| grep cub_ | grep -v grep"	>> 	$FNAME
echo -e "\n\n" 									>>  	$FNAME

echo "[SLAVE : $HA_USER@$SLAVE_HOST ] "						>> 	$FNAME
ssh $HA_USER@$SLAVE_HOST "ps -e -o pid,ppid,size,start_time,command --sort cmd| grep cub_ | grep -v grep"		>> 	$FNAME
echo -e "\n\n" 									>>  	$FNAME

echo "[BROKER-PRI : $HA_USER@$BROKER_PRI_HOST ] "				>> 	$FNAME
ssh $HA_USER@$BROKER_PRI_HOST "ps -e -o pid,ppid,size,start_time,command --sort cmd| grep cub_ | grep -v grep"	>> 	$FNAME
echo -e "\n\n" 									>>  	$FNAME

echo "[BROKER-SEC : $HA_USER@$BROKER_SEC_HOST ] "				>> 	$FNAME
ssh $HA_USER@$BROKER_SEC_HOST "ps -e -o pid,ppid,size,start_time,command --sort cmd| grep cub_ | grep -v grep"	>> 	$FNAME
echo -e "\n\n" 									>>  	$FNAME



