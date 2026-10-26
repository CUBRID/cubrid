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

# Exercise the real fixture scripts with only the native utility boundary replaced.
set -euo pipefail

script_dir=${OOS_FIXTURE_SCRIPTS:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}
sandbox=$(mktemp -d "${TMPDIR:-/tmp}/oos-fixture-lifecycle.XXXXXX")
trap 'rm -rf "$sandbox"' EXIT

fail ()
{
  printf 'FAIL: %s\n' "$*" >&2
  exit 1
}

new_case ()
{
  local name="$1"
  export OOS_FIXTURE_SANDBOX="$sandbox/$name"
  export CUBRID="$OOS_FIXTURE_SANDBOX/install"
  export CUBRID_DATABASES="$OOS_FIXTURE_SANDBOX/registry"
  mkdir -p "$CUBRID/bin" "$CUBRID/conf" "$CUBRID_DATABASES"
  printf '[common]\nmax_clients=100\n' > "$CUBRID/conf/cubrid.conf"
  cp "$CUBRID/conf/cubrid.conf" "$OOS_FIXTURE_SANDBOX/original.conf"
  printf 'otherdb /unrelated/data localhost /unrelated/log\n' > "$CUBRID_DATABASES/databases.txt"
  : > "$OOS_FIXTURE_SANDBOX/commands"
  cat > "$CUBRID/bin/cubrid" <<'CLI'
#!/bin/bash
set -euo pipefail
[ "$CUBRID_DATABASES" = "$OOS_FIXTURE_SANDBOX/registry" ] || exit 99
printf '%s\n' "$*" >> "$OOS_FIXTURE_SANDBOX/commands"
case "$1" in
  createdb)
    # Only the shared fixture is created with its own configuration section.
    if [ "$4" = unittestdb ]; then
      [ "$(awk '$0 == "vacuum_log_block_pages=4" { count++ } END { print count + 0 }' \
        "$CUBRID/conf/cubrid.conf")" -eq 1 ] || exit 96
    fi
    name="$4"
    shift 4
    while [ "$1" != -F ]; do shift; done
    dir="$2"
    mkdir -p "$dir"
    printf 'fixture database bytes\n' > "$dir/$name"
    printf '%s %s localhost %s\n' "$name" "$dir" "$dir" >> "$CUBRID_DATABASES/databases.txt"
    [ ! -f "$OOS_FIXTURE_SANDBOX/fail-create" ] || exit 23
    ;;
  server)
    [ "$2" = stop ] || exit 98
    exit 17
    ;;
  deletedb)
    [ ! -f "$OOS_FIXTURE_SANDBOX/fail-delete" ] || exit 29
    name="$2"
    dir=$(awk -v db="$name" '$1 == db { print $2 }' "$CUBRID_DATABASES/databases.txt")
    rm -f "$dir/$name"
    awk -v db="$name" '$1 != db' "$CUBRID_DATABASES/databases.txt" > "$CUBRID_DATABASES/remaining"
    mv "$CUBRID_DATABASES/remaining" "$CUBRID_DATABASES/databases.txt"
    ;;
  *) exit 97 ;;
esac
CLI
  chmod +x "$CUBRID/bin/cubrid"
}

fixture ()
{
  local script="$1"
  shift
  PATH="$CUBRID/bin:$PATH" bash "$script_dir/$script" "$@" > "$OOS_FIXTURE_SANDBOX/$script.log" 2>&1
}

check_conf ()
{
  cmp -s "$CUBRID/conf/cubrid.conf" "$OOS_FIXTURE_SANDBOX/original.conf" || fail 'configuration was not restored'
}

check_unrelated_registration ()
{
  [ "$(cat "$CUBRID_DATABASES/databases.txt")" = 'otherdb /unrelated/data localhost /unrelated/log' ] \
    || fail 'cleanup changed an unrelated registration'
}

test_preexisting ()
{
  new_case preexisting
  local dir="$CUBRID_DATABASES/unittestdb"
  mkdir -p "$dir"
  printf 'developer database bytes\n' > "$dir/unittestdb"
  cp "$dir/unittestdb" "$OOS_FIXTURE_SANDBOX/original.data"
  printf 'unittestdb %s localhost %s\n' "$dir" "$dir" >> "$CUBRID_DATABASES/databases.txt"
  cp "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/original.registry"

  fixture setup_unittestdb.sh || fail 'setup rejected a preexisting registration'
  fixture cleanup_unittestdb.sh || fail 'cleanup rejected an unowned database'
  [ ! -s "$OOS_FIXTURE_SANDBOX/commands" ] || fail 'preexisting database received a native lifecycle command'
  cmp -s "$dir/unittestdb" "$OOS_FIXTURE_SANDBOX/original.data" || fail 'preexisting data changed'
  cmp -s "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/original.registry" || fail 'registry changed'
  check_conf
}

test_replaced_volume ()
{
  new_case replaced-volume
  local dir="$CUBRID_DATABASES/unittestdb"
  fixture setup_unittestdb.sh || fail 'fixture creation failed'
  # Keep the old volume allocated so the replacement cannot reuse its inode.
  mv "$dir/unittestdb" "$OOS_FIXTURE_SANDBOX/old-volume"
  printf 'replacement database bytes\n' > "$dir/unittestdb"
  cp "$dir/unittestdb" "$OOS_FIXTURE_SANDBOX/replacement.data"
  cp "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/replacement.registry"
  : > "$OOS_FIXTURE_SANDBOX/commands"

  fixture cleanup_unittestdb.sh || fail 'cleanup rejected a replacement database'
  [ ! -s "$OOS_FIXTURE_SANDBOX/commands" ] || fail 'replacement database received a native lifecycle command'
  cmp -s "$dir/unittestdb" "$OOS_FIXTURE_SANDBOX/replacement.data" || fail 'replacement data changed'
  cmp -s "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/replacement.registry" \
    || fail 'replacement registry changed'
}

test_owned_retry ()
{
  new_case owned-retry
  local volume="$CUBRID_DATABASES/unittestdb/unittestdb"
  fixture setup_unittestdb.sh || fail 'fixture creation failed'
  check_conf
  # Normal database writes and a repeated setup retain the fixture's ownership.
  printf 'ordinary database update\n' >> "$volume"
  fixture setup_unittestdb.sh || fail 'setup retry failed'
  fixture cleanup_unittestdb.sh || fail 'owned fixture cleanup failed'
  [ ! -e "$volume" ] || fail 'owned database survived successful deletion'
  check_unrelated_registration
  cp "$OOS_FIXTURE_SANDBOX/commands" "$OOS_FIXTURE_SANDBOX/first-cleanup.commands"
  fixture cleanup_unittestdb.sh || fail 'repeated cleanup failed'
  cmp -s "$OOS_FIXTURE_SANDBOX/commands" "$OOS_FIXTURE_SANDBOX/first-cleanup.commands" \
    || fail 'repeated cleanup issued another lifecycle command'
  [ "$(wc -l < "$OOS_FIXTURE_SANDBOX/commands")" -eq 3 ] || fail 'setup retry recreated the database'
  check_conf
}

test_changed_registration ()
{
  new_case changed-registration
  local volume="$CUBRID_DATABASES/unittestdb/unittestdb"
  fixture setup_unittestdb.sh || fail 'fixture creation failed'
  # Preserve the volume identity while changing the registration's host identity.
  awk '$1 == "unittestdb" { $3 = "replacement-host" } { print }' \
    "$CUBRID_DATABASES/databases.txt" > "$CUBRID_DATABASES/replacement"
  mv "$CUBRID_DATABASES/replacement" "$CUBRID_DATABASES/databases.txt"
  cp "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/replacement.registry"
  cp "$volume" "$OOS_FIXTURE_SANDBOX/replacement.data"
  : > "$OOS_FIXTURE_SANDBOX/commands"

  fixture cleanup_unittestdb.sh || fail 'cleanup rejected a changed registration'
  [ ! -s "$OOS_FIXTURE_SANDBOX/commands" ] || fail 'changed registration received a native lifecycle command'
  cmp -s "$volume" "$OOS_FIXTURE_SANDBOX/replacement.data" || fail 'changed registration lost data'
  cmp -s "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/replacement.registry" \
    || fail 'changed registration was modified'
  check_conf
}

test_failed_creation ()
{
  new_case failed-creation
  fixture setup_unittestdb.sh || fail 'initial fixture creation failed'
  # Simulate a vanished registration with a stale receipt and leftover volume. A failed
  # create may leave exactly the old row and inode behind; that is still not ownership.
  awk '$1 != "unittestdb"' "$CUBRID_DATABASES/databases.txt" > "$CUBRID_DATABASES/remaining"
  mv "$CUBRID_DATABASES/remaining" "$CUBRID_DATABASES/databases.txt"
  touch "$OOS_FIXTURE_SANDBOX/fail-create"
  if fixture setup_unittestdb.sh; then
    fail 'injected creation failure was hidden'
  else
    [ "$?" -eq 23 ] || fail 'native creation failure status changed'
  fi
  check_conf
  cp "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/failed.registry"
  : > "$OOS_FIXTURE_SANDBOX/commands"

  fixture cleanup_unittestdb.sh || fail 'cleanup rejected a failed creation'
  [ ! -s "$OOS_FIXTURE_SANDBOX/commands" ] || fail 'failed creation granted lifecycle authority'
  [ -f "$CUBRID_DATABASES/unittestdb/unittestdb" ] || fail 'failed creation lost partial data'
  cmp -s "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/failed.registry" \
    || fail 'failed creation lost its registration'
}

test_delete_retry ()
{
  new_case delete-retry
  local volume="$CUBRID_DATABASES/unittestdb/unittestdb"
  fixture setup_unittestdb.sh || fail 'fixture creation failed'
  cp "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/owned.registry"
  cp "$volume" "$OOS_FIXTURE_SANDBOX/owned.data"
  touch "$OOS_FIXTURE_SANDBOX/fail-delete"
  if fixture cleanup_unittestdb.sh; then
    fail 'injected deletion failure was hidden'
  else
    [ "$?" -eq 29 ] || fail 'native deletion failure status changed'
  fi
  cmp -s "$volume" "$OOS_FIXTURE_SANDBOX/owned.data" || fail 'failed deletion lost data'
  cmp -s "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/owned.registry" \
    || fail 'failed deletion lost its registration'
  check_conf

  rm "$OOS_FIXTURE_SANDBOX/fail-delete"
  fixture cleanup_unittestdb.sh || fail 'deletion retry failed'
  [ ! -e "$volume" ] || fail 'deletion retry lost ownership authority'
  check_unrelated_registration
  cp "$OOS_FIXTURE_SANDBOX/commands" "$OOS_FIXTURE_SANDBOX/retried.commands"
  fixture cleanup_unittestdb.sh || fail 'cleanup after deletion retry failed'
  cmp -s "$OOS_FIXTURE_SANDBOX/commands" "$OOS_FIXTURE_SANDBOX/retried.commands" \
    || fail 'cleanup after deletion retry issued another lifecycle command'
}

# The isolated fixtures recreate their database on every setup, but only one they created.
test_isolated_preexisting ()
{
  new_case isolated-preexisting
  local dir="$CUBRID_DATABASES/oosnologdb"
  mkdir -p "$dir"
  printf 'developer database bytes\n' > "$dir/oosnologdb"
  cp "$dir/oosnologdb" "$OOS_FIXTURE_SANDBOX/original.data"
  printf 'oosnologdb %s localhost %s\n' "$dir" "$dir" >> "$CUBRID_DATABASES/databases.txt"
  cp "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/original.registry"

  if fixture setup_isolated_db.sh oosnologdb; then
    fail 'isolated setup replaced an unowned database'
  fi
  grep -q oosnologdb "$OOS_FIXTURE_SANDBOX/setup_isolated_db.sh.log" \
    || fail 'isolated setup refusal did not name the database'
  fixture cleanup_isolated_db.sh oosnologdb || fail 'isolated cleanup rejected an unowned database'
  [ ! -s "$OOS_FIXTURE_SANDBOX/commands" ] || fail 'unowned database received a native lifecycle command'
  cmp -s "$dir/oosnologdb" "$OOS_FIXTURE_SANDBOX/original.data" || fail 'unowned data changed'
  cmp -s "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/original.registry" || fail 'registry changed'
  check_conf
}

test_isolated_owned_recreate ()
{
  new_case isolated-owned-recreate
  local volume="$CUBRID_DATABASES/oosrecoverydb/oosrecoverydb"
  fixture setup_isolated_db.sh oosrecoverydb || fail 'isolated creation failed'
  printf 'left by the previous run\n' >> "$volume"
  fixture setup_isolated_db.sh oosrecoverydb || fail 'isolated setup did not recreate its own database'
  [ "$(cat "$volume")" = 'fixture database bytes' ] || fail 'isolated setup kept the previous run'
  fixture cleanup_isolated_db.sh oosrecoverydb || fail 'owned isolated cleanup failed'
  [ ! -e "$volume" ] || fail 'owned isolated database survived successful deletion'
  [ ! -e "$CUBRID_DATABASES/.oos-oosrecoverydb.owner" ] \
    || fail 'isolated cleanup kept the receipt of a deleted database'
  check_unrelated_registration
  cp "$OOS_FIXTURE_SANDBOX/commands" "$OOS_FIXTURE_SANDBOX/first-cleanup.commands"
  fixture cleanup_isolated_db.sh oosrecoverydb || fail 'repeated isolated cleanup failed'
  cmp -s "$OOS_FIXTURE_SANDBOX/commands" "$OOS_FIXTURE_SANDBOX/first-cleanup.commands" \
    || fail 'repeated isolated cleanup issued another lifecycle command'
  check_conf
}

test_isolated_replaced_volume ()
{
  new_case isolated-replaced-volume
  local dir="$CUBRID_DATABASES/oosnologdb"
  fixture setup_isolated_db.sh oosnologdb || fail 'isolated creation failed'
  # Keep the old volume allocated so the replacement cannot reuse its inode.
  mv "$dir/oosnologdb" "$OOS_FIXTURE_SANDBOX/old-volume"
  printf 'replacement database bytes\n' > "$dir/oosnologdb"
  cp "$dir/oosnologdb" "$OOS_FIXTURE_SANDBOX/replacement.data"
  cp "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/replacement.registry"
  : > "$OOS_FIXTURE_SANDBOX/commands"

  fixture cleanup_isolated_db.sh oosnologdb || fail 'isolated cleanup rejected a replacement database'
  if fixture setup_isolated_db.sh oosnologdb; then
    fail 'isolated setup replaced a replacement database'
  fi
  [ ! -s "$OOS_FIXTURE_SANDBOX/commands" ] || fail 'replacement database received a native lifecycle command'
  cmp -s "$dir/oosnologdb" "$OOS_FIXTURE_SANDBOX/replacement.data" || fail 'replacement data changed'
  cmp -s "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/replacement.registry" \
    || fail 'replacement registry changed'
  check_conf
}

test_isolated_failed_creation ()
{
  new_case isolated-failed-creation
  fixture setup_isolated_db.sh oosnologdb || fail 'initial isolated creation failed'
  # As for the shared fixture: a vanished registration leaves a stale receipt, and a failed create
  # may leave exactly the old row and inode behind. That is still not ownership.
  awk '$1 != "oosnologdb"' "$CUBRID_DATABASES/databases.txt" > "$CUBRID_DATABASES/remaining"
  mv "$CUBRID_DATABASES/remaining" "$CUBRID_DATABASES/databases.txt"
  touch "$OOS_FIXTURE_SANDBOX/fail-create"
  if fixture setup_isolated_db.sh oosnologdb; then
    fail 'injected isolated creation failure was hidden'
  else
    [ "$?" -eq 23 ] || fail 'native isolated creation failure status changed'
  fi
  cp "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/failed.registry"
  : > "$OOS_FIXTURE_SANDBOX/commands"

  fixture cleanup_isolated_db.sh oosnologdb || fail 'isolated cleanup rejected a failed creation'
  [ ! -s "$OOS_FIXTURE_SANDBOX/commands" ] || fail 'failed isolated creation granted lifecycle authority'
  [ -f "$CUBRID_DATABASES/oosnologdb/oosnologdb" ] || fail 'failed isolated creation lost partial data'
  cmp -s "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/failed.registry" \
    || fail 'failed isolated creation lost its registration'
  check_conf
}

test_isolated_delete_retry ()
{
  new_case isolated-delete-retry
  local volume="$CUBRID_DATABASES/oosnologdb/oosnologdb"
  fixture setup_isolated_db.sh oosnologdb || fail 'isolated creation failed'
  cp "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/owned.registry"
  cp "$volume" "$OOS_FIXTURE_SANDBOX/owned.data"
  touch "$OOS_FIXTURE_SANDBOX/fail-delete"
  if fixture cleanup_isolated_db.sh oosnologdb; then
    fail 'injected isolated deletion failure was hidden'
  else
    [ "$?" -eq 29 ] || fail 'native isolated deletion failure status changed'
  fi
  cmp -s "$volume" "$OOS_FIXTURE_SANDBOX/owned.data" || fail 'failed isolated deletion lost data'
  cmp -s "$CUBRID_DATABASES/databases.txt" "$OOS_FIXTURE_SANDBOX/owned.registry" \
    || fail 'failed isolated deletion lost its registration'

  rm "$OOS_FIXTURE_SANDBOX/fail-delete"
  fixture cleanup_isolated_db.sh oosnologdb || fail 'isolated deletion retry failed'
  [ ! -e "$volume" ] || fail 'isolated deletion retry lost ownership authority'
  [ ! -e "$CUBRID_DATABASES/.oos-oosnologdb.owner" ] \
    || fail 'isolated cleanup kept the receipt of a deleted database'
  check_unrelated_registration
  check_conf
}

if [ "$#" -eq 0 ]; then
  set -- preexisting replaced_volume owned_retry changed_registration failed_creation delete_retry \
    isolated_preexisting isolated_owned_recreate isolated_replaced_volume isolated_failed_creation \
    isolated_delete_retry
fi
for test_name in "$@"; do
  case "$test_name" in
    preexisting|replaced_volume|owned_retry|changed_registration|failed_creation|delete_retry) ;;
    isolated_preexisting|isolated_owned_recreate|isolated_replaced_volume|isolated_failed_creation) ;;
    isolated_delete_retry) ;;
    *) fail "unknown case: $test_name" ;;
  esac
  "test_$test_name"
  printf 'PASS: %s\n' "$test_name"
done
