#!/bin/sh

############################################################
# record vm stat 
# 
# timestamp swapd free buff cache us sys id wa st
#
############################################################

HA_USER=hatest
MASTER_HOST=d8g674
SLAVE_HOST=d8g675
BROKER_HOST=d8g676

usage()
{
	echo "usage : ps-cub.sh"
}

NOW=`date +%Y%m%d%H%M%S`
FNAME="pscub.$NOW"

echo "[MASTER : $HA_USER@$MASTER_HOST ] "				> 	$FNAME
ssh $HA_USER@$MASTER_HOST "ps -ef | grep cub" 			>> 	$FNAME
echo -e "\n\n" 											>>  $FNAME

echo "[SLAVE : $HA_USER@$SLAVE_HOST ] "					>> 	$FNAME
ssh $HA_USER@$SLAVE_HOST "ps -ef | grep cub" 			>> 	$FNAME
echo -e "\n\n" 											>>  $FNAME

echo "[BROKER : $HA_USER@$BROKER_HOST ] "				>> 	$FNAME
ssh $HA_USER@$BROKER_HOST "ps -ef | grep broker" 		>> 	$FNAME
echo -e "\n\n" 											>>  $FNAME


