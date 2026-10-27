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

# Boot must refuse a database whose first volume carries another storage format stamp before it reads anything
# else, and deletedb must still remove such a database. Databases of 11.5 builds before the stamp hold 0 there.
# The database belongs to this test (setup_isolated_db.sh) and no server runs on it, so volume 0 is patched in
# place: zeroing the stamp is how a pre-stamp database looks to this build.
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/oos_db_common.sh"

db_name="${1:?database name required}"

oos_require_env

# DISK_STORAGE_FORMAT_REVISION, stored at FILEIO_PAGE_RESERVED (32 bytes) + offsetof (DISK_VOLUME_HEADER,
# storage_format) (108, pinned by a static_assert in disk_manager.c) in the first page of volume 0.
readonly expected_revision=1
readonly stamp_offset=140
readonly refusal='format 0, this build 1|형식 0, 현재 빌드 1'

volume="$(oos_database_dir "$db_name")/$db_name"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/oos-format-guard.XXXXXX")
saved_stamp="$work_dir/stamp"
restore_on_exit=false

restore_stamp ()
{
  dd if="$saved_stamp" of="$volume" bs=1 seek="$stamp_offset" count=4 conv=notrunc status=none
}

on_exit ()
{
  if [ "$restore_on_exit" = true ]; then
    restore_stamp
  fi
  rm -rf "$work_dir"
}

trap on_exit EXIT

fail ()
{
  printf 'FAIL: %s\n' "$*" >&2
  exit 1
}

read_stamp ()
{
  od -A n -t d4 -j "$stamp_offset" -N 4 "$volume" | tr -d ' '
}

zero_stamp ()
{
  printf '\0\0\0\0' | dd of="$volume" bs=1 seek="$stamp_offset" count=4 conv=notrunc status=none
}

open_database ()
{
  csql -S -u dba -c 'select count (*) from db_root' "$db_name" < /dev/null > "$work_dir/$1.log" 2>&1
}

# Utilities exit with an error code; a signal (an assert's abort) shows as 128 + signal.
expect_refused ()
{
  local what="$1"
  local log="$2"
  local rc="$3"

  [ "$rc" -ne 0 ] || fail "$what accepted a database without the storage format stamp"
  [ "$rc" -lt 128 ] || fail "$what died (exit $rc) instead of refusing the database"
  grep -Eq "$refusal" "$log" || { cat "$log" >&2; fail "$what did not report the incompatible storage format"; }
}

[ -f "$volume" ] || fail "missing volume $volume"
open_database fresh || { cat "$work_dir/fresh.log" >&2; fail 'a database created by this build was refused'; }
[ "$(read_stamp)" = "$expected_revision" ] || fail "volume 0 stamp is $(read_stamp), expected $expected_revision"

dd if="$volume" of="$saved_stamp" bs=1 skip="$stamp_offset" count=4 status=none
restore_on_exit=true
zero_stamp

rc=0
open_database pre_stamp || rc=$?
expect_refused 'csql -S' "$work_dir/pre_stamp.log" "$rc"

rc=0
cubrid checkdb -S "$db_name" < /dev/null > "$work_dir/checkdb.log" 2>&1 || rc=$?
expect_refused 'checkdb -S' "$work_dir/checkdb.log" "$rc"

restore_stamp
restore_on_exit=false
open_database restored \
  || { cat "$work_dir/restored.log" >&2; fail 'the restored stamp did not make the database usable'; }

# Leave the database refused: the fixture cleanup's deletedb must still remove it.
zero_stamp
printf 'PASS: storage format guard refuses a pre-stamp database and accepts its own\n'
