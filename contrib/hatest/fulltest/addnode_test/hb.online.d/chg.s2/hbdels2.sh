#!/bin/sh

######################################################################
#
# heartbeat initial configuration for 1*Active - 1*Standby rolechange 
#
#

SED=/bin/sed
RM=/bin/rm
CIBADMIN=/usr/sbin/cibadmin
HB_ADDNODE=/usr/share/heartbeat/hb_addnode
HB_DELNODE=/usr/share/heartbeat/hb_delnode


usage() 
{
	echo "$0 {A1-node} {S1-node} {S2-node} {DB1} {DB2}" 
}


mod_constraints_add_s2()
{
	$SED "s/A1_NODE/$1/g" constraints-reply-add-s2.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/S2_NODE/$3/g" \
	| $SED "s/DB_1/$4/g" \
	| $SED "s/DB_2/$5/g" \
	> constraints-reply-add-s2.xml.TEMP
}

mod_constraints_del_s2()
{
	$SED "s/A1_NODE/$1/g" constraints-reply-remove-s2.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/S2_NODE/$3/g" \
	| $SED "s/DB_1/$4/g" \
	| $SED "s/DB_2/$5/g" \
	> constraints-reply-remove-s2.xml.TEMP
}

mod_nodes_del_s2()
{
	$SED "s/A1_NODE/$1/g" nodes-reply-remove-s2.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/S2_NODE/$3/g" \
	| $SED "s/DB_1/$4/g" \
	| $SED "s/DB_2/$5/g" \
	> nodes-reply-remove-s2.xml.TEMP
}

mod_resources_add_s2()
{
	$SED "s/A1_NODE/$1/g" resources-reply-add-s2.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/S2_NODE/$3/g" \
	| $SED "s/DB_1/$4/g" \
	| $SED "s/DB_2/$5/g" \
	> resources-reply-add-s2.xml.TEMP
}

mod_resources_inc_max_clone_for_s2()
{
	$SED "s/A1_NODE/$1/g" resources-reply-inc-max-clone-s2.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/S2_NODE/$3/g" \
	| $SED "s/DB_1/$4/g" \
	| $SED "s/DB_2/$5/g" \
	> resources-reply-inc-max-clone-s2.xml.TEMP
}

mod_resources_dec_max_clone_for_s2()
{
	$SED "s/A1_NODE/$1/g" resources-reply-dec-max-clone-s2.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/S2_NODE/$3/g" \
	| $SED "s/DB_1/$4/g" \
	| $SED "s/DB_2/$5/g" \
	> resources-reply-dec-max-clone-s2.xml.TEMP
}

mod_resources_del_s2()
{
	$SED "s/A1_NODE/$1/g" resources-reply-remove-s2.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/S2_NODE/$3/g" \
	| $SED "s/DB_1/$4/g" \
	| $SED "s/DB_2/$5/g" \
	> resources-reply-remove-s2.xml.TEMP
}


mod_resources_start_s2()
{
	$SED "s/A1_NODE/$1/g" resources-reply-start-s2.xml \
	| $SED "s/S1_NODE/$2/g" \
	| $SED "s/S2_NODE/$3/g" \
	| $SED "s/DB_1/$4/g" \
	| $SED "s/DB_2/$5/g" \
	> resources-reply-start-s2.xml.TEMP
}

cibadmin_exec()
{
	$CIBADMIN -V -o resources -d -x ./resources-reply-remove-s2.xml.TEMP
	sleep 3

	$CIBADMIN -V -o resources -U -x ./resources-reply-dec-max-clone-s2.xml.TEMP
	sleep 3

	$CIBADMIN -V -o constraints -d -x ./constraints-reply-remove-s2.xml.TEMP
	sleep 5 

	$CIBADMIN -V -o nodes -d -x ./constraints-reply-remove-s2.xml.TEMP
}

if [ -f ./constraints-reply-add-s2.xml.TEMP ]; then 
	$RM ./constraints-reply-add-s2.xml.TEMP
fi  

if [ -f ./constraints-reply-remove-s2.xml.TEMP ]; then
	$RM ./constraints-reply-remove-s2.xml.TEMP
fi

if [ -f ./nodes-reply-remove-s2.xml.TEMP ]; then
	$RM ./nodes-reply-remove-s2.xml.TEMP
fi

if [ -f ./resources-reply-add-s2.xml.TEMP ]; then
	$RM ./resources-reply-add-s2.xml.TEMP
fi

if [ -f ./resources-reply-inc-max-clone-s2.xml.TEMP ]; then
	$RM ./resources-reply-inc-max-clone-s2.xml.TEMP
fi

if [ -f ./resources-reply-dec-max-clone-s2.xml.TEMP ]; then
	$RM ./resources-reply-dec-max-clone-s2.xml.TEMP
fi

if [ -f ./resources-reply-remove-s2.xml.TEMP ]; then
	$RM ./resources-reply-remove-s2.xml.TEMP 
fi

if [ -f ./resources-reply-start-s2.xml.TEMP ]; then
	$RM ./resources-reply-start-s2.xml.TEMP
fi


$HB_DELNODE $3
sleep 10

mod_resources_del_s2 $1 $2 $3 $4 $5 
mod_resources_dec_max_clone_for_s2 $1 $2 $3 $4 $5 
mod_constraints_del_s2 $1 $2 $3 $4 $5 
mod_nodes_del_s2 $1 $2 $3 $4 $5 

#mod_constraints_add_s2 $1 $2 $3 $4 $5 
#mod_nodes_del_s2
#mod_resources_add_s2 $1 $2 $3 $4 $5 
#mod_resources_inc_max_clone_for_s2 $1 $2 $3 $4 $5
#mod_resources_start_s2 $1 $2 $3 $4 $5

cibadmin_exec 




