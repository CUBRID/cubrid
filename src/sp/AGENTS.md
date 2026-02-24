# src/sp/ — Stored Procedure JNI Bridge

33 code files. Bridges C server ↔ Java PL engine for stored procedures.

## Key Files

| File | Role |
|------|------|
| `sp_catalog.cpp` | SP catalog management (create/drop/alter stored procedures) |
| `sp_execute.cpp` | SP execution: argument marshalling, result handling |
| `sp_code.cpp` | SP code management (source storage, compilation) |
| `jsp_comm.c` | JNI/socket communication with Java PL engine |
| `jsp_sr.c` | Server-side SP invocation entry points |
| `jsp_cl.c` | Client-side SP API |
| `jsp_file.c` | SP file operations (JAR upload/management) |
| `sp_parser.cpp` | SP signature parsing |
| `sp_type.cpp` | SP type mapping (SQL types ↔ Java types) |
| `sp_value.cpp` | SP value conversion (DB_VALUE ↔ Java values) |
| `pl_connection.cpp` | Connection to PL engine process |
| `pl_comm.cpp` | Protocol for PL engine communication |
| `pl_executor.cpp` | PL execution orchestration |
| `pl_session.cpp` | PL session management |

## Architecture

```
cub_server (C) ──→ JNI/Unix socket ──→ PL Engine (Java)
     │                                       │
  sp_execute.cpp                      pl_engine/
  sp_value.cpp (marshal)              (see pl_engine/AGENTS.md)
```

## Where to Look

| Task | File |
|------|------|
| Fix SP creation/drop | `sp_catalog.cpp` |
| Fix SP execution | `sp_execute.cpp`, `pl_executor.cpp` |
| Fix SP argument passing | `sp_value.cpp`, `sp_type.cpp` |
| Fix PL engine connection | `pl_connection.cpp`, `jsp_comm.c` |
| Fix JAR management | `jsp_file.c` |
| Fix SP signature parsing | `sp_parser.cpp` |

## Type Mapping

SQL types are mapped to Java types in `sp_type.cpp`:
- `DB_INT` ↔ `int/Integer`
- `DB_STRING` ↔ `String`
- `DB_NUMERIC` ↔ `BigDecimal`
- `DB_DATE` ↔ `java.sql.Date`
- Result sets ↔ `java.sql.ResultSet`

## Conventions

- Functions prefixed `sp_` (catalog/execution), `jsp_` (JNI/communication), `pl_` (PL engine protocol)
- DB_VALUE marshalling: `sp_value.cpp` converts between C DB_VALUE and Java representations
- Communication: Unix domain sockets (primary) or TCP for remote PL engine
- Error propagation: Java exceptions → `er_set()` error codes on C side

## Gotchas

- PL engine is a separate Java process — must be running for SP execution
- JNI calls have JVM overhead — SP calls are significantly slower than native SQL functions
- Type mismatches between SQL and Java are common bug source — check `sp_type.cpp` mappings
- Connection pooling to PL engine managed in `pl_connection.cpp` — connection leaks cause hangs

## Owner

CODEOWNERS: @beyondykk9
