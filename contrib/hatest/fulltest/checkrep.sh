#!/bin/sh

#DB_1=nbd1
#DB_2=nbd2
#DB_3=nbd3
#DB_4=nbd4
#MASTER_HOST=d8g674
#SLAVE_HOST=d8g675

THISDIR=`dirname $0`
. $THISDIR/hatest.env

if [ "$1x" = "x" ] 
then 
	SLEEP_SEC=1
else 
	SLEEP_SEC=$1
fi

if [ "$2x" = "x" ] 
then 
	NUM_LOOP=1
else 
	NUM_LOOP=$2
fi


while [ $NUM_LOOP -gt 0 ] ;
do
	NOW=`date +%Y/%m/%d-%k:%M:%S`
	echo ""
	echo "[ NOW : $NOW ]"
	echo "[ $DB_1 ] " 
	cubrid changemode $DB_1@d8g674
	cubrid changemode $DB_1@d8g675
	csql -u nbd -p nbd -c "select count(*) from nbd_comment" $DB_1@$MASTER_HOST | grep -A 1 -w "============="
	csql -u nbd -p nbd -c "select count(*) from nbd_comment" $DB_1@$SLAVE_HOST | grep -A 1 -w "============="

	echo "[ $DB_2 ] " 
	cubrid changemode $DB_2@d8g674
	cubrid changemode $DB_2@d8g675
	csql -u nbd -p nbd -c "select count(*) from nbd_comment" $DB_2@$MASTER_HOST | grep -A 1 -w "============="
	csql -u nbd -p nbd -c "select count(*) from nbd_comment" $DB_2@$SLAVE_HOST | grep -A 1 -w "============="

#echo "[ $DB_3 ] " 
#cubrid changemode $DB_3@d8g674
#cubrid changemode $DB_3@d8g675
#csql -u nbd -p nbd -c "select count(*) from nbd_comment" $DB_3@$MASTER_HOST | grep -A 1 -w "============="
#csql -u nbd -p nbd -c "select count(*) from nbd_comment" $DB_3@$SLAVE_HOST | grep -A 1 -w "============="
#
#echo "[ $DB_4 ] " 
#cubrid changemode $DB_4@d8g674
#cubrid changemode $DB_4@d8g675
#csql -u nbd -p nbd -c "select count(*) from nbd_comment" $DB_4@$MASTER_HOST | grep -A 1 -w "============="
#csql -u nbd -p nbd -c "select count(*) from nbd_comment" $DB_4@$SLAVE_HOST | grep -A 1 -w "============="

	NUM_LOOP=`expr $NUM_LOOP - 1` 
	sleep $SLEEP_SEC
	echo ""
done
