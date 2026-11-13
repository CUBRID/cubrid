#!/bin/bash
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

# Creates the tables test_oos_update_reuse_server updates in unittestdb, one per case, so that one case's
# garbage never shows in another's OOS file. That binary boots the database in SERVER_MODE without a client and
# cannot run DDL, so the rows that must live in a partition or predate a representation change are inserted here.
# BIT VARYING values are stored as given; a long VARCHAR of repeated characters would be compressed inline.

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/oos_db_common.sh"

oos_require_env

columns="id INT PRIMARY KEY, k INT, c1 BIT VARYING, c2 BIT VARYING"
sql=""
for table in t_reuse_inline t_reuse_mixed t_reuse_vacuum t_reuse_snapshot t_reuse_savepoint t_reuse_abort \
	     t_reuse_repl t_reuse_inplace t_reuse_incr; do
  sql+="DROP TABLE IF EXISTS $table; CREATE TABLE $table ($columns); "
done
sql+="DROP TABLE IF EXISTS t_reuse_part; "
sql+="CREATE TABLE t_reuse_part ($columns) PARTITION BY HASH (id) PARTITIONS 2; "
sql+="INSERT INTO t_reuse_part VALUES (1, 0, REPEAT (X'A1', 6000), REPEAT (X'B2', 6000)); "
sql+="DROP TABLE IF EXISTS t_reuse_repr; "
sql+="CREATE TABLE t_reuse_repr ($columns); "
sql+="INSERT INTO t_reuse_repr VALUES (1, 0, REPEAT (X'A1', 6000), REPEAT (X'B2', 6000)); "
sql+="ALTER TABLE t_reuse_repr ADD COLUMN extra INT DEFAULT 7;"

csql -S -u dba "$OOS_UNITTESTDB_NAME" -c "$sql"
