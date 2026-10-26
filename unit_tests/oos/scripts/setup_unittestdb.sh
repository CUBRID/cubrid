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

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/oos_db_common.sh"

oos_require_env

conf=$(oos_cubrid_conf)
backup="$CUBRID_DATABASES/oos_unittestdb_cubrid.conf.backup.$$"

restore_conf ()
{
  if [ -f "$backup" ]; then
    cp "$backup" "$conf"
    rm -f "$backup"
  fi
}

trap restore_conf EXIT

if [ ! -f "$conf" ]; then
  printf 'missing cubrid.conf: %s\n' "$conf" >&2
  exit 1
fi

oos_remove_fixture_sections "$conf"

if oos_database_exists "$OOS_UNITTESTDB_NAME"; then
  exit 0
fi

mkdir -p "$CUBRID_DATABASES"
receipt=$(oos_unittestdb_receipt)
# A receipt for a vanished database cannot authorize a new, possibly failed creation.
rm -f "$receipt"
cp "$conf" "$backup"
oos_append_fixture_section "$conf"

mkdir -p "$(oos_database_dir "$OOS_UNITTESTDB_NAME")"
cubrid createdb --db-volume-size=20M --log-volume-size=20M "$OOS_UNITTESTDB_NAME" en_US.utf8 \
  -F "$(oos_database_dir "$OOS_UNITTESTDB_NAME")"

receipt_tmp=$(mktemp "${receipt}.XXXXXX")
if ! oos_unittestdb_identity > "$receipt_tmp"; then
  rm -f "$receipt_tmp"
  printf 'cannot identify created fixture database %s\n' "$OOS_UNITTESTDB_NAME" >&2
  exit 1
fi
mv "$receipt_tmp" "$receipt"
