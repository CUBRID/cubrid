#!/bin/bash

DIR=$PWD
LOGPATH=$DIR/benchmark/run

if [ $# -eq 0 ]
then
	ls -lrRt $LOGPATH/log/
	echo "which file do you want?"
	read which
else
	if [ "$1" = "all" ]
	then
		which=`ls $LOGPATH/log/*.log`
		which=`find $LOGPATH/log -name "*.log" -prune -exec basename {} \;`
	else
		which=$1
	fi
fi

set -- $which

echo "Processing... $@"
for logfile in $@
do
	echo "================================="
	echo " Report $LOGPATH/log/$logfile (Create time: " `date -d @${logfile:0:10}` ")"
java -classpath "$DIR:$DIR/lib/commons-collections.jar:$DIR/lib/commons-pool-1.1.jar:$DIR/lib/commons-dbcp-1.1.jar:$DIR/lib/ibatis-2.3.0.677.jar:$DIR/nbench.1.0.0.jar:$CUBRID/jdbc/cubrid_jdbc.jar" nbench.report.Reporter $LOGPATH/log/$logfile
	echo "================================="
done
echo "Done."
