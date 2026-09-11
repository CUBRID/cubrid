# Deferred OOS loader regression

Build and install CUBRID with the OOS unit tests enabled. Put that installation's `bin` on `PATH`, and set `CUBRID` to its installation directory. The focused prepared-row lifetime and allocation-error contract test is included in `test_oos_sql_show` and the configured CTest suite.

Run the real loader regression on Linux in an isolated network namespace:

```sh
unshare -Urn sh -c 'ip link set lo up; python3 unit_tests/oos/scripts/test_oos_loader.py'
```

The runner creates a fresh database, uses its own configuration and registry, and starts only that database. It retains the fixture and command logs and prints their directory. Set `OOS_LOADER_FIXTURE_DIR` to an existing **short** directory on a filesystem with enough free space to retain the database and input files; the default is `/tmp`. CUBRID's Unix socket path limits apply. Cleanup stops only server/master processes whose environment points to this exact fixture registry.

The assertions cover:

- Loading through a partition root: values are visible in the expected children, the root has no OOS file, and both children own OOS storage.
- A 600-row mixed batch: each of the 32-byte, 4,000-byte and 50,000-byte payloads appears 200 times. This exceeds the retained preparation budget while compact heap records remain small.
- One 9MiB payload, larger than the preparation budget, with complete value equality after insertion.
- A filtered duplicate-key failure followed by successful rows, with live OOS chunk counts equal to a successful-only control.
- Filtered incomplete input, NULL and empty values; unfiltered batch failure rolls back successful earlier rows and permits a later successful load.
- Two concurrent loader clients, each inserting 300 50,000-byte values with 50-row commit batches, using four server loader workers. Both must finish within the command timeout and all 600 values must match.
- Existing standalone `loaddb --no-logging` value readback. This does not assert logged recovery or enable standalone OOS as a separate feature.

`workload.json`, generated input files, configuration and command logs preserve reproducible parameters for later memory comparisons. The configuration uses 64MiB data buffers, 16MiB log buffers and a 32MiB SQL string limit so the 9MiB hexadecimal comparison value can be constructed. A passing run is functional/concurrency evidence, not a peak-memory benchmark or source/replica HA verification.
