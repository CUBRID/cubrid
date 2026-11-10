#!/usr/bin/env bash
# Replay the relocation-vacuum forward-slot window on a real debug server.
#
# Usage: engine_replay.sh OUTDIR [TABLE_OPTION]
#   Run from a prepared, built and cub-workenv-initialized CUBRID worktree.
#   TABLE_OPTION is appended to CREATE TABLE, for example DONT_REUSE_OID.
#
# Schedule: relocate one row, delete it and commit, stop the vacuum worker
# under gdb in non-stop mode just before log_sysop_commit in the
# REC_RELOCATION branch of vacuum_heap_record, commit a competing INSERT of
# the same size into the table, SIGKILL this environment's cub_master and
# cub_server, then restart and run SA checkdb.
#
# Exit 0: restart and checkdb succeed. Exit 1: restart or checkdb fails
# (finding; restart-gdb.out has the backtrace). Exit 2: setup or harness error.
set -u
OUT=${1:?outdir}; TBLOPT=${2:-}
mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd)
WT=$(git rev-parse --show-toplevel) || exit 2
cd "$WT" || exit 2
[ -f .cub-workenv/env.sh ] || { echo "no .cub-workenv; run cub-workenv init" >&2; exit 2; }
source .cub-workenv/env.sh >/dev/null 2>&1
log() { echo "[$(date +%T)] $*" | tee -a "$OUT/steps.log"; }

# Break before log_sysop_commit in the REC_RELOCATION branch, whatever the release order.
BRK=$(awk '/^vacuum_heap_record \(THREAD_ENTRY/{f=1} f&&/case REC_RELOCATION:/{r=1} r&&/log_sysop_commit \(thread_p\);/{print FILENAME":"NR; exit} r&&/case REC_BIGONE:/{exit}' src/query/vacuum.c)
[ -n "$BRK" ] || { log "cannot locate the REC_RELOCATION sysop commit"; exit 2; }
BRK=vacuum.c:${BRK##*:}
log "worktree $WT commit $(git rev-parse --short HEAD) dirty=$(git status --porcelain -- src | wc -l) break $BRK"

# Always use a new disposable database; never touch an existing one.
DB=relvac$(date +%s)
cub-workenv create-db "$DB" >>"$OUT/setup.log" 2>&1 || { log "create-db failed"; exit 2; }
DBDIR=$(awk -v d="$DB" '$1==d{print $2}' "$CUBRID_DATABASES/databases.txt")
sq() { csql -u dba "$DB" -c "$1" >>"$OUT/csql.log" 2>&1; }
own() {  # own <exe-name>: PIDs of this environment's installation only
  local p; for p in $(pgrep -u "$USER" -x "$1"); do
    [ "$(readlink /proc/$p/exe)" = "$CUBRID/bin/$1" ] && echo "$p"; done; }
one() { local v; v=$(echo $1); case "$v" in ''|*' '*) return 1;; esac; echo "$v"; }

log "database $DB at $DBDIR"
cubrid server start "$DB" >>"$OUT/server.log" 2>&1 || { log "start failed"; exit 2; }
sq "create table t (id int, v varchar(20000)) $TBLOPT;"
sq "create table f (id int, v varchar(1000));"
# sha2 hex is not compressible, so record sizes are what they look like.
rnd() { local n=$1 tag=$2 col=$3 s=""; for k in $(seq 1 $(( (n+127)/128 ))); do s="$s${s:+ || }sha2($col || '$tag$k', 512)"; done; echo "substr($s, 1, $n)"; }
for i in $(seq 1 15); do sq "insert into t values ($i, $(rnd 1000 a $i));"; done
sq "update t set v = $(rnd 5000 b id) where id = 1;"
REL=$(csql -u dba "$DB" -l -c "show heap capacity of t" | awk -F: '/Num_relocated_recs/{gsub(/ /,"",$2); print $2}')
log "relocated records: $REL"
[ "$REL" = 1 ] || { log "precondition failed: no relocated record"; cubrid server stop "$DB" >/dev/null 2>&1; exit 2; }

SPID=$(one "$(for p in $(own cub_server); do tr '\0' ' ' </proc/$p/cmdline | grep -qw "$DB" && echo $p; done)") \
  || { log "need exactly one own cub_server for $DB"; exit 2; }
log "server pid $SPID"

# gdb/MI, non-stop, fed through a FIFO so that $GDB is gdb's own PID.
FIFO="$OUT/gdb.in"; rm -f "$FIFO"; mkfifo "$FIFO"
gdb -q -nx --interpreter=mi3 <"$FIFO" >"$OUT/gdb.log" 2>&1 &
GDB=$!
exec 3>"$FIFO"
killgdb() { exec 3>&-; kill -9 "$GDB" 2>/dev/null; wait "$GDB" 2>/dev/null; }
printf '%s\n' '-gdb-set mi-async on' '-gdb-set non-stop on' '-gdb-set pagination off' \
  '-interpreter-exec console "handle SIGPIPE SIGUSR1 SIGUSR2 SIGALRM nostop noprint pass"' \
  "-target-attach $SPID" "-break-insert $BRK" >&3
# Continue only after the attach stops have arrived, or threads stay stopped.
sleep 5; echo '-exec-continue --all' >&3; sleep 3
hit() { grep -q '\*stopped,reason="breakpoint-hit"' "$OUT/gdb.log"; }
timeout 20 csql -u dba "$DB" -c "select 1" >/dev/null 2>&1 || { log "server unresponsive under gdb"; killgdb; exit 2; }

log "delete the relocated row and commit"
sq "delete from t where id = 1;"
for i in $(seq 1 40); do
  hit && break
  sq "insert into f select rownum, repeat('f', 900) from db_class c1, db_class c2, db_class c3 limit 2000;"
done
for i in $(seq 1 60); do hit && break; sleep 1; done
hit || { log "breakpoint not hit"; killgdb; kill -9 "$SPID"; exit 2; }
log "vacuum worker stopped at $BRK"

# Do not read t here: the stopped worker still holds the home page.
log "competing insert"
timeout 60 csql -u dba "$DB" -c "insert into t values (100, $(rnd 5000 c 100));" >>"$OUT/csql.log" 2>&1
INS=$?
log "competing insert exit $INS (0 committed, 124 blocked)"

MPID=$(one "$(own cub_master)") || { log "need exactly one own cub_master"; killgdb; exit 2; }
kill -9 "$MPID"; kill -9 "$SPID"; sleep 1; killgdb; sleep 2
log "killed own cub_master $MPID and cub_server $SPID with the vacuum sysop open"

RC=0
if timeout 300 cubrid server start "$DB" >>"$OUT/restart.log" 2>&1; then
  csql -u dba "$DB" -c "select id, length(v) from t order by id;" 2>&1 | awk '/^ +[0-9]/{print $1, $2}' >"$OUT/final.txt"
  L100=$(awk '$1==100{print $2}' "$OUT/final.txt")
  log "restart OK; rows $(wc -l <"$OUT/final.txt"); id 100 length ${L100:-absent}"
  cubrid server stop "$DB" >/dev/null 2>&1
  if [ "$INS" = 0 ] && [ "$L100" != 5000 ]; then log "FINDING: committed competing row lost"; RC=1; fi
  cubrid checkdb -S "$DB" >"$OUT/checkdb.log" 2>&1 || { log "FINDING: checkdb failed"; RC=1; }
  log "checkdb done"
else
  RC=1
  log "FINDING: restart failed"
  for p in $(own cub_master); do kill -9 "$p"; done; sleep 1
  cp -a "$DBDIR/.." "$OUT/db-after-crash"
  ( cd "$OUT" && timeout 300 gdb -q -nx -batch -ex 'set pagination off' \
      -ex 'handle SIGPIPE SIGUSR1 SIGUSR2 SIGALRM nostop noprint pass' -ex run -ex 'bt 12' \
      --args "$CUBRID/bin/cub_server" "$DB" >"$OUT/restart-gdb.out" 2>&1 )
  grep -E 'Assertion|^#[5-9] ' "$OUT/restart-gdb.out" | cut -c1-200 | tee -a "$OUT/steps.log"
fi
cubrid service stop >>"$OUT/server.log" 2>&1
exit $RC
