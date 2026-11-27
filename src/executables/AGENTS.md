# src/executables/ — Binary Entry Points & Utilities

Entry points of the CUBRID programs and the admin utilities (`cubrid <utility>`). Follow [src/AGENTS.md](../AGENTS.md) for build modes and CMake registration.

## Binaries

Every binary below is defined in `util/CMakeLists.txt`. `gen_timezone` (`generate_timezone.cpp`) is the exception: `timezones/CMakeLists.txt` defines it.

| Binary | Entry file | Notes |
|--------|-----------|-------|
| `cub_server` | `server.c` | Links `libcubrid` (`SERVER_MODE`) |
| `cub_master` | `master.c` (+ `master_heartbeat.c` on Unix) | Manages the server processes on a host and the HA node heartbeat; not a database server |
| `cubrid` (target `cubrid-bin`) | `util_service.c` | Front end: `service`/`server`/`broker`/`heartbeat`/... commands; runs `cub_admin` for admin utilities |
| `cub_admin` | `util_admin.c` | Loads `libcubridsa` or `libcubridcs` at run time and calls the utility function |
| `csql` | `csql_launcher.c` | Loads `libcubridsa` (`-S`) or `libcubridcs` (`-C`); the shell itself (`csql.c`, `csql_session.c`, `csql_result.c`) is in both libraries |
| `cub_commdb` | `commdb.c` | Sends status and control requests to `cub_master` |

`loaddb`, `unloaddb`, `compactdb`, and the other admin utilities are not separate binaries. They are functions in `libcubridsa`/`libcubridcs` that `cub_admin` calls.

## Admin Utilities

`cubrid <name>` → `us_Service_map[]` (`util_service.c`) → exec `cub_admin` → `ua_Utility_Map[]` (`util_admin.c`) → load a library and call `function_name` from the map entry. The `utility_type` field selects the library:

- `SA_ONLY` → `libcubridsa`; `CS_ONLY` → `libcubridcs`.
- `SA_CS` → `libcubridcs` with `-C`, `libcubridsa` with `-S`. With neither, `util_get_library_name ()` follows the option map defaults: `loaddb` defaults `-S` on (`ua_Load_Option_Map`) and runs SA; `vacuumdb` and `tde` default to `libcubridsa`; the others use `libcubridcs`. Check the utility's option map before you assume its default mode. Examples: `loaddb` (function `loaddb_user` in `src/loaddb/load_db.c`), `unloaddb` (`unloaddb.c`), `compactdb` (`compactdb.c` in SA, `compactdb_cl.c` in CS). A change to an `SA_CS` utility must build and work in both modes.

`util_common.c` holds shared helpers only (option access, name checks). It has no registration table.

### Adding an Admin Utility

1. `utility.h`: add the index to `UTIL_INDEX` just before `LOGFILEDUMP` (the last enumerator, which has no map entry), the name `#define UTIL_OPTION_<NAME> "<name>"`, option letter/long-name defines, and a message set `MSGCAT_UTIL_SET_<NAME>` with its message ids.
2. `util_admin.c`: add the `GETOPT_LONG` and `UTIL_ARG_MAP` option arrays and an entry in `ua_Utility_Map[]` at the array position equal to its `UTIL_INDEX` value (just before the `{-1, ...}` terminator when added as in step 1); `util_admin.c` indexes the map by the enum value.
3. `util_service.c`: add `{ADMIN, UTIL_OPTION_<NAME>, MASK_ADMIN}` to `us_Service_map[]`. Without it, `cubrid <name>` does not reach `cub_admin`.
4. Implement `int <function_name> (UTIL_FUNCTION_ARG *arg)` in a source that builds into each library the type needs: `util_sa.c` is SA-only; `util_cs.c` builds into both `libcubridcs` and `libcubridsa`. On Windows, also export it in `win/cubridsa/cubridsa.def` and/or `win/cubridcs/cubridcs.def`.
5. Add the usage and messages to `msg/en_US.utf8/utils.msg` and `msg/ko_KR.utf8/utils.msg` under `$set <n> MSGCAT_UTIL_SET_<NAME>`.

## Where to Look

| Task | File |
|------|------|
| Server startup | `server.c` |
| csql behavior | `csql.c`, `csql_session.c`, `csql_result.c` |
| `cubrid service/server/broker/heartbeat` commands | `util_service.c` |
| Admin utility options and dispatch | `util_admin.c`, `utility.h` |
| Admin utility bodies | `util_sa.c`, `util_cs.c`, `unloaddb.c`, `compactdb*.c`; loaddb in [src/loaddb/](../loaddb/AGENTS.md) |
| HA node heartbeat, failover | `master_heartbeat.c` |
