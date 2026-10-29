# Copyright 2026 CUBRID Corporation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import json
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest import mock

import bench
import ordinary


class OrdinaryFixtureTest(unittest.TestCase):
    def test_fixture_and_expected_values(self):
        sql = ordinary.fixture(200000, "trial_")
        self.assertIn("CREATE TABLE trial_seed (id INT PRIMARY KEY, a INT, s VARCHAR(100))", sql)
        self.assertIn("MOD(n, 100), SHA2(n || '', 256)", sql)
        self.assertEqual(ordinary.expected(200000), (200000, 9900000))
        self.assertEqual(ordinary.expected(101), (101, 4951))
        self.assertIn("a=MOD(id,100)+1", ordinary.validation("trial_update0", 1))


class ServerAssociationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "bin").mkdir()
        for executable in ("cub_server", "csql"):
            (self.root / "bin" / executable).write_bytes(b"executable")
        self.env = {"CUBRID": str(self.root), "CUBRID_CONF_FILE": "/config/cubrid.conf"}
        self.args = SimpleNamespace(server_pid=123, database="db_a", output=str(self.root / "out"),
                                    label="test", rows=100, scans=1, rounds=1, runs=3,
                                    source="a" * 40, timeout=1)
        self.identity = {"pid": 123, "start_ticks": 10, "executable": str(self.root / "bin/cub_server"),
                         "argv": ["cub_server", "db_a"]}

    def measure(self, server_env=None):
        original_read = Path.read_bytes
        env = self.env if server_env is None else server_env
        environ = b"\0".join(os.fsencode(k) + b"=" + os.fsencode(v) for k, v in env.items()) + b"\0"

        def read_bytes(path):
            return environ if str(path) == "/proc/123/environ" else original_read(path)

        with mock.patch.dict(os.environ, self.env, clear=True), \
             mock.patch.object(bench, "process_identity", return_value=self.identity), \
             mock.patch.object(Path, "read_bytes", read_bytes), \
             mock.patch.object(bench.shutil, "which", return_value=str(self.root / "bin/csql")), \
             mock.patch.object(bench.os, "sched_getaffinity", return_value={0}), \
             mock.patch.object(bench, "run", side_effect=RuntimeError("SQL reached")) as run:
            try:
                bench.measure(self.args)
            finally:
                self.run_calls = run.call_count

    def test_wrong_database_and_substring_fail_before_side_effects(self):
        for database in ("db_b", "db_a_extra"):
            with self.subTest(database=database):
                self.identity["argv"] = ["cub_server", database]
                with self.assertRaisesRegex(ValueError, "requested database"):
                    self.measure()
                self.assertEqual(self.run_calls, 0)
                self.assertFalse((self.root / "out").exists())

    def test_valid_launcher_arguments_recorded_before_sql(self):
        with self.assertRaisesRegex(RuntimeError, "SQL reached"):
            self.measure()
        manifest = json.loads((self.root / "out/summary.json").read_text())
        self.assertEqual(manifest["server"]["argv"], ["cub_server", "db_a"])
        self.assertEqual(self.run_calls, 1)

    def test_configuration_file_mismatch_fails_before_side_effects(self):
        with self.assertRaisesRegex(ValueError, "CUBRID_CONF_FILE"):
            self.measure({**self.env, "CUBRID_CONF_FILE": "/other/cubrid.conf"})
        self.assertEqual(self.run_calls, 0)
        self.assertFalse((self.root / "out").exists())

    def test_ordinary_rejects_wrong_database_before_output(self):
        self.identity["argv"] = ["cub_server", "db_b"]
        with mock.patch.object(bench, "process_identity", return_value=self.identity), \
             mock.patch.dict(os.environ, self.env, clear=True), \
             mock.patch.object(bench, "run") as run:
            with self.assertRaisesRegex(ValueError, "requested database"):
                ordinary.measure(self.args)
        run.assert_not_called()
        self.assertFalse((self.root / "out").exists())


if __name__ == "__main__":
    unittest.main()
