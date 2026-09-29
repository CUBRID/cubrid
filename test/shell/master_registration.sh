#!/bin/bash
#
#  Copyright 2024 CUBRID Corporation
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

#
# master_registration.sh - CBRD-27511 regression/security shell test.
#
# The fix restricts the cub_master SERVER_REQUEST_FROM_SERVER registration path
# to same-host peers and confines the revival exec_path to <CUBRID>/bin. This
# test proves the fix did NOT break normal operation:
#
#   1. a co-located cub_server still registers with cub_master (it connects over
#      the resolved local host name, whose source address the new same-host check
#      must accept), and
#   2. auto_restart_server still revives a server after its process dies, i.e. the
#      registered exec_path (<CUBRID>/bin/cub_server) passes the whitelist.
#
# Requires a working CUBRID installation pointed to by $CUBRID.
# Exit codes: 0 = pass, 1 = failure, 77 = skipped (no CUBRID environment).
#

set -u

if [ -z "${CUBRID:-}" ] || [ ! -x "$CUBRID/bin/cubrid" ]; then
  echo "[SKIP] \$CUBRID is not set to a CUBRID installation"
  exit 77
fi

export PATH="$CUBRID/bin:$PATH"
export LD_LIBRARY_PATH="$CUBRID/lib:${LD_LIBRARY_PATH:-}"

# Do not disturb a CUBRID service that is already running on this host: the test
# starts (and later stops) its own master/server, and CUBRID_DATABASES does not
# isolate the shared master. If a master is already up, skip rather than risk
# stopping someone else's server.
if pgrep -x cub_master >/dev/null 2>&1; then
  echo "[SKIP] a CUBRID master is already running on this host; refusing to disturb a shared service"
  exit 77
fi

DB="cbrd27511_$$"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/cbrd27511.XXXXXX")"
export CUBRID_DATABASES="$WORK"
FAIL=0
STARTED_SERVICE=0

pass () { echo "[PASS] $1"; }
fail () { echo "[FAIL] $1"; FAIL=1; }

cleanup () {
  # stop only our own database server
  cubrid server stop "$DB"  >/dev/null 2>&1
  cubrid deletedb   "$DB"   >/dev/null 2>&1
  # stop the master only if this test started it (guarded above so that no
  # pre-existing service is ever touched)
  if [ "$STARTED_SERVICE" -eq 1 ]; then
    cubrid service stop      >/dev/null 2>&1
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT

# wait until 'cubrid server status' lists $DB (registered with the master)
wait_registered () {
  local tries=$1 i=0
  while [ "$i" -lt "$tries" ]; do
    if cubrid server status 2>/dev/null | grep -qw "$DB"; then
      return 0
    fi
    sleep 1
    i=$((i + 1))
  done
  return 1
}

server_pid () {
  pgrep -f "cub_server[[:space:]]+$DB([[:space:]]|\$)" | head -1
}

echo "=== CBRD-27511 master registration regression test ==="
echo "CUBRID=$CUBRID"
echo "DB=$DB  CUBRID_DATABASES=$CUBRID_DATABASES"

cd "$WORK" || { echo "cannot cd to work dir"; exit 1; }

# --- create a throwaway database -------------------------------------------
if cubrid createdb --db-volume-size=20M --log-volume-size=20M "$DB" en_US >/dev/null 2>&1; then
  pass "createdb"
else
  fail "createdb"
  exit 1
fi

# --- 1) legitimate local registration --------------------------------------
# starting a server auto-starts the master; remember that we own the service now
STARTED_SERVICE=1
cubrid server start "$DB" >/dev/null 2>&1
if wait_registered 60; then
  pass "cub_server registered with cub_master over the resolved host name (issue-2 fix)"
else
  fail "cub_server did NOT register with cub_master (local registration was rejected)"
  cubrid server status 2>&1 | sed 's/^/    /'
fi

# --- 2) auto-restart / revive after the server process dies -----------------
old_pid="$(server_pid)"
if [ -n "$old_pid" ]; then
  kill -9 "$old_pid" 2>/dev/null
  # the master should detect the dropped connection and revive the server,
  # execv()-ing the registered <CUBRID>/bin/cub_server (must pass the whitelist)
  revived=0
  for _ in $(seq 1 40); do
    sleep 1
    new_pid="$(server_pid)"
    if [ -n "$new_pid" ] && [ "$new_pid" != "$old_pid" ]; then
      revived=1
      break
    fi
  done
  if [ "$revived" -eq 1 ] && wait_registered 15; then
    pass "cub_master revived the server after it was killed (whitelisted exec_path)"
  else
    fail "cub_master did NOT revive the server (auto_restart / exec_path whitelist)"
  fi
else
  fail "could not locate the running cub_server process to test revival"
fi

echo "==========================================================="
if [ "$FAIL" -eq 0 ]; then
  echo "RESULT: PASS"
  exit 0
fi
echo "RESULT: FAIL"
exit 1
