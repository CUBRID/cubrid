# src/method/ — Method & SP Invocation from Queries

Handles method calls and stored procedure invocation during query execution.

## Key Files

| File | Role |
|------|------|
| `method_scan.cpp/hpp` | Method scan for query executor — calls methods during scan |
| `query_method.cpp/hpp` | Query-level method invocation |
| `method_query_handler.cpp/hpp` | Method query result handling |
| `method_query_result.cpp/hpp` | Method query result processing |
| `method_query_util.cpp/hpp` | Method query utility functions |
| `method_struct_invoke.cpp/hpp` | Method invocation structure definitions |
| `method_struct_value.cpp/hpp` | Method argument/result value handling |
| `method_struct_oid_info.cpp/hpp` | OID info for method invocation |
| `method_struct_parameter_info.cpp/hpp` | Parameter info structures |
| `method_struct_query.cpp/hpp` | Query structures for method invocation |
| `method_struct_schema_info.cpp/hpp` | Schema info structures for methods |
| `method_schema_info.cpp/hpp` | Schema information handling |
| `method_oid_handler.cpp/hpp` | OID handler for method results |
| `method_callback.cpp/hpp` | Method callback handling |
| `method_error.cpp/hpp` | Method error handling |

## Where to Look

| Task | File |
|------|------|
| Fix method call in query | `method_scan.cpp`, `query_method.cpp` |
| Fix method query handling | `method_query_handler.cpp`, `method_query_result.cpp` |
| Fix method argument passing | `method_struct_value.cpp`, `method_struct_invoke.cpp` |
| Fix method error handling | `method_error.cpp` |
| Fix method callback | `method_callback.cpp` |

## Architecture

```
query_executor.c → scan_manager.c (METHOD_SCAN)
  → method_scan.cpp → query_method.cpp
    → method_struct_invoke.cpp (dispatch)
    → src/sp/ → PL Engine (for Java SP)
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

