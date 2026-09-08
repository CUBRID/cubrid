#!/bin/bash
#
# Copyright 2026 CUBRID Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Public lifecycle regression. Linux user namespaces, mount, ip and timeout
# are required. Both arguments are complete installations, never source trees.
# Database files and logs are retained in the printed temporary directory.
set -euo pipefail

if [[ ${1:-} != --inside ]]; then
  if [[ $# != 2 ]]; then
    echo "Usage: $0 BASELINE_INSTALL CURRENT_INSTALL" >&2
    exit 2
  fi
  evidence_root=${HISTORY_TEST_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/cubrid-history}
  mkdir -p "$evidence_root"
  scratch=$(mktemp -d "$evidence_root/run.XXXXXX")
  echo "Lifecycle evidence: $scratch"
  for variant in baseline current; do
    if [[ $variant == baseline ]]; then source_install=$1; else source_install=$2; fi
    mkdir -p "$scratch/$variant"
    for dir in bin lib lib64 conf msg locales timezones cci jdbc vm; do
      if [[ -d $source_install/$dir ]]; then
        cp -aL --reflink=auto "$source_install/$dir" "$scratch/$variant/"
      fi
    done
    cat > "$scratch/$variant/conf/cubrid.conf" <<'CONF'
[service]
service=server
[common]
data_buffer_size=64M
log_buffer_size=16M
java_stored_procedure=no
CONF
  done
  cp "${BASH_SOURCE[0]}" "$scratch/test.sh"
  cc -shared -fPIC -Wall -Wextra -Werror -o "$scratch/fault.so" \
    "$(dirname "${BASH_SOURCE[0]}")/history_activation_fault.c" -ldl
  printf '127.0.0.1 localhost %s\n' "$(hostname)" > "$scratch/hosts"
  : > "$scratch/resolv.conf"
  exec timeout --signal=KILL 300 unshare -r --mount-proc -i -p -f -n --kill-child=SIGKILL \
    bash -c '
      set -euo pipefail
      mount --bind "$1" /mnt
      mount --bind /mnt/hosts /etc/hosts
      mount --bind /mnt/resolv.conf /etc/resolv.conf
      mount -t tmpfs -o size=256m tmpfs /tmp
      ip link set lo up
      exec bash /mnt/test.sh --inside
    ' _ "$scratch"
fi

ulimit -c 0
mkdir -p /mnt/databases /mnt/work
cd /mnt/work
export CUBRID_DATABASES=/mnt/databases
original_path=$PATH
use_engine ()
{
  export CUBRID=/mnt/$1
  export PATH=$CUBRID/bin:$original_path
  export LD_LIBRARY_PATH=$CUBRID/lib:$CUBRID/lib64:$CUBRID/cci/lib
}
create_database ()
{
  cubrid createdb --db-volume-size=64M --log-volume-size=64M \
    --db-page-size="${HISTORY_PAGE_SIZE:-16K}" --log-page-size="${HISTORY_PAGE_SIZE:-16K}" "$1" en_US > "$1.create.log" 2>&1
}
check_legacy_history ()
{
  local label=$1 end_date transaction
  end_date=$(date +%d-%m-%Y:%H:%M:%S)
  cubrid flashback -p '' -s "$history_start" -e "$end_date" inactive dba.retained < /dev/null > "$label.summary.log" 2>&1
  transaction=$(awk '$1 ~ /^[0-9]+$/ && $2 == "DBA" { print $1; exit }' "$label.summary.log")
  [[ -n $transaction ]]
  printf '%s\n' "$transaction" | cubrid flashback -p '' --detail -s "$history_start" -e "$end_date" inactive dba.retained > "$label.detail.log" 2>&1
  grep -E '^\[ORIGINAL\].*4633' "$label.detail.log" > "$label.sql"
}
use_engine baseline
cubrid_rel > baseline.version
create_database inactive
# Keep the query interval strictly after database creation.
sleep 1
history_start=$(date +%d-%m-%Y:%H:%M:%S)
printf '\nsupplemental_log=1\n' >> "$CUBRID/conf/cubrid.conf"
csql -S -u dba inactive -c 'CREATE TABLE retained (v INTEGER); INSERT INTO retained VALUES (4633);' > inactive.seed.log 2>&1
use_engine current
cubrid_rel > current.version
create_database fresh
csql -S -u dba fresh -c 'SELECT 1;' > fresh.current.log 2>&1
use_engine baseline
if csql -S -u dba fresh -c 'SELECT 1;' > fresh.baseline.log 2>&1; then
  echo 'FAIL: baseline engine opened a fresh current-format database' >&2
  exit 1
fi
if ! grep -qi 'incompatible' fresh.baseline.log; then
  cat fresh.baseline.log >&2
  echo 'FAIL: baseline failed for a reason other than compatibility' >&2
  exit 1
fi
echo 'PASS: baseline rejects fresh current-format database'
use_engine current
csql -S -u dba inactive -c 'SELECT v FROM retained;' > inactive.current.log 2>&1
grep -q '4633' inactive.current.log
use_engine baseline
csql -S -u dba inactive -c 'SELECT v FROM retained;' > inactive.baseline.log 2>&1
grep -q '4633' inactive.baseline.log
echo 'PASS: upgraded engine preserves inactive compatibility and data'
use_engine current
cubrid applyinfo -L /mnt/work inactive > inactive.applyinfo.log 2>&1
grep -q 'DB name.*inactive' inactive.applyinfo.log

mkdir -p /mnt/backup-inactive
use_engine current
cubrid backupdb -S -D /mnt/backup-inactive inactive > inactive.backup.log 2>&1
use_engine baseline
cubrid restoredb -B /mnt/backup-inactive inactive > inactive.restore.log 2>&1
csql -S -u dba inactive -c 'SELECT v FROM retained;' > inactive.restored.log 2>&1
grep -q '4633' inactive.restored.log
echo 'PASS: inactive backup remains restorable by the baseline engine'

use_engine current
# Force OOS placement even for a small, fixed value. Seed an old image with
# logging off, then check that neither side of a change can bypass activation.
csql -S -u dba inactive -c "CREATE TABLE outline_value (id INTEGER, v VARCHAR(256) STORAGE FORCE_OUTLINE); INSERT INTO outline_value VALUES (2, 'abcdefghijklmnopqrstuvwxyz0123456789');" > inactive.oos-ddl.log 2>&1
printf '\nsupplemental_log=1\n' >> "$CUBRID/conf/cubrid.conf"
for mode in S C; do
  if [[ $mode == C ]]; then
    cubrid server start inactive > inactive.start.log 2>&1
  fi
  for statement in \
    "INSERT INTO outline_value VALUES (1, 'abcdefghijklmnopqrstuvwxyz0123456789');" \
    "UPDATE outline_value SET v='short' WHERE id=2;" \
    "DELETE FROM outline_value WHERE id=2;"; do
    if csql "-$mode" -u dba inactive -c "$statement" > "inactive.oos-denied.$mode.log" 2>&1; then
      echo "FAIL: inactive database accepted OOS history without activation ($statement)" >&2
      exit 1
    fi
    grep -qi 'OOS history activation required' "inactive.oos-denied.$mode.log"
  done
  csql "-$mode" -u dba inactive -c "SELECT COUNT(*) AS retained FROM outline_value WHERE id=2 AND v='abcdefghijklmnopqrstuvwxyz0123456789';" > "inactive.oos-rollback.$mode.log" 2>&1
  grep -Eq '^[[:space:]]+1[[:space:]]*$' "inactive.oos-rollback.$mode.log"
  csql "-$mode" -u dba inactive -c 'SELECT COUNT(*) AS remaining FROM outline_value;' > "inactive.oos-count.$mode.log" 2>&1
  grep -Eq '^[[:space:]]+1[[:space:]]*$' "inactive.oos-count.$mode.log"
done
echo 'PASS: inactive OOS inserts, updates and deletes fail without changing rows (SA/CS)'
check_legacy_history before-activation

if cubrid activatehistorydb inactive > inactive.running-activation.log 2>&1; then
  echo 'FAIL: activated a running database' >&2
  exit 1
fi
grep -Eqi 'in use|locked' inactive.running-activation.log
# This namespace contains exactly the one server started above.
server_pid=$(pgrep -x cub_server)
# Stop the private master first so it cannot automatically recover the server.
kill -KILL "$(pgrep -x cub_master)"
kill -KILL "$server_pid"
for attempt in {1..100}; do
  if ! kill -0 "$server_pid" 2>/dev/null; then break; fi
  sleep 0.1
done
if cubrid activatehistorydb inactive > inactive.crashed-activation.log 2>&1; then
  echo 'FAIL: activation recovered an unclean database implicitly' >&2
  exit 1
fi
grep -qi 'requires a clean shutdown' inactive.crashed-activation.log
csql -S -u dba inactive -c 'SELECT v FROM retained;' > inactive.recovery.log 2>&1
grep -q '4633' inactive.recovery.log
echo 'PASS: activation rejects running and unclean databases; explicit recovery preserves data'

cubrid activatehistorydb inactive > inactive.activate.log 2>&1
csql -S -u dba inactive -c 'SELECT v FROM retained;' > inactive.activated.log 2>&1
grep -q '4633' inactive.activated.log
# Repeating activation is harmless, and cannot downgrade the database.
cubrid activatehistorydb inactive > inactive.activate-again.log 2>&1
use_engine baseline
if csql -S -u dba inactive -c 'SELECT v FROM retained;' > inactive.rejected.log 2>&1; then
  echo 'FAIL: baseline engine opened an activated database' >&2
  exit 1
fi
grep -qi 'incompatible' inactive.rejected.log
echo 'PASS: explicit activation preserves data and rejects the baseline engine'

use_engine current
cubrid applyinfo -L /mnt/work inactive > activated.applyinfo.log 2>&1
grep -q 'DB name.*inactive' activated.applyinfo.log
cubrid server start inactive > activated.start.log 2>&1
check_legacy_history after-activation
cmp before-activation.sql after-activation.sql
cubrid server stop inactive > activated.stop.log 2>&1
echo 'PASS: existing non-OOS history remains readable and copied-log reader accepts both formats'
mkdir -p /mnt/backup-current
cubrid backupdb -S -D /mnt/backup-current inactive > activated.backup.log 2>&1
use_engine baseline
: > "$CUBRID/log/inactive_restoredb.err"
if cubrid restoredb -B /mnt/backup-current inactive > activated.baseline-restore.log 2>&1; then
  echo 'FAIL: baseline restored a current-format backup' >&2
  exit 1
fi
grep -qi 'Backup is incompatible' "$CUBRID/log/inactive_restoredb.err"
use_engine current
cubrid restoredb -B /mnt/backup-current inactive > activated.current-restore.log 2>&1
csql -S -u dba inactive -c "SELECT v FROM retained; SELECT v FROM outline_value;" > activated.restored.log 2>&1
grep -q '4633' activated.restored.log
grep -q 'abcdefghijklmnopqrstuvwxyz0123456789' activated.restored.log
use_engine baseline
if csql -S -u dba inactive -c 'SELECT 1;' > activated.restored-baseline.log 2>&1; then
  echo 'FAIL: baseline opened the restored current-format database' >&2
  exit 1
fi
grep -qi 'incompatible' activated.restored-baseline.log
echo 'PASS: current-format backup restores data and retains baseline rejection'

# Suppressing routine fsync must not suppress the activation fence.
printf '\nsuppress_fsync=100\n' >> /mnt/current/conf/cubrid.conf
for phase in write-before write-after sync-before sync-after write-error sync-error; do
  db=f_${phase//-/_}
  use_engine baseline
  create_database "$db"
  csql -S -u dba "$db" -c 'CREATE TABLE retained (v INTEGER); INSERT INTO retained VALUES (4633);' > "$db.seed.log" 2>&1
  use_engine current
  rm -f fault.pid
  HISTORY_FAULT_TARGET="/mnt/work/${db}_lgat" HISTORY_FAULT_PHASE="$phase" LD_PRELOAD=/mnt/fault.so \
    cubrid activatehistorydb "$db" > "$db.interrupted.log" 2>&1 &
  activation_pid=$!
  for attempt in {1..200}; do
    if [[ -s fault.pid ]]; then break; fi
    if ! kill -0 "$activation_pid" 2>/dev/null; then break; fi
    sleep 0.05
  done
  if [[ ! -s fault.pid ]]; then
    echo "FAIL: activation never reached required $phase syscall" >&2
    exit 1
  fi
  if [[ $phase != *error ]]; then
    kill -KILL "$(cat fault.pid)"
  fi
  if wait "$activation_pid"; then
    echo "FAIL: interrupted/failed activation reported success ($phase)" >&2
    exit 1
  fi
  if [[ $phase == write-after ]]; then
    # Opening directly after interruption must establish the fence itself;
    # activation retry must not hide a missing startup synchronization.
    rm -f fault.pid
    if HISTORY_FAULT_TARGET="/mnt/work/${db}_lgat" HISTORY_FAULT_PHASE=sync-error LD_PRELOAD=/mnt/fault.so \
      csql -S -u dba "$db" -c 'SELECT v FROM retained;' > "$db.startup-sync-error.log" 2>&1; then
      echo 'FAIL: startup ignored failure to synchronize the compatibility fence' >&2
      exit 1
    fi
    [[ -s fault.pid ]]
    csql -S -u dba "$db" -c 'SELECT v FROM retained;' > "$db.direct-restart.log" 2>&1
    grep -q '4633' "$db.direct-restart.log"
  fi
  # Retry is the documented recovery operation, even if the first write landed.
  cubrid activatehistorydb "$db" > "$db.retry.log" 2>&1
  csql -S -u dba "$db" -c 'SELECT v FROM retained;' > "$db.recovered.log" 2>&1
  grep -q '4633' "$db.recovered.log"
  use_engine baseline
  if csql -S -u dba "$db" -c 'SELECT 1;' > "$db.baseline.log" 2>&1; then
    echo "FAIL: baseline opened database after activation retry ($phase)" >&2
    exit 1
  fi
  grep -qi 'incompatible' "$db.baseline.log"
  echo "PASS: activation survives $phase and retry preserves data and compatibility"
done
