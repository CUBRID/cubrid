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

# Creates a database owned by one OOS test binary, always from scratch. Unlike the shared
# unittestdb fixture this never preserves an existing database: the binaries that need an
# isolated database also need to know that its disk layout is the one this build writes, and
# they are the only owner, so recreating costs nothing but a createdb.

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/oos_db_common.sh"

db_name="${1:?database name required}"

oos_require_env

if [ "$db_name" = "$OOS_UNITTESTDB_NAME" ]; then
  printf 'refusing to recreate the shared fixture database %s\n' "$db_name" >&2
  exit 1
fi

db_dir=$(oos_database_dir "$db_name")

cubrid server stop "$db_name" 2>/dev/null || true
if oos_database_exists "$db_name"; then
  cubrid deletedb "$db_name"
fi

mkdir -p "$CUBRID_DATABASES"
mkdir -p "$db_dir"
cubrid createdb --db-volume-size=20M --log-volume-size=20M "$db_name" en_US.utf8 -F "$db_dir"
