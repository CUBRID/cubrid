#!/bin/sh

if [ -z "$CUBRID" ]; then
	exit 1
fi

cubrid createdb --db-volume-size=100M --log-volume-size=100M demodb en_US.utf8  > /dev/null 2>&1
cubrid loaddb -u dba -s $CUBRID/demo/demodb_schema -d $CUBRID/demo/demodb_objects demodb > /dev/null 2>&1

"$JAVA_HOME/bin/javac" -cp $CUBRID/jdbc/cubrid_jdbc.jar $CUBRID/demo/test/*.java 
mkdir -p $CUBRID/demo/java/test

for clz in $(ls -d $CUBRID/demo/test/*.class); do
    cp $clz $CUBRID/demo/java/test
	# loadjava demodb $clz > /dev/null 2>&1
done

cubrid loaddb -u dba -s $CUBRID/demo/test/testsp_schema demodb > /dev/null 2>&1
