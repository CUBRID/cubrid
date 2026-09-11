# Deferred OOS transaction regression

Build and install CUBRID with its unit tests enabled, set `CUBRID` to that installation and put its `bin` on `PATH`. On Linux:

```sh
unshare -Urn sh -c 'ip link set lo up; python3 unit_tests/oos/scripts/test_oos_transactions.py'
```

The runner reuses the loader fixture's command logging and fresh database helpers. It requires user/network namespaces, `ip`, `stdbuf` and `/proc`. Each run creates a fresh `t17` database, private registry, configuration, temporary files and runtime logs/locks. It stops only processes with its exact private registry. Set `OOS_LOADER_FIXTURE_DIR` to retain fixtures outside `/tmp`. SQL commands, results, installation path, source revision and executable SHA-256 hashes remain in that directory.

Two persistent `csql -C` sessions use explicit transactions and SQL result barriers. A repeatable-read reader checks original 50KB values before and after a writer commits an UPDATE and partition movement, then after DELETE. A new reader transaction observes the committed result. These are real server MVCC checks.

The logged recovery scenario commits multi-attribute INSERT/UPDATE, leaves a subsequent INSERT/UPDATE/movement uncommitted, and commits a separate barrier transaction to flush preceding WAL. It sends `SIGKILL` to its server **before** closing clients, restarts, and checks complete values, row counts, destination ownership and live OOS chunk counts. The counts distinguish retained empty pages from live chains. It also checks a subsequent write, serial direct-page persistence and completed REDO/UNDO phases in the server log. Automatic server restart is disabled to make the explicit restart deterministic.

Vacuum is disabled in this fixture to isolate transaction and recovery checks from the independent baseline rollback-vacuum defect; this runner does not verify vacuum reclamation. All writes here are logged. Existing no-logging loader compatibility belongs to `test_oos_loader.py`; no logged recovery guarantee is asserted for that mode.

The standalone SQL suite separately exercises preparation owner, record and request allocation errors, pre-OOS-lookup failure, partial OOS batches, heap insertion rejection, duplicate/foreign-key failures, rollback and subsequent successful writes. Its internal/address-reservation test ensures the preparation failure remains armed across bypass operations. Run the configured CTest suite to include those tests; they do not replace this server runner.
