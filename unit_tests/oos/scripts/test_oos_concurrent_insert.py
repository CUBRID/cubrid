#!/usr/bin/env python3
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

"""Start a CS server and test concurrent OOS inserts. Retain logs in OUTPUT_DIR."""
import argparse
import concurrent.futures
import datetime
import glob
import json
import os
import random
import re
import subprocess
import tempfile
import threading
import time
import uuid
from pathlib import Path


def positive(value):
    value = int(value)
    if value < 1:
        raise argparse.ArgumentTypeError('must be positive')
    return value


def command(argv, name, output_dir, deadline, source=None):
    out_path = output_dir / (name + '.out')
    err_path = output_dir / (name + '.err')
    result = {'name': name, 'returncode': None, 'timed_out': False,
              'stdout': str(out_path), 'stderr': str(err_path), 'first_error': None}
    try:
        with out_path.open('wb') as out, err_path.open('wb') as err:
            process = subprocess.Popen(argv, stdin=source or subprocess.DEVNULL,
                                       stdout=out, stderr=err, start_new_session=True)
            try:
                result['returncode'] = process.wait(timeout=max(0.01, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                result['timed_out'] = True
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    except OSError as exc:
        result['first_error'] = str(exc)
    output = out_path.read_text(errors='replace') + err_path.read_text(errors='replace')
    result['first_error'] = result['first_error'] or next(
        (line for line in output.splitlines() if re.search(r'\bERROR\b', line, re.I)), None)
    return result, output


def failed(result):
    return result['returncode'] != 0 or result['timed_out'] or result['first_error'] is not None


def field(output, name):
    values = re.findall(rf'^\s*(?:<\d+>\s*)?{name}\s*:\s*(\d+)\s*$', output, re.I | re.M)
    if len(values) != 1:
        raise ValueError(f'expected one {name}, got {values}')
    return int(values[0])


def server_pid(output, database):
    pids = re.findall(r'^\s*Server\s+' + re.escape(database) +
                      r'\s+\(rel\s+[^\n]*?,\s*pid\s+(\d+)\)', output, re.M)
    if len(pids) > 1:
        raise ValueError(f'multiple server entries for {database}: {pids}')
    return int(pids[0]) if pids else None


def generation(pid):
    # Field 22 follows the parenthesized command, which may contain spaces.
    return Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[19]


def crash_patterns(output_dir, database):
    cubrid = Path(os.environ['CUBRID'])
    patterns = [str(cubrid / 'log/coredump/**/*'), str(output_dir / 'core*'),
                str(Path.cwd() / 'core*'),
                str(Path(os.environ['CUBRID_DATABASES']) / database / '**/core*')]
    pattern = Path('/proc/sys/kernel/core_pattern').read_text().strip()
    if pattern.startswith('|'):
        if 'systemd-coredump' not in pattern:
            raise RuntimeError(f'unsupported core collector: {pattern}')
        collector = True
    else:
        collector = False
        core = Path(pattern)
        if '%' in str(core.parent):
            raise RuntimeError(f'unsupported dynamic core directory: {pattern}')
        filename = re.sub(r'%[a-zA-Z%]', '*', core.name)
        if core.is_absolute():
            patterns.append(str(core.parent / filename))
        else:
            for parent in (Path.cwd(), output_dir, Path(os.environ['CUBRID_DATABASES']) / database):
                patterns.append(str(parent / '**' / filename))
    return patterns, collector


def snapshot(patterns):
    result = {}
    for pattern in patterns:
        for name in glob.glob(pattern, recursive=True):
            path = Path(name)
            if path.is_file():
                stat = path.stat()
                result[str(path)] = (stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns)
    return result


def collector_records(pid, start, cubrid, output_dir, deadline):
    stamp = datetime.datetime.fromtimestamp(start).astimezone().strftime('%Y-%m-%d %H:%M:%S')
    result, output = command(['coredumpctl', '--no-pager', '--json=short', '--since', stamp,
                              'list', f'COREDUMP_PID={pid}'], 'coredumpctl', output_dir, deadline)
    if result['returncode'] == 1 and output.strip() == 'No coredumps found.':
        return []
    if result['returncode'] != 0 or result['timed_out']:
        raise RuntimeError(f'coredumpctl failed: {output.strip()}')
    try:
        records = json.loads(output)
        if not isinstance(records, list):
            raise ValueError('expected a JSON list')
        executable = str(Path(cubrid) / 'bin/cub_server')
        return [record for record in records if int(record['pid']) == pid and
                record['exe'] == executable]
    except (ValueError, TypeError, KeyError) as exc:
        raise RuntimeError(f'invalid coredumpctl output: {exc}') from exc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('database')
    parser.add_argument('output_dir', type=Path)
    parser.add_argument('--sessions', type=positive, default=8)
    parser.add_argument('--rows', type=positive, default=1500)
    parser.add_argument('--rounds', type=positive, default=3)
    parser.add_argument('--timeout', type=positive, default=120)
    args = parser.parse_args()
    if args.sessions * args.rows > 2147483647:
        parser.error('sessions * rows exceeds INT range')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output_dir = Path(tempfile.mkdtemp(prefix='run-', dir=args.output_dir)).resolve()
    run_id = uuid.uuid4().hex[:16]
    summary = {'database': args.database, 'run_id': run_id, 'rounds': [], 'failures': []}
    deadline = time.monotonic() + 550
    run_start = time.time()
    attempted_start = False
    initial_pid = None
    initial_generation = None
    patterns = []
    before = {}
    collector = False
    csql = ['csql', '-C', '-u', 'dba', '--line-output', '--no-pager', args.database]

    def failure(assertion, actual, expected, round_no=None, session=None):
        summary['failures'].append({'assertion': assertion, 'round': round_no,
                                    'actual': actual, 'expected': expected, 'session': session})

    def status(name, limit):
        result, output = command(['cubrid', 'server', 'status'], name, output_dir, limit)
        if failed(result):
            raise RuntimeError(f'server status failed: {result}')
        return server_pid(output, args.database)

    def client(name, sql, limit, barrier=None):
        sql_path = output_dir / (name + '.sql')
        sql_path.write_text(sql)
        if barrier:
            try:
                barrier.wait(timeout=max(0.01, limit - time.monotonic()))
            except threading.BrokenBarrierError:
                return {'name': name, 'returncode': None, 'timed_out': True,
                        'first_error': 'client barrier timed out', 'stdout': None, 'stderr': None}, ''
        with sql_path.open('rb') as source:
            return command(csql, name, output_dir, limit, source)

    try:
        patterns, collector = crash_patterns(output_dir, args.database)
        before = snapshot(patterns)
        if status('status-before', deadline) is not None:
            raise RuntimeError(f'{args.database} is already running; refusing to stop it')
        attempted_start = True
        result, _ = command(['cubrid', 'server', 'start', args.database], 'start', output_dir, deadline)
        if failed(result):
            raise RuntimeError(f'server start failed: {result}')
        initial_pid = status('status-started', deadline)
        if initial_pid is None:
            raise RuntimeError('server absent after start')
        initial_generation = generation(initial_pid)
        summary['server_before'] = {'pid': initial_pid, 'generation': initial_generation}
        for round_no in range(1, args.rounds + 1):
            limit = min(deadline, time.monotonic() + args.timeout)
            table = f'oos_concurrent_{run_id}_{round_no}'
            round_result = {'round': round_no, 'table': table, 'sessions': []}
            summary['rounds'].append(round_result)
            result, _ = client(f'r{round_no}_create', f'CREATE TABLE {table} (id INT PRIMARY KEY, '
                               'payload VARCHAR(6000) STORAGE FORCE_OUTLINE);\n', limit)
            if failed(result):
                failure('create', result, 'successful table creation', round_no)
                break
            sessions = []
            for number in range(args.sessions):
                start = number * args.rows
                rng = random.Random(number)
                sessions.append({'id_start': start, 'id_end': start + args.rows - 1,
                                 'name': f'r{round_no}_s{number}',
                                 'payload': bytes(rng.getrandbits(8) for _ in range(3000)).hex()})
            barrier = threading.Barrier(args.sessions)

            def run_session(session):
                sql = ';autocommit on\n' + ''.join(
                    f"INSERT INTO {table} VALUES ({row}, '{session['payload']}');\n"
                    for row in range(session['id_start'], session['id_end'] + 1))
                result, output = client(session['name'], sql, limit, barrier)
                if not re.search(r'AUTOCOMMIT IS ON', output, re.I):
                    result['first_error'] = result['first_error'] or 'autocommit was not enabled'
                return result

            with concurrent.futures.ThreadPoolExecutor(max_workers=args.sessions) as pool:
                futures = [pool.submit(run_session, session) for session in sessions]
                for session, future in zip(sessions, futures):
                    try:
                        result = future.result()
                    except Exception as exc:
                        result = {'name': session['name'], 'returncode': None, 'timed_out': False,
                                  'first_error': str(exc), 'stdout': None, 'stderr': None}
                    round_result['sessions'].append(result)
                    if failed(result):
                        failure(1, result, 'exit 0 and no ERROR', round_no, result['name'])
            matches = ' OR '.join(f"(id BETWEEN {s['id_start']} AND {s['id_end']} "
                                  f"AND payload = '{s['payload']}')" for s in sessions)
            sql = (f'SELECT COUNT(*) AS total_rows, SUM(CASE WHEN {matches} THEN 1 ELSE 0 END) '
                   f'AS matching_rows FROM {table};\nSHOW HEAP OOS OF {table};\n')
            result, output = client(f'r{round_no}_verify', sql, limit)
            if failed(result):
                failure('verification query', result, 'successful query', round_no)
            for assertion, name in ((2, 'total_rows'), (3, 'matching_rows'), (4, 'Oos_num_recs')):
                try:
                    actual = field(output, name)
                    round_result[name] = actual
                    expected = args.sessions * args.rows
                    if (actual < expected if assertion == 4 else actual != expected):
                        failure(assertion, actual, f'>= {expected}' if assertion == 4 else expected, round_no)
                except ValueError as exc:
                    failure(assertion, str(exc), f'one {name}', round_no)
            if time.monotonic() >= deadline and round_no < args.rounds:
                failure('deadline', round_no, args.rounds)
                break
    except Exception as exc:
        failure('execution', str(exc), 'successful execution')
    finally:
        cleanup_deadline = time.monotonic() + 35
        if attempted_start:
            try:
                current_pid = status('status-final', cleanup_deadline)
                current_generation = generation(current_pid) if current_pid is not None else None
                summary['server_after'] = {'pid': current_pid, 'generation': current_generation}
                if (initial_pid, initial_generation) != (current_pid, current_generation) or initial_pid is None:
                    failure(5, [current_pid, current_generation], [initial_pid, initial_generation])
            except Exception as exc:
                failure(5, str(exc), 'original server process')
            try:
                result, _ = command(['cubrid', 'server', 'stop', args.database], 'stop', output_dir, cleanup_deadline)
                if failed(result):
                    failure('cleanup', result, 'successful server stop')
                remaining = status('status-stopped', cleanup_deadline)
                if remaining is not None:
                    failure('cleanup', remaining, 'server absent')
            except Exception as exc:
                failure('cleanup', str(exc), 'server absent')
        try:
            if patterns:
                after = snapshot(patterns)
                new = {path: value for path, value in after.items() if before.get(path) != value}
                summary['crash_observation'] = {'patterns': patterns, 'files_before': before,
                                                'files_after': after, 'new_files': new,
                                                'collector': 'systemd-coredump' if collector else 'file'}
                if new:
                    failure(6, new, 'no new crash files')
                if collector and initial_pid is not None:
                    records = collector_records(initial_pid, run_start, os.environ['CUBRID'], output_dir, cleanup_deadline)
                    summary['crash_observation']['collector_records'] = records
                    if records:
                        failure(6, records, 'no server coredumps')
        except Exception as exc:
            failure(6, str(exc), 'successful crash observation')
        summary['status'] = 'failed' if summary['failures'] else 'passed'
        (output_dir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({'status': summary['status'], 'output_dir': str(output_dir),
                      'failures': summary['failures']}))
    return 0 if summary['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
