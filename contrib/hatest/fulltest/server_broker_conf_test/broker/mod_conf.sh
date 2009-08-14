#!/bin/sh

cubrid broker stop
echo "broker stop"
cp ${CUBRID}/conf/cubrid_broker.add.conf ${CUBRID}/conf/cubrid_broker.conf
#cp ${CUBRID}/conf/cubrid_broker.ori.conf ${CUBRID}/conf/cubrid_broker.conf
echo "copy done ${CUBRID}/conf/cubrid_broker.conf"
cubrid broker start
echo "broker start"
