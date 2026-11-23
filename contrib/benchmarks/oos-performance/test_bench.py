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
import copy
import resource
import subprocess
import shutil
import sys
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


class BenchmarkEnvironmentTest(unittest.TestCase):
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


class ServerAssociationTest(BenchmarkEnvironmentTest):
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
             mock.patch.object(bench, "matching_utility", return_value="cubrid"), \
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


class FixtureMeasurementTest(BenchmarkEnvironmentTest):
    def exercise(self, before, after):
        calls = []
        def fake_run(command, prefix, timeout):
            calls.append(command)
            if command[0] == "perf":
                case = "narrow" if "narrow.sql" in str(command) else "wide"
                prefix.with_suffix(".stdout").write_text(" ".join(map(str, bench.expected(100)[case])) + "\n")
                prefix.with_suffix(".perf").write_text("\n".join(f"100;;{name};100;100" for name in bench.EVENTS))
            else:
                value = before if "before" in prefix.name else after
                prefix.with_suffix(".stdout").write_text(value)
            prefix.with_suffix(".stderr").write_text("")
            return 1.0
        with mock.patch.object(bench, "validate_server", return_value=(self.identity, self.root / "bin/csql")), \
             mock.patch.object(bench, "matching_utility", return_value="cubrid"), \
             mock.patch.object(bench.os, "sched_getaffinity", return_value={0}), \
             mock.patch.object(bench, "process_identity", return_value=self.identity), \
             mock.patch.object(bench, "capture_conditions", return_value={"same": True}), \
             mock.patch.object(bench, "run", side_effect=fake_run), \
             mock.patch("builtins.print"):
            try:
                bench.measure(self.args)
            finally:
                self.calls = calls

    def test_invalid_preflight_stops_before_perf(self):
        with self.assertRaises(ValueError):
            self.exercise("100 100 2 8000\n", "100 100 0 8000\n")
        self.assertFalse(any(c[0] == "perf" for c in self.calls))
        self.assertFalse(json.loads((self.root / "out/summary.json").read_text())["complete"])

    def test_malformed_preflight_output_stops_before_perf(self):
        for value in ("100 100 0\n", "100 100 0 8000\n1\n", "100 100 NULL 8000\n"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    self.exercise(value, "100 100 0 8000\n")
                self.assertFalse(any(c[0] == "perf" for c in self.calls))
                shutil.rmtree(self.root / "out")

    def test_valid_endpoints_and_timed_queries(self):
        self.exercise("100 100 0 8000\n", "100 100 0 8000\n")
        manifest = json.loads((self.root / "out/summary.json").read_text())
        self.assertTrue(manifest["complete"])
        self.assertEqual(manifest["fixture_validation"], {"version": 1, "before": {"rows": 100, "distinct_ids": 100, "invalid_rows": 0, "payload_bytes": 8000}, "after": {"rows": 100, "distinct_ids": 100, "invalid_rows": 0, "payload_bytes": 8000}})
        for case, query in bench.QUERIES.items():
            self.assertEqual((self.root / "out" / f"{case}.sql").read_text(), query + "\n")
        self.assertEqual(sum(c[0] == "perf" for c in self.calls), 6)

    def test_invalid_postflight_leaves_incomplete(self):
        with self.assertRaises(ValueError):
            self.exercise("100 100 0 8000\n", "100 100 1 8000\n")
        manifest = json.loads((self.root / "out/summary.json").read_text())
        self.assertFalse(manifest["complete"])
        self.assertEqual(sum(c[0] == "perf" for c in self.calls), 6)
        self.assertTrue(any("after" in p.name for p in (self.root / "out").glob("*.stdout")))


class ComparisonTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        env = {name: None for name in bench.LOCALE_ENV}
        conditions = {"version": 1, "boot_id": "boot", "architecture": "x86_64",
                      "server_affinity": [1], "client_affinity": [2],
                      "locale_environment": {"client": env, "server": env.copy()},
                      "server_parameters": {name: "0" for name in bench.PARAMETERS},
                      "charset": {"column_charset": "utf8", "column_collation": "utf8_bin",
                                  "literal_charset": "utf8", "literal_collation": "utf8_bin"}}
        counters = {name: {"value": 100, "unit": "", "running_percent": 100} for name in bench.EVENTS}
        self.manifest = {"schema": 3, "workload": "ordinary_scan", "complete": True, "label": "test",
                         "rows": 100, "scans": 1, "rounds": 1, "runs": 3, "queries": bench.QUERIES,
                         "fixture_sha256": "f", "harness_sha256": "h",
                         "conditions": {"before": conditions, "after": copy.deepcopy(conditions)},
                         "fixture_validation": {"version": 1, "before": {"rows": 100, "distinct_ids": 100, "invalid_rows": 0, "payload_bytes": 8000},
                                                "after": {"rows": 100, "distinct_ids": 100, "invalid_rows": 0, "payload_bytes": 8000}},
                         "samples": [{"round": 1, "case": case, "run": run, "warmup": run == 0,
                                      "elapsed_seconds": 1, "counters": copy.deepcopy(counters)}
                                     for case in bench.QUERIES for run in range(3)]}

    def compare(self, a, b, *options):
        paths = [self.root / "a.json", self.root / "b.json"]
        for path, data in zip(paths, (a, b)):
            path.write_text(json.dumps(data))
        # An unbounded allocation must fail fast here instead of exhausting the host.
        limit = lambda: resource.setrlimit(resource.RLIMIT_AS, (1 << 30, 1 << 30))
        return subprocess.run([sys.executable, str(Path(bench.__file__)), "compare", "--baseline", str(paths[0]),
                               "--candidate", str(paths[1]), *options], capture_output=True, text=True,
                              preexec_fn=limit)

    def test_equal_conditions_and_different_installations(self):
        other = copy.deepcopy(self.manifest)
        self.manifest["client"] = {"executable": "/one/csql"}
        other["client"] = {"executable": "/two/csql"}
        self.assertEqual(self.compare(self.manifest, other).returncode, 0)

    def test_missing_fixture_observations_reject_even_matching_manifests(self):
        data = copy.deepcopy(self.manifest)
        data.pop("fixture_validation", None)
        result = self.compare(data, data)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_invalid_fixture_evidence_rejected_even_when_both_match(self):
        for mutation in ("old1", "old2", "version", "missing_phase", "extra", "bool", "wrong", "not_object"):
            data = copy.deepcopy(self.manifest)
            if mutation == "old1": data["schema"] = 1
            if mutation == "old2": data["schema"] = 2
            if mutation == "version": data["fixture_validation"]["version"] = True
            if mutation == "missing_phase": del data["fixture_validation"]["after"]
            if mutation == "extra": data["fixture_validation"]["before"]["extra"] = 0
            if mutation == "bool": data["fixture_validation"]["before"]["invalid_rows"] = False
            if mutation == "wrong": data["fixture_validation"]["before"]["invalid_rows"] = 2
            if mutation == "not_object": data["fixture_validation"] = []
            with self.subTest(mutation=mutation):
                result = self.compare(data, data)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(result.stdout, "")

    def test_invalid_evidence_never_prints_verdict(self):
        variants = []
        for key in ("server_affinity", "server_parameters", "charset"):
            data = copy.deepcopy(self.manifest)
            for phase in ("before", "after"):
                del data["conditions"][phase][key]
            variants.append(data)
        for field, value in (("server_affinity", [3]), ("server_parameters", {**self.manifest["conditions"]["before"]["server_parameters"], "data_buffer_size": "8"}),
                             ("charset", {**self.manifest["conditions"]["before"]["charset"], "literal_charset": "euckr"})):
            data = copy.deepcopy(self.manifest)
            for phase in ("before", "after"):
                data["conditions"][phase][field] = value
            variants.append(data)
        data = copy.deepcopy(self.manifest)
        data["conditions"]["after"]["client_affinity"] = [9]
        variants.append(data)
        data = copy.deepcopy(self.manifest)
        for phase in ("before", "after"):
            data["conditions"][phase]["server_parameters"]["max_query_cache_entries"] = "1"
        variants.append(data)
        for val in (float("nan"), float("inf"), -1, 0):
            data = copy.deepcopy(self.manifest)
            data["samples"][0]["counters"]["cycles:u"]["value"] = val
            variants.append(data)
        for mutation in ("missing", "duplicate", "counter", "warmup", "elapsed", "schema", "rounds", "runs"):
            data = copy.deepcopy(self.manifest)
            if mutation == "missing": data["samples"].pop()
            if mutation == "rounds": data["rounds"] = 10**9
            if mutation == "runs": data["runs"] = 10**9
            if mutation == "duplicate": data["samples"].append(copy.deepcopy(data["samples"][0]))
            if mutation == "counter": del data["samples"][0]["counters"]["task-clock"]
            if mutation == "warmup": data["samples"][0]["warmup"] = False
            if mutation == "elapsed": data["samples"][0]["elapsed_seconds"] = float("nan")
            if mutation == "schema": data["schema"] = 1
            variants.append(data)
        missing_both = copy.deepcopy(self.manifest)
        for phase in ("before", "after"):
            del missing_both["conditions"][phase]["boot_id"]
        result = self.compare(missing_both, missing_both)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        old = {"schema": 1, "complete": True, "samples": []}
        self.assertEqual(self.compare(old, old).returncode, 2)
        for data in variants:
            with self.subTest(data=data):
                result = self.compare(self.manifest, data)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(result.stdout, "")
        for threshold in ("nan", "inf", "-1"):
            result = self.compare(self.manifest, self.manifest, "--max-overhead-percent", threshold)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
        overflow = copy.deepcopy(self.manifest)
        for sample in overflow["samples"]:
            sample["counters"]["instructions:u"]["value"] = 1e308
        result = self.compare(overflow, overflow)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")

    def test_parameter_and_charset_parsing(self):
        data = "\n".join(f"[S*] {name}=0 (1)" for name in bench.PARAMETERS)
        self.assertEqual(bench.parse_parameters("[  ] data_buffer_size=123 (0)\n" + data)["data_buffer_size"], "0")
        with self.assertRaises(ValueError): bench.parse_parameters(data + "\n[S ] data_buffer_size=1 (0)")
        with self.assertRaises(ValueError): bench.parse_parameters(data.replace("[S*] data_buffer_size=0 (1)", ""))
        with self.assertRaises(ValueError): bench.parse_parameters(data.replace("max_query_cache_entries=0", "max_query_cache_entries=1"))
        self.assertEqual(bench.parse_charset("utf8\tutf8_bin\tutf8\tutf8_bin\n")["literal_collation"], "utf8_bin")
        with self.assertRaises(ValueError): bench.parse_charset("'utf8'\n")

    def test_capture_records_live_outputs_and_affinity(self):
        args = SimpleNamespace(database="db", server_pid=42, timeout=1)
        def fake_run(command, prefix, timeout):
            if "paramdump" in command:
                output = "\n".join(f"[S ] {name}=0 (0)" for name in bench.PARAMETERS)
            else:
                output = "utf8\tutf8_bin\tutf8\tutf8_bin\n"
            prefix.with_suffix(".stdout").write_text(output)
            prefix.with_suffix(".stderr").write_text("")
        with mock.patch.object(bench, "run", side_effect=fake_run), \
             mock.patch.object(bench, "server_environment", return_value={b"LANG": b"C"}), \
             mock.patch.object(bench.os, "sched_getaffinity", side_effect=lambda pid: {1} if pid else {2}):
            captured = bench.capture_conditions(args, self.root, "/install/bin/csql", "/install/bin/cubrid", "before")
        self.assertEqual(captured["server_affinity"], [1])
        self.assertEqual(captured["client_affinity"], [2])
        self.assertEqual(captured["locale_environment"]["server"]["LANG"], "C")
        self.assertTrue((self.root / "conditions-before-parameters.stdout").exists())
        self.assertTrue((self.root / "conditions-before-charset.stdout").exists())

    def test_perf_rejects_duplicate_and_nonfinite_values(self):
        path = self.root / "counter.perf"
        lines = [f"100;;{name};100;100" for name in bench.EVENTS]
        path.write_text("\n".join(lines))
        self.assertEqual(len(bench.counters(path)), 3)
        for bad in (lines + [lines[0]], ["nan;;instructions:u;100;100", *lines[1:]],
                    [lines[0], lines[1], "100;;task-clock;100;101"]):
            path.write_text("\n".join(bad))
            with self.assertRaises(ValueError): bench.counters(path)


if __name__ == "__main__":
    unittest.main()
