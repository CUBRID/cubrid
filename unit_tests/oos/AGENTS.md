# unit_tests/oos/ — OOS Tests (GoogleTest)

This subtree uses GoogleTest, an explicit exception to the Catch2 default in [the parent AGENTS.md](../AGENTS.md). Keep GoogleTest here and reuse the existing fixtures. Outside this subtree, follow the module's existing framework and do not add a new one.

## Framework

- `FetchContent` downloads GoogleTest during configure (`CMakeLists.txt`). Configure fails without network access on first use.
- Targets link `GTest::gtest`, not `gtest_main`. Each test file defines its own `main ()`. It calls `::testing::InitGoogleTest` and installs a global environment that boots the database:
  - `ServerEnv` (`test_oos_common.hpp`) for `SA_MODE` tests.
  - `ServerModeEnv` (`test_oos_server_common.hpp`) for `SERVER_MODE` tests.
  - `SqlServerEnv` (`sql/test_oos_sql_common.hpp`) for SQL-level tests in `sql/`.
  - Pure-logic tests install none (for example `test_oos_record_flags.cpp`).
- To call a file-static engine function, add a `bridge_<name> ()` wrapper in the engine source under `#if defined(CUBRID_UNIT_TEST_ENABLED)`, and declare it in the test file. Example: `bridge_oos_get_max_chunk_size_within_page ()` in `src/storage/oos_file.cpp`, declared in `test_oos_delete.cpp`. The parent build defines `CUBRID_UNIT_TEST_ENABLED` whenever any unit test is enabled.

## Adding a Test

1. Create `test_oos_<name>.cpp` with `main ()` and the environment that matches its mode (see above).
2. Add the name to `SA_MODE_TESTS` (links `cubridsa`, defines `SA_MODE`) or `SERVER_MODE_TESTS` (links `cubrid`, defines `SERVER_MODE`) in `CMakeLists.txt`. The `foreach` creates only the executable. In `sql/`, the `test_oos_sql_*.cpp` glob creates the executable. A `CS_MODE` test is declared on its own (see `test_oos_sql_log_cache`).
3. Register it separately with `add_test(NAME ... COMMAND ...)` in the matching "Register ..." section. A target that is not registered never runs under `ctest`.
4. Give every test a `TIMEOUT` from 1 to 60 seconds with `set_tests_properties`. `oos_check_test_timeouts` runs at the end of configure and stops it with `FATAL_ERROR` if any test in the directory breaks this rule.
5. Choose the database fixture:
   - Tests that use the shared `unittestdb`: `FIXTURES_REQUIRED OOS_DB`, plus `RESOURCE_LOCK OOS_SHARED_UNITTESTDB` (SA tests) or `RUN_SERIAL TRUE` (SERVER tests). Copy the properties of a sibling test in the same list.
   - A test that turns off logging, kills a writer, or damages its database must own a database: add a `setup_isolated_db.sh <db>` / `cleanup_isolated_db.sh <db>` fixture pair with `FIXTURES_SETUP` / `FIXTURES_CLEANUP` and `RESOURCE_LOCK OOS_DATABASES_REGISTRY` (see the CBRD-26950 comment in `CMakeLists.txt`).
   - Pure-logic tests need no fixture.

## Running

The fixtures run the installed `cubrid` from `PATH`, read `$CUBRID` and `$CUBRID_DATABASES`, and change that installation:

- `scripts/setup_unittestdb.sh` edits `$CUBRID/conf/cubrid.conf`. It first removes old fixture sections (this removal is kept), then backs up the file, appends its own section, and restores the backup on exit. An existing `unittestdb` is reused as it is.
- Setup scripts run `cubrid createdb` for `unittestdb` and the per-binary databases. Cleanup scripts run `cubrid server stop` and `cubrid deletedb` only on databases that the fixture created.

So run OOS tests only against a scratch installation of the same build (see the scratch-install rule in [the root AGENTS.md](../../AGENTS.md)):

1. Set `CUBRID` and `CUBRID_DATABASES` to scratch paths, and put `$CUBRID/bin` first in `PATH`.
2. Configure an assert-enabled mode (`debug` or `optdebug`) with `-DUNIT_TEST_OOS=ON`, build, and install into `$CUBRID` (`cmake --build <build dir> --target install`). `ServerEnv` checks the boot with `assert`, which `release` (`-DNDEBUG`) compiles out.
3. Run `ctest --test-dir <build dir> --no-tests=error -R test_oos_<name>`. `ctest` adds the required fixture tests automatically. `ServerEnv` (SA) tests write their error log under `<build dir>/unit_tests/oos/` and `sql/` tests under `<build dir>/unit_tests/oos/sql/` (`test_oos_sql_log*`); `ServerModeEnv` tests call `er_init (NULL, ...)` and use the default error log. `test_oos_error_log.hpp` resolves the exact file a test opened.
