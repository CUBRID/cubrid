#!/bin/sh

######################################################################
#
# heartbeat initial configuration for 1*Active - 1*Standby rolechange 
#
#

SED=/bin/sed
RM=/bin/rm
CIBADMIN=/usr/sbin/cibadmin


usage() 
{
	echo "$0 {A1-node} {S1-node} {DB1} {DB2}" 
}


modify_constraints() 
{
	$SED "s/A_NODE/$1/g" constraints-reply-init.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/DB_1/$3/g" \
	| $SED "s/DB_2/$4/g" \
	> constraints-reply-init.xml.TEMP
}

modify_resources()
{
	$SED "s/A_NODE/$1/g" resources-reply-init.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/DB_1/$3/g" \
	| $SED "s/DB_2/$4/g" \
	> resources-reply-init.xml.TEMP
}

modify_resources_start()
{
	$SED "s/DB_1/$1/g" resources-reply-start.xml \
	| $SED "s/DB_2/$2/g" \
	> resources-reply-start.xml.TEMP
}

cibadmin_exec()
{
	$CIBADMIN -V -o crm_config -U -x ./crm_config-reply.xml	
	$CIBADMIN -V -o constraints -R -x ./constraints-reply-init.xml.TEMP 
	$CIBADMIN -V -o resources -R -x ./resources-reply-init.xml.TEMP	

	sleep 3

	$CIBADMIN -V -o resources -U -x ./resources-reply-start.xml.TEMP
}


if [ -f ./constraints-reply-init.xml.TEMP ]; then 
	$RM ./constraints-reply-init.xml.TEMP
fi

if [ -f ./resources-reply-init.xml.TEMP ]; then
	$RM ./resources-reply-init.xml.TEMP
fi 

if [ -f ./resources-reply-start.xml.TEMP ]; then
	$RM ./resources-reply-start.xml.TEMP
fi 

modify_constraints $1 $2 $3 $4
modify_resources $1 $2 $3 $4
modify_resources_start $3 $4

cibadmin_exec 





