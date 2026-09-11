# Deferred OOS replacement verification

The replacement is based on `f4299ac0cd777a2a964c1f197ae5ebf9841a4936`.
A draft PR is a review checkpoint; it does not establish merge readiness.
Record the source commit, compiler/configuration, binary and shared-library
SHA-256 hashes, commands, exit codes and retained results for every run.
Do not carry an earlier revision's pass forward without identifying that revision
and explaining what changed. Keep private database volumes out of PR evidence.

## Acceptance boundaries

| Boundary | Entry point | Required observation |
|---|---|---|
| SQL writes and routing | `test_oos_sql_show` | Complete values and per-heap OOS ownership for INSERT, UPDATE and movement, supported keys, defaults and LOBs |
| Duplicate probes | `test_oos_sql_show` | REPLACE and duplicate-key UPDATE probes create no OOS values; eventual writes preserve constraints |
| Raw and special producers | `test_oos_sql_show` | Serialized rows and redistribution retain values/MVCC; serial and internal/address inputs remain complete |
| Failures and lifetimes | SQL/server tests | Allocation/storage/heap/index failures roll back live chains and leave the next operation usable |
| Queued loader | [Loader runner](README.loader.md) | Retained input ownership, destination heaps, mixed/oversized rows, filtered failures and concurrent workers |
| Replication and HA loader | [Replication runner](README.replication.md) | Source/replica value equality and order; failed replica rows roll back their OOS values |
| Transactions and recovery | [Transaction runner](README.transactions.md) | Old-version reads, committed redo, uncommitted undo, complete multi-chunk values and serial persistence |
| Memory | [Paired measurements](README.memory.md) | Identical baseline/candidate workloads; investigate peak increases and retain allocation/lifetime evidence |

Build with `UNIT_TEST_OOS=ON`. Run the configured suite with
`ctest --test-dir <build-directory> --output-on-failure` and run the three real
server runners separately using their documented isolated environments. CTest
alone does not run those external loader, replication and crash scenarios, nor
does it run the CTP SQL, medium or shell suites. Select a short fixture directory
on a filesystem with enough free space for retained databases.

## Evidence classification

Separate current-revision passes, historical passes, reproduced baseline defects,
introduced failures, and unavailable or unperformed checks. Fix introduced
failures before treating the replacement as verified. For every outstanding
required check, record an owner, exact command or environment prerequisite, and
an observable completion condition in the linked detailed PR report.

Scoped Valgrind runs with `--undef-value-errors=no` can establish checked invalid
access/leak results, but cannot establish undefined-value cleanliness. Keep an
unsuppressed baseline comparison when classifying existing diagnostics. Peak RSS
is process memory, not a direct count of live prepared payloads; the loader's
8MiB budget is per worker and excludes staging and one oversized legal row.

The pinned baseline retains its existing stored format and independent OOS
conformance gaps. Transaction tests with vacuum disabled do not certify vacuum
reclamation. No-logging loader readback does not certify logged crash recovery.
Source/standby replication does not certify multi-node heartbeat failover.
Standalone-loader OOS activation remains separate from preserving the baseline's
standalone loading behavior. Do not describe these boundaries as new passes.

## Remote acceptance evidence

Pin the full engine commit before collecting GitHub checks and CTP SQL, medium
and shell results. Record each job URL, attempt, terminal status, test counts and
the actual testcase repository/commit from checkout logs. Testcase branches are
resolved independently of the engine branch; a fallback to `develop` may use
expectations for features absent from the tested engine. Keep that mismatch
visible instead of changing expected results merely to make a run pass.

For a suspected baseline failure, replay the same testcase and answer bytes on
the baseline and candidate in isolated CTP environments. Preserve both results
and compare their failure signatures. Unordered queries may return different
permutations between CI and local runs; identical local outputs establish the
paired comparison, not byte identity with the remote output. A reproduced
baseline failure remains a failed CI result.

A missing commit status does not prove a suite was never scheduled. Inspect the
exact CircleCI workflow and pipeline: a shell job can be blocked on an unstarted
`download-build` prerequisite. Record that job and its prerequisite, retain the
existing trigger, and collect the completed job once the prerequisite succeeds.
Do not infer a shell pass or issue a duplicate trigger from an absent status.

For the published `be7c01a6d2d05d461cb1e5b6e0127c15ffb1950b` revision,
[PR #7927](https://github.com/CUBRID/cubrid/pull/7927) has separate
[CI acceptance evidence](https://github.com/vimkim/my-cubrid-docs/blob/main/cbrd-27089/ci_analysis_report_be7c01a_codex.md).
Final acceptance must reconcile that evidence with the producer and memory
matrix; local CTest success does not close outstanding remote checks.
