# src/ — CUBRID Engine Core

## OVERVIEW

24 C/C++ modules (~1.3M lines). All `.c` files compiled as C++17. Error model: `er_set()` + return codes, no exceptions.

## MODULE MAP

| Module | Files | Purpose | Key File(s) |
|--------|-------|---------|-------------|
| base | 170 | Memory, strings, error infra | `system_parameter.c` (12k), `error_manager.c` |
| broker | 138 | Connection broker + CAS | `cas_execute.c` (10k) |
| query | 82+ | Execution engine + parallel | `query_executor.c` (26k), `string_opfunc.c` (28k) |
| parser | 38 | SQL parse → XASL | `xasl_generation.c` (28k), `type_checking.c` (23k) |
| optimizer | 16 | Query plan + rewrite | `query_planner.c` (12k), `query_graph.c` (9k) |
| storage | 56 | Disk structures | `btree.c` (36k), `heap_file.c` (26k), `page_buffer.c` (16k) |
| transaction | 80 | WAL, locks, recovery | `log_manager.c` (15k), `lock_manager.c` (9k) |
| object | 80 | Schema, domains, primitives | `schema_manager.c` (16k), `object_primitive.c` (14k) |
| communication | 15 | Client↔server RPC | `network_interface_sr.c` / `_cl.c` (~11k each) |
| connection | 28 | Connection lifecycle | |
| compat | 48 | DB API compat layer | |
| executables | 52 | CLI binaries | `csql.c`, `util_sa.c`, `util_cs.c` |
| loaddb | 28 | Bulk data loader | |
| sp | 33 | Stored proc C bridge | Pairs with `pl_engine/` Java side |
| method | 30 | Server-side methods | |
| xasl | 18 | XASL IR structures | Shared between parser → executor |
| thread | 25 | Thread pool, workers | |
| monitor | 11 | Perf counters | |
| session | ~5 | Session state | |
| cm_common | 17 | Shared manager utils | |
| api | ~5 | Public C API | |
| heaplayers | ~5 | Custom allocators | |
| debugging | ~3 | Debug helpers | |
| win_tools | 11 | Windows cubridtray | |

## DATA FLOW

```
SQL text → parser/ (parse + semantic check)
        → optimizer/ (plan + rewrite)
        → xasl/ (IR generation)
        → query/ (execution, scans, joins)
        → storage/ (B-tree, heap, page I/O)
        → transaction/ (WAL, locks, MVCC)
```

## CONVENTIONS

- Functions return `int` (`NO_ERROR` / `ER_FAILED`) — check every call
- `er_set(ER_ERROR_SEVERITY, ...)` before returning error
- Memory: `db_private_alloc` / `malloc` + explicit free, NO RAII
- Thread-safe via explicit mutex/lock patterns in `thread/`
- Build links different subsets per mode: `sa/` (standalone), `cs/` (client-server), `broker/`

## ANTI-PATTERNS

- Do NOT use C++ STL containers in hot paths without profiling
- Do NOT add `#pragma once` — project uses `#ifndef` guards
- Do NOT throw exceptions — entire codebase uses C error model
- Do NOT split large files — 10k+ line files are intentional here
