#!/bin/bash
# smoke_csql.sh - B5 PR1 smoke: CAS_FC_CSQL_REQUEST server-rendered execution.
#
# Boots the installed $CUBRID stack with a DIRECT_HANDOFF=ON broker and runs
# probe_csql.py against it (rendered SELECT / statement error / ;schema /
# allowlist refusal).  Same hygiene as smoke_direct.sh.
#
# usage: smoke_csql.sh <dbname> <broker_port>

set -u

DB="${1:?dbname required}"
BROKER_PORT="${2:?broker port required}"

: "${CUBRID:?CUBRID env required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRCONF="$CUBRID/conf/cubrid_broker.conf"
BRCONF_BAK="$BRCONF.smoke_csql.bak"

fail() { echo "SMOKE_CSQL: FAIL - $*" >&2; exit 1; }

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

[%B5CSQL]
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

python3 "$SCRIPT_DIR/probe_csql.py" "$BROKER_PORT" "$DB" dba "" || fail "probe"

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

echo "SMOKE_CSQL: SUCCESS"
