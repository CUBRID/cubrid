#!/bin/bash

ARG1=$1
RUNPATH=benchmark/run
PROPERTIES="read_n_comment_nbd1_lv1.properties \
            read_n_comment_nbd2_lv1.properties \
            read_n_comment_nbd3_lv1.properties \
            read_n_comment_nbd4_lv1.properties \
            read_only_nbd1_lv1.properties \
            read_only_nbd2_lv1.properties \
            read_only_nbd3_lv1.properties \
            read_only_nbd4_lv1.properties"

starttime=`date`
echo $starttime
if [ -z $ARG1 ]; then
  for P in $PROPERTIES; do
    ./run.sh $P &
    test -z "$PIDLIST" && PIDLIST="$!" || PIDLIST="$PIDLIST $!"
    sleep 1
  done
  trap 'echo Kill $PIDLIST;ps -o pid= --ppid "$PIDLIST"|xargs kill' INT TERM
  wait $PIDLIST
  trap - INT TERM
elif [ $ARG1 = 'c' ]; then
  #`ps -ef | grep java | awk '{print \$2}' | xargs kill -9`
  rm -rf $RUNPATH/*
fi
endtime=`date`
echo "Start : $starttime, End: $endtime"
