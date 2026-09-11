# Deferred OOS replication regression

Build and install the tested CUBRID revision with its unit tests enabled, put its `bin` on `PATH`, and set `CUBRID` to that installation. On Linux, run:

```sh
unshare -Urn sh -c 'ip link set lo up; python3 unit_tests/oos/scripts/test_oos_replication.py'
```

The runner uses the fresh-database and command-log helpers in `test_oos_loader.py`. It creates two independent databases named `t17`, each with its own registry, configuration, runtime lock/log directories and master port. The private runtime directories link to the same tested binaries. A single-node heartbeat promotes the source; the second server remains standby. Real `copylogdb` and `applylogdb` processes transfer and apply source logs. This tests replication, not multi-node heartbeat failover.

The current hostname must resolve to an IPv4 address; the runner adds that address to loopback inside the private network namespace. User/network namespaces and `ip` must be available. Set `OOS_LOADER_FIXTURE_DIR` to an existing short directory with sufficient disk space (for example a directory under `/home`); the default is `/tmp`. Both retained fixture paths are printed. Only processes belonging to these fixtures are stopped.

A committed primary-key barrier establishes that the replica has processed preceding operations before comparisons. Assertions cover:

- Multi-attribute INSERT, single- and multi-chunk values, UPDATE with an unassigned OOS value, partition movement, OOS-to-inline UPDATE, and DELETE controls.
- Values in their destination partitions, no OOS storage under the partition root, and equal live chunk counts on fresh source/replica INSERT fixtures. Physical OIDs are deliberately not compared.
- HA loading of 90 distinct mixed-size values through 17-row commit batches, after loader input values have been cleared. Every row's expected value is checked on both servers.
- A failed multi-row source INSERT rolls back its earlier row and OOS chunks; a subsequent inline INSERT replicates normally.
- Filtered source-loader duplicate failure followed by successful rows, with live OOS chunk counts equal to a successful-only control.
- A deliberately divergent replica row: ignore one duplicate-key apply error, preserve that replica row, apply the next source row, and verify that no live OOS chunks from the failed row remain.

The duplicate error is ignored only in these isolated test configurations. The divergence scenario intentionally compares the unaffected and subsequent values instead of demanding equality for the rejected source row. SQL and utility output remains in each fixture; all workload SQL and generated input definitions are in the runner.

The in-process `test_oos_server` suite additionally checks rejection of a truncated OOS-only force area and injected OOS publication allocation failure, including rollback and system-operation cleanup.

A successful standalone or in-process server test does not replace this source/replica check. If the required runtime is unavailable, record this test as outstanding rather than passed.
