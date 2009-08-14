#!/bin/bash

DBNAME=$1
cd $CUBRID_DATABASES/$DBNAME
cubrid backupdb -r -l 0 -o backup0.out -D ./backup $DBNAME@localhost
cd -
