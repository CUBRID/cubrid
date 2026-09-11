#!/usr/bin/env python3
# Copyright 2026 CUBRID Corporation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.
"""Measure isolated CS workloads; invoke once per installation in a network namespace.

Uses fresh servers per workload. Server VmHWM is the kernel lifetime RSS peak;
client maxrss is GNU time's wait4 peak. Neither includes another process's memory.
Retains inputs, config, command logs and JSON. No acceptance threshold is imposed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from test_oos_loader import LoaderFixture


def server_status(fixture):
    marker = ("CUBRID_DATABASES=" + str(fixture.path / "db")).encode()
    for proc in Path("/proc").glob("[0-9]*"):
        try:
            if ((proc / "exe").resolve().name == "cub_server"
                    and marker in (proc / "environ").read_bytes().split(b"\0")):
                status = (proc / "status").read_text()
                return {key: int(re.search(rf"^{key}:\s+(\d+)", status, re.M)[1])
                        for key in ("VmRSS", "VmHWM")}
        except (OSError, ProcessLookupError):
            continue
    raise RuntimeError("fixture server not found")


def measure(kind, rows, sizes):
    fixture = LoaderFixture()
    try:
        fixture.run("create", ["cubrid", "createdb", "--db-volume-size=64M",
                              "--log-volume-size=64M", "t17", "en_US.utf8"])
        fixture.run("start", ["cubrid", "server", "start", "t17"])
        fixture.sql("CREATE TABLE bulk(id INT, b BIT VARYING)")
        before = server_status(fixture)
        if kind == "sql":
            workload = fixture.path / "input.sql"
            workload.write_text("INSERT INTO bulk SELECT ROWNUM, CAST(REPEAT('AB',"
                                + str(sizes[0]) + ") AS BIT VARYING) FROM db_root CONNECT BY LEVEL <= "
                                + str(rows) + "; COMMIT;\n")
            command = ["csql", "-C", "-u", "dba", "-i", str(workload), "t17"]
        else:
            workload = fixture.rows("input.rows", "bulk", range(rows), sizes)
            command = ["cubrid", "loaddb", "-C", "-u", "dba", "--no-statistics",
                       "-c", "10240", "-d", str(workload), "t17"]
        start = time.monotonic()
        fixture.run("workload", ["/usr/bin/time", "-f", "%M", "-o",
                                 str(fixture.path / "client.maxrss"), *command])
        elapsed = time.monotonic() - start
        after = server_status(fixture)
        # Check values after recording the write peak: readback can allocate large values.
        for remainder, size in enumerate(sizes):
            expected = len(range(remainder, rows, len(sizes)))
            fixture.count("bulk", f"MOD(id,{len(sizes)})={remainder} "
                          f"AND b=CAST(REPEAT('AB',{size}) AS BIT VARYING)", expected)
        fixture.count("bulk", "1=1", rows)
        result = dict(kind=kind, rows=rows, payload_bytes=sizes, commit_rows=10240 if kind == "loader" else rows,
                      fixture=str(fixture.path), server_before_kib=before, server_after_kib=after,
                      client_peak_kib=int((fixture.path / "client.maxrss").read_text()), elapsed_seconds=elapsed)
        (fixture.path / "measurement.json").write_text(json.dumps(result, indent=2))
        fixture.run("stop", ["cubrid", "server", "stop", "t17"])
        return result
    finally:
        fixture.close()


def binaries():
    install = Path(os.environ["CUBRID"]).resolve()
    paths = set()
    for name in ("cubrid", "csql"):
        path = Path(shutil.which(name)).resolve()
        if not path.is_relative_to(install):
            raise RuntimeError(f"{name} on PATH is outside {install}: {path}")
        paths.add(path)
    paths.add((install / "bin/cub_server").resolve())
    paths.update(p.resolve() for p in (install / "lib").glob("libcubrid*.so*"))
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source", required=True, help="tested source revision, including dirty marker if applicable")
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    assert args.repetitions > 0
    result = dict(source=args.source, installation=os.environ["CUBRID"], binary_sha256=binaries(),
                  version=subprocess.check_output(["cubrid", "--version"], text=True),
                  method="server /proc VmHWM; client GNU time maxrss; KiB; fresh server per workload",
                  samples=[])
    for repetition in range(args.repetitions):
        for kind, rows, sizes in [("sql", 10000, (32,)), ("sql", 1000, (50000,)),
                                 ("loader", 600, (32, 4000, 50000)), ("loader", 1000, (50000,))]:
            sample = measure(kind, rows, sizes)
            if binaries() != result["binary_sha256"]:
                raise RuntimeError("tested installation changed during measurement")
            sample["repetition"] = repetition + 1
            result["samples"].append(sample)
            args.output.write_text(json.dumps(result, indent=2))
            print(json.dumps(sample), flush=True)


if __name__ == "__main__":
    main()
