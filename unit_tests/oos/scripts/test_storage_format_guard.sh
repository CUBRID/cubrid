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
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source "$script_dir/oos_db_common.sh"

db_name="${1:?database name required}"

oos_require_env

# File offset of the stamp in volume 0, pinned by a static_assert in disk_manager.c.
readonly stamp_offset=140

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
    restore_stamp || printf 'cannot restore the storage format stamp of %s\n' "$volume" >&2
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

write_stamp ()
{
  local value="$1" byte encoded='' i
  for i in "${byte_order[@]}"; do
    printf -v byte '\\%03o' "$(( (value >> (8 * i)) & 255 ))"
    encoded+="$byte"
  done
  printf '%b' "$encoded" | dd of="$volume" bs=1 seek="$stamp_offset" count=4 conv=notrunc status=none
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
  local stamp="$4"

  [ "$rc" -ne 0 ] || fail "$what accepted a database with storage format $stamp"
  [ "$rc" -lt 128 ] || fail "$what died (exit $rc) instead of refusing the database"
  grep -Eq "format $stamp, this build $revision|형식 $stamp, 현재 빌드 $revision" "$log" \
    || { cat "$log" >&2; fail "$what did not report the incompatible storage format"; }
}

[ "$db_name" = oosformatdb ] && oos_database_owned "$db_name" || fail "database $db_name is not the owned format guard fixture"
[ -f "$volume" ] || fail "missing volume $volume"
open_database fresh || { cat "$work_dir/fresh.log" >&2; fail 'a database created by this build was refused'; }
# A database created by this build carries its DISK_STORAGE_FORMAT_REVISION, never the pre-stamp 0.
revision=$(read_stamp)
[ -n "$revision" ] && [ "$revision" -ne 0 ] || fail "volume 0 of a database created by this build has stamp '$revision'"

dd if="$volume" of="$saved_stamp" bs=1 skip="$stamp_offset" count=4 status=none
restore_on_exit=true
if [ "$(printf '\1\0\0\0' | od -A n -t u4 | tr -d ' ')" -eq 1 ]; then
  byte_order=(0 1 2 3)
else
  byte_order=(3 2 1 0)
fi

stamps=(0)
if [ "$revision" -gt 1 ]; then
  stamps+=("$((revision - 1))")
fi
stamps+=("$((revision + 1))")

for stamp in "${stamps[@]}"; do
  write_stamp "$stamp"

  rc=0
  open_database mismatch || rc=$?
  expect_refused 'csql -S' "$work_dir/mismatch.log" "$rc" "$stamp"

  rc=0
  cubrid checkdb -S "$db_name" < /dev/null > "$work_dir/checkdb.log" 2>&1 || rc=$?
  expect_refused 'checkdb -S' "$work_dir/checkdb.log" "$rc" "$stamp"

  # xboot_emergency_patch has its own copy of the check.
  rc=0
  cubrid emergency_patchlog "$db_name" < /dev/null > "$work_dir/patchlog.log" 2>&1 || rc=$?
  expect_refused 'emergency_patchlog' "$work_dir/patchlog.log" "$rc" "$stamp"
done

restore_stamp
open_database restored \
  || { cat "$work_dir/restored.log" >&2; fail 'the restored stamp did not make the database usable'; }

# Leave the database refused: the fixture cleanup's deletedb must still remove it.
write_stamp 0
restore_on_exit=false
printf 'PASS: storage format guard refuses incompatible formats and accepts its own\n'
