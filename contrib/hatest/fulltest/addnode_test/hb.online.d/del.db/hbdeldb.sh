#!/bin/sh

######################################################################
#
# heartbeat initial configuration for 1*Active - 1*Standby rolechange 
#
#

SED=/bin/sed
RM=/bin/rm
CIBADMIN=/usr/sbin/cibadmin
CIBXML="/var/lib/heartbeat/crm/cib.xml"
DBNAME="$1"
RESOURCES=`crm_resource --list | grep Resource | grep $DBNAME | awk '{print $3}'`
RSC_MS=`crm_resource --list | grep "Master/Slave" | grep $DBNAME | awk '{print $3}'`
echo $RSC_MS
RSC_GROUP=`crm_resource --list | grep "Resource" | grep $DBNAME | awk '{print $3}'`
echo $RSC_GROUP
HB_ADDNODE=/usr/share/heartbeat/hb_addnode
HB_DELNODE=/usr/share/heartbeat/hb_delnode


usage() 
{
	echo "$0 {DB}"
}

mod_constraints_del_db()
{
        echo "<constraints>" >> constraints-reply-remove-db.xml.TEMP
        for R in $RSC_MS; do
                cat $CIBXML | grep $R | \
                grep "rsc_location" >> constraints-reply-remove-db.xml.TEMP
        done
        for R in $RSC_GROUP; do
                cat $CIBXML | grep $R | \
                grep "rsc_location" >> constraints-reply-remove-db.xml.TEMP
        done
        echo "</constraints>" >> constraints-reply-remove-db.xml.TEMP
}

mod_resources_del_db()
{
        echo "<resources>" >> resources-reply-remove-db.xml.TEMP
        for R in $RSC_MS; do
                echo "<master_slave id=\"$R\"/>" >> resources-reply-remove-db.xml.TEMP
        done
        for R in $RSC_GROUP; do
                echo "<group id=\"$R\"/>" >> resources-reply-remove-db.xml.TEMP
        done
        echo "</resources>" >> resources-reply-remove-db.xml.TEMP
}

cibadmin_exec()
{
	$CIBADMIN -V -o resources -d -x ./resources-reply-remove-db.xml.TEMP
	sleep 3

	$CIBADMIN -V -o constraints -d -x ./constraints-reply-remove-db.xml.TEMP
	sleep 5 
}

if [ -f ./constraints-reply-remove-db.xml.TEMP ]; then
	$RM ./constraints-reply-remove-db.xml.TEMP
fi

if [ -f ./resources-reply-remove-db.xml.TEMP ]; then
	$RM ./resources-reply-remove-db.xml.TEMP 
fi

mod_resources_del_db
mod_constraints_del_db

cibadmin_exec 




