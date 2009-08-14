#!/bin/sh

CUBRID=CUB_HOME
CUBRID_USER=CUB_USER
CUBRID_DATABASES=CUB_DATABASES
CUBRID_BROKER_CONF_FILE=${CUBRID_DATABASES}/cubrid_broker.conf
PATH=${PATH}:${CUBRID}/bin
LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${CUBRID}/lib

export CUBRID
export CUBRID_DATABASES
export CUBRID_BROKER_CONF_FILE
export PATH
export LD_LIBRARY_PATH

##

RETVAL=0
case "$1" in
    start)
		$CUBRID/bin/cubrid broker start
        ;;
    stop)
		$CUBRID/bin/cubrid broker stop
        ;;
    status)
		$CUBRID/bin/cubrid broker $@
        ;;
    restart)
		$CUBRID/bin/cubrid broker restart
        ;;
	env)
		env | grep CUBRID
		;;
    *)
        echo $"Usage: $0 {start|stop|status}"
        ;;
esac
exit $RETVAL


