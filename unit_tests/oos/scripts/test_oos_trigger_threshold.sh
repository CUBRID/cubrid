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

# startup: the accepted range per page size. system: system-class rows and restoredb.
case "${1:-startup}" in
  startup)
    configure 3948
    cubrid createdb --db-volume-size=20M --log-volume-size=20M --db-page-size=4K \
      -F "$work_dir/db4k" oos_threshold_4k en_US.utf8 < /dev/null > "$work_dir/output" 2>&1 \
      || fail 'createdb rejected threshold 3948 on a 4K database'

    for value in 0 988 3948; do
      configure "$value"
      select_db oos_threshold_4k "$value"
    done

    for value in 1 24 25 987 3949 4096 4K 16385 -1 malformed 8192; do
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
    ;;
  system)
    configure 0
    cubrid createdb --db-volume-size=20M --log-volume-size=20M --db-page-size=4K \
      -F "$work_dir/db4k" oos_threshold_4k en_US.utf8 < /dev/null > "$work_dir/output" 2>&1 \
      || fail 'createdb failed on a 4K database'
    # System-class rows keep the automatic target. A _db_serial row whose comment moved to OOS must not grow back in
    # place when the threshold is raised; NEXT VALUE rewrites the row in place on a page that is full again.
    serial_sql="$work_dir/serial.sql"
    : > "$serial_sql"
    for i in $(seq 1 40); do
      comment=$(od -An -vtx1 -N500 /dev/urandom | tr -d ' \n')
    printf "create serial sl%d nocache comment '%s';\nselect sl%d.next_value;\n" "$i" "$comment" "$i" >> "$serial_sql"
    done
    for i in $(seq 1 750); do
    printf 'create serial ss%d nocache;\n' "$i" >> "$serial_sql"
    done
    csql -S -u dba -i "$serial_sql" oos_threshold_4k < /dev/null > "$work_dir/output" 2>&1 \
      || fail 'could not create the serials'
    configure 3948
    : > "$serial_sql"
    for i in $(seq 1 40); do
    printf 'select sl%d.next_value;\n' "$i" >> "$serial_sql"
    done
    csql -S -u dba -i "$serial_sql" oos_threshold_4k < /dev/null > "$work_dir/output" 2>&1 \
      || fail 'NEXT VALUE failed after the threshold was raised'
    [ "$(grep -cE '^ +2 *$' "$work_dir/output")" -eq 40 ] \
      || fail 'NEXT VALUE did not return the second value of every serial'
    cubrid checkdb -S oos_threshold_4k < /dev/null > "$work_dir/output" 2>&1 || fail 'checkdb failed after NEXT VALUE'
    configure 0

    # restoredb lays out no records, so it must not stop on the threshold after it has overwritten the volumes.
    query oos_threshold_4k 'create table t_restore (a int); insert into t_restore values (1)' \
      || fail 'could not create t_restore'
    mkdir -p "$work_dir/backup"
    cubrid backupdb -S -D "$work_dir/backup" oos_threshold_4k < /dev/null > "$work_dir/output" 2>&1 \
      || fail 'backupdb failed'
    query oos_threshold_4k 'insert into t_restore values (2); insert into t_restore values (3)' \
      || fail 'could not insert after the backup'
    configure 3949
    cubrid restoredb -B "$work_dir/backup" oos_threshold_4k < /dev/null > "$work_dir/output" 2>&1 \
      || fail 'restoredb stopped on an out-of-range threshold'
    expect_refused oos_threshold_4k 'select count (*) from t_restore' 'threshold 3949 after restoredb'
    configure 0
    query oos_threshold_4k 'select count (*) from t_restore' || fail 'could not read t_restore after restoredb'
    grep -qE '^ +3 *$' "$work_dir/output" || fail 'restoredb did not restore the rows committed after the backup'
    printf 'PASS: OOS threshold system-class and restore checks\n'
    ;;
  *)
    printf 'usage: %s [startup|system]\n' "$0" >&2
    exit 2
    ;;
esac
