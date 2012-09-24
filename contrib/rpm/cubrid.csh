#!/bin/csh

setenv CUBRID /opt/cubrid
setenv CUBRID_DATABASES $CUBRID/databases
setenv CUBRID_LANG en_US

if ( ${?LD_LIBRARY_PATH} ) then
	set LIB_PATH = `echo $LD_LIBRARY_PATH | grep -i cubrid`
	if ( $LIB_PATH == "" ) then
		setenv LD_LIBRARY_PATH $CUBRID/lib:$LD_LIBRARY_PATH
	endif
else
	setenv LD_LIBRARY_PATH $CUBRID/lib
endif

set BIN_PATH = `echo $path | grep -i cubrid`
if ( $BIN_PATH == "" ) then
	set path=($CUBRID/{bin,cubridmanager} $path)
endif

