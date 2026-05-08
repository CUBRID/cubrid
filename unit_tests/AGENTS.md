# unit_tests/ — Catch2 Unit Tests

Catch2 v2.11.3 framework. Tests for C/C++ engine modules.

## Enabling Tests

```bash
# All tests
cmake -DUNIT_TESTS=ON <path>

# Individual module
cmake -DUNIT_TEST_STRING_BUFFER=ON <path>

# Build specific test
cmake --build build_preset_debug --target test_string_buffer

# Run all
cd build_preset_debug && ctest
```

## Test Modules

| Directory | CMake Flag | Status |
|-----------|-----------|--------|
| `memory_alloc/` | `UNIT_TEST_MEMORY_ALLOC` | ✓ Active |
| `string_buffer/` | `UNIT_TEST_STRING_BUFFER` | ✓ Active |
| `thread/` | `UNIT_TEST_THREAD` | ✓ Active |
| `packing/` | `UNIT_TEST_PACKING` | ✓ Active |
| `object_factory/` | `UNIT_TEST_OBJECT_FACTORY` | ✓ Active |
| `resource_tracker/` | `UNIT_TEST_RESOURCE_TRACKER` | ✓ Active |
| `monitor/` | `UNIT_TEST_MONITOR` | ✓ Active |
| `lockfree/` | `UNIT_TEST_LOCKFREE` | ✗ Disabled (compilation fails) |
| `loaddb/` | `UNIT_TEST_LOADDB` | ✗ Disabled (compilation fails) |
| `memory_monitor/` | `UNIT_TEST_MEMORY_MONITOR` | ✗ Disabled (compilation fails) |

## Structure

```
unit_tests/
├── CMakeLists.txt       # Master — includes modules, fetches Catch2
├── common/              # Shared test utilities
└── <module>/
    ├── CMakeLists.txt   # Module test target
    └── test_*.cpp       # Test source (TEST_CASE macros)
```

## Where to Look

| Task | Location |
|------|----------|
| Add test for module X | Create `unit_tests/X/`, add `CMakeLists.txt` + test source |
| Fix test compilation | Check module's `CMakeLists.txt` for missing sources/includes |
| Fix disabled module | `lockfree/`, `loaddb/`, `memory_monitor/` — marked TODO in master CMakeLists.txt |
| Share test utilities | `common/` directory |

## Writing Tests (Catch2 v2)

```cpp
#include "catch2/catch.hpp"

TEST_CASE ("description", "[tag]")
{
  SECTION ("sub-test")
    {
      REQUIRE (actual == expected);
      CHECK (condition);  /* non-fatal */
    }
}
```

## Conventions

- One test file per logical area: `test_<feature>.cpp`
- Tags: `[module_name]` for categorization
- Use `common/` for shared fixtures, mocks
- Follow GNU brace style (same as production code)
- Test binaries named `test_<module>` — registered with `ctest`

## Adding a New Test Module
1. Create `unit_tests/<module>/` with `CMakeLists.txt` + test source
2. Add `option(UNIT_TEST_<MODULE>)` and `add_subdirectory(<module>)` to master CMakeLists.txt
3. Write test sources using `TEST_CASE` macros, link against engine libs + Catch2

## Gotchas

- Catch2 v2.11.3 (NOT v3) — use `#include "catch2/catch.hpp"`, not `catch2/catch_all.hpp`
- Catch2 fetched via `ExternalProject_Add` at CMake time — needs network on first build
- Disabled modules commented out with `#[[ ... ]]` in CMakeLists.txt — not just flags
- Tests link against engine libraries — include path and link dependency issues are common
