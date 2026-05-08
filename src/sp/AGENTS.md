# src/sp/ — Stored Procedure JNI Bridge

Bridges C server ↔ Java PL engine for stored procedures.

## Key Files

| File | Role |
|------|------|
| `sp_catalog.cpp/hpp` | SP catalog management (create/drop/alter stored procedures) |
| `sp_code.cpp/hpp` | SP code management (source storage, compilation) |
| `sp_constants.hpp` | SP-related constants |
| `jsp_cl.cpp/h` | Client-side SP API |
| `pl_sr.cpp/h` | Server-side SP invocation entry points |
| `pl_sr_jvm.cpp/h` | JVM-specific server-side SP support |
| `pl_comm.c/h` | Socket communication with Java PL engine |
| `pl_file.c/h` | SP file operations (JAR upload/management) |
| `pl_connection.cpp/hpp` | Connection to PL engine process |
| `pl_executor.cpp/hpp` | PL execution orchestration |
| `pl_session.cpp/hpp` | PL session management |
| `pl_signature.cpp/hpp` | SP signature parsing and handling |
| `pl_execution_stack_context.cpp/hpp` | PL execution stack context |
| `pl_query_cursor.cpp/hpp` | Query cursor management in PL execution |
| `pl_compile_handler.cpp/hpp` | PL compile handler |
| `pl_struct_compile.cpp/hpp` | PL compile structure definitions |
| `method_invoke_group.cpp/hpp` | Grouped method invocation (shared with `src/method/`) |

## Architecture

```
cub_server (C) ──→ JNI/Unix socket ──→ PL Engine (Java)
     │                                       │
  pl_executor.cpp                     pl_engine/
  pl_signature.cpp (marshal)          (see pl_engine/AGENTS.md)
```

## Where to Look

| Task | File |
|------|------|
| Fix SP creation/drop | `sp_catalog.cpp` |
| Fix SP execution | `pl_executor.cpp`, `pl_sr.cpp` |
| Fix SP argument passing | `pl_signature.cpp`, `pl_struct_compile.cpp` |
| Fix PL engine connection | `pl_connection.cpp`, `pl_comm.c` |
| Fix JAR management | `pl_file.c` |
| Fix SP signature parsing | `pl_signature.cpp` |

## Type Mapping

SQL types are mapped to Java types via `pl_signature.cpp` and `pl_struct_compile.cpp`:
- `DB_INT` ↔ `int/Integer`
- `DB_STRING` ↔ `String`
- `DB_NUMERIC` ↔ `BigDecimal`
- `DB_DATE` ↔ `java.sql.Date`
- Result sets ↔ `java.sql.ResultSet`

## Conventions

- Functions prefixed `sp_` (catalog/execution), `jsp_` (JNI/communication), `pl_` (PL engine protocol)
- DB_VALUE marshalling: `pl_signature.cpp` converts between C DB_VALUE and Java representations
- Communication: Unix domain sockets (primary) or TCP for remote PL engine
- Error propagation: Java exceptions → `er_set()` error codes on C side

## Gotchas

- PL engine is a separate Java process — must be running for SP execution
- JNI calls have JVM overhead — SP calls are significantly slower than native SQL functions
- Type mismatches between SQL and Java are common bug source — check `pl_signature.cpp` mappings
- Connection pooling to PL engine managed in `pl_connection.cpp` — connection leaks cause hangs

