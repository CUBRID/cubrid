#!/bin/bash
#
#  Copyright 2008 Search Solution Corporation
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

if [ -z "$CUBRID" ]; then
	exit 1
fi

# mkdir -p $CUBRID/demo/method
# cubrid_esql -u $CUBRID/demo/method/method_factorial.ec
# gcc -c $CUBRID/demo/method/method_factorial.c -I$CUBRID/include -fPIC
# gcc -o $CUBRID/demo/method/method_factorial.so $CUBRID/demo/method/method_factorial.o -shared -L$CUBRID/lib -lcubridesql -lm

cubrid createdb --db-volume-size=100M --log-volume-size=100M demodb en_US.utf8  > /dev/null 2>&1
cubrid loaddb -u dba -s $CUBRID/demo/demodb_schema -d $CUBRID/demo/demodb_objects demodb > /dev/null 2>&1
