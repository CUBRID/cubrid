# src/base/ — Core Utilities & Infrastructure

170 code files. Foundational layer used by every other module.

## Key Files

| File | Role |
|------|------|
| `error_code.h` | All error codes (negative ints, `NO_ERROR = 0`) |
| `error_manager.c/h` | `er_set()`, error stack, logging |
| `memory_alloc.c/h` | `db_private_alloc()`, `free_and_init()` macros |
| `memory_wrapper.hpp` | **MUST be last include** in every .c/.cpp — CI enforced |
| `porting.h` | Platform abstraction (Linux/Windows) |
| `area_alloc.c/h` | Slab-like area allocator for fixed-size objects |
| `lock_free.c/h` | Lock-free hash table, freelist (hazard pointers) |
| `adjustable_array.c` | Dynamic array implementation |
| `chartype.c/h` | Character classification functions |
| `intl_support.c/h` | `intl_identifier_casecmp()` — SQL identifier comparison |
| `environment_variable.c` | `$CUBRID` env var resolution |
| `perf_monitor.c` | Statistics counters, performance tracking |
| `misc_string.c` | String utilities |
| `bit.c/h` | Bit manipulation functions |
| `mem_block.hpp` | C++ RAII memory block |
| `fileline_location.hpp` | `ARG_FILE_LINE` source location tracking |
| `packable_object.hpp` | Serialization base class (C++) |
| `resource_tracker.hpp` | Leak detection for pages, locks, memory |

## Subdirectories

| Dir | Purpose |
|-----|---------|
| `lob/` | LOB locator base (part of cross-cutting LOB concern) |
| `locale/` | Locale-specific collation, casing |

## Where to Look

| Task | File |
|------|------|
| Add error code | `error_code.h` → also update 4 other places (see root AGENTS.md) |
| Fix memory leak | `memory_alloc.c`, `resource_tracker.hpp` |
| Fix lock-free data structure | `lock_free.c` — hazard-pointer based |
| Port to new platform | `porting.h`, `porting.c` |
| Add perf counter | `perf_monitor.c` |
| Fix string/charset issue | `intl_support.c`, `chartype.c` |

## Conventions (module-specific)

- `er_set()` always needs `ARG_FILE_LINE` as 2nd arg — macro expands to `__FILE__, __LINE__`
- Error severity levels: `ER_FATAL_ERROR_SEVERITY`, `ER_ERROR_SEVERITY`, `ER_WARNING_SEVERITY`, `ER_NOTIFICATION_SEVERITY`
- Lock-free structures use epoch-based reclamation via `lf_freelist`
- `resource_tracker.hpp` tracks page fixes, lock acquisitions — assert on unbalanced ops in debug builds

## Anti-Patterns

- **Never** `#include "memory_wrapper.hpp"` before other includes
- **Never** use bare `free()` — always `free_and_init()` or `db_private_free_and_init()`
- **Never** modify `heaplayers/` files (3rd-party) — they're referenced from here but live separately

## Owner

CODEOWNERS: @beyondykk9 (except `memory_monitor_*` → @hornetmj)
