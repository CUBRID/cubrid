# Deferred OOS memory comparison

Use two clean, equivalently configured CUBRID installations: the pinned baseline
`f4299ac0c` and the deferred-write candidate. Build both with the same GCC Debug
preset and OOS unit-test options. Memory measurements here are Debug-build
resource evidence, not release throughput benchmarks.

Set `CUBRID`, `PATH`, and `LD_LIBRARY_PATH` to the installation being measured.
Run the following separately for each installation, with a different output path:

```sh
OOS_LOADER_FIXTURE_DIR=/path/with/free/space unshare -Urn sh -c '
  ip link set lo up
  python3 unit_tests/oos/scripts/measure_oos_memory.py \
    --source <tested-commit> --output /path/to/results.json
'
```

The short fixture parent must exist and have several GB available. Each sample
retains a fresh database, input, configuration and logs. The runner verifies PATH
and installation binary/library hashes and rejects an installation changed during
measurement. Run installation updates after the measurement finishes.

Three repetitions cover these nonpartitioned, equivalent successful workloads:

| Producer | Rows | Payload bytes per row | Commit rows |
|---|---:|---|---:|
| SQL INSERT SELECT | 10,000 | 32 | 10,000 |
| SQL INSERT SELECT | 1,000 | 50,000 | 1,000 |
| Server loaddb | 600 | 32 / 4,000 / 50,000 cyclically | 10,240 |
| Server loaddb | 1,000 | 50,000 | 10,240 |

Payloads are uncompressed VARBIT values. Every sample checks row counts and full
value equality after measuring the write. Fresh servers use 64MiB data buffers,
16MiB log buffers and four loader workers, matching the loader regression fixture.
The SQL client is only submitting an INSERT SELECT, so its small RSS does not
represent the database server's write memory.

Server `VmHWM` is the Linux kernel's peak resident memory from startup through the
write, read before verification queries. The pre-write `VmRSS` and `VmHWM` are
also recorded. Client peak RSS comes from GNU time (`wait4`); server and client
peaks are separate, and must not be summed as a simultaneous whole-system peak.
Server peak includes startup, buffer pools, parser/loader staging, log buffers,
allocator retention and preparation. It is not a preparation-only allocation
counter. OS page cache and helper processes are outside these measurements.

`OosSqlShow.PreparationMemoryAndMoveAssignmentPreserveValues` prints owned retained
bytes and final record sizes for the same three value sizes, verifies move
assignment over an occupied owner after input destruction, and checks persisted
values. `LoaderQueueRetainsClearedInputsAndRollsBackBulkFailure` prints the retained
allocation total for 64 queued 50,000-byte rows, including owner container capacity.
These use the existing public accounting API, without asserting private container
sizes. Other prepared-row SQL tests exercise owner, record and request allocation
errors, partial insertion and rollback. Run them with the configured CTest fixture.

Investigate increases using both measurements and the lifetime of queued payloads.
There is no percentage allowance. The existing 8MiB loader preparation flush budget
is per worker, allows a single oversized row, and excludes input staging and bulk
metadata; it is not a process RSS cap. Full loader/HA and transaction regressions
remain separate from this resource comparison.
