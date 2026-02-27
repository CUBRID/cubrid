# src/loaddb/ — Bulk Data Loader

Bison/flex grammar for CUBRID's `loaddb` data format.

## Key Files

| File | Role |
|------|------|
| `load_grammar.yy` | Bison grammar for loaddb input format |
| `load_lexer.l` | Flex lexer for loaddb input |
| `load_driver.cpp` | Load driver — orchestrates parsing and insertion |
| `load_session.cpp` | Load session management, batch processing |
| `load_worker_manager.cpp` | Worker thread management for parallel loading |
| `load_server_loader.cpp` | Server-side loading (direct heap/index insertion) |
| `load_common.cpp` | Shared utilities |
| `load_class_registry.cpp` | Class (table) registry during load |
| `load_error_handler.cpp` | Error handling during bulk load |
| `load_db_value_converter.cpp` | String → DB_VALUE conversion for loaded data |
| `load_object.c` | Object construction from parsed data |
| `load_object_table.c` | Object table management |

## Where to Look

| Task | File |
|------|------|
| Fix loaddb parse error | `load_grammar.yy`, `load_lexer.l` |
| Fix data conversion | `load_db_value_converter.cpp` |
| Fix load performance | `load_worker_manager.cpp`, `load_session.cpp` |
| Fix server-side loading | `load_server_loader.cpp` |
| Fix error handling | `load_error_handler.cpp` |

## Load Modes

- **Standalone** (`SA_MODE`): Direct file access, single-process
- **Client-server** (`CS_MODE`): Data sent over network to server, parallel workers

## Data Format

```
%class table_name (col1 col2 col3)
1 'hello' 3.14
2 'world' 2.71
```

- `%class` directive specifies target table
- Data rows follow, space-separated
- Supports: strings, numbers, dates, NULL, set types, OID references

## Conventions

- Functions prefixed `load_` or `ldr_`
- C++ classes in `cubload` namespace
- Worker threads managed by `load_worker_manager` — configurable parallelism
- Batch processing: rows collected into batches, then bulk-inserted

## Gotchas

- Grammar generates C code via bison — `load_grammar.yy` modifications require bison regen
- Server-side loader bypasses normal INSERT path — directly manipulates heap/index
- Some unit tests disabled (`LOADDB` module) due to compilation issues

