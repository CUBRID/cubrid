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
#   6. client-only session parameter (create_table_reuseoid) resolves through
#      the session on the folded compile path, both SET directions (wf159)
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

# 5. ;time off reaches the server-side renderer (wire flag sync — the
#    timing suffix must disappear; postmerge-review fix)
printf ';time off\nSELECT 1;\n' | "$CSQL" -u dba "$DB" >"$WORK/out5t" 2>&1 || fail ";time off run"
grep -q "row selected" "$WORK/out5t" || fail ";time off SELECT rendering"
grep -q "sec)" "$WORK/out5t" && fail ";time off did not suppress the timing suffix: $(cat "$WORK/out5t")"
echo "THIN: ;time off shipped to the renderer"

# 6. client-only session parameter reaches the folded compile (wf159):
#    create_table_reuseoid is PRM_FOR_CLIENT|PRM_FOR_SESSION only — a SET must
#    steer the in-server compile through the session parameter array, both ways
out="$("$CSQL" -u dba "$DB" -c "SET SYSTEM PARAMETERS 'create_table_reuseoid=no'; CREATE CLASS thin_ro1 (a INT); SELECT is_reuse_oid_class FROM db_class WHERE class_name='thin_ro1';" 2>"$WORK/err6")" \
  || fail "reuseoid=no leg run ($(cat "$WORK/err6"))"
printf '%s\n' "$out" | grep -q "'NO'" || fail "reuseoid=no ignored by folded compile: $out"
out="$("$CSQL" -u dba "$DB" -c "SET SYSTEM PARAMETERS 'create_table_reuseoid=yes'; CREATE CLASS thin_ro2 (a INT); SELECT is_reuse_oid_class FROM db_class WHERE class_name='thin_ro2';" 2>"$WORK/err6")" \
  || fail "reuseoid=yes leg run ($(cat "$WORK/err6"))"
printf '%s\n' "$out" | grep -q "'YES'" || fail "reuseoid=yes ignored by folded compile: $out"
"$CSQL" -u dba "$DB" -c "DROP CLASS thin_ro1; DROP CLASS thin_ro2;" >/dev/null 2>&1 || fail "reuseoid case cleanup"
echo "THIN: client-only session parameter steers the folded compile (both SET directions)"

# 6b. client-half stdout messages reach the rendered output: the fat client
#     printed "Statistics updated successfully" on its own stdout; the folded
#     body must route it into the request capture (CSC_CLIENT_STDOUT)
out="$("$CSQL" -u dba "$DB" -c "CREATE CLASS thin_st (a INT); UPDATE STATISTICS ON thin_st; DROP CLASS thin_st;" 2>"$WORK/err6b")" \
  || fail "update statistics run ($(cat "$WORK/err6b"))"
printf '%s\n' "$out" | grep -q "Statistics updated successfully: 1 table, 1 column" \
  || fail "update statistics confirmation missing from thin output: $out"
echo "THIN: client-half stdout message (Statistics updated) rendered"

# 6c. GET reads the current session, including owned string values. Filter
#     GET sections so SET's echoed input cannot satisfy the assertions.
parameter_values() {
  awk '/^=== Get Param Input ===$/ { in_get=1; next }
       /^=== / { in_get=0 }
       in_get && /^[a-z_]+=/ { print }' "$1"
}
printf ';get lock_timeout_in_secs\n;get timezone\n' > "$WORK/params-get.sql"
"$CSQL" -u dba "$DB" -i "$WORK/params-get.sql" >"$WORK/params-before" 2>"$WORK/params-before.err" \
  || fail "session parameter baseline"
parameter_values "$WORK/params-before" > "$WORK/params-before.values"
[ "$(wc -l < "$WORK/params-before.values")" -eq 2 ] || fail "session parameter baseline missing"
cat > "$WORK/params-change.sql" <<'EOF'
;set lock_timeout_in_secs="5"
;set timezone="+09:00"
;get lock_timeout_in_secs
;get timezone
;set lock_timeout_in_secs="0"
;set timezone="-04:00"
;get lock_timeout_in_secs
;get timezone
EOF
"$CSQL" -u dba "$DB" -i "$WORK/params-change.sql" >"$WORK/params-change" 2>"$WORK/params-change.err" \
  || fail "session parameter SET/GET"
parameter_values "$WORK/params-change" > "$WORK/params-change.values"
printf 'lock_timeout_in_secs=5\ntimezone="+09:00"\nlock_timeout_in_secs=0\ntimezone="-04:00"\n' \
  > "$WORK/params-expected.values"
diff -u "$WORK/params-expected.values" "$WORK/params-change.values" || fail "GET returned stale session values"
"$CSQL" -u dba "$DB" -i "$WORK/params-get.sql" >"$WORK/params-after" 2>"$WORK/params-after.err" \
  || fail "new session parameter baseline"
parameter_values "$WORK/params-after" > "$WORK/params-after.values"
diff -u "$WORK/params-before.values" "$WORK/params-after.values" || fail "session parameters leaked to a new connection"
echo "THIN: GET follows session SET without changing other connections"

# 7. SA-mode fat flavor untouched (server must be down for -S)
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
