#!/usr/bin/env bash
# Replay the commit saved-space window (CBRD-27936) on a real debug server.
#
# Usage: engine_replay_commit.sh OUTDIR [TABLE_OPTION]
#   Run from a prepared, built and cub-workenv-initialized CUBRID worktree.
#
# Schedule: put two rows of 8000 and 7000 bytes on one heap page, which leaves
# about 128 free bytes. Session U shrinks row 1 to 100 bytes in place, which
# saves about 7900 bytes for U, and commits. gdb (non-stop) stops U's worker
# in log_commit_local right after spage_free_saved_spaces when that call runs
# before the commit record is appended. Session T grows row 2 to 12000 bytes;
# this fits in place only with U's released bytes. T commits, which flushes
# the log through U's UPDATE record. Then this environment's cub_master and
# cub_server get SIGKILL, and the server restarts. U has no commit record and
# is a loser, so restart undo must write the 8000-byte image back.
#
# Exit 0: restart and checkdb succeed and T's committed row survives.
# Exit 1: finding. Exit 2: setup or harness error.
set -u
OUT=${1:?outdir}; TBLOPT=${2:-}
mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd)
WT=$(git rev-parse --show-toplevel) || exit 2
cd "$WT" || exit 2
[ -f .cub-workenv/env.sh ] || { echo "no .cub-workenv; run cub-workenv init" >&2; exit 2; }
source .cub-workenv/env.sh >/dev/null 2>&1
log() { echo "[$(date +%T)] $*" | tee -a "$OUT/steps.log"; }

# Break on the first statement after spage_free_saved_spaces in log_commit_local.
BRK=$(awk '/^log_commit_local \(THREAD_ENTRY/{f=1} f&&/spage_free_saved_spaces \(thread_p, tdes->first_save_entry\);/{getline; print NR; exit}' src/transaction/log_manager.c)
[ -n "$BRK" ] || { log "cannot locate spage_free_saved_spaces in log_commit_local"; exit 2; }
ORDER=$(awk -v b="$BRK" '/^log_commit_local \(THREAD_ENTRY/{f=1} f&&/log_append_commit_log \(thread_p, tdes, &commit_lsa\);/{print (NR<b)?"commit-record-first":"free-first"; exit}' src/transaction/log_manager.c)
BRK=log_manager.c:$BRK
log "worktree $WT commit $(git rev-parse --short HEAD) dirty=$(git status --porcelain -- src | wc -l) break $BRK order $ORDER"

DB=cwin$(date +%s)
cub-workenv create-db "$DB" >>"$OUT/setup.log" 2>&1 || { log "create-db failed"; exit 2; }
DBDIR=$(awk -v d="$DB" '$1==d{print $2}' "$CUBRID_DATABASES/databases.txt")
sq() { csql -u dba "$DB" -c "$1" >>"$OUT/csql.log" 2>&1; }
own() { local p; for p in $(pgrep -u "$USER" -x "$1"); do
    [ "$(readlink /proc/$p/exe)" = "$CUBRID/bin/$1" ] && echo "$p"; done; }
one() { local v; v=$(echo $1); case "$v" in ''|*' '*) return 1;; esac; echo "$v"; }
rnd() { local n=$1 tag=$2 col=$3 s=""; for k in $(seq 1 $(( (n+127)/128 ))); do s="$s${s:+ || }sha2($col || '$tag$k', 512)"; done; echo "substr($s, 1, $n)"; }

log "database $DB at $DBDIR"
cubrid server start "$DB" >>"$OUT/server.log" 2>&1 || { log "start failed"; exit 2; }
sq "create table t (id int, v varchar(20000)) $TBLOPT;"
sq "insert into t values (1, 'a'), (2, 'b');"
sq "update t set v = $(rnd 8000 a id) where id = 1;"
sq "update t set v = $(rnd 7000 b id) where id = 2;"
CAP=$(csql -u dba "$DB" -l -c "show heap capacity of t")
NP=$(echo "$CAP" | awk -F: '/ Num_pages /{gsub(/ /,"",$2); print $2}')
FREE=$(echo "$CAP" | awk -F: '/Avg_free_space_per_page  /{gsub(/ /,"",$2); print $2}')
REL=$(echo "$CAP" | awk -F: '/Num_relocated_recs/{gsub(/ /,"",$2); print $2}')
log "pages $NP free $FREE relocated $REL"
[ "$NP" = 1 ] && [ "$REL" = 0 ] && [ "$FREE" -lt 4000 ] || { log "precondition failed"; cubrid server stop "$DB" >/dev/null 2>&1; exit 2; }

SPID=$(one "$(for p in $(own cub_server); do tr '\0' ' ' </proc/$p/cmdline | grep -qw "$DB" && echo $p; done)") \
  || { log "need exactly one own cub_server for $DB"; exit 2; }
log "server pid $SPID"

FIFO="$OUT/gdb.in"; rm -f "$FIFO"; mkfifo "$FIFO"
gdb -q -nx --interpreter=mi3 <"$FIFO" >"$OUT/gdb.log" 2>&1 &
GDB=$!
exec 3>"$FIFO"
killgdb() { exec 3>&-; kill -9 "$GDB" 2>/dev/null; wait "$GDB" 2>/dev/null; }
printf '%s\n' '-gdb-set mi-async on' '-gdb-set non-stop on' '-gdb-set pagination off' \
  '-interpreter-exec console "handle SIGPIPE SIGUSR1 SIGUSR2 SIGALRM nostop noprint pass"' \
  "-target-attach $SPID" "-break-insert $BRK" >&3
sleep 5; echo '-exec-continue --all' >&3; sleep 3
hit() { grep -q '\*stopped,reason="breakpoint-hit"' "$OUT/gdb.log"; }
timeout 20 csql -u dba "$DB" -c "select 1" >/dev/null 2>&1 || { log "server unresponsive under gdb"; killgdb; exit 2; }
hit && { log "breakpoint hit before U"; killgdb; exit 2; }

log "U: shrink row 1 and commit (runs in the background)"
csql -u dba "$DB" -c "update t set v = $(rnd 100 c id) where id = 1;" >>"$OUT/csql-u.log" 2>&1 &
UPID=$!
for i in $(seq 1 30); do hit && break; sleep 1; done
hit || { log "breakpoint not hit"; killgdb; kill -9 "$SPID"; exit 2; }
log "U stopped at $BRK"

log "T: grow row 2 and commit"
timeout 60 csql -u dba "$DB" -c "update t set v = $(rnd 12000 d id) where id = 2;" >>"$OUT/csql.log" 2>&1
TRC=$?
log "T exit $TRC (0 committed, 124 blocked)"

MPID=$(one "$(own cub_master)") || { log "need exactly one own cub_master"; killgdb; exit 2; }
kill -9 "$MPID"; kill -9 "$SPID"; sleep 1; killgdb; kill "$UPID" 2>/dev/null; sleep 2
log "killed own cub_master $MPID and cub_server $SPID while U was committing"

RC=0
if timeout 300 cubrid server start "$DB" >>"$OUT/restart.log" 2>&1; then
  csql -u dba "$DB" -c "select id, length(v) from t order by id;" 2>&1 | awk '/^ +[0-9]/{print $1, $2}' >"$OUT/final.txt"
  L1=$(awk '$1==1{print $2}' "$OUT/final.txt"); L2=$(awk '$1==2{print $2}' "$OUT/final.txt")
  log "restart OK; id 1 length $L1, id 2 length $L2"
  cubrid server stop "$DB" >/dev/null 2>&1
  if [ "$TRC" = 0 ] && [ "$L2" != 12000 ]; then log "FINDING: T's committed row lost"; RC=1; fi
  cubrid checkdb -S "$DB" >"$OUT/checkdb.log" 2>&1 || { log "FINDING: checkdb failed"; RC=1; }
  log "checkdb done"
else
  RC=1
  log "FINDING: restart failed"
  for p in $(own cub_master); do kill -9 "$p"; done; sleep 1
  cp -a "$DBDIR/.." "$OUT/db-after-crash"
  ( cd "$OUT" && timeout 300 gdb -q -nx -batch -ex 'set pagination off' \
      -ex 'handle SIGPIPE SIGUSR1 SIGUSR2 SIGALRM nostop noprint pass' \
      -ex 'break logpb_fatal_error' -ex run -ex 'bt 14' \
      --args "$CUBRID/bin/cub_server" "$DB" >"$OUT/restart-gdb.out" 2>&1 )
  grep -E 'Assertion|Breakpoint 1,|^#[0-9]+ ' "$OUT/restart-gdb.out" | cut -c1-200 | head -16 | tee -a "$OUT/steps.log"
fi
cubrid service stop >>"$OUT/server.log" 2>&1
exit $RC
