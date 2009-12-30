#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1

INSTALL_TMP_DIR=~/.hatest.tmp

# load environment
. hatest.env

. ./common.sh
. ./env-setup.sh
. ./heartbeat-setup.sh


setup_env $HA_USER $SLAVE_HOST

RETVAL=0
check_host_in_hosts $HA_USER $MASTER_HOST $SLAVE_HOST 
RETVAL=$?
if [ $RETVAL -eq 1 ] ; then
	exit RET_FAIL
fi

exec_heartbeat_on_slave $HA_USER $SLAVE_HOST

