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

"""Reconstructed PR7990 ordinary-table scan benchmark; Python standard library only."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import time


QUERIES = {
    "narrow": "SELECT SUM(CAST(v AS BIGINT)) FROM n;",
    "wide": "SELECT SUM(CAST(id AS BIGINT)), SUM(CAST(v AS BIGINT)), SUM(LENGTH(payload)) FROM n;",
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fixture(rows):
    # An indexed id cannot cover either measured query. No OOS-sized attribute.
    value = " + ".join(f"d{i}.d * {10 ** i}" for i in range(6))
    sources = ", ".join(f"bench_digits d{i}" for i in range(6))
    return f"""CREATE TABLE bench_digits (d INTEGER);
INSERT INTO bench_digits VALUES (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
CREATE TABLE n (id INTEGER PRIMARY KEY, v INTEGER, payload VARCHAR(80));
INSERT INTO n
SELECT {value} + 1, MOD({value} + 1, 100), REPEAT('x', 80)
FROM {sources}
WHERE {value} < {rows}
ORDER BY 1;
COMMIT;
UPDATE STATISTICS ON n;
DROP TABLE bench_digits;
COMMIT;
"""


def expected(rows):
    periods, tail = divmod(rows, 100)
    values = periods * 4950 + tail * (tail + 1) // 2
    return {"narrow": (values,), "wide": (rows * (rows + 1) // 2, values, rows * 80)}


def csql_command(database, sql):
    executable = shutil.which("csql")
    if not executable:
        raise ValueError("csql is unavailable; select the matching built installation first")
    return [executable, "-C", "-u", "dba", "--no-pager", "-t", "-N", "-i", str(sql), database]


def run(command, prefix, timeout):
    with prefix.with_suffix(".stdout").open("w") as stdout, prefix.with_suffix(".stderr").open("w") as stderr:
        start = time.monotonic()
        result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                timeout=timeout, check=False)
    if result.returncode:
        raise ValueError(f"command failed ({result.returncode}); see {prefix}.stderr")
    error_text = prefix.with_suffix(".stderr").read_text()
    if re.search(r"ERROR:|Error:", error_text):
        raise ValueError(f"CSQL error; see {prefix}.stderr")
    return time.monotonic() - start


def results(path):
    return [tuple(map(int, line.split())) for line in Path(path).read_text().splitlines()
            if re.fullmatch(r"\s*-?\d+(?:\s+-?\d+)*\s*", line)]


def counters(path):
    found = {}
    for fields in csv.reader(Path(path).read_text().splitlines(), delimiter=";"):
        if len(fields) < 5 or fields[2] not in ("instructions:u", "cycles:u", "task-clock"):
            continue
        name = fields[2]
        try:
            value, running = float(fields[0]), float(fields[4])
        except ValueError as error:
            raise ValueError(f"unusable {name} counter: {fields}") from error
        if value <= 0 or running < 95:
            raise ValueError(f"invalid or multiplexed {name} counter: {fields}")
        found[name] = {"value": value, "unit": fields[1], "running_percent": running}
    if set(found) != {"instructions:u", "cycles:u", "task-clock"}:
        raise ValueError(f"incomplete perf counters: {path}")
    return found


def process_identity(pid):
    proc = Path(f"/proc/{pid}")
    # Keep the process start time to detect PID reuse during a measurement.
    stat = (proc / "stat").read_text().rsplit(")", 1)[1].split()
    return {"pid": pid, "start_ticks": int(stat[19]), "executable": str((proc / "exe").resolve(strict=True))}


def measure(args):
    identity = process_identity(args.server_pid)
    install = Path(os.environ["CUBRID"]).resolve()
    if Path(identity["executable"]) != (install / "bin/cub_server").resolve():
        raise ValueError("server PID and selected CUBRID installation do not match")
    environment = dict(item.split(b"=", 1) for item in Path(f"/proc/{args.server_pid}/environ").read_bytes().split(b"\0")
                       if b"=" in item)
    for name in ("CUBRID", "CUBRID_DATABASES", "CUBRID_CONF"):
        if environment.get(name.encode(), b"") != os.environ.get(name, "").encode():
            raise ValueError(f"server PID and client environment disagree on {name}")
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = {
        "schema": 1, "label": args.label, "rows": args.rows, "scans": args.scans,
        "rounds": args.rounds, "runs": args.runs, "queries": QUERIES,
        "fixture_sha256": hashlib.sha256(fixture(args.rows).encode()).hexdigest(),
        "harness_sha256": digest(__file__), "server": identity,
        "server_sha256": digest(identity["executable"]), "source": args.source,
        "affinity": sorted(os.sched_getaffinity(args.server_pid)),
        "environment": {name: os.environ.get(name) for name in
                        ("CUBRID", "CUBRID_CONF", "CUBRID_CONF_FILE", "CUBRID_DATABASES", "PRESET_MODE")},
        "samples": [], "complete": False,
    }
    summary = output / "summary.json"

    def save():
        summary.write_text(json.dumps(manifest, indent=2) + "\n")

    save()
    validation = output / "validate.sql"
    validation.write_text("SELECT COUNT(*), MIN(id), MAX(id), SUM(LENGTH(payload)) FROM n;\n")
    run(csql_command(args.database, validation), output / "validate", args.timeout)
    if results(output / "validate.stdout") != [(args.rows, 1, args.rows, args.rows * 80)]:
        raise ValueError("fixture validation failed; see validate.stdout")
    for case, sql in QUERIES.items():
        (output / f"{case}.sql").write_text((sql + "\n") * args.scans)
    for round_index in range(args.rounds):
        for case in QUERIES:
            for run_index in range(args.runs):
                prefix = output / f"r{round_index + 1}-{case}-{run_index}"
                counter_file = prefix.with_suffix(".perf")
                command = ["perf", "stat", "-x", ";", "-e", "instructions:u,cycles:u,task-clock",
                           "-p", str(args.server_pid), "-o", str(counter_file), "--"]
                command += csql_command(args.database, output / f"{case}.sql")
                elapsed = run(command, prefix, args.timeout)
                if process_identity(args.server_pid) != identity:
                    raise ValueError("server process changed during measurement")
                if results(prefix.with_suffix(".stdout")) != [expected(args.rows)[case]] * args.scans:
                    raise ValueError(f"incorrect query result or statement count: {prefix}.stdout")
                sample = {"round": round_index + 1, "case": case, "run": run_index,
                          "warmup": run_index == 0, "elapsed_seconds": elapsed,
                          "counters": counters(counter_file)}
                manifest["samples"].append(sample)
                save()
                print(f"{args.label} round={round_index + 1} {case} run={run_index} "
                      f"instructions={sample['counters']['instructions:u']['value']:.0f} wall={elapsed:.3f}s",
                      flush=True)
    manifest["complete"] = True
    save()
    print(summary)


def compare(args):
    baseline, candidate = (json.loads(Path(path).read_text()) for path in (args.baseline, args.candidate))
    if not baseline["complete"] or not candidate["complete"]:
        raise ValueError("incomplete measurement; no performance verdict is possible")
    for key in ("schema", "rows", "scans", "rounds", "runs", "queries", "fixture_sha256", "harness_sha256"):
        if baseline[key] != candidate[key]:
            raise ValueError(f"measurements disagree on {key}")
    report = {"baseline": baseline["label"], "candidate": candidate["label"], "cases": {}}
    regression = False
    inconclusive = False
    for case in QUERIES:
        groups = [[s for s in data["samples"] if s["case"] == case and not s["warmup"]]
                  for data in (baseline, candidate)]
        expected_runs = {(r, i) for r in range(1, baseline["rounds"] + 1)
                         for i in range(1, baseline["runs"])}
        for group in groups:
            if len(group) != len(expected_runs) or {(s["round"], s["run"]) for s in group} != expected_runs:
                raise ValueError(f"missing or duplicate retained samples for {case}")
        values = [[s["counters"]["instructions:u"]["value"] for s in group] for group in groups]
        medians = [statistics.median(group) for group in values]
        overhead = (medians[1] / medians[0] - 1) * 100
        spread = [100 * (max(group) - min(group)) / median for group, median in zip(values, medians)]
        wall = [statistics.median(s["elapsed_seconds"] for s in group) for group in groups]
        verdict = "inconclusive" if max(spread) > args.max_spread_percent else (
            "regression" if overhead > args.max_overhead_percent else "within_budget")
        report["cases"][case] = {"instructions_overhead_percent": overhead,
                                  "median_instructions": medians, "spread_percent": spread,
                                  "median_elapsed_seconds": wall, "verdict": verdict}
        regression |= verdict == "regression"
        inconclusive |= verdict == "inconclusive"
    print(json.dumps(report, indent=2))
    return 2 if inconclusive else 1 if regression else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("fixture", help="emit SQL for a fresh, dedicated database")
    make.add_argument("--rows", type=int, default=200000)
    bench = commands.add_parser("measure", help="measure an already running, isolated server")
    bench.add_argument("--database", required=True)
    bench.add_argument("--server-pid", type=int, required=True)
    bench.add_argument("--label", required=True)
    bench.add_argument("--source", required=True, help="full source commit SHA")
    bench.add_argument("--output", required=True, help="new artifact directory")
    bench.add_argument("--rows", type=int, default=200000)
    bench.add_argument("--scans", type=int, default=20)
    bench.add_argument("--rounds", type=int, default=2)
    bench.add_argument("--runs", type=int, default=5, help="first run of each round/case is discarded")
    bench.add_argument("--timeout", type=float, default=120)
    check = commands.add_parser("compare", help="0 within budget, 1 regression, 2 inconclusive")
    check.add_argument("--baseline", required=True)
    check.add_argument("--candidate", required=True)
    check.add_argument("--max-overhead-percent", type=float, default=5)
    check.add_argument("--max-spread-percent", type=float, default=2)
    args = parser.parse_args()
    if hasattr(args, "rows") and not 1 <= args.rows <= 1000000:
        parser.error("rows must be between 1 and 1000000")
    if args.command == "measure" and (args.scans < 1 or args.rounds < 1 or args.runs < 3):
        parser.error("measure needs scans >= 1, rounds >= 1 and runs >= 3")
    if args.command == "measure" and not re.fullmatch(r"[0-9a-f]{40}", args.source):
        parser.error("source must be a full 40-character engine commit SHA")
    try:
        if args.command == "fixture":
            print(fixture(args.rows), end="")
            return 0
        if args.command == "measure":
            measure(args)
            return 0
        return compare(args)
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
