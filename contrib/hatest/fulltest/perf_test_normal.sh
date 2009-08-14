#!/bin/sh

THISDIR=`dirname $0`
. $THISDIR/hatest.env

PERF_TEST_DIR="$THISDIR/perf_test"

cp -rf $PERF_TEST_DIR/* $NBENCH_HOME/

$THISDIR/prev-test.sh
sleep 120
(cd $NBENCH_HOME;./runall_perf.sh)
sleep 10
$THISDIR/post-test.sh
