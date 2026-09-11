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
"""Source/standby replication regression; run in a private Linux network namespace.

Reuses the loader runner's fresh database, registry, command logging and SQL checks.
See README.replication.md. Neither database nor its physical OIDs are shared.
"""
import json
import os
import signal
from pathlib import Path
import re
import socket
import subprocess
import time

from test_oos_loader import LoaderFixture


class ReplicationFixture:
    def __init__(self):
        self.source = LoaderFixture()
        self.replica = LoaderFixture()
        self.processes = []
        self.barrier = 0
        self.monitored = []
        # Runtime locks and utility logs use CUBRID/var and CUBRID/log, not the
        # database registry. Give each node a private view of the same binaries.
        installation = Path(os.environ["CUBRID"])
        for node in (self.source, self.replica):
            runtime = node.path / "runtime"
            runtime.mkdir()
            writable = {"var", "log", "tmp", "databases", "conf"}
            for child in installation.iterdir():
                if child.name not in writable:
                    (runtime / child.name).symlink_to(child)
            for name in writable:
                (runtime / name).mkdir()
            node.env["CUBRID"] = str(runtime)

    def launch(self, node, name, args):
        with (node.path / (name + ".log")).open("w") as out:
            process = subprocess.Popen(args, cwd=node.path, env=node.env,
                                       stdout=out, stderr=subprocess.STDOUT)
        self.processes.append(process)
        if name != "master":
            self.monitored.append(process)
        return process

    def start(self):
        # The existing hostname must resolve to a local interface for heartbeat.
        address = socket.gethostbyname(socket.gethostname())
        if not address.startswith("127."):
            subprocess.run(["ip", "addr", "add", address + "/32", "dev", "lo"], check=True)
        for node, port in ((self.source, 35419), (self.replica, 35420)):
            node.run("create", ["cubrid", "createdb", "--db-volume-size=64M",
                                "--log-volume-size=64M", "t17", "en_US.utf8"])
            conf = node.path / "conf/cubrid.conf"
            conf.write_text(conf.read_text().replace("35419", str(port)) +
                            "ha_mode=on\nha_applylogdb_ignore_error_list=-670\n")
            node.env["CUBRID_HA_CONF_FILE"] = str(node.path / "conf/cubrid_ha.conf")
            Path(node.env["CUBRID_HA_CONF_FILE"]).write_text(
                "[common]\nha_node_list=oos@" + socket.gethostname() +
                f"\nha_db_list=t17\nha_port_id={port + 10000}\n")
        self.source.run("heartbeat", ["cubrid", "heartbeat", "start"])
        for attempt in range(30):
            _, output = self.source.run(f"mode-{attempt}", ["cubrid", "changemode", "t17@localhost"])
            if "is active." in output:
                break
            time.sleep(1)
        else:
            raise AssertionError("source did not become active")
        self.launch(self.replica, "master", ["cub_master"])
        time.sleep(2)
        self.launch(self.replica, "server", ["cub_server", "t17"])
        time.sleep(3)
        _, output = self.replica.run("mode", ["cubrid", "changemode", "t17@localhost"])
        assert "is standby." in output, output
        self.logs = self.replica.path / "copied"
        self.logs.mkdir()
        self.launch(self.source, "copy", ["cub_admin", "copylogdb", "-L", str(self.logs),
                                          "-m", "async", "t17@localhost"])
        time.sleep(2)
        self.launch(self.replica, "apply", ["cub_admin", "applylogdb", "-L", str(self.logs), "t17@localhost"])
        # A newly attached applier starts at the current log position. Wait for its
        # durable checkpoint before creating any schema that this test must replicate.
        for attempt in range(30):
            _, output = self.replica.run(f"applier-ready-{attempt}",
                ["csql", "-C", "-u", "dba", "-c", "SELECT COUNT(*) FROM _db_ha_apply_info", "t17"])
            if re.findall(r"^\s*(\d+)\s*$", output, re.M) == ["1"]:
                break
            time.sleep(1)
        else:
            raise AssertionError("applier did not initialize its checkpoint")
        self.source.sql("CREATE TABLE barrier(id INT PRIMARY KEY)")
        self.sync()

    def sync(self):
        self.barrier += 1
        self.source.sql(f"INSERT INTO barrier VALUES({self.barrier})")
        for attempt in range(60):
            code, output = self.replica.run(f"barrier-{self.barrier}-{attempt}",
                ["csql", "-C", "-u", "dba", "-c",
                 f"SELECT COUNT(*) FROM barrier WHERE id={self.barrier}", "t17"], success=False)
            if code == 0 and re.findall(r"^\s*(\d+)\s*$", output, re.M) == ["1"]:
                return
            assert all(p.poll() is None for p in self.monitored), "replication process exited"
            time.sleep(1)
        raise AssertionError(f"replica did not reach barrier {self.barrier}; logs: {self.replica.path}")

    def count_both(self, table, condition, expected):
        for node in (self.source, self.replica):
            node.count(table, condition, expected)

    def chunks(self, node, table):
        output = node.sql(f"SHOW HEAP OOS OF {table}")
        return int(next(line.split()[10] for line in output.splitlines() if f"'dba.{table}'" in line))

    def test(self):
        s, r = self.source, self.replica
        s.sql("CREATE TABLE part(id INT PRIMARY KEY, b BIT VARYING, c BIT VARYING) "
              "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN(10), "
              "PARTITION p1 VALUES LESS THAN MAXVALUE)")
        s.sql("INSERT INTO part VALUES(1,REPEAT(X'AB',50000),REPEAT(X'CD',6000)), "
              "(2,X'EF',NULL),(3,REPEAT(X'12',6000),REPEAT(X'34',50000))")
        self.sync()
        self.count_both("part", "id=1 AND b=CAST(REPEAT('AB',50000) AS BIT VARYING) "
                        "AND c=CAST(REPEAT('CD',6000) AS BIT VARYING)", 1)
        self.count_both("part", "id=3 AND b=CAST(REPEAT('12',6000) AS BIT VARYING) "
                        "AND c=CAST(REPEAT('34',50000) AS BIT VARYING)", 1)
        assert self.chunks(r, "part__p__p0") == self.chunks(s, "part__p__p0")
        print("PASS INSERT multi-attribute/multi-chunk values and no duplicate replica chains", flush=True)
        s.sql("UPDATE part SET b=REPEAT(X'56',51000) WHERE id=1")
        self.sync()
        self.count_both("part__p__p0", "id=1 AND b=CAST(REPEAT('56',51000) AS BIT VARYING) "
                        "AND c=CAST(REPEAT('CD',6000) AS BIT VARYING)", 1)
        print("PASS same-partition UPDATE before movement can overwrite its result", flush=True)
        s.sql("UPDATE part SET id=11 WHERE id=1")
        self.sync()
        self.count_both("part__p__p1", "id=11 AND b=CAST(REPEAT('56',51000) AS BIT VARYING) "
                        "AND c=CAST(REPEAT('CD',6000) AS BIT VARYING)", 1)
        self.count_both("part__p__p0", "id=1", 0)
        for node in (s, r):
            assert self.chunks(node, "part") == 0
        print("PASS UPDATE, unassigned value, movement, destination ownership and publication order", flush=True)
        s.sql("DELETE FROM part WHERE id=3")
        s.sql("UPDATE part SET b=X'90', c=NULL WHERE id=11")
        self.sync()
        self.count_both("part", "id=11 AND b=X'90' AND c IS NULL", 1)
        self.count_both("part", "id=3", 0)
        print("PASS OOS-to-inline UPDATE before DELETE can hide its result", flush=True)
        s.sql("DELETE FROM part WHERE id=11")
        self.sync()
        self.count_both("part", "1=1", 1)
        self.count_both("part", "id=2 AND b=X'EF' AND c IS NULL", 1)
        print("PASS DELETE and OOS-to-inline controls", flush=True)

        s.sql("CREATE TABLE loaded(id INT PRIMARY KEY, b BIT VARYING)")
        rows = s.path / "ha.rows"
        with rows.open("w") as out:
            out.write("%class loaded (id b)\n")
            for i in range(90):
                out.write(f"{i} X'" + f"{i+1:02X}" * (32, 6000, 50000)[i % 3] + "'\n")
        s.load("ha-load", rows, batch=17)
        self.sync()
        self.count_both("loaded", "1=1", 90)
        for i in range(90):
            self.count_both("loaded", f"id={i} AND b=CAST(REPEAT('{i+1:02X}',"
                            f"{(32, 6000, 50000)[i % 3]}) AS BIT VARYING)", 1)
        assert self.chunks(r, "loaded") == self.chunks(s, "loaded")
        print("PASS HA loader queued-row lifetime, mixed payloads and row order", flush=True)

        s.sql("CREATE TABLE filtered(id INT PRIMARY KEY, b BIT VARYING)")
        s.sql("INSERT INTO filtered VALUES(1,X'CD')")
        control = s.path / "errors.control"
        control.write_text("-670\n")
        s.load("ha-filtered", s.rows("filtered.rows", "filtered", (1, 2, 3), (50000,)),
               extra=("--error-control-file", str(control)))
        s.sql("CREATE TABLE successful(id INT PRIMARY KEY, b BIT VARYING)")
        s.load("ha-successful", s.rows("successful.rows", "successful", (2, 3), (50000,)))
        self.sync()
        self.count_both("filtered", "id=1 AND b=X'CD'", 1)
        self.count_both("filtered", "id IN (2,3) AND b=CAST(REPEAT('AB',50000) AS BIT VARYING)", 2)
        for node in (s, r):
            assert self.chunks(node, "filtered") == self.chunks(node, "successful")
        print("PASS HA loader filtered failure and consecutive publications", flush=True)
        code, _ = s.run("failed-source-statement", ["csql", "-C", "-u", "dba", "-c",
            "INSERT INTO filtered VALUES(4,REPEAT(X'EF',50000)),(1,REPEAT(X'EF',50000))", "t17"], success=False)
        assert code != 0, "duplicate source statement must fail"
        s.sql("INSERT INTO filtered VALUES(5,X'90')")
        self.sync()
        self.count_both("filtered", "id=4", 0)
        self.count_both("filtered", "id=5 AND b=X'90'", 1)
        for node in (s, r):
            assert self.chunks(node, "filtered") == self.chunks(node, "successful")
        print("PASS source statement rollback and following inline publication", flush=True)


        r.count("_db_ha_apply_info", "fail_counter=0", 1)

        # Deliberate replica divergence: ignore a duplicate INSERT, then apply the next row.
        s.sql("CREATE TABLE divergent(id INT PRIMARY KEY, b BIT VARYING)")
        self.sync()
        r.run("maintenance", ["cubrid", "changemode", "-m", "maintenance", "t17@localhost"])
        r.sql("INSERT INTO divergent VALUES(1,X'CD')")
        r.run("standby", ["cubrid", "changemode", "-m", "standby", "t17@localhost"])
        s.sql("INSERT INTO divergent VALUES(1,REPEAT(X'AB',50000)),(2,REPEAT(X'AB',50000))")
        self.sync()
        r.count("divergent", "id=1 AND b=X'CD'", 1)
        r.count("divergent", "id=2 AND b=CAST(REPEAT('AB',50000) AS BIT VARYING)", 1)
        assert 2 * self.chunks(r, "divergent") == self.chunks(r, "successful"), "failed replica row left OOS chunks"
        r.count("_db_ha_apply_info", "fail_counter=1", 1)
        print("PASS ignored replica failure rolls back OOS group; next operation is intact", flush=True)

    def owned_processes(self, names):
        registries = {("CUBRID_DATABASES=" + str(node.path / "db")).encode()
                      for node in (self.source, self.replica)}
        for proc in Path("/proc").glob("[0-9]*"):
            try:
                if ((proc / "exe").resolve().name in names and
                    registries.intersection((proc / "environ").read_bytes().split(b"\0"))):
                    yield int(proc.name)
            except OSError:
                pass

    def close(self):
        # Stop supervisors first; otherwise heartbeat can restart an applier after
        # its launcher exits, retaining the database-name apply lock.
        for names in ({"cub_master"}, {"cub_admin", "cub_server"}):
            for sig in (signal.SIGTERM, signal.SIGKILL):
                for pid in self.owned_processes(names):
                    try:
                        os.kill(pid, sig)
                    except ProcessLookupError:
                        pass
                for _ in range(50):
                    if not list(self.owned_processes(names)):
                        break
                    time.sleep(0.1)
        for process in self.processes:
            process.wait(timeout=5)


if __name__ == "__main__":
    interfaces = json.loads(subprocess.check_output(["ip", "-j", "link", "show"]))
    if {link["ifname"] for link in interfaces} != {"lo"}:
        raise SystemExit("Run this test in a private network namespace; see README.replication.md")
    fixture = ReplicationFixture()
    try:
        fixture.start()
        fixture.test()
    finally:
        fixture.close()
