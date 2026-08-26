#!/bin/bash
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

# smoke.sh — track-A standard in-process smoke (workspace #122 D5).
#
# Boots cub_server with the env-gated in-process tracer so one server thread
# compiles, executes and fetches the given SQL in the server address space
# (0-hop), then checks the tracer output for SUCCESS.  Cases accumulate here
# as later stages fold more of the client half into the server.
#
# Usage: smoke.sh <dbname> [sql...]
#   Requires $CUBRID installed and <dbname> already created; starts and stops
#   the server itself, so <dbname> must not be running.

set -eu

DB="${1:?usage: smoke.sh <dbname> [sql...]}"
shift
if [ $# -eq 0 ]; then
  set -- "SELECT 1" "SELECT COUNT(*) FROM db_class"
fi

# This script restarts cub_master (see below), which would take down every
# other database on the host — refuse to run if any server is active.
active="$(cubrid server status 2>/dev/null | grep -c '^ Server' || true)"
if [ "$active" -gt 0 ]; then
  echo "ABORT: other CUBRID servers are running on this host; smoke.sh restarts cub_master and would stop them." >&2
  cubrid server status >&2 || true
  exit 2
fi

fail=0
for sql in "$@"; do
  out="$(pwd)/m0_smoke.$$.out"
  rm -f "$out"

  # cub_master is often already running as a long-lived daemon with an
  # environment fixed at its own boot time, so CUBRID_M0_TRACER_* set here
  # would never reach it (and therefore never reach the cub_server it
  # forks). `cubrid server start` only re-spawns master when none is
  # running (see util_service.c: css_does_master_exist()), so stop it here
  # to force a fresh master that inherits this iteration's env.
  cubrid service stop >/dev/null 2>&1 || true

  # `|| true`: a failed start must fall through to the poll below and be
  # reported as FAIL, not abort the whole run via set -e
  CUBRID_M0_TRACER_SQL="$sql" CUBRID_M0_TRACER_OUT="$out" \
    cubrid server start "$DB" >/dev/null 2>&1 || true

  ok=0
  for _ in $(seq 1 60); do
    if [ -f "$out" ] && grep -q "M0_TRACER: SUCCESS" "$out"; then
      ok=1
      break
    fi
    sleep 1
  done

  cubrid server stop "$DB" >/dev/null 2>&1 || true

  echo "--- [$sql]"
  [ -f "$out" ] && cat "$out"
  if [ $ok -eq 1 ]; then
    echo "PASS: $sql"
  else
    echo "FAIL: $sql (no SUCCESS in tracer output)"
    fail=1
  fi
  rm -f "$out"
done

exit $fail
