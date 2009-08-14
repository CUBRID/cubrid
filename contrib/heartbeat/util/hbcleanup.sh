#!/bin/bash

if [ $# -ne 1 ]
then
        echo ""
        echo "Tool for clean up failed resources"
        echo "  Usage $0 <resource prefix>"
        echo ""
        echo "  Ex) $0 cubrid"
        exit 1
fi

cleanup_command=$(crm_mon -1 | grep -A 9999 "Failed actions" | grep $1 | sed -e 's/\(cubrid_.*\)_\(start\|stop\|monitor\|notify\|promote\|demote\)_.*node=\([^,]*\).*/crm_resource -C -r \1 -H \3;/g')

if [ "x$cleanup_command" = "x" ]
then
        echo "Nothing to cleanup"
else
        echo $cleanup_command
        eval $cleanup_command
fi

