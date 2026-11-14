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

import contextlib
import io
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

import test_oos_concurrent_insert as driver


class DriverChecks(unittest.TestCase):
    def test_exit_zero_error_and_timeout(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            result, _ = driver.command([sys.executable, '-c', "print('ERROR: insert failed')"],
                                       'error', path, time.monotonic() + 10)
            self.assertEqual(result['returncode'], 0)
            self.assertTrue(driver.failed(result))
            self.assertEqual(result['first_error'], 'ERROR: insert failed')
            result, _ = driver.command([sys.executable, '-c', 'import time; time.sleep(10)'],
                                       'timeout', path, time.monotonic() + 0.1)
            self.assertTrue(result['timed_out'])
            self.assertTrue(driver.failed(result))

    def test_fields_require_exactly_one(self):
        self.assertEqual(driver.field(' Oos_num_recs : 10\n', 'Oos_num_recs'), 10)
        for output in ('', ' Oos_num_recs : 10\n Oos_num_recs : 11\n'):
            with self.assertRaises(ValueError):
                driver.field(output, 'Oos_num_recs')

    def test_server_name_and_duplicate_status(self):
        output = ' Server other (rel 11, pid 3)\n Server db (rel 11, pid 4)\n'
        self.assertEqual(driver.server_pid(output, 'db'), 4)
        self.assertIsNone(driver.server_pid(output, 'missing'))
        with self.assertRaises(ValueError):
            driver.server_pid(output + ' Server db (rel 11, pid 5)\n', 'db')

    def test_changed_core_file(self):
        with tempfile.TemporaryDirectory() as temp:
            core = Path(temp) / 'core.1'
            before = driver.snapshot([str(Path(temp) / 'core*')])
            core.write_text('crash')
            after = driver.snapshot([str(Path(temp) / 'core*')])
            self.assertNotEqual(before, after)

    def test_collector_empty_records_and_error(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            good = {'returncode': 0, 'timed_out': False}
            empty = {'returncode': 1, 'timed_out': False}
            with mock.patch.object(driver, 'command', return_value=(empty, 'No coredumps found.\n')):
                self.assertEqual(driver.collector_records(42, time.time(), temp, path, 1), [])
            records = [{'pid': 42, 'exe': str(path / 'bin/cub_server'), 'time': 1},
                       {'pid': 42, 'exe': '/other/cub_server', 'time': 2}]
            with mock.patch.object(driver, 'command', return_value=(good, json.dumps(records))):
                self.assertEqual(driver.collector_records(42, time.time(), temp, path, 1), records[:1])
            with mock.patch.object(driver, 'command', return_value=(empty, 'permission denied')):
                with self.assertRaises(RuntimeError):
                    driver.collector_records(42, time.time(), temp, path, 1)

    def test_verdict_and_cleanup(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'log/coredump').mkdir(parents=True)
            state = {'running': False, 'statuses': 0, 'stopped': False}

            def fake_command(argv, name, output_dir, deadline, source=None):
                result = {'name': name, 'returncode': 0, 'timed_out': False,
                          'stdout': '', 'stderr': '', 'first_error': None}
                if name.startswith('status'):
                    state['statuses'] += 1
                    if state['running']:
                        pid = os.getpid() if name != 'status-final' else os.getppid()
                        return result, f' Server db (rel 11, pid {pid})\n'
                elif name == 'start':
                    state['running'] = True
                elif name == 'stop':
                    state['running'] = False
                    state['stopped'] = True
                elif name.endswith('_verify'):
                    return result, 'total_rows : 2\nmatching_rows : 2\nOos_num_recs : 1\n'
                elif name.endswith('_s0'):
                    (root / 'core.1').write_text('crash')
                    return result, 'AUTOCOMMIT IS ON\n'
                return result, ''

            env = {'CUBRID': str(root), 'CUBRID_DATABASES': str(root)}
            with mock.patch.dict(os.environ, env), mock.patch.object(driver, 'command', fake_command), \
                 mock.patch.object(driver, 'crash_patterns', return_value=([str(root / 'core*')], False)), \
                 mock.patch.object(sys, 'argv', ['driver', 'db', str(root / 'output'),
                                                '--sessions', '1', '--rows', '2', '--rounds', '1']), \
                 contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(driver.main(), 1)
            self.assertTrue(state['stopped'])
            report = json.loads(output.getvalue())
            self.assertEqual({item['assertion'] for item in report['failures']}, {4, 5, 6})
            self.assertTrue((Path(report['output_dir']) / 'summary.json').is_file())


if __name__ == '__main__':
    unittest.main()
