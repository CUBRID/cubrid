#!/bin/bash

cleanup_command=$(crm_mon -1 | grep -A 9999 "Failed actions" | grep cubrid | sed -e 's/\(cubrid_.*\)_\(start\|stop\|monitor\|notify\|promote\|demote\)_.*node=\([^,]*\).*/crm_resource -C -r \1 -H \3;/g')

if [ "x$cleanup_command" = "x" ]
then
	echo "Nothing to cleanup"
else
	echo $cleanup_command
	eval $cleanup_command
fi
