#!/bin/sh

MASTER=d8g674

csql -u dba nbd1@$MASTER -c "revoke select on NBD_COMMENT from tmpuser;"
csql -u dba nbd2@$MASTER -c "revoke select on NBD_COMMENT from tmpuser;"
csql -u dba nbd3@$MASTER -c "revoke select on NBD_COMMENT from tmpuser;"
csql -u dba nbd4@$MASTER -c "revoke select on NBD_COMMENT from tmpuser;"
echo "NBD_COMMENT SELECT REVOKE DONE"
