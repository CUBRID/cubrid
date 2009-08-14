#!/bin/bash

# This util support Master/Slave resource or Group resource.
#  ie) It can NOT start/stop a single resource.

usage ()
{
        echo ""
        echo "Tool for start up all resources"
        echo "  Usage $0 <start|stop>"
        echo ""
        echo "  Ex) $0 start"
}

if [ $# -ne 1 ]
then
        usage
        exit 1
fi

resource_list=$(crm_resource -LQ | egrep -w "Master/Slave Set:|Resource Group:" | sed -e 's/.*:\ \(.*\)/\1/g')

case $1 in
  "start")
        value="started"
        ;;
  "stop")
        value="stopped"
        ;;
  *)
        usage
        exit 1
        ;;
esac

for resource in $resource_list
do
        xml="<nvpair id=\"${resource}_metaattr_target_role\" name=\"target_role\" value=\"$value\"/>"
	command="cibadmin -M -X '$xml'"
	echo $command
	eval $command
done

