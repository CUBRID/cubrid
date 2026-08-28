#!/bin/bash
# smoke_jdbc.sh - track-B standard smoke: the real JDBC driver over a
# DIRECT_HANDOFF broker (workspace#122 D5) - connect -> DDL/DML -> cancel ->
# reconnect, 1-hop onto cub_server's folded CAS speaker (stage B1).
#
# usage: smoke_jdbc.sh <dbname> <broker_port> [jdbc_jar]
#
# Requirements: $CUBRID installed, <dbname> created, java/javac 1.8+, no other
# CUBRID service running from this install (fail-closed, as smoke.sh), and the
# install's cubrid_broker.conf is REPLACED for the run (backed up/restored).
# The JDBC jar defaults to $CUBRID/jdbc/cubrid_jdbc.jar, then
# ~/CUBRID/jdbc/cubrid_jdbc.jar.

set -u

DB="${1:?dbname required}"
BROKER_PORT="${2:?broker port required}"
JAR="${3:-}"

: "${CUBRID:?CUBRID env required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRCONF="$CUBRID/conf/cubrid_broker.conf"
BRCONF_BAK="$BRCONF.smoke_jdbc.bak"

fail() { echo "SMOKE_JDBC: FAIL - $*" >&2; exit 1; }

if [ -z "$JAR" ]; then
  for cand in "$CUBRID/jdbc/cubrid_jdbc.jar" "$HOME/CUBRID/jdbc/cubrid_jdbc.jar"; do
    [ -f "$cand" ] && JAR="$cand" && break
  done
fi
[ -f "${JAR:-}" ] || fail "JDBC jar not found (pass it as arg 3)"
command -v javac >/dev/null || fail "javac not found"

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

workdir="$(mktemp -d "$SCRIPT_DIR/.smoke_jdbc.XXXXXX")" || fail "mktemp"
cleanup_work() { rm -rf "$workdir"; }
trap 'cleanup; cleanup_work' EXIT INT TERM

javac -d "$workdir" "$SCRIPT_DIR/B1JdbcSmoke.java" || fail "javac"

cubrid server start "$DB" >/dev/null 2>&1 || fail "server start"
cubrid broker start >/dev/null 2>&1 || fail "broker start"
sleep 1

java -cp "$workdir:$JAR" B1JdbcSmoke "$BROKER_PORT" "$DB" dba "" || fail "jdbc scenario"

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

echo "SMOKE_JDBC: SUCCESS"
