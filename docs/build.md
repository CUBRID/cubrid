# Building and Installing CUBRID

How to build, install, and test the engine without destroying a build tree or a live installation.
Read this before you build, install, run unit tests, or change build presets.

## Prerequisites

See [install_build_requirements.md](install_build_requirements.md) and `CMakeLists.txt`.

## Build Commands

- Pass an explicit build target, e.g. `./build.sh -m <mode> build`, or use `cmake --preset <preset> && cmake --build --preset <preset>` (presets are in `CMakePresets.json`).
- Without a target, `build.sh` runs `all` (`clean build dist` for `release`/`debug`/`optdebug`). `clean` deletes everything inside the `-b` build directory (default `build_x86_64_<mode>`) and the `-p` prefix (default `<build directory>/_install/CUBRID`). Point neither at a directory you want to keep, such as a live `$CUBRID` or another build tree.
- `./build.sh -h` lists the options, but `-i` and `-a` are not implemented (they print usage and exit 1), and its `-b` default text is out of date (CBRD-28411).

## Scratch Install

Install only into a scratch directory. Presets install into `$env{CUBRID}`, so before you configure, set `$CUBRID` (and `$CUBRID_DATABASES`) to scratch paths, or pass `-DCMAKE_INSTALL_PREFIX=<scratch>` only for builds without unit tests.

Tests that boot a database, such as `unit_tests/oos/`, take `$CUBRID`, `$CUBRID_DATABASES`, and `PATH` as they are when CMake configures (`$ENV{...}` in `unit_tests/oos/CMakeLists.txt`) and edit `$CUBRID/conf/cubrid.conf`, so set both variables to the scratch install before you configure and keep them set when the tests run.

## Build Modes

`Release` and `RelWithDebInfo` (`release`) are the only modes compiled with `-Werror` (other than `-Werror=format-security` everywhere) and they compile asserts out (`-DNDEBUG`); `debug` and `optdebug` keep asserts and do not fail on general warnings (`CMakeLists.txt`).

Before finalizing engine C/C++ changes, verify both `release` and one assert-enabled mode; during intermediate iterations, state which mode was built.

## Unit Tests

Configure with `-DUNIT_TESTS=ON` or `-DUNIT_TEST_<MODULE>=ON` (see `unit_tests/CMakeLists.txt`) and build the test targets. Module-level steps are in [unit_tests/AGENTS.md](../unit_tests/AGENTS.md).

- `ctest` runs only modules whose `CMakeLists.txt` calls `add_test`; run any other module's `test_<module>` binary directly (CBRD-28388 tracks the missing registrations).
- Use `ctest --no-tests=error`: plain `ctest` with no registered test prints "No tests were found" and exits 0.
- No CI workflow configures `UNIT_TESTS` or runs `ctest`, so run the affected module's tests locally.
