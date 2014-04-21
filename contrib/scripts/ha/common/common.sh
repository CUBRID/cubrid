#!/bin/bash
#################################################################################
# program constatne
#################################################################################
SUCCESS=0
FAIL=1
YES=0
SKIP=1
STDIN=

#################################################################################
# program common function
#################################################################################
function error()
{
	msg=$1
	is_continue=$2
	
	if [ -z $is_continue ]; then
		is_continue=false
	fi
	
	echo -e "\033[38m<< ERROR >> $msg\033[39m"
	
	if ! $is_continue; then
		exit $FAIL
	fi
}