#!/bin/sh

############################################################
# record vm stat 
# 
# timestamp swapd free buff cache us sys id wa st
#
############################################################

usage()
{
	echo "usage : cpumem-ssh.sh uesr-name remote-host" 
}

if [ $# -ne 2 ]; then 
	usage 
	exit 
fi

# max 6 hour
NUMLOOP=0
while [ $NUMLOOP -lt 21600 ]; do
	NOW=`date +%Y%m%d%H%M%S`

	# every 0,10,20,30,40,50,60 second
	if [ "${NOW:13:1}" = "0" ] ; then 
		echo -n $NOW " "
		ssh $1@$2 "vmstat 2 2" > .$2.cpumem
		awk '{ if (NR==4) printf "%-10u %-10u %-10u %-10u %-3u %-3u %-3u %-3u %-3u\n",$3,$4,$5,$6,$13,$14,$15,$16,$17 }' .$2.cpumem
	fi
	sleep 1
	NUMLOOP=`expr $NUMLOOP + 1`
done


