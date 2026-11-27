# src/loaddb/ — Bulk Data Loader

The `cubrid loaddb` utility: a Bison/Flex grammar for the loaddb object-file format, plus an SA loader and a parallel server-side loader. Follow [src/AGENTS.md](../AGENTS.md) for build modes; the utility registration is described in [src/executables/AGENTS.md](../executables/AGENTS.md).

## Load Modes

`cubrid loaddb` is an `SA_CS` utility. Its entry function is `loaddb_user ()` in `load_db.c`, which builds into both `libcubridsa` and `libcubridcs`. A change can affect either path, so build and check both.

- **Standalone** (`-S`, the default; `libcubridsa`): `ldr_sa_load ()` in `load_sa_loader.cpp` parses and loads in the utility process. `ua_Load_Option_Map` (`util_admin.c`) defaults `-S` on, so `cubrid loaddb` runs SA unless `-C` is given.
- **Client-server** (`-C`; `libcubridcs`): `ldr_server_load ()` in `load_db.c` sends the input to `cub_server`. The server parses it with `load_driver` and loads it in parallel with `load_session`, `load_worker_manager`, and `load_server_loader` (server-only files).

| File | Server (`cubrid/`) | CS (`cs/`) | SA (`sa/`) |
|------|:--:|:--:|:--:|
| `load_db.c`, `load_object.c` | — | ✓ | ✓ |
| `load_common.cpp` | ✓ | ✓ | ✓ |
| `load_driver.cpp`, `load_class_registry.cpp`, `load_db_value_converter.cpp`, `load_error_handler.cpp`, `load_semantic_helper.cpp` | ✓ | — | ✓ |
| `load_sa_loader.cpp`, `load_object_table.c` | — | — | ✓ |
| `load_session.cpp`, `load_worker_manager.cpp`, `load_server_loader.cpp` | ✓ | — | — |

## Where to Look

| Task | File |
|------|------|
| Input syntax, tokens | `load_grammar.yy`, `load_lexer.l` |
| Value conversion | `load_db_value_converter.cpp` |
| SA loading | `load_sa_loader.cpp` |
| Server loading, batches, workers | `load_session.cpp`, `load_worker_manager.cpp`, `load_server_loader.cpp` |
| Error reporting | `load_error_handler.cpp` |
| Command-line options, mode choice | `load_db.c` |

## Gotchas

- The build generates C++ from the grammar: `load_grammar.yy` → `${CMAKE_BINARY_DIR}/load_grammar.cpp` and `load_lexer.l` → `load_lexer.cpp` (targets `gen_loader_grammar` and `gen_loader_lexer`, root `CMakeLists.txt`). Edit only the `.yy`/`.l` sources. The outputs are in the build directory and are not committed.
- The server loader does not use the SQL `INSERT` path. It calls `locator_insert_force ()` / `locator_multi_insert_force ()` directly.
- The `unit_tests/loaddb/` module is disabled; see [unit_tests/AGENTS.md](../../unit_tests/AGENTS.md) before you enable it.
