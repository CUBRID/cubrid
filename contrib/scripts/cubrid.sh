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
export CUBRID CUBRID_DATABASES

LD_LIBRARY_PATH=$CUBRID/lib:$CUBRID/cci/lib:$LD_LIBRARY_PATH
PATH=$CUBRID/bin:/usr/sbin:$PATH
export LD_LIBRARY_PATH PATH

#
#  tuning setting for glib memory library
#  
#  For more information on environment variables, see https://www.gnu.org/software/libc/manual/html_node/Malloc-Tunable-Parameters.html.
#  (Notice) To using the environment variables below, you should to remove comment them and add them to the export statement.
#
#MALLOC_MMAP_MAX_=65536            # default : 65536
#MALLOC_MMAP_THRESHOLD_=131072     # default : 131072 (128K)
MALLOC_TRIM_THRESHOLD_=0           # default : 131072 (128K)
#MALLOC_ARENA_MAX=                 # default : core * 8
export MALLOC_TRIM_THRESHOLD_

#
# preloading library for another memory library
#
#LD_PRELOAD=/usr/lib64/jemalloc.so.1
#export LD_PRELOAD
