#!/bin/bash

#collectl -i 2 -f nobackup
#collectl -P -scmdn -p nobackup.raw --from 08:29-08:30 --thru 08:29:10 -oT

DB_LIST="nbd1 nbd2"
NODE_LIST="cdnt14v1.cub  cdnt15v1.cub  cdnt14v3.cub"
  
echo "====================================================================="
echo " Date	DB     $NODE_LIST		"
echo "====================================================================="
for D in $DB_LIST; do
	ACTIVE_COUNT=0
	for N in $NODE_LIST; do
		MODE=`cubrid changemode $D\@$N 2>&1 | awk '{print $9}'`
		if [ "$MODE" == "active." ]; then
			ACTIVE_COUNT=`csql -u dba -c "select count(*) from nbd_comment" $D\@$N 2>&1 | sed -n 6p | awk '{print $1}'`
			ACTIVE_NODE=$N
		fi
	done
	DATE=`date +"%H:%M:%S"`
	#echo -n
        SUMMARY="$DATE $D "
	for N in $NODE_LIST; do
		COUNT=`csql -u dba -c "select count(*) from nbd_comment" $D\@$N 2>&1 | sed -n 6p | awk '{print $1}'`
		if [ -z $COUNT ]; then
			COUNT=0
		fi
		if [ "$N" == "$ACTIVE_NODE" ]; then
			NDIFF="A"
		else
			NDIFF=`expr $COUNT - $ACTIVE_COUNT`
		fi
		
		SUMMARY="$SUMMARY $COUNT($NDIFF)"
	done
	echo $SUMMARY
	rm -f csql.err.*
done
