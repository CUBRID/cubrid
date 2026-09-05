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

# smoke_direct.sh - B1 direct-handoff smoke: 1-hop connect over the real wire.
#
# Boots the installed $CUBRID stack with a DIRECT_HANDOFF=ON broker and runs
# probe_direct.py against it (connect / health check / cancel / anti-spoof /
# request-loop answer).  The JDBC smoke supersedes this at the b1-jdbc-smoke
# stage; this is the PR2 "first 1-hop" checkpoint.
#
# usage: smoke_direct.sh <dbname> <broker_port>
#
# Requirements: $CUBRID installed, <dbname> created, no other CUBRID service
# running from this install (fail-closed, same hygiene as smoke.sh), and the
# cubrid_broker.conf of this install is REPLACED for the run (backed up and
# restored on exit).

set -u

DB="${1:?dbname required}"
BROKER_PORT="${2:?broker port required}"

: "${CUBRID:?CUBRID env required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRCONF="$CUBRID/conf/cubrid_broker.conf"
BRCONF_BAK="$BRCONF.smoke_direct.bak"

fail() { echo "SMOKE_DIRECT: FAIL - $*" >&2; exit 1; }

# fail-closed service check (mirrors smoke.sh)
svc_out="$(cubrid service status 2>&1)" || true
if printf '%s\n' "$svc_out" | grep -q '^ Server '; then
  fail "other CUBRID servers are running from this install"
fi

cleanup() {
  cubrid broker stop >/dev/null 2>&1 || true
  cubrid server stop "$DB" >/dev/null 2>&1 || true
  cubrid service stop >/dev/null 2>&1 || true
  if [ -f "$BRCONF_BAK" ]; then
    mv -f "$BRCONF_BAK" "$BRCONF"
  fi
}
trap cleanup EXIT INT TERM

cp -f "$BRCONF" "$BRCONF_BAK" || fail "cannot back up broker conf"

cat > "$BRCONF" <<EOF
[broker]
MASTER_SHM_ID           =30001
ADMIN_LOG_FILE          =log/broker/cubrid_broker.log

[%B1DIRECT]
SERVICE                 =ON
BROKER_PORT             =$BROKER_PORT
MIN_NUM_APPL_SERVER     =1
MAX_NUM_APPL_SERVER     =20
APPL_SERVER_SHM_ID      =30002
LOG_DIR                 =log/broker/sql_log
ERROR_LOG_DIR           =log/broker/error_log
SQL_LOG                 =OFF
TIME_TO_KILL            =120
SESSION_TIMEOUT         =300
KEEP_CONNECTION         =AUTO
DIRECT_HANDOFF          =ON
EOF

cubrid server start "$DB" >/dev/null 2>&1 || fail "server start"
cubrid broker start >/dev/null 2>&1 || fail "broker start"
sleep 1

python3 "$SCRIPT_DIR/probe_direct.py" "$BROKER_PORT" "$DB" dba "" || fail "probe"

# teardown: the adopted-session sign-off path runs on broker stop.  The stop
# commands themselves are tolerated (master may auto-exit once the broker is
# gone, making a late `server stop` unable to reach it) — what gates is the
# verified end state: nothing from this install left running.
cubrid broker stop >/dev/null 2>&1 || true
cubrid server stop "$DB" >/dev/null 2>&1 || true
cubrid service stop >/dev/null 2>&1 || true
for _ in $(seq 1 20); do
  pgrep -f "$CUBRID/bin/cub_" >/dev/null 2>&1 || break
  sleep 0.5
done
if pgrep -af "$CUBRID/bin/cub_" 2>/dev/null | grep -q .; then
  fail "processes still running after teardown"
fi

echo "SMOKE_DIRECT: SUCCESS"
