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

SVCONF="$CUBRID/conf/cubrid.conf"
SVCONF_BAK="$SVCONF.smoke_jdbc.bak"
ACLFILE=""
ACLIPS=""
GENERATED_CERT=0

cleanup() {
  cubrid broker stop >/dev/null 2>&1 || true
  cubrid server stop "$DB" >/dev/null 2>&1 || true
  cubrid service stop >/dev/null 2>&1 || true
  if [ -f "$BRCONF_BAK" ]; then
    mv -f "$BRCONF_BAK" "$BRCONF"
  fi
  if [ -f "$SVCONF_BAK" ]; then
    mv -f "$SVCONF_BAK" "$SVCONF"
  fi
  if [ -n "$ACLFILE" ]; then
    rm -f "$ACLFILE" "$ACLIPS"
  fi
  if [ "$GENERATED_CERT" = 1 ]; then
    rm -f "$CUBRID/conf/cas_ssl_cert.crt" "$CUBRID/conf/cas_ssl_cert.key"
  fi
}
trap cleanup EXIT INT TERM

cp -f "$BRCONF" "$BRCONF_BAK" || fail "cannot back up broker conf"

# stage B2: turn the server-resident CAS log producers on (cas_* sysprms) so
# this run also verifies SQL/slow/access/DDL log production (#116 D4), and
# run every connect through a live ACCESS_CONTROL table (B2-D8; reject
# semantics are unit-tested — loopback is always allowed by design)
ACLFILE="$CUBRID/conf/b2_smoke_acl.txt"
ACLIPS="$CUBRID/conf/b2_smoke_ips.txt"
printf '*\n' > "$ACLIPS" || fail "cannot write acl ip file"
printf '[%%B1DIRECT]\n%s:*:%s\n[%%B1SSL]\n%s:*:%s\n' "$DB" "$ACLIPS" "$DB" "$ACLIPS" > "$ACLFILE" || fail "cannot write acl file"
cp -f "$SVCONF" "$SVCONF_BAK" || fail "cannot back up cubrid.conf"
grep -q '^\[common\]' "$SVCONF" || fail "cubrid.conf has no [common] section"
sed -i "/^\[common\]/a cas_sql_log=all\ncas_slow_log=yes\ncas_access_log=yes\ncas_long_query_time=1000\nddl_audit_log=yes\ncas_max_prepared_stmt_count=64\ncas_access_control=yes\ncas_access_control_file=$ACLFILE" "$SVCONF" \
  || fail "cannot enable cas_* log parameters"

# scope the log assertions to this run
rm -f "$CUBRID"/log/broker/sql_log/"${DB}"_*.sql.log "$CUBRID"/log/broker/sql_log/"${DB}"_*.slow.log \
      "$CUBRID"/log/broker/"${DB}".access "$CUBRID"/log/ddl_audit/"${DB}"_*_ddl.log 2>/dev/null || true

# self-signed cert for the SSL leg (B2-D9): the server terminates TLS with
# the CAS's historical cert paths, $CUBRID/conf/cas_ssl_cert.{crt,key}
SSL_PORT=$((BROKER_PORT + 1))
CERT="$CUBRID/conf/cas_ssl_cert.crt"
KEY="$CUBRID/conf/cas_ssl_cert.key"
if [ ! -f "$CERT" ] || [ ! -f "$KEY" ]; then
  openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=b2smoke" \
    -keyout "$KEY" -out "$CERT" >/dev/null 2>&1 || fail "cannot generate the smoke SSL cert"
  GENERATED_CERT=1
fi

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

[%B1SSL]
SERVICE                 =ON
BROKER_PORT             =$SSL_PORT
MIN_NUM_APPL_SERVER     =1
MAX_NUM_APPL_SERVER     =20
APPL_SERVER_SHM_ID      =30003
LOG_DIR                 =log/broker/sql_log
ERROR_LOG_DIR           =log/broker/error_log
SQL_LOG                 =OFF
TIME_TO_KILL            =120
SESSION_TIMEOUT         =300
KEEP_CONNECTION         =AUTO
DIRECT_HANDOFF          =ON
SSL                     =ON
DIRECT_HANDOFF_SSL_DB   =$DB
EOF

workdir="$(mktemp -d "$SCRIPT_DIR/.smoke_jdbc.XXXXXX")" || fail "mktemp"
cleanup_work() { rm -rf "$workdir"; }
trap 'cleanup; cleanup_work' EXIT INT TERM

javac -d "$workdir" "$SCRIPT_DIR/B1JdbcSmoke.java" || fail "javac"

cubrid server start "$DB" >/dev/null 2>&1 || fail "server start"
cubrid broker start >/dev/null 2>&1 || fail "broker start"
sleep 1

java -cp "$workdir:$JAR" B1JdbcSmoke "$BROKER_PORT" "$DB" dba "" || fail "jdbc scenario"

# the same battery over TLS (server-side termination, B2-D9; xa self-skips)
java -cp "$workdir:$JAR" B1JdbcSmoke "$SSL_PORT" "$DB" dba "" ssl || fail "jdbc ssl scenario"

# --- log production checks (B2-D1..D6): the sessions above must have produced
# per-slot CAS-format logs under the server's ownership -------------------
sqllog="$(ls "$CUBRID"/log/broker/sql_log/"${DB}"_*.sql.log 2>/dev/null | head -1)"
[ -n "$sqllog" ] || fail "no per-session SQL log was produced"
grep -q "connect db" "$sqllog" || fail "SQL log misses the connect unit"
grep -qi "CREATE TABLE b1_smoke" "$sqllog" || fail "SQL log misses statement text"
grep -q "EID = " "$sqllog" || fail "SQL log misses the error EID cross-reference"
slowlog="$(ls "$CUBRID"/log/broker/sql_log/"${DB}"_*.slow.log 2>/dev/null | head -1)"
[ -n "$slowlog" ] || fail "no slow log was produced (SLEEP cases exceed cas_long_query_time=1s)"
grep -qi "SLEEP" "$slowlog" || fail "slow log misses the SLEEP statement"
[ -s "$CUBRID/log/broker/${DB}.access" ] || fail "no access log line was produced"
ddllog="$(ls "$CUBRID"/log/ddl_audit/"${DB}"_*_ddl.log 2>/dev/null | head -1)"
[ -n "$ddllog" ] || fail "no DDL audit log was produced"
grep -qi "CREATE TABLE" "$ddllog" || fail "DDL audit log misses CREATE TABLE"
# the CAS log format must stay readable by the existing tooling (#116 D4)
if [ -x "$CUBRID/bin/broker_log_top" ]; then
  topdir="$(mktemp -d "$SCRIPT_DIR/.log_top.XXXXXX")" || fail "mktemp log_top"
  (cd "$topdir" && "$CUBRID/bin/broker_log_top" "$sqllog" >/dev/null 2>&1) || { rm -rf "$topdir"; fail "broker_log_top cannot parse the server-produced SQL log"; }
  rm -rf "$topdir"
fi

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
