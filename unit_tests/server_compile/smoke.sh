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

# smoke.sh — standard in-process smoke for the compiler-fold track.
#
# Boots cub_server with the env-gated in-process tracer so one server thread
# compiles, executes and fetches the given SQL in the server address space
# (0-hop), then checks the tracer output for SUCCESS.  Cases accumulate here
# as later stages fold more of the client half into the server.
#
# Usage: smoke.sh <dbname> [sql...]
#   Requires $CUBRID installed and <dbname> already created; starts and stops
#   the server itself, so <dbname> must not be running.
#
# A case may carry trailing directives, stripped before the SQL reaches the
# tracer:
#   @BIND=v1,v2   comma-separated integers bound as host variables
#                 (exported as CUBRID_M0_TRACER_BIND)
#   @EXPECT=str   additionally require "first value = str" in the tracer output
#   @ISOLATION=RR run the tracer transaction at REPEATABLE READ
#                 (exported as CUBRID_M0_TRACER_ISOLATION)

set -eu

DB="${1:?usage: smoke.sh <dbname> [sql...]}"
shift
if [ $# -eq 0 ]; then
  # case 3 joins on an OID-typed catalog attribute (exercises server-side OID comparison);
  # case 4 binds host variables (native DB_VALUE array across the fold boundary);
  # case 5 runs at REPEATABLE READ (exercises the fold's RR transaction lock)
  set -- "SELECT 1" "SELECT COUNT(*) FROM db_class" \
    "SELECT COUNT(*) FROM _db_class a, _db_class b WHERE a.class_of = b.class_of" \
    "SELECT ? + ? @BIND=30,12 @EXPECT=42" \
    "SELECT COUNT(*) FROM db_class @ISOLATION=RR @EXPECT=74"
fi

# This script restarts cub_master via `cubrid service stop` (see below), which
# also stops brokers/gateways/manager/heartbeat of this installation — refuse
# to run if any server OR any other service is active. Fail-closed: an
# unreadable service state is a reason to refuse, not to proceed. One
# `cubrid service status` output covers every section (master, server,
# broker, manager, heartbeat).
svc_rc=0
svc_out="$(cubrid service status 2>&1)" || svc_rc=$?
master_down=0
if printf '%s' "$svc_out" | grep -qiE 'master.*not running'; then
  master_down=1
fi
if [ $svc_rc -ne 0 ] && [ $master_down -eq 0 ]; then
  echo "ABORT: cannot determine CUBRID service state (fail-closed):" >&2
  printf '%s\n' "$svc_out" >&2
  exit 2
fi
# DB servers need a master; the check is only meaningful when one is up
if [ $master_down -eq 0 ] && printf '%s\n' "$svc_out" | grep -q '^ Server '; then
  echo "ABORT: other CUBRID servers are running on this host; smoke.sh restarts cub_master and would stop them." >&2
  printf '%s\n' "$svc_out" >&2
  exit 2
fi
# brokers/gateways/manager run WITHOUT a master, so this check must not be
# short-circuited by master-down. name-based: manager reports as "cubrid
# manager server is running" (a server-word filter misses it), and active
# heartbeat prints HA-Node Info/HA-Process Info lines that never say
# "heartbeat" or "is running". '@'-prefixed section headers are skipped;
# "not installed" (optional manager) is not "active".
services="$(printf '%s\n' "$svc_out" | grep -v '^@' | grep -iE '(broker|gateway|manager|heartbeat|HA-Node|HA-Process)' | grep -viE 'not running|stopped|not installed' || true)"
if [ -n "$services" ]; then
  echo "ABORT: non-server CUBRID services are running; 'cubrid service stop' would take them down:" >&2
  echo "$services" >&2
  exit 2
fi

# Interruption must not leave the server (or a master carrying tracer env)
# behind, nor the current output artifact.
cleanup() {
  cubrid server stop "$DB" >/dev/null 2>&1 || true
  cubrid service stop >/dev/null 2>&1 || true
  [ -n "${out:-}" ] && rm -f "$out"
}
trap cleanup EXIT INT TERM

fail=0
for case_spec in "$@"; do
  # split off the @BIND= / @EXPECT= directives (see header); everything before
  # the first directive is the SQL, trailing blanks trimmed
  sql="$case_spec"
  bind=""
  expect=""
  isolation=""
  case "$case_spec" in
    *"@BIND="*|*"@EXPECT="*|*"@ISOLATION="*)
      sql="$(printf '%s' "$case_spec" | sed -e 's/ *@BIND=[^ ]*//' -e 's/ *@EXPECT=[^ ]*//' -e 's/ *@ISOLATION=[^ ]*//')"
      bind="$(printf '%s' "$case_spec" | sed -n 's/.*@BIND=\([^ ]*\).*/\1/p')"
      expect="$(printf '%s' "$case_spec" | sed -n 's/.*@EXPECT=\([^ ]*\).*/\1/p')"
      isolation="$(printf '%s' "$case_spec" | sed -n 's/.*@ISOLATION=\([^ ]*\).*/\1/p')"
      ;;
  esac

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
  CUBRID_M0_TRACER_SQL="$sql" CUBRID_M0_TRACER_OUT="$out" CUBRID_M0_TRACER_BIND="$bind" \
    CUBRID_M0_TRACER_ISOLATION="$isolation" \
    cubrid server start "$DB" >/dev/null 2>&1 || true

  ok=0
  for _ in $(seq 1 60); do
    # line-anchored: the start line echoes the SQL, so an unanchored match
    # would accept a query whose text contains the SUCCESS string
    if [ -f "$out" ] && grep -q "^M0_TRACER: SUCCESS$" "$out"; then
      ok=1
      break
    fi
    sleep 1
  done

  # @EXPECT: SUCCESS alone is not enough — the fetched value must match
  if [ $ok -eq 1 ] && [ -n "$expect" ] && ! grep -q "^M0_TRACER: first value = ${expect}$" "$out"; then
    ok=0
  fi

  cubrid server stop "$DB" >/dev/null 2>&1 || true
  # a PASS with the server still up is not a pass — shutdown health is part of
  # the gate (shutdown crashes hid exactly here); fail-closed on unknown status.
  stopped=1
  stop_note="server still running after stop"
  if ! status_out="$(cubrid server status 2>/dev/null)"; then
    stopped=0
    stop_note="server status check failed after stop"
  elif printf '%s\n' "$status_out" | grep -q "^ Server ${DB} "; then
    stopped=0
  fi

  echo "--- [$sql]"
  [ -f "$out" ] && cat "$out"
  if [ $ok -eq 1 ] && [ $stopped -eq 1 ]; then
    echo "PASS: $sql"
  elif [ $ok -eq 0 ]; then
    echo "FAIL: $sql (no SUCCESS in tracer output)"
    fail=1
  else
    echo "FAIL: $sql ($stop_note)"
    fail=1
  fi
  rm -f "$out"
done

# don't leave a master whose environment still carries the last tracer SQL —
# the next ordinary `cubrid server start` would silently re-run the tracer.
# Verified fail-closed: PASS requires positive evidence the master is gone.
cubrid service stop >/dev/null 2>&1 || true
final_out="$(cubrid service status 2>&1)" || true
if printf '%s' "$final_out" | grep -qi 'master is running'; then
  echo "FAIL: cub_master still running (with tracer env) after final service stop" >&2
  fail=1
elif ! printf '%s' "$final_out" | grep -qiE 'master.*not running'; then
  echo "FAIL: cannot verify master shutdown after final service stop:" >&2
  printf '%s\n' "$final_out" >&2
  fail=1
fi
trap - EXIT INT TERM

exit $fail
