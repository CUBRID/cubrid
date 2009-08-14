#!/bin/sh

NOW=`date +%Y%m%d%H%M%S`

# See how we were called.
case "$1" in
  start)
	./cpumem-ssh.sh hatest d8g674 >> d8g674.cpumem.$NOW &
	./cpumem-ssh.sh hatest d8g675 >> d8g675.cpumem.$NOW &
	./cpumem-ssh.sh hatest d8g676 >> d8g676.cpumem.$NOW &
	./cpumem-ssh.sh hatest d8g677 >> d8g677.cpumem.$NOW &
	;;
  stop)
	pkill -u hatest cpumem-ssh.sh
	;;
  status)
	ps -ef | grep cpumem-ssh.sh
	;;
  delete)
	rm d8g674.cpumem.*
	rm d8g675.cpumem.*
	rm d8g676.cpumem.*
	rm d8g677.cpumem.*
	;;

  *)
	echo $"Usage: $0 {start|stop|delete}"
   	exit 1
esac

exit 0
