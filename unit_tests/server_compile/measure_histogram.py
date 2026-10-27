#!/usr/bin/env python3
"""Paired on/off cost of optional collection, using one binary and connection.

Usage: measure_histogram.py BROKER_PORT DBNAME [REQUESTS_PER_SAMPLE]
Reports measurements, not a regression verdict. Run with the release install.
"""

import json
import statistics
import sys
import time

from probe_histogram import Connection, rows


def main():
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    assert 100 <= count <= 10000
    conn = Connection(int(sys.argv[1]), sys.argv[2])
    samples = {"off": [], "on": []}
    ratios = []
    try:
        for pair in range(7):
            order = ("off", "on") if pair % 2 == 0 else ("on", "off")
            elapsed = {}
            for mode in order:
                conn.command(";.hist " + mode)
                for _ in range(100):
                    conn.version()
                if mode == "on":
                    conn.command(";.clear_hist")
                start = time.perf_counter_ns()
                for _ in range(count):
                    conn.version()
                elapsed[mode] = (time.perf_counter_ns() - start) / count / 1000
                samples[mode].append(elapsed[mode])
                if mode == "on":
                    measured = rows(conn.command(";.dump_hist"))
                    assert measured["get_db_version"][0] == count, measured
            ratios.append(elapsed["on"] / elapsed["off"])
        print(json.dumps({
            "workload": "CAS get_db_version, sequential requests on one connection",
            "requests_per_sample": count,
            "unit": "client elapsed microseconds per request (not server histogram time)",
            "samples": samples,
            "median_us": {mode: statistics.median(values) for mode, values in samples.items()},
            "range_us": {mode: [min(values), max(values)] for mode, values in samples.items()},
            "paired_on_off_ratios": ratios,
            "median_paired_ratio": statistics.median(ratios),
            "note": "Same ELF on/off comparison; no baseline-build regression claim.",
        }, indent=2))
    finally:
        conn.command(";.hist off")
        conn.close()


if __name__ == "__main__":
    main()
