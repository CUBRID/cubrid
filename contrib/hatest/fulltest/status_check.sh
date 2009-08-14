#!/bin/sh

THISDIR=`dirname $0`
. $THISDIR/hatest.env

exe_if_alive ()
{
	TESTHOST=$1
	COMMAND=$2

	ping -q -w 1 -c 1 $TESTHOST > /dev/null 2>&1
	if [ $? -eq 0 ]
	then
		#echo "Run <$COMMAND> at $TESTHOST"
		eval $COMMAND 2> /dev/null
		return $?
	else
		echo "DEAD"
	fi

	return 1
}


do_check_diff ()
{
	CHECKDB=$1
	CHECKHOST1=$2
	CHECKHOST2=$3
	echo "Check total count in $CHECKDB at $CHECKHOST1 and $CHECKHOST2"
	result1=`exe_if_alive $CHECKHOST1 "csql -u dba -c \"select count(*) from nbd_comment\" $CHECKDB@$CHECKHOST1" | sed -n 6p | awk '{print $1}'`
	result2=`exe_if_alive $CHECKHOST2 "csql -u dba -c \"select count(*) from nbd_comment\" $CHECKDB@$CHECKHOST2" | sed -n 6p | awk '{print $1}'`
	if [ "$result1" = "DEAD" -o "$result2" = "DEAD" ] 
	then
		diff="DEAD"
	else
		typeset -i count1="$result1"
		typeset -i count2="$result2"
		diff=`expr $count1 - $count2`
	fi
	echo `date +"%H:%M:%S"`": $CHECKDB $CHECKHOST1: $count1 - $CHECKHOST2: $count2 (diff: $diff)"
}

do_check_killtran ()
{
	CHECKDB=$1
	CHECKHOST1=$2
	CHECKHOST2=$3
	echo "Check killtran in $CHECKDB at $CHECKHOST1"
	exe_if_alive $CHECKHOST1 "cubrid killtran -f $CHECKDB@$CHECKHOST1"
	echo "Check killtran in $CHECKDB at $CHECKHOST2"
	exe_if_alive $CHECKHOST2 "cubrid killtran -f $CHECKDB@$CHECKHOST2"
}

do_check_mode ()
{
	CHECKDB=$1
	CHECKHOST1=$2
	CHECKHOST2=$3
	echo "Check HA mode in $CHECKDB at $CHECKHOST1 and $CHECKHOST2"
	result1=`exe_if_alive $CHECKHOST1 "cubrid changemode $CHECKDB@$CHECKHOST1"`
	result2=`exe_if_alive $CHECKHOST2 "cubrid changemode $CHECKDB@$CHECKHOST2"`
	case $result1 in
		*to-be-active.)
			status1="to-be-active"
			;;
		*to-be-standby.)
			status1="to-be-standby"
			;;
		*active.)
			status1="active"
			;;
		*standby.)
			status1="standby"
			;;
		"DEAD")
			status1="DEAD"
			;;
		*)
			status1="NA"
	esac

	case $result2 in
		*to-be-active.)
			status2="to-be-active"
			;;
		*to-be-standby.)
			status2="to-be-standby"
			;;
		*active.)
			status2="active"
			;;
		*standby.)
			status2="standby"
			;;
		"DEAD")
			status2="DEAD"
			;;
		*)
			status2="NA"
	esac

	echo `date +"%H:%M:%S"`": $CHECKDB $CHECKHOST1: $status1 - $CHECKHOST2: $status2"
}

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
	for db in $DB_1 $DB_2
	do
		echo "Check for DB $db"
		do_check_mode $db $MASTER_HOST $SLAVE_HOST
		echo "---"
		do_check_diff $db $MASTER_HOST $SLAVE_HOST
		echo "---"
		do_check_killtran $db $MASTER_HOST $SLAVE_HOST
		echo "---"
	done
	NUM_LOOP=`expr $NUM_LOOP - 1` 
	sleep $SLEEP_SEC
	echo ""
done
