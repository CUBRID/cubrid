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
"""Logged recovery and concurrent SQL transactions in an isolated server fixture.

Run with the tested installation on PATH in a private network namespace; see
README.transactions.md. Retains commands, results and binary provenance.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import time

from test_oos_loader import LoaderFixture


def value(pattern):
    return f"CAST(REPEAT('{pattern}',50000) AS BIT VARYING)"


class Session:
    def __init__(self, fixture, name):
        self.log = fixture.path / (name + ".log")
        self.commands = fixture.path / (name + ".sql")
        self.step = 0
        with self.log.open("w") as out:
            self.process = subprocess.Popen(
                ["stdbuf", "-oL", "-eL", "csql", "-C", "-s", "--no-auto-commit",
                 "--no-pager", "-u", "dba", "t17"], cwd=fixture.path, env=fixture.env,
                stdin=subprocess.PIPE, stdout=out, stderr=subprocess.STDOUT, text=True)
        fixture.sessions.append(self)

    def sql(self, sql):
        self.step += 1
        marker = f"barrier_{self.step}"
        start = self.log.stat().st_size
        command = sql.rstrip("; \n") + f";\nSELECT '{marker}';\n"
        with self.commands.open("a") as out:
            out.write(command)
        self.process.stdin.write(command)
        self.process.stdin.flush()
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            output = self.log.read_text(errors="replace")[start:]
            assert "ERROR:" not in output, (sql, output)
            if re.search(r"^\s*'" + marker + r"'\s*$", output, re.M):
                return output
            assert self.process.poll() is None, (sql, output, self.process.returncode)
            time.sleep(0.05)
        raise AssertionError(("SQL barrier timed out", sql, output))

    def count(self, table, condition, expected):
        output = self.sql(f"SELECT COUNT(*) FROM {table} WHERE {condition}")
        assert re.findall(r"^\s*(\d+)\s*$", output, re.M) == [str(expected)], output

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=10)
        self.process.stdin.close()


class TransactionFixture(LoaderFixture):
    def __init__(self):
        super().__init__()
        self.sessions = []
        installation = Path(os.environ["CUBRID"])
        for name in ("cubrid", "cub_server", "csql"):
            resolved = shutil.which(name)
            assert resolved and Path(resolved).resolve() == (installation / "bin" / name).resolve(), (name, resolved, installation)
        runtime = self.path / "runtime"
        runtime.mkdir()
        writable = {"var", "log", "tmp", "databases", "conf"}
        for child in installation.iterdir():
            if child.name not in writable:
                (runtime / child.name).symlink_to(child)
        for name in writable:
            (runtime / name).mkdir()
        self.env["CUBRID"] = str(runtime)
        # Isolate transaction/recovery ownership from the independent baseline
        # rollback-vacuum defect. This runner does not claim vacuum verification.
        with Path(self.env["CUBRID_CONF_FILE"]).open("a") as out:
            out.write("vacuum_disable=yes\ncheckpoint_interval=1h\nauto_restart_server=no\n")
        revision = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True)
        source_diff = subprocess.run(["git", "diff", "HEAD", "--", "src", "unit_tests"], capture_output=True, check=True)
        provenance = {"source_revision": revision.stdout.strip(),
                      "source_diff_sha256": hashlib.sha256(source_diff.stdout).hexdigest(),
                      "installation": str(installation), "logging": True,
                      "vacuum_disabled": True}
        provenance["binaries"] = {name: hashlib.sha256((installation / "bin" / name).read_bytes()).hexdigest()
                                  for name in ("cub_server", "csql")}
        provenance["libraries"] = {str(path.relative_to(installation)): hashlib.sha256(path.read_bytes()).hexdigest()
                                   for path in (installation / "lib").glob("libcubrid*.so.*") if path.is_file()}
        (self.path / "provenance.json").write_text(json.dumps(provenance, indent=2))

    def chunks(self, table):
        output = self.sql(f"SHOW HEAP OOS OF {table}")
        return int(next(line.split()[10] for line in output.splitlines() if f"'dba.{table}'" in line))

    def ownership(self, table):
        output = self.sql(f"SHOW ALL HEAP OOS OF {table}")
        rows = [line.split() for line in output.splitlines() if f"'dba.{table}" in line]
        assert len(rows) == 3, output
        assert int(rows[0][5]) == 0, output
        return [int(row[10]) for row in rows]

    def crash(self):
        servers = []
        for proc in Path("/proc").glob("[0-9]*"):
            try:
                if ((proc / "exe").resolve().name == "cub_server" and
                    ("CUBRID_DATABASES=" + str(self.path / "db")).encode()
                    in (proc / "environ").read_bytes().split(b"\0")):
                    servers.append(int(proc.name))
            except OSError:
                pass
        assert len(servers) == 1, servers
        os.kill(servers[0], signal.SIGKILL)
        # Kill the server first: disconnecting a client first would test normal
        # transaction rollback instead of crash undo.
        for session in self.sessions:
            session.close()
        self.sessions.clear()
        deadline = time.monotonic() + 10
        while Path(f"/proc/{servers[0]}/exe").exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.run("restart", ["cubrid", "server", "start", "t17"])

    def test(self):
        self.run("create", ["cubrid", "createdb", "--db-volume-size=64M",
                            "--log-volume-size=64M", "t17", "en_US.utf8"])
        self.run("start", ["cubrid", "server", "start", "t17"])
        self.sql("CREATE TABLE snapshots(id INT PRIMARY KEY, b BIT VARYING) "
                 "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10), "
                 "PARTITION p1 VALUES LESS THAN MAXVALUE)")
        self.sql(f"INSERT INTO snapshots VALUES(1,{value('AA')}),(2,{value('BB')})")
        reader = Session(self, "old-reader")
        writer = Session(self, "writer")
        reader.sql("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ")
        reader.count("snapshots", f"id=1 AND b={value('AA')}", 1)
        writer.sql(f"UPDATE snapshots SET b={value('CC')} WHERE id=1")
        writer.sql(f"UPDATE snapshots SET id=12,b={value('DD')} WHERE id=2")
        reader.count("snapshots", f"id=1 AND b={value('AA')}", 1)
        reader.count("snapshots", f"id=2 AND b={value('BB')}", 1)
        writer.sql("COMMIT")
        reader.count("snapshots", f"id=1 AND b={value('AA')}", 1)
        reader.count("snapshots", f"id=2 AND b={value('BB')}", 1)
        writer.sql("DELETE FROM snapshots WHERE id=1")
        writer.sql("COMMIT")
        reader.count("snapshots", f"id=1 AND b={value('AA')}", 1)
        reader.sql("COMMIT")
        reader.count("snapshots", "id=1", 0)
        reader.count("snapshots", f"id=12 AND b={value('DD')}", 1)
        reader.sql("COMMIT")
        self.ownership("snapshots")
        print("PASS server MVCC before/after writer commit, movement, DELETE and new snapshot", flush=True)
        reader.close()
        writer.close()

        self.sql("CREATE TABLE recovery(id INT PRIMARY KEY, b BIT VARYING, c BIT VARYING) "
                 "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10), "
                 "PARTITION p1 VALUES LESS THAN MAXVALUE)")
        self.sql("CREATE TABLE log_barrier(id INT)")
        self.sql("CREATE SERIAL persist_serial START WITH 1 NOCACHE")
        output = self.sql("SELECT CAST(persist_serial.NEXT_VALUE AS INTEGER)")
        assert re.findall(r"^\s*(\d+)\s*$", output, re.M) == ["1"], output
        self.sql(f"INSERT INTO recovery VALUES(1,{value('11')},CAST(REPEAT('AB',6000) AS BIT VARYING))")
        committed = Session(self, "committed")
        committed.sql(f"INSERT INTO recovery VALUES(2,{value('22')},CAST(REPEAT('CD',6000) AS BIT VARYING))")
        committed.sql(f"UPDATE recovery SET b={value('33')} WHERE id=1")
        committed.sql("COMMIT")
        expected_chunks = self.ownership("recovery")
        pending = Session(self, "uncommitted")
        pending.sql(f"INSERT INTO recovery VALUES(13,{value('44')},CAST(REPEAT('EF',6000) AS BIT VARYING))")
        pending.sql(f"UPDATE recovery SET b={value('55')} WHERE id=2")
        pending.sql(f"UPDATE recovery SET id=11,b={value('66')} WHERE id=1")
        pending.count("recovery", f"id=13 AND b={value('44')}", 1)
        pending.count("recovery", f"id=2 AND b={value('55')}", 1)
        pending.count("recovery", f"id=11 AND b={value('66')}", 1)
        # Committing a separate transaction flushes the preceding uncommitted WAL
        # too, without committing the rows that recovery must undo.
        self.sql("INSERT INTO log_barrier VALUES(1)")
        self.crash()
        output = self.sql("SELECT CAST(persist_serial.NEXT_VALUE AS INTEGER)")
        assert re.findall(r"^\s*(\d+)\s*$", output, re.M) == ["2"], output
        self.count("recovery", "1=1", 2)
        self.count("recovery", f"id=1 AND b={value('33')} AND c=CAST(REPEAT('AB',6000) AS BIT VARYING)", 1)
        self.count("recovery", f"id=2 AND b={value('22')} AND c=CAST(REPEAT('CD',6000) AS BIT VARYING)", 1)
        assert self.ownership("recovery") == expected_chunks, (expected_chunks, self.ownership("recovery"))
        self.sql(f"INSERT INTO recovery VALUES(14,{value('77')},X'AB')")
        self.count("recovery", f"id=14 AND b={value('77')} AND c=X'AB'", 1)
        self.ownership("recovery")
        print("PASS committed INSERT/UPDATE, crash undo INSERT/UPDATE/movement, multi-chunk equality and live ownership", flush=True)
        recovery_log = (self.path / "runtime/log/server/t17_latest.err").read_text(errors="replace")
        assert "REDO Phase is finished" in recovery_log, recovery_log
        assert "UNDO Phase is finished" in recovery_log, recovery_log
        print("PASS serial direct-page persistence and completed recovery REDO/UNDO phases", flush=True)
        self.run("stop", ["cubrid", "server", "stop", "t17"])

    def close(self):
        for session in self.sessions:
            session.close()
        super().close()


if __name__ == "__main__":
    fixture = TransactionFixture()
    try:
        fixture.test()
    finally:
        fixture.close()
