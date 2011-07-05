#!/bin/bash

#---------------------------
# create memcached instances
#---------------------------
memcached -d -p 11211 -m 50 &
memcached -d -p 11212 -m 50 &
sleep 2

# -------
# DO TEST
# -------
echo "Enter 01_operational..."
cd 01_operational
/bin/sh test.sh
echo "Leave 01_operational..."
cd ..

#-----------------------------
#tear down memcached instances
#-----------------------------
pkill memcached
