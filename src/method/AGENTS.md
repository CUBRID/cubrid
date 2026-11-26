# src/method/ — Method Scans, Client Callbacks, and the DB_VALUE Wire Packer

This module does three jobs:
- It runs C methods (instance/class methods) called from queries.
- It serves the SQL that a running stored procedure sends back to the client.
- It holds `dbvalue_java`, the C side of the `DB_VALUE` wire format shared with the PL server.

Java and PL/CSQL stored procedures called in expressions do not go through a method scan: see [src/sp/AGENTS.md](../sp/AGENTS.md).

## Call Paths

```
C method in a query (S_METHOD_SCAN):
  scan_manager.c scan_open_method_scan () → method_scan.cpp
    → src/sp/method_invoke_group.cpp (asserts PL_TYPE_IS_METHOD)
    → xs_callback_send_args () / xs_callback_send_and_receive () to the client
    → query_method.cpp method_dispatch () runs the method on the client (CS_MODE and SA_MODE variants)

Java / PL/CSQL SP in an expression (TYPE_SP regu variable):
  src/query/fetch.c → src/sp/pl_executor.cpp → cub_pl

SQL from a running SP (server-side JDBC):
  cub_pl → cub_server → client callback → method_callback.cpp (callback_handler)
    → method_query_handler.cpp / method_oid_handler.cpp
```

## Key Files

| File | Role |
|------|------|
| `method_scan.cpp/hpp` | Method scan used by the executor |
| `query_method.cpp/hpp` | Client side of method calls (`method_dispatch ()`, one variant each for `CS_MODE` and `SA_MODE`) |
| `method_callback.cpp/hpp` | `cubmethod::callback_handler`: entry point for requests from server-side JDBC in a running SP |
| `method_query_handler.cpp/hpp`, `method_query_result.cpp/hpp`, `method_oid_handler.cpp/hpp` | Prepare/execute/fetch and OID requests from those callbacks |
| `method_struct_value.cpp/hpp` | `dbvalue_java`: `DB_VALUE` packing for the PL protocol |
| `method_struct_invoke.cpp/hpp` | Invoke request structures |
| `method_struct_*.cpp/hpp` (others) | Packable structures for queries, OIDs, parameters, and schema info |
| `method_error.cpp/hpp` | Error handling |

Mode membership differs per file (for example `method_scan.cpp` is not in `CS_MODE`, and `method_callback.cpp` is not in `SERVER_MODE`). Check `cubrid/`, `sa/`, and `cs/CMakeLists.txt` before you move code between files.

## Rules

- `dbvalue_java` must stay in sync with `CUBRIDPacker.java` / `CUBRIDUnpacker.java` (`pl_engine/pl_server/src/main/java/com/cubrid/jsp/data/`). Change both sides in the same commit.
- A method scan invokes the method once per row. In `CS_MODE` each call is a network round-trip to the client.
