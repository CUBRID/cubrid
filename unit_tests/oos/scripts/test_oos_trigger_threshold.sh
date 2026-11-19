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

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/oos-threshold.XXXXXX")
trap 'rm -rf "${work_dir:?}"' EXIT
export CUBRID_DATABASES="$work_dir/registry"
export CUBRID_CONF_FILE="$work_dir/cubrid.conf"
export CUBRID_TMP="$work_dir/tmp"
unset CUBRID_OOS_TRIGGER_THRESHOLD
mkdir -p "$CUBRID_DATABASES" "$CUBRID_TMP" "$work_dir/db4k" "$work_dir/db16k"

fail ()
{
  cat "$work_dir/output" >&2
  printf 'FAIL: %s\n' "$*" >&2
  exit 1
}

configure ()
{
  printf '[common]\noos_trigger_threshold=%s\n' "$1" > "$CUBRID_CONF_FILE"
}

query ()
{
  csql -S -u dba -c "$2" "$1" < /dev/null > "$work_dir/output" 2>&1
}

select_db ()
{
  query "$1" 'select count (*) from db_root' || fail "$1 did not open with threshold $2"
}

expect_refused ()
{
  local rc=0
  query "$1" "$2" || rc=$?
  [ "$rc" -ne 0 ] && [ "$rc" -lt 128 ] || fail "$3 returned $rc instead of a normal error"
  grep -qi 'oos_trigger_threshold' "$work_dir/output" || fail "$3 did not name the parameter"
}

configure 3948
cubrid createdb --db-volume-size=20M --log-volume-size=20M --db-page-size=4K \
  -F "$work_dir/db4k" oos_threshold_4k en_US.utf8 < /dev/null > "$work_dir/output" 2>&1 \
  || fail 'createdb rejected threshold 3948 on a 4K database'

for value in 0 988 3948; do
  configure "$value"
  select_db oos_threshold_4k "$value"
done

for value in 1 24 25 987 3949 4096 16385 -1 malformed 8192; do
  configure "$value"
  expect_refused oos_threshold_4k 'select count (*) from db_root' "threshold $value"
done

configure 0
select_db oos_threshold_4k 0
expect_refused oos_threshold_4k "SET SYSTEM PARAMETERS 'oos_trigger_threshold=988'" 'static SET'
select_db oos_threshold_4k 0

cubrid createdb --db-volume-size=20M --log-volume-size=20M --db-page-size=16K \
  -F "$work_dir/db16k" oos_threshold_16k en_US.utf8 < /dev/null > "$work_dir/output" 2>&1 \
  || fail 'createdb failed on a 16K database'
for value in 4060 8192 16236; do
  configure "$value"
  select_db oos_threshold_16k "$value"
done
for value in 4059 16237; do
  configure "$value"
  expect_refused oos_threshold_16k 'select count (*) from db_root' "threshold $value on 16K"
done
printf 'PASS: OOS threshold startup validation\n'
