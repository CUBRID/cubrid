# CUBRID DATABASE — PROJECT KNOWLEDGE BASE

**Generated:** 2026-02-24  
**Commit:** a59899038  
**Branch:** opencode

## OVERVIEW

CUBRID is an open-source relational DBMS written in C/C++ (1.3M lines) with a Java-based PL/CSQL engine (~46k lines). CMake build system, C++17 required. Apache 2.0 (engine) + BSD (connectors).

## STRUCTURE

```
cubrid/
├── src/                  # Core engine (C/C++) — 24 modules
│   ├── base/             # Foundation: memory, strings, error handling (170 files)
│   ├── broker/           # Connection broker / CAS (138 files)
│   ├── query/            # Query execution + parallel framework
│   ├── parser/           # SQL parser, semantic check, XASL generation
│   ├── optimizer/        # Query planner + rewriter
│   ├── storage/          # B-tree, heap, page buffer, file I/O
│   ├── transaction/      # Logging, locking, recovery, locator
│   ├── object/           # Schema manager, object domains, primitives
│   ├── communication/    # Client-server network interface
│   ├── connection/       # Connection management
│   ├── compat/           # DB API compatibility layer
│   ├── executables/      # CLI tools (cubrid, csql, etc.)
│   ├── loaddb/           # Bulk loader
│   ├── sp/               # Stored procedure bridge (C side)
│   ├── method/           # Server-side method execution
│   ├── xasl/             # XASL (eXtended Access Specification Language) structures
│   ├── thread/           # Thread management
│   ├── monitor/          # Performance monitoring
│   ├── session/          # Session state
│   ├── cm_common/        # Common manager utilities
│   ├── api/              # Public C API
│   ├── heaplayers/       # Custom memory allocators
│   ├── debugging/        # Debug utilities
│   └── win_tools/        # Windows-specific tools (cubridtray)
├── pl_engine/            # PL/CSQL engine (Java, Gradle)
├── unit_tests/           # C++ unit tests (CMake/CTest)
├── contrib/              # Language drivers: PHP, Perl, Python, .NET, collectd
├── 3rdparty/             # Vendored dependencies
├── win/                  # Windows build (includes OpenSSL headers)
├── cmake/                # CMake modules (Find*.cmake)
├── conf/                 # Default config templates
├── locales/              # Locale/collation data
├── timezones/            # Timezone data
├── demo/                 # Demo database SQL
├── docs/                 # Build requirement docs
├── broker/, cs/, sa/     # CMakeLists for broker/client-server/standalone modes
│   cubrid/, cubrid-cci/  # Git submodule stubs
│   cubrid-jdbc/          # Git submodule stub
└── build.sh              # Linux build script (cmake + ninja)
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| SQL parsing | `src/parser/` | `parse_tree_cl.c` (20k), `xasl_generation.c` (28k) |
| Query execution | `src/query/query_executor.c` | 26k lines, core scan/join logic |
| B-tree operations | `src/storage/btree.c` | 36k lines, largest single file |
| Transaction/logging | `src/transaction/log_manager.c` | WAL implementation |
| Lock manager | `src/transaction/lock_manager.c` | ~10k lines |
| Page buffer | `src/storage/page_buffer.c` | Buffer pool management |
| Schema DDL | `src/object/schema_manager.c` | 16k lines |
| Network protocol | `src/communication/network_interface_sr.c` / `_cl.c` | Server/client sides ~11k each |
| Parallel query | `src/query/parallel/` | PX framework (heap scan, hash join, etc.) |
| Stored procedures | `pl_engine/` (Java) + `src/sp/` (C bridge) | PL/CSQL compiler + runtime |
| Build configuration | `CMakeLists.txt` (root, 951 lines) | All compile flags, targets |
| Add CLI tool | `src/executables/` | Each tool has own source file |

## CODEOWNERS

| Area | Owner |
|------|-------|
| base, broker, parser, compat, xasl, object, sp, method | @beyondykk9 |
| optimizer, query executor, hash scan/join, parallel, statistics | @shparkcubrid |
| storage, transaction, vacuum, memory_monitor | @hornetmj |
| win_tools | @hwany7seo |
| pl_engine | @beyondykk9 |

## CONVENTIONS

- **C compiled as C++**: `.c` files compiled with C++17 compiler (see `c_to_cpp.sh`)
- **Header guards**: Use `#ifndef _FILENAME_H_` style (not `#pragma once`)
- **Error handling**: Uses `er_set()` / `ER_FAILED` / `NO_ERROR` return codes, NOT exceptions
- **Memory**: Custom allocators in `src/heaplayers/`; memory monitor tracks leaks
- **Naming**: `snake_case` for functions/variables, `UPPER_CASE` for macros/constants
- **File naming**: `module_subsystem.c` / `.h` pattern (e.g. `btree.c`, `log_manager.c`)
- **Build modes**: standalone (sa), client-server (cs), broker — each has own link targets
- **PR template**: Must reference JIRA ticket `CBRD-XXXX`
- **CLA required** before PR merge

## ANTI-PATTERNS

- **Do NOT** add new 3rdparty deps without CMake integration
- **Do NOT** use C++ exceptions in engine code (C error model throughout)
- **Do NOT** bypass `er_set()` error reporting
- Large files (10k+ lines) are normal here — do NOT split without team consensus

## COMMANDS

```bash
# Full build (release, ninja)
./build.sh

# Debug build
./build.sh -m debug

# Build with unit tests
./build.sh -m debug -- -DUNIT_TESTS=ON

# CMake presets
cmake --preset=default

# Check code style (CI)
.github/workflows/codestyle.sh

# PL engine (Java)
cd pl_engine && ./gradlew build
```

## NOTES

- Git submodules: `cubrid-cci`, `cubrid-jdbc`, `cubridmanager` are stubs (may need `git submodule update`)
- VERSION file at root controls release version
- `.circleci/`, `.travis.yml`, `Jenkinsfile` — multiple CI systems (CircleCI, Travis, Jenkins)
- `win/` directory has Windows-specific 3rdparty (OpenSSL headers)
- Files routinely exceed 10k-30k lines — this is intentional, not tech debt
