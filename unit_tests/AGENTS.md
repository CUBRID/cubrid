# unit_tests/ — C/C++ Unit Tests

Follow [docs/build.md](../docs/build.md) for presets, build modes, and scratch installs. For OOS tests (`oos/`, GoogleTest), read [oos/AGENTS.md](oos/AGENTS.md); the Catch2 and module steps below do not apply there.

## Modules

`unit_tests/CMakeLists.txt` is the source of truth. Each module builds only under `if (UNIT_TESTS OR UNIT_TEST_<MODULE>)`, where `<MODULE>` is the directory name in upper case (for example `UNIT_TEST_STRING_BUFFER`). Each module builds `test_<module>` into `<build dir>/bin/`.

| Directory | Framework | Registered with `ctest` |
|-----------|-----------|:--:|
| `btree_sort_merge/`, `log_recovery/`, `radix_sort/`, `scope_exit/` | Catch2 | yes |
| `double_write_buffer/`, `json_builder/`, `stream_session/` | Catch2 | no |
| `lockfree/`, `log_prior_inflight/`, `memory_alloc/`, `monitor/`, `object_factory/`, `packing/`, `resource_tracker/`, `string_buffer/`, `thread/` | own `main` | no |
| `oos/` | GoogleTest | yes |

- Follow the framework the module already uses. A Catch2 `TEST_CASE` added to an own-`main` module is never compiled into a runner.
- `loaddb/` and `memory_monitor/` are disabled inside `#[[ ... ]]` blocks. The disabled `UNIT_TEST_LOADDB` block calls `add_subdirectory(monitor)`, a copy error. To enable `loaddb/`, change that block to `add_subdirectory(loaddb)`. Do not only uncomment it.
- `lockfree/` is enabled. The comment above the `log_prior_inflight` block that says `lockfree/` does not compile is stale.
- `UNIT_TEST_OBJECT_FACTORY` has no `option()` line, so `cmake -LH` does not list it. `-DUNIT_TEST_OBJECT_FACTORY=ON` still works.

## Build and Run

```bash
# Use a build directory of its own (see "Engine side effect" below)
cmake --preset debug -B build_unit_debug -DUNIT_TEST_STRING_BUFFER=ON -DUNIT_TEST_SCOPE_EXIT=ON   # or -DUNIT_TESTS=ON
cmake --build build_unit_debug --target test_string_buffer test_scope_exit

# Modules registered with ctest
ctest --test-dir build_unit_debug --no-tests=error -R test_scope_exit

# Modules not registered with ctest: run the binary
build_unit_debug/bin/test_string_buffer
```

- `ctest` runs only modules whose `CMakeLists.txt` calls `add_test`. With no registered test, plain `ctest` prints "No tests were found!!!" and exits 0, which looks like a pass. Use `--no-tests=error`, and run an unregistered module's binary directly. Read its `main` for arguments and for how it reports a failure. CBRD-28388 tracks the missing registrations; update the table above when it lands.
- `-DUNIT_TESTS=ON` also enables `oos/`, whose fixtures need a scratch install (see [oos/AGENTS.md](oos/AGENTS.md)).

## Engine Side Effect

Any `UNIT_TESTS` or `UNIT_TEST_*` set to `ON` makes root `CMakeLists.txt` call `add_compile_definitions(CUBRID_UNIT_TEST_ENABLED)` for every target. Engine sources (for example `src/storage/heap_file.c` and `src/storage/oos_file.cpp`) then compile test hooks into `libcubrid`, `libcubridsa`, and `libcubridcs`. The option stays in `CMakeCache.txt` for later configures. Configure unit tests in a separate build directory. Do not use that build for performance numbers or release verification.

## Adding a Catch2 Module

1. Create `unit_tests/<module>/` with `CMakeLists.txt`, a `test_main.cpp` that holds only `#define CATCH_CONFIG_MAIN` and the include, and `test_<feature>.cpp` files. Use Catch2 v2.11.3: `#include "catch2/catch.hpp"`, not v3's `catch2/catch_all.hpp`. Copy `scope_exit/` as the template.
2. In that `CMakeLists.txt`: `add_executable(test_<module> ...)`, `add_dependencies(test_<module> ${CATCH2_TARGET})` (Catch2 is header-only; do not link it), engine libraries only if the code under test needs them, and `add_test(NAME test_<module> COMMAND test_<module>)`.
3. In `unit_tests/CMakeLists.txt`: add `option (UNIT_TEST_<MODULE> "...")` and a guarded block `if (UNIT_TESTS OR UNIT_TEST_<MODULE>)` → `add_subdirectory(<module>)`.
4. Add the module to the table above.

## Gotchas

- Network on first use: Catch2 is cloned by `ExternalProject_Add` during the build. OOS GoogleTest is downloaded by `FetchContent` during configure.
- Shared test helpers are in `common/` (static library `test_common`).
