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
import math
import platform
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


EVENTS = ("instructions:u", "cycles:u", "task-clock")
PARAMETERS = ("data_buffer_size", "log_buffer_size", "max_plan_cache_entries",
              "max_query_cache_entries", "query_cache_size_in_pages")
LOCALE_ENV = ("LANG", "LC_ALL", "LC_CTYPE", "LC_COLLATE")


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


def csql_command(database, sql, executable):
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
        if not math.isfinite(value) or value <= 0 or not math.isfinite(running) or not 95 <= running <= 100 or name in found:
            raise ValueError(f"invalid or multiplexed {name} counter: {fields}")
        found[name] = {"value": value, "unit": fields[1], "running_percent": running}
    if set(found) != {"instructions:u", "cycles:u", "task-clock"}:
        raise ValueError(f"incomplete perf counters: {path}")
    return found


def process_identity(pid):
    proc = Path(f"/proc/{pid}")
    # Keep the process start time to detect PID reuse during a measurement.
    stat = (proc / "stat").read_text().rsplit(")", 1)[1].split()
    cmdline = (proc / "cmdline").read_bytes().split(b"\0")
    if cmdline[-1] == b"":
        cmdline.pop()
    return {"pid": pid, "start_ticks": int(stat[19]), "executable": str((proc / "exe").resolve(strict=True)),
            "argv": [os.fsdecode(arg) for arg in cmdline]}


def validate_server(pid, database):
    identity = process_identity(pid)
    install = Path(os.environ["CUBRID"]).resolve()
    if Path(identity["executable"]) != (install / "bin/cub_server").resolve():
        raise ValueError("server PID and selected CUBRID installation do not match")
    argv = identity["argv"]
    if len(argv) != 2 or os.path.basename(argv[0]) != "cub_server" or os.fsencode(argv[1]) != os.fsencode(database):
        raise ValueError("server PID is not the cub_server for the requested database")
    environment = dict(item.split(b"=", 1) for item in Path(f"/proc/{pid}/environ").read_bytes().split(b"\0")
                       if b"=" in item)
    for name in ("CUBRID", "CUBRID_DATABASES", "CUBRID_CONF", "CUBRID_CONF_FILE"):
        if environment.get(name.encode(), b"") != os.environ.get(name, "").encode():
            raise ValueError(f"server PID and client environment disagree on {name}")
    executable = shutil.which("csql")
    if not executable:
        raise ValueError("csql is unavailable; select the matching built installation first")
    client = Path(executable).resolve(strict=True)
    if client != (install / "bin/csql").resolve(strict=True):
        raise ValueError(f"csql executable {client} does not match selected CUBRID installation {install}; "
                         f"put {install / 'bin'} first in PATH")
    if process_identity(pid) != identity:
        raise ValueError("server process changed during preflight")
    return identity, client



def matching_utility():
    install = Path(os.environ["CUBRID"]).resolve()
    executable = shutil.which("cubrid")
    if not executable or Path(executable).resolve(strict=True) != (install / "bin/cubrid").resolve(strict=True):
        raise ValueError("cubrid utility does not match selected CUBRID installation")
    return str(Path(executable).resolve())


def server_environment(pid):
    return dict(item.split(b"=", 1) for item in Path(f"/proc/{pid}/environ").read_bytes().split(b"\0")
                if b"=" in item)


def parse_parameters(text):
    found = {}
    for line in text.splitlines():
        match = re.fullmatch(r"\[S[ *]\]\s+([a-z_]+)\s*=\s*(.*?)\s+\(.*\)\s*", line)
        if match and match[1] in PARAMETERS:
            if match[1] in found or not match[2]:
                raise ValueError(f"duplicate or empty server parameter {match[1]}")
            found[match[1]] = match[2]
    if set(found) != set(PARAMETERS):
        raise ValueError(f"missing server parameters: {sorted(set(PARAMETERS) - set(found))}")
    if found["max_query_cache_entries"] != "0" or found["query_cache_size_in_pages"] != "0":
        raise ValueError("query result cache must be disabled")
    return found


def parse_charset(text):
    rows = [line.split() for line in text.splitlines()]
    rows = [row for row in rows if len(row) == 4 and
            all(re.fullmatch(r"[A-Za-z0-9_]+", field) for field in row)]
    if len(rows) != 1:
        raise ValueError("invalid fixture charset and collation output")
    return dict(zip(("column_charset", "column_collation", "literal_charset", "literal_collation"), rows[0]))


def capture_conditions(args, output, client, utility, phase):
    prefix = output / f"conditions-{phase}"
    run([utility, "paramdump", "-C", args.database], output / f"{prefix.name}-parameters", args.timeout)
    sql = output / f"{prefix.name}.sql"
    sql.write_text("SELECT CHARSET(payload), COLLATION(payload), CHARSET('x'), COLLATION('x') FROM n LIMIT 1;\n")
    run(csql_command(args.database, sql, str(client)), output / f"{prefix.name}-charset", args.timeout)
    server_env = server_environment(args.server_pid)
    locale = {"client": {name: os.environ.get(name) for name in LOCALE_ENV},
              "server": {name: os.fsdecode(server_env[name.encode()]) if name.encode() in server_env else None
                         for name in LOCALE_ENV}}
    conditions = {"version": 1, "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
                  "architecture": platform.machine(),
                  "server_affinity": sorted(os.sched_getaffinity(args.server_pid)),
                  "client_affinity": sorted(os.sched_getaffinity(0)), "locale_environment": locale,
                  "server_parameters": parse_parameters((output / f"{prefix.name}-parameters.stdout").read_text()),
                  "charset": parse_charset((output / f"{prefix.name}-charset.stdout").read_text())}
    (output / f"{prefix.name}.json").write_text(json.dumps(conditions, indent=2) + "\n")
    validate_conditions(conditions)
    return conditions


def validate_conditions(value):
    if not isinstance(value, dict) or set(value) != {"version", "boot_id", "architecture", "server_affinity",
            "client_affinity", "locale_environment", "server_parameters", "charset"} or type(value["version"]) is not int or value["version"] != 1:
        raise ValueError("invalid conditions schema")
    for key in ("boot_id", "architecture"):
        if not isinstance(value[key], str) or not value[key].strip():
            raise ValueError(f"invalid condition {key}")
    for key in ("server_affinity", "client_affinity"):
        cpus = value[key]
        if not isinstance(cpus, list) or not cpus or any(type(n) is not int or n < 0 for n in cpus) or sorted(set(cpus)) != cpus:
            raise ValueError(f"invalid condition {key}")
    locale = value["locale_environment"]
    if not isinstance(locale, dict) or set(locale) != {"client", "server"}:
        raise ValueError("invalid locale environment")
    for env in locale.values():
        if not isinstance(env, dict) or set(env) != set(LOCALE_ENV) or any(v is not None and not isinstance(v, str) for v in env.values()):
            raise ValueError("invalid locale environment")
    params = value["server_parameters"]
    if not isinstance(params, dict) or set(params) != set(PARAMETERS) or any(not isinstance(v, str) or not v.strip() for v in params.values()):
        raise ValueError("invalid server parameters")
    if params["max_query_cache_entries"] != "0" or params["query_cache_size_in_pages"] != "0":
        raise ValueError("query result cache must be disabled")
    charset = value["charset"]
    if not isinstance(charset, dict) or set(charset) != {"column_charset", "column_collation", "literal_charset", "literal_collation"} or any(not isinstance(v, str) or not v for v in charset.values()):
        raise ValueError("invalid charset conditions")


def positive_number(value):
    return type(value) in (int, float) and math.isfinite(value) and value > 0


def validate_manifest(data):
    if not isinstance(data, dict) or type(data.get("schema")) is not int or data["schema"] != 2 or data.get("workload") != "ordinary_scan":
        raise ValueError("unsupported manifest schema or workload")
    if data.get("complete") is not True:
        raise ValueError("incomplete measurement; no performance verdict is possible")
    for key, minimum in (("rows", 1), ("scans", 1), ("rounds", 1), ("runs", 3)):
        if type(data.get(key)) is not int or data[key] < minimum:
            raise ValueError(f"invalid {key}")
    if data["rows"] > 1000000 or data.get("queries") != QUERIES:
        raise ValueError("invalid workload")
    for key in ("label", "fixture_sha256", "harness_sha256"):
        if not isinstance(data.get(key), str) or not data[key]:
            raise ValueError(f"invalid {key}")
    conditions = data.get("conditions")
    if not isinstance(conditions, dict) or set(conditions) != {"before", "after"}:
        raise ValueError("missing observed conditions")
    for condition in conditions.values():
        validate_conditions(condition)
    if conditions["before"] != conditions["after"]:
        raise ValueError("conditions changed during measurement")
    samples = data.get("samples")
    if not isinstance(samples, list):
        raise ValueError("invalid samples")
    expected_samples = {(r, case, i) for r in range(1, data["rounds"] + 1) for case in QUERIES for i in range(data["runs"])}
    seen = set()
    for sample in samples:
        if not isinstance(sample, dict) or type(sample.get("round")) is not int or type(sample.get("run")) is not int or not isinstance(sample.get("case"), str):
            raise ValueError("invalid sample")
        key = (sample["round"], sample["case"], sample["run"])
        if key not in expected_samples or key in seen or type(sample.get("warmup")) is not bool or sample["warmup"] != (sample["run"] == 0):
            raise ValueError("missing, duplicate or invalid sample")
        seen.add(key)
        if not positive_number(sample.get("elapsed_seconds")):
            raise ValueError("invalid elapsed time")
        metrics = sample.get("counters")
        if not isinstance(metrics, dict) or set(metrics) != set(EVENTS):
            raise ValueError("invalid counters")
        for counter in metrics.values():
            if not isinstance(counter, dict) or not positive_number(counter.get("value")) or not isinstance(counter.get("unit"), str) or not positive_number(counter.get("running_percent")) or not 95 <= counter["running_percent"] <= 100:
                raise ValueError("invalid counter")
    if seen != expected_samples:
        raise ValueError("missing samples")


def measure(args):
    identity, client = validate_server(args.server_pid, args.database)
    utility = matching_utility()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    manifest = {
        "schema": 2, "workload": "ordinary_scan", "label": args.label, "rows": args.rows, "scans": args.scans,
        "rounds": args.rounds, "runs": args.runs, "queries": QUERIES,
        "fixture_sha256": hashlib.sha256(fixture(args.rows).encode()).hexdigest(),
        "harness_sha256": digest(__file__), "server": identity,
        "server_sha256": digest(identity["executable"]), "source": args.source,
        "client": {"executable": str(client), "sha256": digest(client)},
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
    run(csql_command(args.database, validation, str(client)), output / "validate", args.timeout)
    if results(output / "validate.stdout") != [(args.rows, 1, args.rows, args.rows * 80)]:
        raise ValueError("fixture validation failed; see validate.stdout")
    for case, sql in QUERIES.items():
        (output / f"{case}.sql").write_text((sql + "\n") * args.scans)
    manifest["conditions"] = {"before": capture_conditions(args, output, client, utility, "before")}
    save()
    if process_identity(args.server_pid) != identity:
        raise ValueError("server process changed during condition capture")
    for round_index in range(args.rounds):
        for case in QUERIES:
            for run_index in range(args.runs):
                prefix = output / f"r{round_index + 1}-{case}-{run_index}"
                counter_file = prefix.with_suffix(".perf")
                command = ["perf", "stat", "-x", ";", "-e", "instructions:u,cycles:u,task-clock",
                           "-p", str(args.server_pid), "-o", str(counter_file), "--"]
                command += csql_command(args.database, output / f"{case}.sql", str(client))
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
    manifest["conditions"]["after"] = capture_conditions(args, output, client, utility, "after")
    if process_identity(args.server_pid) != identity or manifest["conditions"]["before"] != manifest["conditions"]["after"]:
        save()
        raise ValueError("server or conditions changed during measurement")
    manifest["complete"] = True
    save()
    print(summary)


def compare(args):
    baseline, candidate = (json.loads(Path(path).read_text()) for path in (args.baseline, args.candidate))
    for data in (baseline, candidate):
        validate_manifest(data)
    for threshold in (args.max_overhead_percent, args.max_spread_percent):
        if not math.isfinite(threshold) or threshold < 0:
            raise ValueError("thresholds must be finite and nonnegative")
    if baseline["conditions"]["before"] != candidate["conditions"]["before"]:
        raise ValueError("measurements disagree on observed conditions")
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
        if not all(positive_number(value) for value in medians):
            raise ValueError(f"invalid instruction median for {case}")
        overhead = (medians[1] / medians[0] - 1) * 100
        spread = [100 * (max(group) - min(group)) / median for group, median in zip(values, medians)]
        wall = [statistics.median(s["elapsed_seconds"] for s in group) for group in groups]
        if not all(math.isfinite(value) for value in [overhead, *spread, *wall]):
            raise ValueError(f"nonfinite comparison statistics for {case}")
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
    except (OSError, ValueError, KeyError, TypeError, OverflowError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
