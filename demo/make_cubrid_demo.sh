#!/bin/sh

if [ -z "$CUBRID" ]; then
	exit 1
fi

cubrid createdb demodb &> /dev/null
cubrid loaddb -u dba -s $CUBRID/demo/demodb_schema -d $CUBRID/demo/demodb_objects demodb &> /dev/null
