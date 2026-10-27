# CBRD-27785: ordinary-table scan reproduction

This harness reconstructs the small-row control described in
[the PR7990 performance report](https://github.com/CUBRID/cubrid/pull/7990#issuecomment-6434778190).
The original DDL and SQL were unavailable. This is a new reproduction of the
reported workload shape, not an exact replay of that harness.

The first stage uses 200,000 rows, two integer columns, and an 80-byte variable
column. Every value fits inline. The narrow scan aggregates the unindexed
integer; the wide scan aggregates both integers and the variable column's
length. Aggregate checksums keep result transfer small and detect incomplete or
incorrect query execution. Later stages must cover OOS-sized rows and DML
separately; these scans do not establish their performance or correctness.

## Prepare comparable installations

Use the same compiler, optimization flags, third-party libraries, configuration,
and page size. Build three exact revisions in separate directories:

| Label | Engine source |
| --- | --- |
| base | `4781495f01523a3ecc5891e3377ad62cf876a1cd` |
| report | `4408135c0833ec959da3590f12c14efa69610c7e` |
| current | `dcbf09e4f7fd02ff9c8fc6493ec8887fc2919d67` |

Use release builds. Record the full commits, compiler options, executable hashes,
configuration, machine state, and CPU placement with the run artifacts. Recreate
the database independently under each installation; feature storage layouts are
not interchangeable. Use a dedicated registry, master port, configuration and
temporary directory for each installation.

Set `data_buffer_size=4G`, `max_query_cache_entries=0`, and
`query_cache_size_in_pages=0` in each selected configuration.
Keep the remaining tuning equal. Start one benchmark database at a time, finish
fixture loading before warmup, and do not run DML during scan measurements.
Preserve the database and logs after stopping the task-owned instance. Do not
stop unrelated databases or services.

## Load the fixture

Select the matching native CUBRID environment. Create a fresh, dedicated database
with the native `cubrid createdb` command and start its server. The harness does
not create, delete, start, or stop databases.

```sh
python3 bench.py fixture > n-fixture.sql
csql -C -u dba --no-pager -i n-fixture.sql oos27785
```

The fixture deliberately fails if its table names already exist. Do not rerun it
against a populated database.

## Measure

Find the PID of this installation's `cub_server` for `oos27785`. Supply a new
artifact directory for each invocation:

```sh
python3 bench.py measure --database oos27785 --server-pid SERVER_PID \
  --label base --source FULL_ENGINE_COMMIT --output /path/to/artifacts/base
```

The default is two rounds of five runs per case, with run zero discarded in
each round. Each run performs 20 scans over one persistent CSQL connection.
`perf stat` attaches to the server process rather than counting the CSQL client.
It records user instructions, user cycles, and task-clock. The harness rejects
unavailable counters, hardware events running below 95%, mismatched client and
server installations, changed process identity, and incorrect result counts or
checksums. Raw SQL, stdout, stderr, counters, executable identity and retained
samples remain in the artifact directory.

Before executing SQL or starting counters, the harness resolves `csql` from
`PATH` and checks that it resolves to `$CUBRID/bin/csql`. Put the selected
installation's `bin` directory first in `PATH`; a client from another installation
is rejected even when it can connect to the selected server. Every query uses
that checked absolute executable path. The summary records the resolved CSQL
path and its SHA-256 fingerprint alongside the server identity.

## Compare

```sh
python3 bench.py compare --baseline /path/to/artifacts/base/summary.json \
  --candidate /path/to/artifacts/candidate/summary.json --max-overhead-percent 2
```

Exit status is 0 for results within the supplied instruction budget, 1 for a
detected regression, and 2 for invalid or inconclusive evidence. The initial
diagnostic budget is 5% instructions; retained-sample spread above 2% makes the
comparison inconclusive. The 5% default helps detect the reported 8–10% symptom.
For final class 1 acceptance, explicitly supply the approved 2% budget shown
above. Class 1 means actual ordinary heap records on both engines, with no
overflow or OOS records; verify the storage path with native diagnostics. Classes
2 (heap versus OOS), 3 (overflow versus OOS), and UPDATE performance require
separate measurements and are not covered by this gate. The command reports
elapsed time but does not use it to judge CPU regression on a shared host.

Compare `current` separately. Inspect execution plans and buffer residency before
attributing a reproduced difference to attribute decoding. Repeating a report's
percentage alone does not identify a cause. For final evidence, repeat in reverse
revision order and report round medians and interference observations.
