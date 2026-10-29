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

"""Run against a running CS database: python3 test_oos_concurrent_insert.py DB OUTPUT_DIR.

Requires csql on PATH and DBA access with no password. Retains tables and all artifacts.
The timeout covers each round, including setup, concurrent inserts, and validation.
"""
import argparse
import concurrent.futures
import json
import random
import re
import subprocess
import threading
import time
import uuid
from pathlib import Path


def positive(value):
    value = int(value)
    if value < 1:
        raise argparse.ArgumentTypeError('must be positive')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('database')
    parser.add_argument('output_dir', type=Path)
    parser.add_argument('--sessions', type=positive, default=8)
    parser.add_argument('--rows', type=positive, default=1500)
    parser.add_argument('--rounds', type=positive, default=1)
    parser.add_argument('--timeout', type=positive, default=120)
    args = parser.parse_args()
    if args.sessions * args.rows > 2147483647:
        parser.error('sessions * rows exceeds INT range')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    run_id = uuid.uuid4().hex[:16]
    summary = {'database': args.database, 'run_id': run_id, 'rounds': [], 'status': 'running'}
    command = ['csql', '-C', '-u', 'dba', '--line-output', '--no-pager', args.database]

    def client(name, sql, deadline, barrier=None):
        sql_path = args.output_dir / (name + '.sql')
        out_path = args.output_dir / (name + '.out')
        err_path = args.output_dir / (name + '.err')
        sql_path.write_text(sql)
        with sql_path.open('rb') as source, out_path.open('wb') as out, err_path.open('wb') as err:
            if barrier:
                barrier.wait(timeout=max(0.01, deadline - time.monotonic()))
            process = subprocess.Popen(command, stdin=source, stdout=out, stderr=err)
            try:
                code = process.wait(timeout=max(0.01, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                raise RuntimeError(name + ' timed out')
        output = out_path.read_text(errors='replace') + err_path.read_text(errors='replace')
        if code != 0 or re.search(r'\bERROR\b', output, re.IGNORECASE):
            raise RuntimeError(f'{name} failed (exit {code}); see logs')
        return output

    try:
        for round_no in range(args.rounds):
            deadline = time.monotonic() + args.timeout
            table = f'oos_concurrent_{run_id}_{round_no}'
            result = {'table': table, 'sessions': [], 'status': 'running'}
            summary['rounds'].append(result)
            client(f'r{round_no}_create', f'CREATE TABLE {table} (id INT PRIMARY KEY, '
                   'payload VARCHAR(6000) STORAGE FORCE_OUTLINE);\n', deadline)
            for session in range(args.sessions):
                start = session * args.rows
                rng = random.Random(session)
                payload = bytes(rng.getrandbits(8) for _ in range(3000)).hex()
                result['sessions'].append({'id_start': start, 'id_end': start + args.rows - 1,
                                           'name': f'r{round_no}_s{session}', 'payload': payload})
            barrier = threading.Barrier(args.sessions)
            def run_session(session):
                sql = ''.join(f"INSERT INTO {table} VALUES ({row}, '{session['payload']}');\n"
                              for row in range(session['id_start'], session['id_end'] + 1))
                client(session['name'], sql, deadline, barrier)
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.sessions) as pool:
                futures = [pool.submit(run_session, session) for session in result['sessions']]
                for future in futures:
                    future.result()
            matches = ' OR '.join(f"(id BETWEEN {s['id_start']} AND {s['id_end']} "
                                  f"AND payload = '{s['payload']}')" for s in result['sessions'])
            sql = (f'SELECT COUNT(*) AS total_rows, SUM(CASE WHEN {matches} THEN 1 ELSE 0 END) '
                   f'AS matching_rows FROM {table};\nSHOW HEAP OOS OF {table};\n')
            output = client(f'r{round_no}_verify', sql, deadline)
            def field(name):
                values = re.findall(rf'^\s*(?:<\d+>\s*)?{name}\s*:\s*(\d+)\s*$', output, re.I | re.M)
                if len(values) != 1:
                    raise RuntimeError(f'expected one {name} in verification output, got {values}')
                return int(values[0])
            total, matching, chunks = field('total_rows'), field('matching_rows'), field('Oos_num_recs')
            result.update(total_rows=total, matching_rows=matching, oos_num_recs=chunks)
            if total != args.sessions * args.rows or matching != total or chunks <= 0:
                raise RuntimeError(f'{table}: wrong row count, payload, or OOS record count')
            result['status'] = 'passed'
        summary['status'] = 'passed'
    except Exception as exc:
        summary['status'] = 'failed'
        summary['error'] = str(exc)
    finally:
        (args.output_dir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({'status': summary['status'], 'output_dir': str(args.output_dir),
                      'error': summary.get('error')}))
    return 0 if summary['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
