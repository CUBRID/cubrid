#!/bin/bash
# smoke_thin.sh - B5 PR3 smoke: the real thin csql binary end-to-end.
#
# Boots the installed $CUBRID server (no broker needed - local csql uses
# DIRECT_CONNECT) and runs the thin csql through a scenario battery:
#   1. -c single statement (rendered SELECT)
#   2. -i multi-statement file with a syntax error in the middle
#      (continue-on-error rendering, ERR on stderr, exit code)
#   3. session commands: ;schema ;database ;plan + statement
#   4. DDL/DML round trip with rollback semantics (autocommit off exit = abort)
#   5. SA-mode (-S) fat csql still works on the same DB (bit-untouched flavor)
#
# usage: smoke_thin.sh <dbname>

set -u

DB="${1:?dbname required}"
: "${CUBRID:?CUBRID env required}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="$SCRIPT_DIR/.smoke_thin_work"
CSQL="$CUBRID/bin/csql"

fail() { echo "SMOKE_THIN: FAIL - $*" >&2; exit 1; }

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
[ -S "${CUBRID_TMP:-/tmp}/CUBRID_adopt_$DB" ] || fail "adoption socket missing after server start"

# 1. -c single statement
out="$("$CSQL" -u dba "$DB" -c "SELECT 1;" 2>"$WORK/err1")" || fail "-c exit code ($(cat "$WORK/err1"))"
printf '%s\n' "$out" | grep -q "row selected" || fail "-c missing rowcount: $out"
printf '%s\n' "$out" | grep -q "=== " || fail "-c missing banner"
echo "THIN: -c statement rendered"

# 2. -i file with a mid-file error
cat > "$WORK/batch.sql" <<'EOF'
CREATE TABLE thin_t (a INT, b VARCHAR(10));
INSERT INTO thin_t VALUES (1,'x'),(2,'y');
SELECT * FROM thin_t ORDER BY a;
SELEC bad;
SELECT COUNT(*) FROM thin_t;
DROP TABLE thin_t;
EOF
# fat ground truth (single-line default): a mid-file statement error is
# reported on stderr but the run continues and exits 0; -c errors exit 1
"$CSQL" -u dba "$DB" -i "$WORK/batch.sql" >"$WORK/out2" 2>"$WORK/err2"
rc=$?
[ $rc -eq 0 ] || fail "-i with mid-file error exits 0 in single-line mode (got $rc)"
grep -q "2 rows selected" "$WORK/out2" || fail "-i SELECT * rendering: $(cat "$WORK/out2")"
grep -qi "syntax" "$WORK/err2" || fail "-i syntax error not on stderr: $(cat "$WORK/err2")"
echo "THIN: -i batch with mid-file error behaves (exit 0, error on stderr, run continued)"

# the batch continued past the error: the trailing DROP ran, no residue
"$CSQL" -u dba "$DB" -c "SELECT COUNT(*) FROM thin_t;" >"$WORK/out2b" 2>&1
[ $? -ne 0 ] || fail "thin_t should have been dropped by the batch tail"
"$CSQL" -u dba "$DB" -c "SELEC bad;" >/dev/null 2>&1
[ $? -eq 1 ] || fail "-c with error should exit 1"
echo "THIN: batch continued past error; -c error exits 1"

# 3. session commands
printf ';schema db_class\n;database\n;plan simple\nSELECT COUNT(*) FROM db_class;\n' \
  | "$CSQL" -u dba "$DB" >"$WORK/out3" 2>"$WORK/err3" || fail ";schema/;database/;plan run ($(cat "$WORK/err3"))"
grep -q "<Class Name>" "$WORK/out3" || grep -q "db_class" "$WORK/out3" || fail ";schema rendering"
grep -q "row selected" "$WORK/out3" || fail "statement after session cmds"
echo "THIN: session commands rendered"

# 4. autocommit-off exit rolls back (fat exit semantics via SUB_TRAN)
printf 'CREATE TABLE thin_r (a INT);\nINSERT INTO thin_r VALUES (7);\n' \
  | "$CSQL" -u dba --no-auto-commit "$DB" >"$WORK/out4" 2>&1
"$CSQL" -u dba "$DB" -c "SELECT COUNT(*) FROM thin_r;" >"$WORK/out4b" 2>&1
[ $? -ne 0 ] || fail "thin_r should have been rolled back on exit"
echo "THIN: no-autocommit exit rolled back"

# 5. SA-mode fat flavor untouched (server must be down for -S)
cubrid server stop "$DB" >/dev/null 2>&1 || true
sleep 1
"$CSQL" -S -u dba "$DB" -c "SELECT 1;" >"$WORK/out5" 2>"$WORK/err5" || fail "-S run ($(cat "$WORK/err5"))"
grep -q "row selected" "$WORK/out5" || fail "-S rendering"
echo "THIN: -S (SA fat flavor) intact"

cubrid service stop >/dev/null 2>&1 || true
for _ in $(seq 1 20); do
  pgrep -f "$CUBRID/bin/cub_" >/dev/null 2>&1 || break
  sleep 0.5
done
if pgrep -af "$CUBRID/bin/cub_" 2>/dev/null | grep -q .; then
  fail "processes still running after teardown"
fi

echo "SMOKE_THIN: SUCCESS"
