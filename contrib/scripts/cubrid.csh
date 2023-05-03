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

setenv CUBRID /opt/cubrid
setenv CUBRID_DATABASES $CUBRID/databases

if ( ${?LD_LIBRARY_PATH} ) then
	setenv LD_LIBRARY_PATH $CUBRID/lib:$CUBRID/cci/lib:$LD_LIBRARY_PATH
else
	setenv LD_LIBRARY_PATH $CUBRID/lib:$CUBRID/cci/lib
endif

set path=($CUBRID/bin /usr/sbin $path)
set curses_lib=

setenv SHLIB_PATH $LD_LIBRARY_PATH
setenv LIBPATH $LD_LIBRARY_PATH
