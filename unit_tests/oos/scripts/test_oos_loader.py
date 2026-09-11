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
"""Real server loaddb regression. Run with the tested installation on PATH.

Linux example: unshare -Urn sh -c 'ip link set lo up; python3 test_oos_loader.py'
Every run retains an isolated fixture and command logs (/tmp by default). No existing
server or database is stopped. Peak-memory comparisons can reuse these exact
rows, batch sizes and worker settings on both installations.
"""
import concurrent.futures
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time


class LoaderFixture:
    def __init__(self):
        self.path = Path(tempfile.mkdtemp(prefix="oos-loader17-", dir=os.environ.get("OOS_LOADER_FIXTURE_DIR")))
        self.env = os.environ.copy()
        for name in ("conf", "db", "tmp"):
            (self.path / name).mkdir()
        self.env.update(CUBRID_CONF_FILE=str(self.path / "conf/cubrid.conf"),
                        CUBRID_DATABASES=str(self.path / "db"), CUBRID_TMP=str(self.path / "tmp"))
        (self.path / "conf/cubrid.conf").write_text(
            "[common]\ncubrid_port_id=35419\ndata_buffer_size=64M\nlog_buffer_size=16M\n"
            "loaddb_worker_count=4\nstring_max_size_bytes=32M\n")
        (self.path / "db/databases.txt").touch()
        self.sequence = 0
        print(f"fixture: {self.path}", flush=True)

    def isolate_runtime(self, installation):
        """Keep node locks and utility logs private while sharing installed binaries."""
        runtime = self.path / "runtime"
        runtime.mkdir()
        writable = {"var", "log", "tmp", "databases", "conf"}
        for child in installation.iterdir():
            if child.name not in writable:
                (runtime / child.name).symlink_to(child)
        for name in writable:
            (runtime / name).mkdir()
        self.env["CUBRID"] = str(runtime)

    def run(self, label, args, success=True):
        with (self.path / f"{label}.log").open("w") as log:
            result = subprocess.run(args, cwd=self.path, env=self.env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=120)
        output = (self.path / f"{label}.log").read_text(errors="replace")
        if success:
            assert result.returncode == 0, (label, result.returncode, output)
            assert "ERROR:" not in output, (label, output)
        return result.returncode, output

    def sql(self, statement):
        self.sequence += 1
        return self.run(f"sql-{self.sequence}", ["csql", "-C", "-u", "dba", "-c", statement, "t17"])[1]

    def count(self, table, condition, expected):
        output = self.sql(f"SELECT COUNT(*) AS matches FROM {table} WHERE {condition}")
        values = re.findall(r"^\s*(\d+)\s*$", output, re.M)
        assert values == [str(expected)], output

    def rows(self, name, table, ids, sizes=(32, 4000, 50000)):
        path = self.path / name
        with path.open("w") as out:
            out.write(f"%class {table} (id b)\n")
            for number in ids:
                size = sizes[number % len(sizes)]
                out.write(f"{number} X'" + "AB" * size + "'\n")
        return path

    def load(self, name, path, batch=10240, extra=(), success=True):
        return self.run(name, ["cubrid", "loaddb", "-C", "-u", "dba", "--no-statistics",
                              "-c", str(batch), "-d", str(path), *extra, "t17"], success)

    def close(self):
        # Own only processes launched from this exact fresh fixture directory.
        for proc in Path("/proc").glob("[0-9]*"):
            try:
                if ((proc / "exe").resolve().name in ("cub_server", "cub_master")
                    and ("CUBRID_DATABASES=" + str(self.path / "db")).encode()
                    in (proc / "environ").read_bytes().split(b"\0")):
                    os.kill(int(proc.name), signal.SIGTERM)
            except (OSError, ProcessLookupError):
                pass

    def test(self):
        self.run("create", ["cubrid", "createdb", "--db-volume-size=64M", "--log-volume-size=64M", "t17", "en_US.utf8"])
        self.run("start", ["cubrid", "server", "start", "t17"])
        self.sql("CREATE TABLE part(id INT, b BIT VARYING) PARTITION BY RANGE(id) "
                 "(PARTITION p0 VALUES LESS THAN(10), PARTITION p1 VALUES LESS THAN MAXVALUE)")
        self.load("partition", self.rows("partition.rows", "part", range(20), (50000,)), batch=7)
        self.count("part", "b=CAST(REPEAT('AB',50000) AS BIT VARYING)", 20)
        self.count("part__p__p0", "id < 10", 10)
        ownership = self.sql("SHOW ALL HEAP OOS OF part")
        rows = [line.split() for line in ownership.splitlines() if "'dba.part" in line]
        assert len(rows) == 3, ownership
        assert [int(row[5]) for row in rows] == [0, 1, 1], ownership
        print("PASS partition values and destination ownership", flush=True)

        self.sql("CREATE TABLE bulk(id INT, b BIT VARYING)")
        self.load("bulk", self.rows("bulk.rows", "bulk", range(600)))
        for remainder, size in enumerate((32, 4000, 50000)):
            self.count("bulk", f"MOD(id,3)={remainder} AND b=CAST(REPEAT('AB',{size}) AS BIT VARYING)", 200)
        print("PASS cleared-input lifetime, mixed sizes and retained-byte batch boundary", flush=True)
        self.load("oversized", self.rows("oversized.rows", "bulk", (600,), (9 * 1024 * 1024,)))
        self.count("bulk", "id=600 AND b=CAST(REPEAT('AB',9437184) AS BIT VARYING)", 1)
        print("PASS single row larger than the retained-byte budget", flush=True)

        self.sql("CREATE TABLE filtered(id INT PRIMARY KEY, b BIT VARYING)")
        self.sql("INSERT INTO filtered VALUES(1, X'CD')")
        control = self.path / "errors.control"
        control.write_text("-670\n")
        self.load("filtered", self.rows("filtered.rows", "filtered", (1, 2, 3), (50000,)),
                  extra=("--error-control-file", str(control)))
        self.count("filtered", "id=1 AND b=X'CD'", 1)
        self.count("filtered", "id IN (2,3) AND b=CAST(REPEAT('AB',50000) AS BIT VARYING)", 2)
        # Failed-row OOS chunks must be undone by the row sysop.
        output = self.sql("SHOW HEAP OOS OF filtered")
        filtered_chunks = int(next(line.split()[10] for line in output.splitlines() if "'dba.filtered'" in line))
        self.sql("CREATE TABLE control(id INT PRIMARY KEY, b BIT VARYING)")
        self.load("control", self.rows("control.rows", "control", (2, 3), (50000,)))
        output = self.sql("SHOW HEAP OOS OF control")
        assert filtered_chunks == int(next(line.split()[10] for line in output.splitlines() if "'dba.control'" in line)), output
        print("PASS filtered failure rollback and subsequent successful rows", flush=True)
        self.sql("CREATE TABLE input_filter(id INT, b BIT VARYING)")
        control.write_text("-569\n")
        incomplete = self.path / "incomplete.rows"
        incomplete.write_text("%class input_filter (id b)\n1\n2 NULL\n3 X''\n")
        self.load("input-filter", incomplete, extra=("--error-control-file", str(control)))
        self.count("input_filter", "id=1", 0)
        self.count("input_filter", "id=2 AND b IS NULL", 1)
        self.count("input_filter", "id=3 AND BIT_LENGTH(b)=0", 1)
        self.sql("CREATE TABLE fatal(id INT PRIMARY KEY, b BIT VARYING)")
        self.sql("INSERT INTO fatal VALUES(1, X'CD')")
        code, _ = self.load("fatal", self.rows("fatal.rows", "fatal", (2, 1, 3), (50000,)), success=False)
        assert code != 0, "unfiltered duplicate must fail the batch"
        self.count("fatal", "id IN (2,3)", 0)
        self.load("after-fatal", self.rows("after-fatal.rows", "fatal", (4,), (50000,)))
        self.count("fatal", "id=4 AND b=CAST(REPEAT('AB',50000) AS BIT VARYING)", 1)
        print("PASS filtered incomplete input, NULL/empty and failed-batch rollback", flush=True)

        self.sql("CREATE TABLE concurrent_rows(id INT, b BIT VARYING)")
        files = [self.rows(f"concurrent-{i}.rows", "concurrent_rows", range(i * 300, (i + 1) * 300), (50000,)) for i in range(2)]
        begin = time.monotonic()
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(self.load, f"concurrent-{i}", path, 50) for i, path in enumerate(files)]
            for future in futures:
                future.result(timeout=120)
        self.count("concurrent_rows", "b=CAST(REPEAT('AB',50000) AS BIT VARYING)", 600)
        print(f"PASS concurrent bulk progress: {time.monotonic()-begin:.2f}s", flush=True)
        (self.path / "workload.json").write_text(json.dumps({"workers": 4, "bulk_rows": 600,
            "bulk_payload_bytes": [32, 4000, 50000], "bulk_commit_rows": 10240,
            "concurrent_clients": 2, "concurrent_rows_per_client": 300,
            "concurrent_payload_bytes": 50000, "concurrent_commit_rows": 50, "oversized_payload_bytes": 9437184}, indent=2))
        self.run("stop", ["cubrid", "server", "stop", "t17"])
        self.run("sa-schema", ["csql", "-S", "-u", "dba", "-c",
                               "CREATE TABLE no_logging(id INT, b BIT VARYING)", "t17"])
        no_logging = self.rows("no-logging.rows", "no_logging", range(3), (50000,))
        self.run("sa-no-logging", ["cubrid", "loaddb", "-S", "-u", "dba", "--no-statistics",
                                   "--no-logging", "-d", str(no_logging), "t17"])
        _, output = self.run("sa-readback", ["csql", "-S", "-u", "dba", "-c",
            "SELECT COUNT(*) FROM no_logging WHERE b=CAST(REPEAT('AB',50000) AS BIT VARYING)", "t17"])
        assert re.findall(r"^\s*(\d+)\s*$", output, re.M) == ["3"], output
        print("PASS existing SA no-logging readback (no recovery guarantee claimed)", flush=True)


if __name__ == "__main__":
    fixture = LoaderFixture()
    try:
        fixture.test()
    finally:
        fixture.close()
