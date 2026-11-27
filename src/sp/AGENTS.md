# src/sp/ — Stored Procedures: Engine Side of the PL Server

The engine side of Java and PL/CSQL stored procedures: DDL and catalog, execution, and the socket protocol to the PL server process. The Java side is [pl_engine/AGENTS.md](../../pl_engine/AGENTS.md).

## Architecture

```
cub_server / SA process                         cub_pl process (PL_MODE)
  pl_sr.cpp: forks and monitors cub_pl  ──→      src/executables/pl.cpp + pl_sr_jvm.cpp
  pl_executor.cpp: packs args, reads result      starts the JVM in-process (JNI_CreateJavaVM)
  pl_connection.cpp: connection pool             runs pl_server.jar (pl_engine/)
  pl_comm.c ── socket ─────────────────────→
```

- `cub_server` has no JNI. `pl_sr_jvm.cpp` builds only into `cub_pl` (`util/CMakeLists.txt`). SP call latency is socket round-trips, not JNI.
- Transport (`pl_connect_server ()` in `pl_comm.c`): a Unix domain socket on Linux by default. TCP to `127.0.0.1` on Windows, or when `stored_procedure_uds` is off (then `stored_procedure_port` is used). There is no remote PL server.
- The engine starts `cub_pl` (`server_monitor_task` in `pl_sr.cpp`). Do not start `cub_pl` by hand or add a startup step. A call in progress when `cub_pl` dies fails with `ER_SP_NETWORK_ERROR`. In `SERVER_MODE`, the `pl-monitor` daemon checks `cub_pl` once per second and restarts it. Until the daemon sees the death, the state stays running, and a new call fails at connect with `ER_SP_NETWORK_ERROR` or `ER_SP_CANNOT_CONNECT_PL_SERVER`. After that, a new call blocks in `connection_pool::claim ()` (`pl_server_wait_for_ready ()`) until `cub_pl` is running again, with no timeout; it fails with `ER_SP_CANNOT_START_PL_SERVER` only if the first start fails during server boot. In `SA_MODE`, the engine starts `cub_pl` once and does not restart it: a new call retries only when the start failed (then `ER_SP_CANNOT_START_PL_SERVER`). After `cub_pl` dies, the state stays running, so a new call fails at connect with `ER_SP_CANNOT_CONNECT_PL_SERVER` or `ER_SP_NETWORK_ERROR` (`pl_connection.cpp`).
- SQL that a running procedure executes, and its COMMIT/ROLLBACK, come back to the client as callbacks (`src/method/method_callback.cpp`); the client checks them against `sp_context_rules.cpp`.

## Key Files

| File | Role |
|------|------|
| `jsp_cl.cpp/h` | Client entry points for SP DDL and CALL: `jsp_create_stored_procedure ()`, `jsp_alter_stored_procedure ()`, `jsp_drop_stored_procedure ()`, `jsp_call_stored_procedure ()` |
| `sp_catalog.cpp/hpp` | Catalog insert helpers (`sp_add_stored_procedure ()`, ...), called from `jsp_cl.cpp` |
| `sp_code.cpp/hpp` | Stored SP source/code |
| `sp_context_rules.cpp/hpp` | What a procedure may do, checked against where it was invoked |
| `pl_sr.cpp/h` | Server side: PL server manager and monitor, connection pool access |
| `pl_executor.cpp/hpp` | SP execution: argument packing and result unpacking with `cubmethod::dbvalue_java` |
| `pl_signature.cpp/hpp` | Signature metadata (`pl_signature`, `pl_arg`: modes, type ids, defaults); no value marshalling |
| `pl_struct_compile.cpp/hpp` | PL/CSQL compile protocol (`compile_request`, `compile_response`, `sql_semantics`) |
| `pl_compile_handler.cpp/hpp` | PL/CSQL compile requests |
| `pl_session.cpp/hpp`, `pl_execution_stack_context.cpp/hpp`, `pl_query_cursor.cpp/hpp` | Session, nested call stack, and cursors of SP SQL |
| `pl_comm.c/h`, `pl_connection.cpp/hpp` | Socket I/O and connection pool |
| `pl_file.c/h` | PL server info file (`PL_SERVER_INFO`: pid, port), not JAR files |
| `pl_sr_jvm.cpp/h` | JVM startup inside `cub_pl` only |
| `method_invoke_group.cpp/hpp` | Method scan invocation for C methods (see [src/method/AGENTS.md](../method/AGENTS.md)) |

## Where to Look

| Task | File |
|------|------|
| CREATE / ALTER / DROP PROCEDURE or FUNCTION | `jsp_cl.cpp`, then `sp_catalog.cpp` |
| SP call in a query (`TYPE_SP` regu variable) | `src/query/fetch.c` → `pl_executor.cpp` |
| Argument or return value conversion | `pl_executor.cpp`, `src/method/method_struct_value.cpp` (`dbvalue_java`), and `CUBRIDPacker.java` / `CUBRIDUnpacker.java` in `pl_engine/pl_server/src/main/java/com/cubrid/jsp/data/` |
| PL server start, restart, connection | `pl_sr.cpp`, `pl_connection.cpp`, `pl_comm.c` |
| JAR loading (`loadjava`) | `src/executables/loadjava.cpp` |

## Rules

- **Wire format**: the C packer (`dbvalue_java` in `src/method/method_struct_value.cpp`) and the Java `CUBRIDPacker` / `CUBRIDUnpacker` must change together, in the same commit. For any other protocol change, find the Java class that reads or writes the same message and change it in the same commit. The Java side is spread across `com.cubrid.jsp.protocol` (`RequestCode`, `Header`, the semantics and bootstrap messages), `com.cubrid.jsp.data` (`CompileRequest` / `CompileInfo` for `compile_request` / `compile_response`), and `com.cubrid.jsp.impl.SUFunctionCode` (the same values as the C `METHOD_CALLBACK_*` enum in `src/sp/sp_constants.hpp`).
- Java exceptions come back as error codes on the C side and are reported with `er_set ()`.
- Take PL server connections from the pool with `connection_pool::claim ()` and hold them in the returned `connection_view` (a `std::unique_ptr` whose deleter gives the connection back). A connection kept outside a view can leak and hang later calls.
