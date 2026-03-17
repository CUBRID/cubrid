# src/executables/ — Binary Entry Points & Utilities

All CUBRID binary programs start here.

## Key Files

| File | Role |
|------|------|
| `server.c` | `cub_server` entry point — database server process |
| `csql.c` | `csql` interactive SQL shell |
| `csql_launcher.c` | csql command-line argument parsing |
| `csql_result.c` | csql query result display |
| `csql_session.c` | csql session management |
| `unloaddb.c` | `unloaddb` data export entry point |
| `compactdb.c` | `compactdb` space reclamation |
| `util_service.c` | `cubrid` service command (start/stop/status) |
| `util_cs.c` | Client-server utility functions |
| `util_sa.c` | Standalone utility functions |
| `util_common.c` | Shared utility helpers |
| `master.c` | `cub_master` — master process (manages server processes) |
| `master_heartbeat.c` | HA heartbeat in master process |
| `commdb.c` | Master communication utility |

## Where to Look

| Task | File |
|------|------|
| Fix server startup | `server.c` |
| Fix csql behavior | `csql.c`, `csql_session.c`, `csql_result.c` |
| Fix loaddb/unloaddb | `src/loaddb/` (loaddb code), `unloaddb.c` |
| Fix service commands | `util_service.c` |
| Fix HA/heartbeat | `master_heartbeat.c` |
| Add new utility | Create entry point here + add to CMakeLists.txt |

## Binary → Source Map

| Binary | Entry File | Build Target |
|--------|-----------|--------------|
| `cub_server` | `server.c` | `cubrid/CMakeLists.txt` |
| `csql` | `csql_launcher.c` → `csql.c` | `cs/CMakeLists.txt` |
| `loaddb` | `src/loaddb/` (standalone loader) | `sa/CMakeLists.txt` |
| `unloaddb` | `unloaddb.c` | `sa/CMakeLists.txt` |
| `compactdb` | `compactdb.c` | `sa/CMakeLists.txt` |
| `cub_master` | `master.c` | top-level CMake |
| `cubrid` | `util_service.c` | top-level CMake |

## Conventions

- Utility programs: parse args → call into engine libraries → exit
- Standalone utilities (`loaddb`, `unloaddb`, `compactdb`) link `cubridsa` (SA_MODE)
- Client-server utilities link `cubridcs` (CS_MODE)
- `csql` functions prefixed `csql_` — self-contained subsystem within this dir
- Utility registration in `util_common.c` — defines available subcommands

## Gotchas

- `loaddb` can run in standalone or client-server mode — different code paths
- `csql` has its own mini-framework (session, result display, input handling)
- `master.c` manages all database server processes on a host — not a database server itself

