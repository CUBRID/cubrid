#!/bin/sh

MASTER=d8g674

csql -u dba nbd1@$MASTER -c "grant select on NBD_COMMENT to tmpuser;"
csql -u dba nbd2@$MASTER -c "grant select on NBD_COMMENT to tmpuser;"
csql -u dba nbd3@$MASTER -c "grant select on NBD_COMMENT to tmpuser;"
csql -u dba nbd4@$MASTER -c "grant select on NBD_COMMENT to tmpuser;"
echo "NBD_COMMENT SELECT GRANT DONE"
