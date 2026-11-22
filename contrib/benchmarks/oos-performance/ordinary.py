#!/usr/bin/env python3
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

"""Reproduce the reviewer's ordinary-table scan and fresh-clone UPDATE."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import subprocess
import time

import bench


def fixture(rows, prefix):
    return f"""CREATE TABLE {prefix}seq (n INT PRIMARY KEY);
INSERT INTO {prefix}seq SELECT ROWNUM FROM db_attribute a, db_attribute b, db_attribute c LIMIT {rows};
CREATE TABLE {prefix}seed (id INT PRIMARY KEY, a INT, s VARCHAR(100));
INSERT INTO {prefix}seed SELECT n, MOD(n, 100), SHA2(n || '', 256) FROM {prefix}seq;
COMMIT;
UPDATE STATISTICS ON {prefix}seed;
"""


def expected(rows):
    periods, tail = divmod(rows, 100)
    return (rows, periods * 4950 + tail * (tail + 1) // 2)


def validation(table, increment):
    return (f"SELECT COUNT(*), MIN(id), MAX(id), "
            f"SUM(CASE WHEN a=MOD(id,100)+{increment} AND s=SHA2(id || '',256) "
            f"THEN 1 ELSE 0 END), SUM(LENGTH(s)) FROM {table};\n")


def wait_for_idle(pid):
    def cpu_ticks():
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])

    observations = []
    streak = 0
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        before = cpu_ticks()
        time.sleep(0.25)
        used = (cpu_ticks() - before) / os.sysconf("SC_CLK_TCK")
        observations.append(used)
        streak = streak + 1 if used <= 0.0101 else 0
        if streak >= 8:
            return observations
    raise ValueError(f"server did not become idle before counters: {observations}")


def measure(args):
    identity, client = bench.validate_server(args.server_pid, args.database)
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    environment = {name: os.environ.get(name) for name in
                   ("CUBRID", "CUBRID_CONF", "CUBRID_CONF_FILE", "CUBRID_DATABASES", "PRESET_MODE")}
    config = environment["CUBRID_CONF_FILE"]
    manifest = {"schema": 1, "database": args.database, "label": args.label, "source": args.source,
                "rows": args.rows, "prefix": args.prefix, "runs": args.runs,
                "fixture_sha256": hashlib.sha256(fixture(args.rows, args.prefix).encode()).hexdigest(),
                "harness_sha256": bench.digest(__file__), "helpers_sha256": bench.digest(bench.__file__),
                "server": identity, "server_sha256": bench.digest(identity["executable"]),
                "client": {"executable": str(client), "sha256": bench.digest(client)},
                "environment": environment, "configuration_sha256": bench.digest(config) if config else None,
                "affinity": sorted(os.sched_getaffinity(args.server_pid)),
                "update_start_state": "fresh ordered clone for every sample", "samples": [], "complete": False}
    if config:
        (output / "cubrid.conf").write_bytes(Path(config).read_bytes())

    def save():
        (output / "summary.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def sql(name, text):
        path = output / (name + ".sql")
        path.write_text(text)
        bench.run(bench.csql_command(args.database, path, str(client)), output / name, args.timeout)
        return bench.results(output / (name + ".stdout"))

    def check(table, name, increment=0):
        actual = sql(name, validation(table, increment))
        if actual != [(args.rows, 1, args.rows, args.rows, args.rows * 64)]:
            raise ValueError(f"invalid table contents: {name}.stdout: {actual}")

    def storage(table, name):
        statements = f"SHOW HEAP CAPACITY OF {table};\n"
        if args.show_oos:
            statements += f"SHOW HEAP OOS OF {table};\n"
        sql(name, statements)

    save()
    seed = args.prefix + "seed"
    check(seed, "seed-validation")
    one = hashlib.sha256(b"1").hexdigest().upper()
    if sql("sha2-case", f"SELECT COUNT(*) FROM {seed} WHERE id=1 AND s='{one}';\n") != [(1,)]:
        raise ValueError("unexpected SHA2 output")
    scan = args.prefix + "scan"
    def clone(table):
        return (f"CREATE TABLE {table} (id INT PRIMARY KEY, a INT, s VARCHAR(100));\n"
                           f"INSERT INTO {table} SELECT * FROM {seed} ORDER BY id;\nCOMMIT;\n"
                           f"UPDATE STATISTICS ON {table};\n")
    sql("scan-clone", clone(scan))
    check(scan, "scan-validation")
    storage(scan, "scan-storage")
    queries = {"scan": f"SELECT COUNT(*), SUM(a) FROM {scan};",
               "predicate": f"SELECT COUNT(*) FROM {scan} WHERE SUBSTR(s,30,1)='a';"}
    manifest["queries"] = queries | {"update": "UPDATE <fresh clone> SET a=a+1; COMMIT;"}
    save()
    for case in ("scan", "predicate", "update"):
        for index in range(args.runs):
            name = f"{case}-{index}"
            if case == "update":
                table = args.prefix + "update" + str(index)
                sql(name + "-clone", clone(table))
                check(table, name + "-before")
                storage(table, name + "-storage")
                query = f"UPDATE {table} SET a=a+1;\nCOMMIT;"
            else:
                query = queries[case]
            path = output / (name + ".sql")
            path.write_text(query + "\n")
            perf = output / (name + ".perf")
            command = ["perf", "stat", "-x", ";", "-e", "instructions:u,cycles:u,task-clock",
                       "-p", str(args.server_pid), "-o", str(perf), "--"]
            command += bench.csql_command(args.database, path, str(client))
            idle = wait_for_idle(args.server_pid)
            elapsed = bench.run(command, output / name, args.timeout)
            if bench.process_identity(args.server_pid) != identity:
                raise ValueError("server process changed during measurement")
            if case != "update":
                want = expected(args.rows) if case == "scan" else (0,)
                if bench.results(output / (name + ".stdout")) != [want]:
                    raise ValueError(f"unexpected result: {name}.stdout")
            else:
                check(table, name + "-after", 1)
            manifest["samples"].append({"case": case, "run": index, "warmup": index == 0,
                                        "elapsed_seconds": elapsed, "pre_count_idle_cpu_seconds": idle,
                                        "counters": bench.counters(perf)})
            save()
    manifest["complete"] = True
    save()
    print(output / "summary.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("fixture", "measure"):
        command = commands.add_parser(name)
        command.add_argument("--rows", type=int, default=200000)
        command.add_argument("--prefix", required=True, help="unique SQL identifier prefix, ending in underscore")
        if name == "measure":
            command.add_argument("--database", required=True)
            command.add_argument("--server-pid", type=int, required=True)
            command.add_argument("--label", required=True)
            command.add_argument("--source", required=True)
            command.add_argument("--output", required=True)
            command.add_argument("--runs", type=int, default=5)
            command.add_argument("--timeout", type=float, default=300)
            command.add_argument("--show-oos", action="store_true", help="capture SHOW HEAP OOS on supporting engines")
    args = parser.parse_args()
    if not 1 <= args.rows <= 1000000 or not re.fullmatch(r"[a-z][a-z0-9_]{0,25}_", args.prefix):
        parser.error("rows must be 1..1000000 and prefix a lowercase SQL identifier ending in underscore")
    if args.command == "measure" and (args.runs < 5 or not re.fullmatch(r"[0-9a-f]{40}", args.source)):
        parser.error("measure requires at least five runs and a full 40-character source SHA")
    try:
        if args.command == "fixture":
            print(fixture(args.rows, args.prefix), end="")
        else:
            measure(args)
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
