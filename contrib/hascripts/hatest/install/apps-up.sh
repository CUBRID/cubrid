#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1

INSTALL_TMP_DIR=~/.hatest.tmp

# load environment
. hatest.env

. ./common.sh
. ./env-setup.sh
. ./cubrid-setup.sh
. ./heartbeat-setup.sh


setup_env 				$HA_USER $APPS_HOST

RETVAL=0
check_host_in_hosts $HA_USER $MASTER_HOST $SLAVE_HOST $BROKER_HOST $APPS_HOST
RETVAL=$?
if [ $RETVAL -eq 1 ] ; then
	exit RET_FAIL
fi

setup_cubrid_apps		$HA_USER $APPS_HOST

