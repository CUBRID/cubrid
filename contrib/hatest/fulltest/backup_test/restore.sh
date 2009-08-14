#!/bin/bash

DBNAME=$1
cd $CUBRID_DATABASES/$DBNAME
cubrid restoredb -l 0 -o restore0.out -d backuptime -B ./backup $DBNAME
cd -
