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
printf "%-14s  %-3s %-3s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n" 'TIMESTAMP' 'cpu' 'sys' 'free' 'buff' 'cache' 'kbin' 'pktin' 'kbout' 'pktout' 
NUMLOOP=0
while [ $NUMLOOP -lt 21600 ]; do
	NOW=`date +%Y%m%d%H%M%S`

	# every 0,10,20,30,40,50,60 second
	if [ "${NOW:13:1}" = "0" ] ; then 
		echo -n $NOW " "
		ssh $1@$2 "collectl -scmdn -i1.0 -oT -w -c 2" > .$2.cpumem
		awk '{ if (NR==5) printf "%-3u %-3u %-10u %-10u %-10u %-10u %-10u %-10u %-10u\n",$2,$3,$6,$7,$8,$16,$17,$18,$19 }' .$2.cpumem
	fi
	sleep 1
	NUMLOOP=`expr $NUMLOOP + 1`
done


