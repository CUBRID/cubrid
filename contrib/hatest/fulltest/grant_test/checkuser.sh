#!/bin/sh

MASTER=$1
SLAVE=$2

csql -u tmpuser -p tmpuser nbd1@$MASTER -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd2@$MASTER -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd3@$MASTER -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd4@$MASTER -c "select count(*) from nbd_comment"
echo "MASTER DONE"

csql -u tmpuser -p tmpuser nbd1@$SLAVE -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd2@$SLAVE -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd3@$SLAVE -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd4@$SLAVE -c "select count(*) from nbd_comment"
echo "SLAVE DONE"


