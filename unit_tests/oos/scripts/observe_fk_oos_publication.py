#!/usr/bin/env python3
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
"""Observe FK SET NULL publication in an isolated, HA-active debug server.

Run in the selected native work environment:
  python3 unit_tests/oos/scripts/observe_fk_oos_publication.py \
      --database claim07 --pid SERVER_PID --output-dir /path/to/new/results

Requires GDB attach permission and matching debug sources. The database registry
must contain only the selected test database. Only tables prefixed fk_oos07_ are
replaced. This observes native publication; it does not run a separate replica.
GDB reads inferior state without changing it.
"""

import argparse
import json
import os
from pathlib import Path
import random
import signal
import string
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--source-root', type=Path,
                        default=Path(__file__).resolve().parents[3],
                        help='Source checkout matching the observed debug binary')
    args = parser.parse_args()
    source = args.source_root.resolve()
    install = Path(os.environ['CUBRID']).resolve()
    if Path(f'/proc/{args.pid}/exe').resolve() != install / 'bin/cub_server':
        raise SystemExit('Server executable does not match CUBRID')
    cmdline = Path(f'/proc/{args.pid}/cmdline').read_bytes().split(b'\0')
    if cmdline[:2] != [b'cub_server', args.database.encode()]:
        raise SystemExit('Server database does not match --database')
    registry = Path(os.environ['CUBRID_DATABASES']) / 'databases.txt'
    names = [line.split()[0] for line in registry.read_text().splitlines()
             if line.strip() and not line.lstrip().startswith('#')]
    if names != [args.database]:
        raise SystemExit('Use an isolated registry containing only the test database')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    out = args.output_dir.resolve()

    def sql(name, text, error=False):
        (out / f'{name}.sql').write_text(text)
        with (out / f'{name}.log').open('wb') as log:
            result = subprocess.run([str(install / 'bin/csql'), '-C', '-u', 'dba', '-i',
                                     str(out / f'{name}.sql'), args.database],
                                    stdin=subprocess.DEVNULL, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=40)
        answer = (out / f'{name}.log').read_text()
        if error:
            if 'ERROR:' not in answer or 'foreign key' not in answer.lower():
                raise AssertionError(answer)
        elif result.returncode or 'ERROR:' in answer:
            raise AssertionError(answer)
        return answer

    rng = random.Random(799007)
    payload = ''.join(rng.choices(string.ascii_letters + string.digits, k=20000))
    child_payload = ''.join(rng.choices(string.ascii_letters + string.digits, k=6000))
    fixture = '''DROP TABLE IF EXISTS fk_oos07_integer;
DROP TABLE IF EXISTS fk_oos07_payload;
DROP TABLE IF EXISTS fk_oos07_restrict;
DROP TABLE IF EXISTS fk_oos07_parent;
CREATE TABLE fk_oos07_parent(id INTEGER PRIMARY KEY, payload VARCHAR(30000), second_payload VARCHAR(7000));
CREATE TABLE fk_oos07_integer(id INTEGER PRIMARY KEY, parent_id INTEGER,
 FOREIGN KEY(parent_id) REFERENCES fk_oos07_parent(id) ON UPDATE SET NULL);
CREATE TABLE fk_oos07_payload(id INTEGER PRIMARY KEY, parent_id INTEGER,
 payload VARCHAR(7000), second_payload VARCHAR(7000),
 FOREIGN KEY(parent_id) REFERENCES fk_oos07_parent(id) ON UPDATE SET NULL);
CREATE TABLE fk_oos07_restrict(id INTEGER PRIMARY KEY, parent_id INTEGER,
 FOREIGN KEY(parent_id) REFERENCES fk_oos07_parent(id) ON UPDATE RESTRICT);
'''
    for key in (10, 20, 30, 40, 50):
        fixture += f"INSERT INTO fk_oos07_parent VALUES({key},'{payload}','{child_payload}');\n"
    fixture += 'INSERT INTO fk_oos07_integer VALUES(1,20),(2,30),(3,30),(4,30);\n'
    for key, parent in ((1, 40), (2, 40), (3, 40), (4, 50)):
        fixture += f"INSERT INTO fk_oos07_payload VALUES({key},{parent},'{child_payload}','{child_payload}');\n"
    fixture += 'INSERT INTO fk_oos07_restrict VALUES(1,50);\n'
    sql('fixture', fixture)

    # Locate the existing boundary in baseline and fixed sources. No fixed line
    # numbers are tied to unrelated changes in this large source file.
    locator = (source / 'src/transaction/locator_sr.c').read_text().splitlines()
    call = next(i for i, line in enumerate(locator)
                if 'error_code = locator_check_primary_key_update (thread_p, index, old_key);' in line)
    before = max(i for i in range(call) if 'if (pk_btid_index == i)' in locator[i]) + 1
    # The fixed implementation suspends the containers before the existing LSA
    # save. Observe the outer state before that suspension.
    suspended = [i for i in range(max(0, call - 35), call)
                 if 'preserved_oos_oids;' in locator[i]]
    if suspended:
        before = suspended[0] + 1
    after = next(i for i in range(call + 1, len(locator))
                 if 'if (error_code != NO_ERROR)' in locator[i]) + 1
    gdb_python = r'''
import gdb, json
pending = []
repl_names = ('RVREPL_DATA_UPDATE', 'RVREPL_OOS_INSERT', 'RVREPL_DUMMY_OOS_RECORD')
repl_events = {int(gdb.parse_and_eval(name)): name for name in repl_names}
def vector(value):
    impl = value['_M_impl']
    start, finish = impl['_M_start'], impl['_M_finish']
    return [(start + i).dereference() for i in range(int(finish-start))]
def oid(value):
    return [int(value[x]) for x in ('volid','pageid','slotid')]
def lsa(value):
    return [int(value['pageid']), int(value['offset'])]
def state(frame):
    thread = frame.read_var('thread_p').dereference()
    tdes = frame.read_var('tdes').dereference()
    queue = tdes['oos_insert_lsa_queue']
    return dict(oids=[oid(v['oid']) for v in vector(thread['oos_oids'])],
                stamps=[lsa(v['identity_stamp']) for v in vector(thread['oos_oids'])],
                queue=[lsa(v) for v in vector(queue['m_queue'])],
                head=int(queue['m_head']), repl=lsa(tdes['repl_insert_lsa']))
def emit(event):
    print('FKOOS07 ' + json.dumps(event), flush=True)
class Before(gdb.Breakpoint):
    def stop(self):
        frame = gdb.newest_frame()
        event = dict(kind='before', cls=oid(frame.read_var('class_oid').dereference()),
                     state=state(frame))
        pending.append(event)
        emit(event)
        return False
class After(gdb.Breakpoint):
    def stop(self):
        frame = gdb.newest_frame()
        emit(dict(kind='after', cls=oid(frame.read_var('class_oid').dereference()),
                  state=state(frame), error=int(frame.read_var('error_code'))))
        pending.pop()
        return False
class Repl(gdb.Breakpoint):
    def stop(self):
        frame = gdb.newest_frame()
        code = int(frame.read_var('rcvindex'))
        if code in repl_events:
            thread = frame.read_var('thread_p').dereference()
            refs = [oid(v['oid']) for v in vector(thread['oos_oids'])]
            emit(dict(kind='repl', cls=oid(frame.read_var('class_oid').dereference()),
                      oid=oid(frame.read_var('inst_oid').dereference()),
                      event=repl_events[code], refs=refs))
        return False
Before('locator_sr.c:BEFORE')
After('locator_sr.c:AFTER')
Repl('repl_log_insert')
print('FKOOS07 READY', flush=True)
'''.replace('BEFORE', str(before)).replace('AFTER', str(after))
    (out / 'trace.gdb').write_text('set pagination off\nset confirm off\n'
                                  'set print thread-events off\npython\n' + gdb_python +
                                  '\nend\ncontinue\n')
    trace_path = out / 'trace.log'
    with trace_path.open('wb') as trace:
        debugger = subprocess.Popen(['gdb', '--nx', '-q', '-batch', '-iex',
                                     'set debuginfod enabled off', '-p', str(args.pid),
                                     '-x', str(out / 'trace.gdb')], stdin=subprocess.DEVNULL,
                                    stdout=trace, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 20
            while 'FKOOS07 READY' not in trace_path.read_text():
                if debugger.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(trace_path.read_text())
                time.sleep(0.1)
            for key in (10, 20, 30, 40):
                sql(f'update-{key}', f'UPDATE fk_oos07_parent SET id={key+1} WHERE id={key};')
            sql('error', 'UPDATE fk_oos07_parent SET id=51 WHERE id=50;', error=True)
            # Subsequent work also checks that ordinary transaction cleanup
            # permits another OOS preparation and successful publication.
            sql('after-error', 'UPDATE fk_oos07_parent SET id=12 WHERE id=11;')
        finally:
            if debugger.poll() is None:
                debugger.send_signal(signal.SIGINT)
                debugger.wait(timeout=10)
    raw = trace_path.read_text()
    if 'Python Exception' in raw or 'Error occurred in Python' in raw:
        raise AssertionError(raw)
    events = [json.loads(line[8:]) for line in raw.splitlines() if line.startswith('FKOOS07 {')]
    (out / 'events.json').write_text(json.dumps(events, indent=2) + '\n')
    before_events = [e for e in events if e['kind'] == 'before']
    after_events = [e for e in events if e['kind'] == 'after']
    assert len(before_events) == len(after_events) == 6, events
    parent = before_events[0]['cls']
    failures = []
    for first, last in zip(before_events, after_events):
        if first['cls'] != last['cls'] or first['state'] != last['state']:
            failures.append('Outer OIDs, queue, head or replication LSA changed across FK action')
    assert after_events[4]['error'] != 0, after_events
    assert all(e['error'] == 0 for i, e in enumerate(after_events) if i != 4), after_events
    # Group publications by the heap UPDATE that terminates each logical row.
    publications = []
    pending = []
    for event in events:
        if event['kind'] != 'repl':
            continue
        pending.append(event)
        if event['event'] == 'RVREPL_DATA_UPDATE':
            publications.append(pending)
            pending = []
    parent_rows = [row for row in publications if row[-1]['cls'] == parent]
    assert len(parent_rows) == 5, parent_rows
    for row in publications:
        refs = row[-1]['refs']
        inserts = [e['oid'] for e in row[:-1]]
        if inserts != refs or any(e['cls'] != row[-1]['cls'] for e in row):
            failures.append('OOS items do not match the logical row references in order')
    expected_count = len(parent_rows[0]) - 1
    if expected_count == 0 or any(len(row)-1 != expected_count for row in parent_rows):
        failures.append('Parent OOS publication differs from no-child control')
    child_rows = [row for row in publications if row[-1]['cls'] != parent]
    assert len(child_rows) == 8, child_rows
    assert sum(bool(row[:-1]) for row in child_rows) == 4, child_rows
    answer = sql('values', f'''SELECT COUNT(*) AS parent_values_ok FROM fk_oos07_parent
WHERE id IN (12,21,31,41,50) AND payload='{payload}' AND second_payload='{child_payload}';
SELECT COUNT(*) AS integer_nulls_ok FROM fk_oos07_integer WHERE parent_id IS NULL;
SELECT COUNT(*) AS child_values_ok FROM fk_oos07_payload
WHERE id IN (1,2,3) AND parent_id IS NULL
 AND payload='{child_payload}' AND second_payload='{child_payload}';
SELECT COUNT(*) AS failed_child_unchanged FROM fk_oos07_payload
WHERE id=4 AND parent_id=50 AND payload='{child_payload}' AND second_payload='{child_payload}';''')
    counts = []
    lines = answer.splitlines()
    for i, line in enumerate(lines):
        if line.strip() and set(line.strip()) == {'='}:
            counts.append(int(lines[i+1].strip()))
    assert counts == [5,4,3,1], answer
    if failures:
        raise AssertionError('\n'.join(failures))
    print(f'PASS: {expected_count} ordered parent OOS items per update; '
          'integer, multiple and OOS children; FK failure restoration; local values')
    print('Separate replica convergence was not executed.')


if __name__ == '__main__':
    main()
