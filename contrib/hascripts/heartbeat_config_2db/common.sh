#!/bin/bash

# check user is exist and is in wheel group
# return : 0: okay 1: user not exit 2: user exist but not in wheel group
#  ex) check_user_exist hatest d8g675
check_user_exist ()
{
	if [ $# -lt 2 ]
	then
		echo "Error: Invalid argument number"
		return 1
	fi

	USERNAME=$1
	NODENAME=$2

	echo "Checking for user $USERNAME@$NODENAME..."
	GROUPLIST=$(ssh $USERNAME@$NODENAME groups)
	if [ $? -ne 0 ]
	then
		echo "Invalid user $USERNAME."
		return $?
	else
		echo "Okay. user $USERNAME is exist."
	fi

	echo "Checking for wheel group..."
	is_in_wheel=0
	for GNAME in "$GROUPLIST"
	do
		if [ "$GNAME" = "wheel" ]
		then
			echo "$USERNAME is in group wheel"
			is_in_wheel=1
		fi
	done

	if [ $is_in_wheel -ne 1 ]
	then
		echo "$USERNAME is NOT in group wheel"
		return 2
	fi

	return 0
}

# check host is exist in /etc/hosts
# return : 0: okay 1: not okay
#  ex) check_host_in_hosts hatest d8g674 d8g675 d8g676 d8g677
check_host_in_hosts ()
{
	if [ $# -lt 2 ]
	then
		echo "Error: Invalid argument number"
		return 1
	fi

	USERNAME=$1 && shift 1
	failed=""
	HOSTLIST=$@
	CHECKLIST=$(echo ${HOSTLIST// /|})
	for i in $HOSTLIST
	do
		OUTPUT=$(ssh $USERNAME@$i cat /etc/hosts | egrep -w "$CHECKLIST")
		if [ $? -ne 0 ]
		then
			echo "Some of $HOSTLIST is in /etc/hosts at $i is missing."
			failed="$failed $i"
		else
			echo "Okay. $HOSTLIST is in /etc/hosts at $i."
		fi
	done

	if [ ${#failed} -gt 0 ]
	then
		echo "Check $HOSTLIST is in /etc/hosts at$failed"
		return 1
	fi

	return 0
}

