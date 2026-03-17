# AGENTS.md — CUBRID Database Engine


## Overview

CUBRID is an open-source C/C++17 RDBMS with a Java PL engine for stored procedures.
Apache 2.0 license. Version 11.5.x.

**Note:** `.c` files are compiled as C++17 (see `c_to_cpp.sh`). Large files (10K–30K+ lines) are intentional, not tech debt.

## Structure

```
├── src/                 # C/C++ engine (see src/AGENTS.md)
│   ├── parser/          # SQL → parse tree → XASL (bison/flex)
│   ├── optimizer/       # Cost-based query planning
│   ├── query/           # XASL execution, scan managers
│   ├── storage/         # Buffer pool, heap files, B-tree
│   ├── transaction/     # MVCC, WAL, locking, recovery, boot
│   ├── object/          # Schema, auth, information_schema
│   ├── compat/          # Client API (db_*), DB_VALUE
│   ├── base/            # Error handling, memory, lock-free, porting
│   ├── xasl/            # XASL node type definitions
│   ├── executables/     # Binaries: cub_server, csql, utilities
│   ├── broker/          # Connection broker (CAS processes)
│   ├── sp/              # Stored procedure JNI bridge
│   ├── connection/      # Client-server TCP/heartbeat
│   ├── method/          # Method/SP invocation from queries
│   ├── thread/          # Worker pools, daemons (C++17)
│   ├── loaddb/          # Bulk loader (bison/flex grammar)
│   ├── monitor/         # Performance statistics
│   ├── session/         # Per-connection session state
│   ├── communication/   # Internal protocol (C++)
│   ├── heaplayers/      # Embedded malloc/heap allocators
│   ├── cm_common/       # CUBRID Manager shared utils
│   ├── api/             # Public C API extensions (cubrid_log.c)
│   ├── debugging/       # Compiler warning helpers, type utilities (strict_warnings, type_helper)
│   └── win_tools/       # Windows service/tray tools
├── unit_tests/          # Catch2 v2.11.3 (see unit_tests/AGENTS.md)
├── pl_engine/           # Java PL server, Gradle (see pl_engine/AGENTS.md)
├── cubrid-jdbc/         # JDBC driver (git submodule, Ant build)
├── cubrid-cci/          # CCI C driver (git submodule, CMake build)
├── cubridmanager/       # CUBRID Manager server (git submodule)
├── broker/              # Broker configs + top-level CMake target
├── cs/                  # Client-server library (cubridcs) CMake target
├── sa/                  # Standalone library (cubridsa) CMake target
├── cubrid/              # Server binary (cub_server) CMake target
├── cmake/               # CMake modules, CPack scripts
├── conf/                # Broker config templates, SSL certs
├── msg/                 # Localized error messages (en_US, ko_KR)
├── locales/             # Locale data and libraries
├── timezones/           # Timezone data
├── contrib/             # Python, PHP, Perl drivers + cloud Docker
├── tests/               # Shell integration tests
├── docs/                # Build requirements docs
├── debian/              # DEB packaging
└── win/                 # Windows build scripts
```

> **Note:** Submodule canonical URLs and pinned revisions are defined in `.gitmodules`.

## Where to Look

| Task | Location | Notes |
|------|----------|-------|
| Add SQL syntax | `src/parser/csql_grammar.y` | Bison grammar, ~646KB |
| Add built-in function | parser → type_checking → xasl_generation → query/ | Full pipeline across 4 modules |
| Add info schema view | `src/object/schema_system_catalog_install.cpp` + `*_install_query_spec.cpp` | Strict formatting rules |
| Fix query execution | `src/query/query_executor.c` | Entry: `qexec_execute_mainblock()` |
| Fix index scan | `src/storage/btree.c` | Entry: `btree_find()`, `btree_range_search()` |
| Fix locking/deadlock | `src/transaction/lock_manager.c` | Wait-for graph in `wait_for_graph.c` |
| Fix MVCC visibility | `src/transaction/mvcc.c` | `mvcc_satisfies_snapshot()` |
| Add error code | `error_code.h` + `dbi_compat.h` + `cubrid.msg` + `ER_LAST_ERROR` | Also CCI's `base_error_code.h` |
| Fix buffer pool | `src/storage/page_buffer.c` | LRU replacement, dirty tracking |
| Add stored procedure feature | `src/sp/` + `pl_engine/` | JNI bridge + Java PL engine |
| Fix broker/CAS | `src/broker/broker.c`, `cas.c` | Separate processes via shared memory |
| Fix WAL/recovery | `src/transaction/log_*.c` | `log_append.cpp` for writes |
| Modify client API | `src/compat/db_*.c` | `db_make_*`, `db_value_*` patterns |
| Add unit test | `unit_tests/<module>/` | See `unit_tests/AGENTS.md` |

## Query Processing Pipeline

```
SQL text
  → Lexer (csql_lexer.l)
  → Parser (csql_grammar.y → PT_NODE tree)
  → Name Resolution (name_resolution.c)
  → Semantic Check (semantic_check.c)
  → XASL Generation (xasl_generation.c → XASL_NODE tree)
  → Serialization (xasl_to_stream.c, client→server)
  → Deserialization (stream_to_xasl.c, server side)
  → Execution (query_executor.c, scans via scan_manager.c)
```

## Build Commands

```bash
./build.sh -m debug            # Debug build (most common)
./build.sh -m release           # Release build
./build.sh -m debug -c "-DUNIT_TESTS=ON"  # With unit tests
cmake --preset debug && cmake --build build_preset_debug  # CMake presets
```

Run `./build.sh -h` for full options (`-m` mode, `-g` generator, `-C` compiler, `-z` packaging).

### build.sh Options

| Flag | Values | Default |
|------|--------|---------|
| `-m` | `release`, `debug`, `coverage`, `profile` | `release` |
| `-g` | `ninja`, `make` | `ninja` |
| `-t` | `64` | `64` |
| `-c` | Extra CMake options | — |
| `-C` | `gcc`, `clang`, `clang14`, ... | `gcc` |
| `-z` | `src`, `tarball`, `shell`, `cci`, `jdbc`, `rpm` | — |

## Build Modes & Preprocessor Guards

Same source compiles to 3 different binaries via preprocessor flags:

| Guard | Binary | Dir | Purpose |
|-------|--------|-----|---------|
| `SERVER_MODE` | `cub_server` | `cubrid/` | Server process |
| `SA_MODE` | `cubridsa` lib | `sa/` | Standalone (client+server in-process) |
| `CS_MODE` | `cubridcs` lib | `cs/` | Client library (connects to server) |

Parser/optimizer code is client-side: guarded with `#if !defined(SERVER_MODE)`.

## Code Style

### Formatting (CI-enforced)

- **Indentation**: 2 spaces, no tabs. **Line width**: 120 chars
- **C/H files**: `indent -l120 -lc120`. **C++/HPP**: `astyle --style=gnu`. **Java**: `google-java-format`
- **Braces**: GNU style — opening brace on new line, indented to body level. Function braces at column 0
- **Pointer asterisk**: Attached to variable: `PT_NODE *node`. **Spaces** before `(` in calls

### Naming

- C functions: `module_action_object` (e.g. `pt_make_flat_name_list`)
- C++ namespaces: short lowercase (`cubthread`, `lockfree`). Classes: `snake_case`
- Macros/constants/C struct typedefs: `UPPER_SNAKE` (`NO_ERROR`, `PT_NODE`)
- Header guards: `_FILENAME_H_` (NOT `#pragma once`)

### Includes

- Project: `"quotes"` — System: `<angle brackets>`. `config.h` first in `.c` files
- **`memory_wrapper.hpp` MUST be the LAST include** with comment: `// XXX: SHOULD BE THE LAST INCLUDE HEADER`
- C files: `/* ... */` comments only. C++ files: `//` acceptable. File headers: Apache 2.0 license block

## Error Handling

Error codes in `src/base/error_code.h` — always negative, `NO_ERROR = 0`. Use `er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CODE, ...)` and check `error != NO_ERROR`.

Adding new error codes requires updates in 6 places:
1. `src/base/error_code.h` — Define the code
2. `src/compat/dbi_compat.h` — Client-visible copy
3. `msg/en_US.utf8/cubrid.msg` — English message
4. `msg/ko_KR.utf8/cubrid.msg` — Korean message
5. Update `ER_LAST_ERROR` constant
6. Also: CCI's `base_error_code.h` if client-facing

## Memory Management

Use `free_and_init(ptr)` (never bare `free`), `db_private_alloc(thread_p, size)` for server, `parser_alloc(parser, len)` for parser-lifetime.

## Key Data Structures

| Structure | Header | Role |
|-----------|--------|------|
| `PT_NODE` | `src/parser/parse_tree.h` | Parse tree node — union-based, linked list |
| `XASL_NODE` | `src/query/xasl.h` (main node), `src/xasl/` (modular type headers) | Executable query plan — serialized client→server |
| `DB_VALUE` | `src/compat/dbtype_def.h` | Universal value container |
| `PAGE_BUFFER` | `src/storage/page_buffer.h` | Buffer pool with LRU, hash, dirty tracking |
| `LOCK_RESOURCE` | `src/transaction/lock_manager.h` | Lock with owner list and waiters |
| `LOG_RECORD_HEADER` | `src/transaction/log_record.hpp` | WAL record with LSN and txn ID |
| `MVCC_TRANS_STATUS` | `src/transaction/mvcc.h` | MVCC snapshot for visibility checks |

## CI

- **GitHub Actions** (`.github/workflows/check.yml`): License headers, PR title, code style, cppcheck, memory_wrapper check
- **Jenkins** (`Jenkinsfile`): Primary build — parallel release/debug, Docker `cubridci/cubridci:develop`
- **CircleCI** (`.circleci/config.yml`): SQL tests (10x parallel), shell tests (50x parallel)

PR title must match `^\[[A-Z]+-\d+\]\s.+` (e.g. `[CBRD-12345] Fix buffer overflow in btree`). CLA required before merge.

## Anti-Patterns (This Project)

- **Never** use `free()` directly — use `free_and_init()` to nullify
- **Never** use `#pragma once` — use `#ifndef _FILENAME_H_` guards
- **Never** place `memory_wrapper.hpp` before other includes — it MUST be last
- **Never** skip the `// XXX: SHOULD BE THE LAST INCLUDE HEADER` comment above `memory_wrapper.hpp`
- **Never** include files from `src/heaplayers/` in cppcheck — they are 3rd-party
- **Never** suppress cppcheck errors with inline comments without approval
- **Avoid** `lea_heap.c` modifications — 3rd-party code (181KB malloc implementation)
- **Never** use C++ exceptions in engine code — C error model (`er_set` + return codes) throughout
- **Never** use RAII for memory in engine C code — use `db_private_alloc`/`malloc` + explicit `free_and_init()`
- **Do NOT** split large files (10K+ lines) — they are intentional, not tech debt

## Gotchas

- Parser/optimizer run **client-side** (`#if !defined(SERVER_MODE)`), not on the server
- `src/broker/` and top-level `broker/` are different: src/ has implementation, top-level has CMake target
- LOB handling: LOB locator in `src/object/lob_locator.cpp`, external storage backend in `src/storage/es.c` — cross-cutting concern
- `csql_grammar.y` is 646KB — edits need care, bison regeneration is slow
- Backup files (`.c~`, `.cpp.orig`) exist in repo — ignore them
- Some unit test modules are disabled: `LOCKFREE`, `LOADDB`, `MEMORY_MONITOR` (compilation issues)
- Build requires GCC 8+ (devtoolset-8 recommended), JDK 1.8+, CMake 3.21+

## Important References

- `src/parser/parse_tree.h` — PT_NODE definitions and macros
- `src/base/error_code.h` — All error codes
- `src/object/schema_system_catalog_constants.h` — Catalog table name constants
- `docs/install_build_requirements.md` — Build dependency installation

