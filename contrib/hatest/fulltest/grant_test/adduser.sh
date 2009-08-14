#!/bin/sh

MASTER=$1
SLAVE=$2

csql -u dba nbd1@$MASTER -c "create user tmpuser password 'tmpuser';"
csql -u dba nbd2@$MASTER -c "create user tmpuser password 'tmpuser';"
csql -u dba nbd3@$MASTER -c "create user tmpuser password 'tmpuser';"
csql -u dba nbd4@$MASTER -c "create user tmpuser password 'tmpuser';"

csql -u dba nbd1@$MASTER -c "grant select, insert, update, delete on nbd_comment to tmpuser;"
csql -u dba nbd2@$MASTER -c "grant select, insert, update, delete on nbd_comment to tmpuser;"
csql -u dba nbd3@$MASTER -c "grant select, insert, update, delete on nbd_comment to tmpuser;"
csql -u dba nbd4@$MASTER -c "grant select, insert, update, delete on nbd_comment to tmpuser;"

csql -u tmpuser -p tmpuser nbd1@$MASTER -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd2@$MASTER -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd3@$MASTER -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd4@$MASTER -c "select count(*) from nbd_comment"
echo "MASTER DONE"
sleep 1

csql -u tmpuser -p tmpuser nbd1@$SLAVE -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd2@$SLAVE -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd3@$SLAVE -c "select count(*) from nbd_comment"
csql -u tmpuser -p tmpuser nbd4@$SLAVE -c "select count(*) from nbd_comment"
echo "SLAVE DONE"

echo "ADD USER DONE"

