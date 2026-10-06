#!/usr/bin/env bash
# Copyright 2016 CUBRID Corporation
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

# Manual Release benchmark, outside CTest. Each sample owns a private database.
# Usage: bash benchmark_workspace_oos.sh INSTALL OUTPUT LABEL [REPEATS]
# Retains fixtures, command output and measurements; deletes only successful sample databases.
set -euo pipefail
install=$(realpath "${1:?installation required}")
output=$(realpath -m "${2:?new output directory required}")
label=${3:?label required}
repeats=${4:-3}
mkdir "$output"
export CUBRID="$install"
export PATH="$install/bin:$PATH"
export LD_LIBRARY_PATH="$install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
printf 'label,workload,rows,payload_bytes,repeat,elapsed,user_cpu,system_cpu\n' > "$output/results.csv"

for workload in small large; do
  if [[ $workload == small ]]; then
    rows=100000
    size=48
  else
    rows=10000
    size=5000
  fi
  payload=$(awk -v size="$size" 'BEGIN {srand(27424); for(i=0;i<size;i++) printf "%02x", int(rand()*256)}')
  fixture="$output/$workload.objects"
  awk -v rows="$rows" -v payload="$payload" 'BEGIN {
    print "%class bench (id v)";
    for(i=0;i<rows;i++) printf "%d X\047%s\047\n", i, payload;
  }' > "$fixture"
  for ((repeat=0; repeat<repeats; repeat++)); do
    root="$output/$workload-$repeat"
    mkdir "$root"
    export CUBRID_DATABASES="$root"
    export CUBRID_CONF_FILE="$root/cubrid.conf"
    printf '[common]\ndata_buffer_size=128M\nlog_buffer_size=32M\n' > "$CUBRID_CONF_FILE"
    (
      cd "$root"
      cubrid createdb --db-volume-size=256M --log-volume-size=256M --db-page-size=16K \
        -F "$root" bench en_US.utf8 > createdb.out 2>&1
      csql -S -u dba -c 'CREATE TABLE bench(id INTEGER, v BIT VARYING); COMMIT;' bench > schema.out 2>&1
      /usr/bin/time -f '%e,%U,%S' -o timing.csv \
        cubrid loaddb -S -u dba -d "$fixture" bench > loaddb.out 2>&1
      cat > check.sql <<SQL
SELECT CASE WHEN COUNT(*)=$rows AND MIN(id)=0 AND MAX(id)=$((rows-1))
AND SUM(CASE WHEN v=X'$payload' THEN 1 ELSE 0 END)=$rows
THEN 'VALUE_OK' ELSE 'VALUE_BAD' END AS verdict FROM bench;
SHOW HEAP OOS OF bench;
SQL
      csql -S -u dba -i check.sql bench > check.out 2>&1
      grep -q "'VALUE_OK'" check.out
      if grep -qE "ERROR:|'VALUE_BAD'" schema.out loaddb.out check.out; then
        printf 'Verification failed: %s\n' "$root" >&2
        exit 1
      fi
      cubrid deletedb bench > deletedb.out 2>&1
      printf '%s,%s,%s,%s,%s,%s\n' "$label" "$workload" "$rows" "$size" "$repeat" "$(cat timing.csv)" \
        | tee -a "$output/results.csv"
    )
  done
done
