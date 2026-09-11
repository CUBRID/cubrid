#!/bin/bash
set -euo pipefail
mount --make-rprivate /
mount --bind "/home/vimkim/temp/p06/ctp-release" /mnt
mount --bind /home/vimkim /mnt/original-home
mount --bind /mnt/home /home/vimkim
mount --bind /mnt/hosts /etc/hosts
mount -t tmpfs -o size=512m tmpfs /tmp
ip link set lo up
mkdir /tmp/pgbuf-ctp
export CUBRID=/mnt/install CUBRID_DATABASES=/mnt/databases CUBRID_TMP=/tmp/pgbuf-ctp
export PATH=/mnt/install/bin:$PATH LD_LIBRARY_PATH=/mnt/install/lib:/mnt/install/cci/lib
export CTP_HOME=/mnt/CTP TMPDIR=/home/vimkim/temp
export PGBUF_INSPECTOR_TEST_DIR=/home/vimkim/gh/cb/CBRD-27398-pgbuf-inspector-contract/unit_tests/pgbuf_inspector
export PGBUF_INSPECTOR_FIXTURE=/home/vimkim/gh/cb/CBRD-27398-pgbuf-inspector-contract/build_preset_release_gcc/bin/pgbuf_inspector_fixture
printf 'namespace pid=%s net=%s ipc=%s\n' "$(readlink /proc/self/ns/pid)" "$(readlink /proc/self/ns/net)" "$(readlink /proc/self/ns/ipc)"
set +e
cubrid-shell-debug.sh /home/vimkim/gh/tc/CBRD-27398-pgbuf-inspector-fixtures/shell/_06_issues/_26_2h/cbrd_27398

result=$?
mkdir -p /mnt/evidence
cp -a /tmp/shell_single*.log /tmp/shell_single*.conf /mnt/evidence/ 2>/dev/null
mkdir -p /mnt/evidence/case
cp /home/vimkim/gh/tc/CBRD-27398-pgbuf-inspector-fixtures/shell/_06_issues/_26_2h/cbrd_27398/cases/{engine-identity.log,controlled.log,attachment.log,cbrd_27398.result} /mnt/evidence/case/
exit "$result"
