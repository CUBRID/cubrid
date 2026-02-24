# unit_tests/ — C++ Unit Tests

## OVERVIEW

CTest-based unit tests for core engine modules. Enable with `-DUNIT_TESTS=ON` or individual `-DUNIT_TEST_XXX=ON`.

## STRUCTURE

| Directory | Tests For |
|-----------|-----------|
| `common/` | Shared test utilities |
| `lockfree/` | Lock-free data structures |
| `memory_alloc/` | Memory allocator |
| `memory_monitor/` | Memory leak tracking |
| `monitor/` | Performance counters |
| `object_factory/` | Object creation |
| `packing/` | Serialization |
| `resource_tracker/` | Resource lifecycle |
| `string_buffer/` | String buffer ops |
| `thread/` | Thread pool |
| `loaddb/` | Bulk loader |

## COMMANDS

```bash
# Build all unit tests
./build.sh -m debug -- -DUNIT_TESTS=ON

# Build specific test
./build.sh -m debug -- -DUNIT_TEST_MEMORY_MONITOR=ON

# Run after build
cd build_x86_64_debug && ctest
```
