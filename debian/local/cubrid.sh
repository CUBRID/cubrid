#!/bin/sh
#
#
#  Copyright 2016 CUBRID Corporation
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#

CUBRID=/opt/cubrid
CUBRID_DATABASES=$CUBRID/databases

LIB_PATH=`echo $LD_LIBRARY_PATH | grep -i cubrid`
if [ "$LIB_PATH" = "" ];
then
	LD_LIBRARY_PATH=$CUBRID/lib:$CUBRID/cci/lib:$LD_LIBRARY_PATH
fi

BIN_PATH=`echo $PATH | grep -i cubrid`
if [ "$BIN_PATH" = "" ];
then
	PATH=$CUBRID/bin:$CUBRID/cubridmanager:$PATH
fi

export CUBRID CUBRID_DATABASES LD_LIBRARY_PATH PATH
