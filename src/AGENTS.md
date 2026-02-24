# src/ — CUBRID Engine Source

**Compilation**: `.c` files are compiled with C++17 compiler (see `c_to_cpp.sh`). No C++ exceptions — C error model throughout.

26 modules. Same source → 3 binaries via `SERVER_MODE`/`SA_MODE`/`CS_MODE` guards.

## Module Map

| Module | Files | Role |
|--------|-------|------|
| `parser/` | 36 | SQL → PT_NODE tree, name resolution, semantic check, XASL gen |
| `optimizer/` | 16 | Cost-based query planning, QO_PLAN tree |
| `query/` | 82 | XASL execution, scan managers, aggregation, sorting |
| `storage/` | 56 | Buffer pool, heap files, B-tree, disk/file manager, extendible hash |
| `transaction/` | 80 | MVCC, WAL, locking, recovery, boot sequence |
| `object/` | 80 | Schema mgmt, catalog, auth, triggers, information_schema views |
| `compat/` | 47 | Public client API (`db_*`), DB_VALUE operations |
| `base/` | 170 | Error codes, memory, lock-free structures, porting layer |
| `xasl/` | 18 | XASL/REGU_VARIABLE node type definitions |
| `broker/` | 138 | Connection broker, CAS processes, shared memory IPC |
| `sp/` | 33 | Stored procedure JNI bridge to Java PL engine |
| `executables/` | 52 | Binary entry points: cub_server, csql, loaddb, utilities |
| `connection/` | 28 | Client-server TCP, heartbeat, CSS protocol |
| `method/` | 30 | Method/SP invocation from query execution |
| `thread/` | 25 | C++17 worker pools, daemon threads, manager |
| `loaddb/` | 26 | Bulk data loader with bison/flex grammar |
| `monitor/` | 8 | Performance statistics collection |
| `session/` | 6 | Per-connection session state |
| `communication/` | 6 | Internal C++ protocol classes |
| `heaplayers/` | 12 | 3rd-party malloc/heap allocators — DO NOT MODIFY |
| `cm_common/` | 8 | CUBRID Manager shared utilities |
| `api/` | — | API layer |
| `debugging/` | — | Debug utilities |
| `win_tools/` | — | Windows service/tray tools |

## Cross-Module Dependencies

```
parser/ ──→ optimizer/ ──→ xasl/ ──→ query/
   │                                    │
   └── compat/ (DB_VALUE)               ├── storage/ (scans, btree, heap)
                                        └── transaction/ (locks, MVCC, WAL)
```

- `parser/` and `optimizer/` are **client-side only** (`#if !defined(SERVER_MODE)`)
- `query/`, `storage/`, `transaction/` are **server-side**
- `compat/` bridges both sides (DB_VALUE used everywhere)
- `base/` is foundational — used by all modules

## Build Integration

No per-module CMakeLists.txt. All source lists in top-level `cubrid/CMakeLists.txt`, `cs/CMakeLists.txt`, `sa/CMakeLists.txt`.

To add a new source file: edit the appropriate top-level CMakeLists.txt, not anything inside `src/`.

## Preprocessor Mode Boundaries

| Code area | SERVER_MODE | SA_MODE | CS_MODE |
|-----------|-------------|---------|---------|
| Parser/optimizer | ✗ | ✓ | ✓ |
| Query execution | ✓ | ✓ | ✗ |
| Storage/buffer | ✓ | ✓ | ✗ |
| Transaction/lock | ✓ | ✓ | ✗ |
| Client API (db_*) | ✗ | ✓ | ✓ |
| Connection/network | ✓ | ✗ | ✓ |
