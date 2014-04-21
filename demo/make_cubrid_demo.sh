#!/bin/sh

if [ -z "$CUBRID" ]; then
	exit 1
fi

cubrid createdb --db-volume-size=100M --log-volume-size=100M demodb en_US > /dev/null 2>&1
cubrid loaddb -u dba -s $CUBRID/demo/demodb_schema -d $CUBRID/demo/demodb_objects demodb > /dev/null 2>&1
