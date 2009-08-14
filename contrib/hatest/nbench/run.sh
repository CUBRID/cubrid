#!/bin/bash
DIR=$PWD
LOGPATH=$DIR/benchmark/run
cd benchmark
java -classpath "$DIR:$DIR/lib/commons-collections.jar:$DIR/lib/commons-pool-1.1.jar:$DIR/lib/commons-dbcp-1.1.jar:$DIR/lib/ibatis-2.3.0.677.jar:$DIR/nbench.1.0.0.jar:$CUBRID/jdbc/cubrid_jdbc.jar" NBench $1 2>&1 | tee $LOGPATH/$1.log

