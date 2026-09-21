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


# Public lifecycle regression. Linux user namespaces, mount, ip and timeout
# are required. The argument is a complete installation, never a source tree.
# Database files and logs are retained in the printed temporary directory.
set -euo pipefail

if [[ ${1:-} != --inside ]]; then
  [[ $# == 1 ]] || exit 2
  root=${HISTORY_TEST_ROOT:-$HOME/.cache/cubrid-history}
  mkdir -p "$root"
  scratch=$(mktemp -d "$root/cdc.XXXXXX")
  echo "CDC evidence: $scratch"
  mkdir "$scratch/install"
  for dir in include bin lib lib64 conf msg locales timezones cci jdbc vm; do
    [[ ! -d $1/$dir ]] || cp -aL --reflink=auto "$1/$dir" "$scratch/install/"
  done
  if [[ -n ${HISTORY_WRITER_INSTALL:-} ]]; then
    mkdir "$scratch/writer"
    for dir in include bin lib lib64 conf msg locales timezones cci jdbc vm; do
      [[ ! -d $HISTORY_WRITER_INSTALL/$dir ]] || cp -aL --reflink=auto "$HISTORY_WRITER_INSTALL/$dir" "$scratch/writer/"
    done
  fi
  cp "${BASH_SOURCE[0]}" "$scratch/test.sh"
  cp "$(dirname "${BASH_SOURCE[0]}")/cdc_history_client.c" "$scratch/client.c"
  cp "$(dirname "${BASH_SOURCE[0]}")/cdc_history_payload.hex" "$scratch/payload.hex"
  printf '127.0.0.1 localhost %s\n' "$(hostname)" > "$scratch/hosts"
  : > "$scratch/resolv.conf"
  exec timeout --signal=KILL 300 unshare -r --mount-proc -i -p -f -n --kill-child=SIGKILL bash -c '
    set -eu
    mount --bind "$1" /mnt
    mount --bind /mnt/hosts /etc/hosts
    mount --bind /mnt/resolv.conf /etc/resolv.conf
    mount -t tmpfs -o size=256m tmpfs /tmp
    ip link set lo up
    exec bash /mnt/test.sh --inside
  ' _ "$scratch"
fi
ulimit -c 0
export CUBRID=/mnt/install CUBRID_DATABASES=/mnt/databases
export PATH=$CUBRID/bin:$PATH LD_LIBRARY_PATH=$CUBRID/lib:$CUBRID/lib64
mkdir -p /mnt/databases /mnt/work
cd /mnt/work
mkdir -p "$CUBRID/log/server"
trap 'csql -u dba history -c "SELECT 1;" > final-survival.log 2>&1 || true' EXIT
cat > "$CUBRID/conf/cubrid.conf" <<CONF
[service]
service=server
[common]
cubrid_port_id=1523
data_buffer_size=64M
log_buffer_size=16M
java_stored_procedure=no
supplemental_log=${HISTORY_SUPPLEMENTAL:-1}
log_compress=${HISTORY_COMPRESS:-no}
log_max_archives=2147483647
CONF
if [[ -d /mnt/writer ]]; then
  cp "$CUBRID/conf/cubrid.conf" /mnt/writer/conf/cubrid.conf
  export CUBRID=/mnt/writer
  export PATH=$CUBRID/bin:$PATH LD_LIBRARY_PATH=$CUBRID/lib:$CUBRID/lib64
  mkdir -p "$CUBRID/log/server"
fi
cc -Wall -Wextra -I"$CUBRID/include" /mnt/client.c -L"$CUBRID/lib" -lcubridcs -o /mnt/client
cubrid createdb --db-volume-size=64M --log-volume-size=64M --db-page-size="${HISTORY_PAGE_SIZE:-4K}" --log-page-size="${HISTORY_PAGE_SIZE:-4K}" history en_US > create.log 2>&1
cubrid server start history > start.log 2>&1
initial_payload="CAST(REPEAT('AA', 32768) AS BIT VARYING)"
if [[ ${HISTORY_ENTROPY:-0} == 1 ]]; then
  initial_payload="X'$(cat /mnt/payload.hex)'"
fi
partition_clause=''
[[ ${HISTORY_PARTITION:-0} != 1 ]] || partition_clause='PARTITION BY HASH(id) PARTITIONS 2'
csql -u dba history -c "CREATE TABLE t (id INT PRIMARY KEY, payload BIT VARYING(300000), extra BIT VARYING(8192) STORAGE FORCE_OUTLINE) $partition_clause; INSERT INTO t SELECT rownum, $initial_payload, CAST(REPEAT('CC',513) AS BIT VARYING) FROM db_class LIMIT 10;" > setup.log 2>&1
csql -u dba history -c 'SHOW ALL HEAP OOS OF t;' > seed-oos.log 2>&1
if [[ ${HISTORY_FAULTS:-0} == 1 ]]; then
  cubrid server stop history > fault-stop.log 2>&1
  cp "$CUBRID/conf/cubrid.conf" /mnt/normal.conf
  for fault in 500002 500003 500004 500006; do
    cp /mnt/normal.conf "$CUBRID/conf/cubrid.conf"
    printf '\nfault_injection_ids=%s\n' "$fault" >> "$CUBRID/conf/cubrid.conf"
    for operation in insert update delete; do
      case $operation in
        insert) statement="INSERT INTO t VALUES (11, CAST(REPEAT('CC', 32768) AS BIT VARYING), CAST(REPEAT('CC',513) AS BIT VARYING));" ;;
        update) statement="UPDATE t SET payload = CAST(REPEAT('CC', 32768) AS BIT VARYING) WHERE id = 1;" ;;
        delete) statement="DELETE FROM t WHERE id = 1;" ;;
      esac
      csql -S -u dba history -c "$statement" > "fault-$fault-$operation.log" 2>&1 || true
      grep -qi 'fault injection' "fault-$fault-$operation.log"
      csql -S -u dba history -c "SELECT COUNT(*) FROM t WHERE payload = CAST(REPEAT('AA',32768) AS BIT VARYING);" > "fault-$fault-$operation-count.log" 2>&1
      grep -Eq '^ *10 *$' "fault-$fault-$operation-count.log"
    done
  done
  cp /mnt/normal.conf "$CUBRID/conf/cubrid.conf"
  echo 'PASS: construction, image append, DML reference and user metadata failures roll back INSERT/UPDATE/DELETE'
  cubrid server start history > fault-restart.log 2>&1
fi
if [[ ${HISTORY_LIFECYCLE:-0} == 1 ]]; then
  csql -u dba history -c 'DELETE FROM t;' > clear.log 2>&1
fi
sleep 1
if [[ ${HISTORY_TRIGGER:-0} == 1 ]]; then
  csql -u dba history -c "CREATE TABLE audit LIKE t; CREATE TRIGGER ti AFTER INSERT ON t EXECUTE INSERT INTO audit VALUES (obj.id, obj.payload, obj.extra); CREATE TRIGGER tu AFTER UPDATE ON t EXECUTE UPDATE audit SET payload = obj.payload WHERE id = obj.id; CREATE TRIGGER td BEFORE DELETE ON t EXECUTE DELETE FROM audit WHERE id = obj.id;" > triggers.log 2>&1
  if [[ ${HISTORY_LIFECYCLE:-0} != 1 ]]; then
    csql -u dba history -c 'INSERT INTO audit SELECT * FROM t;' > audit-seed.log 2>&1
  fi
fi
sleep 1
history_start=$(date +%d-%m-%Y:%H:%M:%S)
if [[ ${HISTORY_SUPPLEMENTAL:-1} != 0 ]]; then
  /mnt/client find start.lsa 1
fi
csql -u dba history -c 'SHOW LOG HEADER;' > wal-before.log 2>&1
workload_start=$(date +%s%N)
if [[ ${HISTORY_LIFECYCLE:-0} == 1 ]]; then
  csql -u dba history -c "INSERT INTO t SELECT rownum, $initial_payload, CAST(REPEAT('CC',513) AS BIT VARYING) FROM db_class LIMIT 10;" > lifecycle.log 2>&1
  csql -u dba history -c 'SHOW ALL HEAP OOS OF t;' > before-update.log 2>&1
  csql -u dba history -c "UPDATE t SET payload = CAST(REPEAT('BB', 32768) AS BIT VARYING);" >> lifecycle.log 2>&1
fi
if [[ ${HISTORY_CORRUPT:-0} == 1 ]]; then
  cubrid server stop history > corrupt-stop.log 2>&1
  printf '\nfault_injection_ids=500005\n' >> "$CUBRID/conf/cubrid.conf"
  cubrid server start history > corrupt-start.log 2>&1
fi
csql -u dba history -c 'SHOW ALL HEAP OOS OF t;' > before-vacuum.log 2>&1
csql -u dba history -c 'DELETE FROM t;' > delete.log 2>&1
workload_end=$(date +%s%N)
printf 'workload_ns=%s\n' "$((workload_end - workload_start))" > cost.log
awk '/VmHWM|VmRSS/ {print}' "/proc/$(pgrep -x cub_server)/status" >> cost.log
csql -u dba history -c 'SHOW LOG HEADER;' > wal-after.log 2>&1
if [[ ${HISTORY_CRASH:-0} == 1 ]]; then
  kill -KILL "$(pgrep -x cub_master)"
  kill -KILL "$(pgrep -x cub_server)"
  for ((retry=0; retry<50; retry++)); do
    pgrep -x cub_server > /dev/null || break
    sleep .1
  done
else
  cubrid server stop history > stop.log 2>&1
fi
cubrid vacuumdb -S history > vacuum.log 2>&1
if [[ ${HISTORY_BACKUP:-0} == 1 ]]; then
  mkdir /mnt/backup
  cubrid backupdb -S -D /mnt/backup history > backup.log 2>&1
  cubrid restoredb -B /mnt/backup history > restore.log 2>&1
fi
csql -S -u dba history -c 'SHOW ALL HEAP OOS OF t;' > after-vacuum.log 2>&1
# The SHOW result contains table name, class OID, then twelve numeric fields.
# Its OOS_NUM_RECS column is the public reclamation witness.
python3 - <<'PYCODE'
import re
from pathlib import Path
for name, empty in [('before-vacuum.log', False), ('after-vacuum.log', True)]:
    rows = [x for x in Path(name).read_text().splitlines() if "'dba.t" in x]
    assert rows, name
    count = sum(int(row.split()[10]) for row in rows)
    if not empty:
        samples = ['before-update.log']
        if __import__('os').environ.get('HISTORY_TRIGGER') == '1': samples.append('seed-oos.log')
        for sample in samples:
            if Path(sample).exists():
                earlier = [x for x in Path(sample).read_text().splitlines() if "'dba.t" in x]
                count = max(count, sum(int(row.split()[10]) for row in earlier))
    if not empty:
        before = count
        assert count > 0, (name, count, rows)
    elif __import__('os').environ.get('HISTORY_PARTITION') == '1':
        assert count < before, (name, before, count, rows)
    else:
        assert count == 0, (name, count, rows)
    print(name, 'OOS_NUM_RECS=', count)
PYCODE
[[ ${HISTORY_SUPPLEMENTAL:-1} != 0 ]] || { cat cost.log; exit 0; }
export CUBRID=/mnt/install
export PATH=$CUBRID/bin:$PATH LD_LIBRARY_PATH=$CUBRID/lib:$CUBRID/lib64
cubrid server start history > restart.log 2>&1
if [[ ${HISTORY_REPOSITION:-0} == 1 ]]; then
  /mnt/client find resume.lsa 1
  csql -u dba history -c 'CREATE TABLE supported (id INT PRIMARY KEY, val INT); INSERT INTO supported VALUES (99, 4633);' > reposition-sql.log 2>&1
fi
/mnt/client "${HISTORY_CLIENT_MODE:-drain}" start.lsa 1
/mnt/client "${HISTORY_CLIENT_MODE:-drain}" start.lsa 0
csql -u dba history -c 'SELECT COUNT(*) FROM t;' > survival.log 2>&1

sleep 1
history_end=$(date +%d-%m-%Y:%H:%M:%S)
flashback_tables=(dba.t)
[[ ${HISTORY_PARTITION:-0} != 1 ]] || flashback_tables=(dba.t__p__p0 dba.t__p__p1)
[[ ${HISTORY_TRIGGER:-0} != 1 ]] || flashback_tables+=(dba.audit)
cubrid flashback -p '' -s "$history_start" -e "$history_end" history "${flashback_tables[@]}" < /dev/null > flashback-summary.log 2>&1
awk '$1 ~ /^[0-9]+$/ && $2 == "DBA" {print $1}' flashback-summary.log > transactions.txt
[[ -s transactions.txt ]]
if [[ ${HISTORY_CLIENT_MODE:-drain} == reject ]]; then
  transaction=$(head -1 transactions.txt)
  if printf '%s\n' "$transaction" | cubrid flashback -p '' --detail -s "$history_start" -e "$history_end" history "${flashback_tables[@]}" > flashback-rejected.log 2>&1; then
    echo 'Expected a checked flashback rejection' >&2
    exit 1
  fi
  grep -Eq 'Historical OOS values are not available|historical row image is malformed' flashback-rejected.log
  csql -u dba history -c 'SELECT 1;' > survival.log 2>&1
  echo 'Flashback rejected unsupported history; server remains available'
  exit 0
fi
while read -r transaction; do
  printf '%s\n' "$transaction" | cubrid flashback -p '' --detail -s "$history_start" -e "$history_end" history "${flashback_tables[@]}" > "flashback-$transaction.log" 2>&1
done < transactions.txt
python3 - <<'PYCODE'
from pathlib import Path
lines = [line for p in Path('.').glob('flashback-*.log') for line in p.read_text().splitlines() if line.startswith('[ORIGINAL]')]
expected = 30 if __import__('os').environ.get('HISTORY_LIFECYCLE') == '1' else 10
if __import__('os').environ.get('HISTORY_TRIGGER') == '1': expected *= 2
assert len(lines) == expected, (len(lines), expected)
for line in lines:
    assert ('aa' * 32768 in line.lower()) or ('bb' * 32768 in line.lower()) or (Path('/mnt/payload.hex').read_text().strip() in line.lower()), line[:100]
print('Flashback full historical values:', len(lines))
PYCODE
