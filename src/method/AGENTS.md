# src/method/ — Method & SP Invocation from Queries

30 code files. Handles method calls and stored procedure invocation during query execution.

## Key Files

| File | Role |
|------|------|
| `method_scan.c` | Method scan for query executor — calls methods during scan |
| `method_def.hpp` | Method definition structures |
| `method_invoke.cpp` | Method invocation dispatch |
| `method_invoke_builtin.cpp` | Built-in method execution |
| `method_invoke_java.cpp` | Java SP method invocation |
| `method_invoke_group.cpp` | Grouped method invocation |
| `method_connection_sr.cpp` | Server-side method connection handling |
| `method_connection_cl.cpp` | Client-side method connection handling |
| `method_query_handler.cpp` | Method query result handling |
| `method_runtime_context.cpp` | Runtime context for method execution |
| `method_struct_value.cpp` | Method argument/result value handling |
| `method_compile.cpp` | Method compilation support |
| `method_struct_oid_info.cpp` | OID info for method invocation |

## Where to Look

| Task | File |
|------|------|
| Fix method call in query | `method_scan.c`, `method_invoke.cpp` |
| Fix Java SP invocation | `method_invoke_java.cpp` |
| Fix built-in method | `method_invoke_builtin.cpp` |
| Fix method argument passing | `method_struct_value.cpp` |
| Fix method connection | `method_connection_sr.cpp` / `_cl.cpp` |

## Architecture

```
query_executor.c → scan_manager.c (METHOD_SCAN)
  → method_scan.c → method_invoke.cpp
    → method_invoke_builtin.cpp (C methods)
    → method_invoke_java.cpp → sp/ → PL Engine
```

## Conventions

- Functions prefixed `method_`
- Method scan integrates with query executor via `SCAN_TYPE_METHOD`
- Java methods route through `src/sp/` to PL engine
- Built-in methods execute in-process

## Gotchas

- Method calls during scans can be expensive — each row triggers invocation
- Java method calls involve cross-process communication — timeout handling critical
- Method results must be properly converted to `DB_VALUE` for query engine

## Owner

CODEOWNERS: @beyondykk9
