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

# smoke_gate.sh - B5 PR4 smoke: the utility-channel client-type allowlist.
#
# Boots the installed $CUBRID server and verifies:
#   1. an admin utility still works over the legacy channel (cubrid tranlist)
#   2. the thin csql still works (CAS wire, unaffected by the gate)
#   3. optional: a LEGACY fat csql binary (pre-B5 install) is refused with a
#      handshake error - pass its path as $2 (skipped when absent)
#
# usage: smoke_gate.sh <dbname> [legacy_csql_path]

set -u

DB="${1:?dbname required}"
LEGACY_CSQL="${2:-}"
: "${CUBRID:?CUBRID env required}"

WORK="$(cd "$(dirname "$0")" && pwd)/.smoke_gate_work"

fail() { echo "SMOKE_GATE: FAIL - $*" >&2; exit 1; }

svc_out="$(cubrid service status 2>&1)" || true
if printf '%s\n' "$svc_out" | grep -q '^ Server '; then
  fail "other CUBRID servers are running from this install"
fi

cleanup() {
  cubrid server stop "$DB" >/dev/null 2>&1 || true
  cubrid service stop >/dev/null 2>&1 || true
  rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

rm -rf "$WORK"; mkdir -p "$WORK"

cubrid server start "$DB" >/dev/null 2>&1 || fail "server start"
sleep 1

# 1. admin utility over the legacy channel
cubrid spacedb "$DB" >"$WORK/spacedb" 2>&1 || fail "spacedb over the utility channel: $(cat "$WORK/spacedb")"
grep -qi "space" "$WORK/spacedb" || fail "spacedb output: $(cat "$WORK/spacedb")"
echo "GATE: admin utility (spacedb) admitted on the utility channel"

# 2. thin csql unaffected
"$CUBRID/bin/csql" -u dba "$DB" -c "SELECT 1;" >"$WORK/thin" 2>&1 || fail "thin csql: $(cat "$WORK/thin")"
grep -q "row selected" "$WORK/thin" || fail "thin csql rendering"
echo "GATE: thin csql (CAS wire) admitted"

# 3. legacy fat csql refused (optional)
if [ -n "$LEGACY_CSQL" ] && [ -x "$LEGACY_CSQL" ]; then
  "$LEGACY_CSQL" -u dba "$DB" -c "SELECT 1;" >"$WORK/legacy" 2>&1
  rc=$?
  [ $rc -ne 0 ] || fail "legacy fat csql was admitted (must be refused)"
  echo "GATE: legacy fat csql refused (exit $rc: $(head -2 "$WORK/legacy" | tr '\n' ' '))"
else
  echo "GATE: legacy csql check skipped (no binary given)"
fi

cubrid server stop "$DB" >/dev/null 2>&1 || true
cubrid service stop >/dev/null 2>&1 || true
for _ in $(seq 1 20); do
  pgrep -f "$CUBRID/bin/cub_" >/dev/null 2>&1 || break
  sleep 0.5
done
if pgrep -af "$CUBRID/bin/cub_" 2>/dev/null | grep -q .; then
  fail "processes still running after teardown"
fi

echo "SMOKE_GATE: SUCCESS"
