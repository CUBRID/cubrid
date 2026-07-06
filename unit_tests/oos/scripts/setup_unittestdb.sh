#!/usr/bin/env bash
#
#  Copyright 2026 CUBRID Corporation
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
source "$script_dir/oos_unittestdb_common.sh"

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

if oos_database_exists; then
  exit 0
fi

mkdir -p "$CUBRID_DATABASES"
cp "$conf" "$backup"
oos_append_fixture_section "$conf"

mkdir -p "$(oos_unittestdb_dir)"
cubrid createdb --db-volume-size=20M --log-volume-size=20M "$OOS_UNITTESTDB_NAME" en_US.utf8 \
  -F "$(oos_unittestdb_dir)"
