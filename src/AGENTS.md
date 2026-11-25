# src/ — CUBRID Engine Source

Follow [the root AGENTS.md](../AGENTS.md) for repository-wide build, CI, and reporting rules. Consult the target module's `AGENTS.md` (and `src/storage/docs/` when working in `src/storage/`) before changing subsystem behavior.

**Compilation language**: most `.c` sources in engine targets are compiled as C++17 (`-std=c++17` in `CMakeLists.txt`) via per-target `set_source_files_properties(... PROPERTIES LANGUAGE CXX)` in CMake. Note that `c_to_cpp.sh` only renames `.c` files to `.cpp` with `git mv`; always check the owning target `CMakeLists.txt` for actual compilation settings.

## Build-Mode Boundaries

Engine sources compile into three primary modes controlled by preprocessor definitions:

- `SERVER_MODE` — server shared library (`cubrid/CMakeLists.txt` → `libcubrid`) and server executable (`util/CMakeLists.txt` → `cub_server`).
- `SA_MODE` — standalone library (`sa/CMakeLists.txt` → `libcubridsa`) combining client and server in-process.
- `CS_MODE` — client-server library (`cs/CMakeLists.txt` → `libcubridcs`) and client utilities (e.g., `util/CMakeLists.txt`).

Never infer mode membership from directory name alone; verify both the owning `CMakeLists.txt` source list and `#if defined(SERVER_MODE)` / `SA_MODE` / `CS_MODE` guards:

- `src/parser/` and most of `src/optimizer/` build only in client/standalone (`CS_MODE`/`SA_MODE`; omitted from `cubrid/CMakeLists.txt` and guarded against `SERVER_MODE` in headers), **except** `src/optimizer/histogram/` (`histogram_builder.cpp` and `histogram_reader.cpp` build in all three targets, and `histogram_sampler_sr.cpp` builds in `SERVER_MODE`/`SA_MODE`).
- `src/query/`, `src/storage/`, and `src/transaction/` are primarily `SERVER_MODE`/`SA_MODE`, yet each contributes a client subset to `cs/CMakeLists.txt` (e.g., `query_cl.c`, `xasl_to_stream.c`, `string_opfunc.c`; `es.c`, `file_io.c`, `oid.c`, `statistics_cl.c`, `tde.c`; `boot_cl.c`, `transaction_cl.c`, `locator_cl.c`, `log_comm.c`).
- `src/compat/` builds broadly in `SA_MODE`/`CS_MODE` and as a reduced subset in `SERVER_MODE`; `src/connection/` uses `_sr`/`_cl` files plus shared sources.
- Keep `#error Belongs to server module` and similar mode guards intact when refactoring headers.

## CMake Source Registration

There are no per-module `CMakeLists.txt` files under `src/`. Top-level target files reference `src/` via `*_DIR` variables defined in root `CMakeLists.txt`:

- Core engine libraries: `cubrid/CMakeLists.txt`, `sa/CMakeLists.txt`, `cs/CMakeLists.txt`.
- Executables, utilities, brokers, and unit tests: `util/CMakeLists.txt`, `broker/CMakeLists.txt`, `cm_common/CMakeLists.txt`, `timezones/CMakeLists.txt`, `win/CMakeLists.txt`, and `unit_tests/<module>/CMakeLists.txt`.

When adding a new source file under `src/`:

1. Search top-level `CMakeLists.txt` files for sibling files in the same module to identify every target that must compile it.
2. Add the file (and tracked headers where listed) to each owning target's `CMakeLists.txt`, ensuring `PROPERTIES LANGUAGE CXX` covers any C++-compiled `.c` file.
3. If the file is listed in `cubrid/CMakeLists.txt`, satisfy the `memory_wrapper.hpp` include check below.
4. Build every affected target (`libcubrid`/`cub_server`, `libcubridsa`, `libcubridcs`, utilities, broker, or unit tests).

## Resource, Memory, and Error Handling

- **Allocator/deallocator pairing** (`src/base/memory_alloc.h`, `src/parser/parser.h`):
  - `db_private_alloc (thread_p, size)` ↔ `db_private_free_and_init (thread_p, ptr)`
  - `malloc`/`calloc`/`realloc` ↔ `free_and_init (ptr)`
  - `os_malloc`/`os_calloc` ↔ `os_free_and_init (ptr)`
  - `parser_alloc (parser, len)` → parser-context arena lifetime (never freed manually)
  - Verify ownership and cleanup on every error and early-return path. Existing C++ RAII helpers (`src/base/scope_exit.hpp`, `src/base/mem_block.hpp`, `std::unique_ptr`, `std::lock_guard`) are used where appropriate; follow the surrounding module's conventions.
  - `db_private_alloc` memory belongs to the allocating thread's private heap in `SERVER_MODE` (`db_private_get_heapid_from_thread ()`): free it on the same thread. Allocate any buffer that crosses threads (parallel worker ↔ requester) on the thread that frees it, or with `malloc`. In `CS_MODE`, `db_private_alloc` delegates to `db_ws_alloc`.
  - `PEEK` results are borrowed, not owned: a peeked `RECDES` points into the fixed page (see `src/storage/docs/heap-record-pages.md`), a peeked `QFILE_TUPLE_RECORD` points into the list scan's current page with `size == 0` (`qfile_retrieve_tuple ()`), and `fetch_peek_dbval ()` returns a value the caller does not own. Never free or realloc a peeked pointer; use `COPY` / `fetch_copy_dbval ()` when the data must outlive the page fix or the next fetch.
- **Error-code interfaces vs. throwing STL/C++ operations**:
  - Engine interfaces use negative error codes in `src/base/error_code.h` (`NO_ERROR = 0`) and report errors with `er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CODE, ...)`. Do not let C++ exceptions escape across C-style engine interfaces.
  - Prefer `.cpp` files for new STL-heavy code. Whenever calling throwing STL operations (in `.cpp` or C++-compiled `.c`), catch exceptions at or before the C-interface boundary and translate them via `er_set (...)` and an error-code return (see `src/storage/heap_file.c`, `src/optimizer/histogram/histogram_sampler_sr.cpp`, and `src/query/string_regex_std.cpp`).
  - The error context is per thread (`cuberr::context`): an `er_set` on a worker thread is not visible to the thread answering the client. Hand errors over as `src/query/parallel/` does — the worker calls `move_top_error_message_to_this ()`, and the requester swaps it into its own context after `wait_workers ()`.
- **Adding a new error code** (per `src/base/error_code.h`): update `src/base/error_code.h` (error definition and `ER_LAST_ERROR`), `src/compat/dbi_compat.h`, `msg/en_US.utf8/cubrid.msg`, `msg/ko_KR.utf8/cubrid.msg`, and `cubrid-cci/src/cci/base_error_code.h` when client-facing.

## Formatting, Includes, and Static Checks

- **Formatters** (`.github/workflows/codestyle.sh`): `.c`/`.h`/`.i` use GNU `indent -l120 -lc120`; `.cpp`/`.hpp`/`.ipp` use `astyle` (GNU style, 2-space indent, 120-char line limit).
- **Protecting C++ syntax in `.c`/`.h` files**: GNU `indent` misformats C++ syntax (`try`/`catch`, templates, lambdas). Wrap C++-only blocks inside legacy `.c`/`.h` files with:

  ```c
  /* *INDENT-OFF* */
  /* C++ syntax code */
  /* *INDENT-ON* */
  ```

- **`memory_wrapper.hpp` include order** (`.github/workflows/check.yml`, `memory-monitor-check` job):
  - Applies to `.c`/`.cpp` files whose basename appears in `cubrid/CMakeLists.txt`: `src/base/memory_monitor_sr.cpp`, `src/base/memory_monitor_api.cpp`, and `src/base/memory_wrapper.cpp` are exempt, and `src/heaplayers/lea_heap.c` is exempt from the position and comment checks.
  - Place `#include "memory_wrapper.hpp"` as the last `#include`, with `// XXX: SHOULD BE THE LAST INCLUDE HEADER` on the immediately preceding line.
  - **Checked exception**: `src/debugging/strict_warnings_on.hpp` and `src/debugging/strict_warnings_off.hpp` may follow `memory_wrapper.hpp`.
  - Keep `memory_wrapper.hpp` out of header files (`.h`/`.hpp`); use `src/base/memory_cwrapper.h` in headers when allocation wrappers are needed.
- **Conventions & static analysis**: include `"config.h"` first in `.c` files; use `#ifndef _FILENAME_H_` header guards in core/legacy headers; avoid inline `cppcheck` suppressions without review.

## Cross-Module Impact Checks

- **XASL changes**: check all four pipeline stages together — generation (`src/parser/xasl_generation.c`, `src/parser/xasl_regu_alloc.cpp`), serialization (`src/query/xasl_to_stream.c`), deserialization (`src/query/stream_to_xasl.c`, `src/xasl/xasl_unpack_info.hpp`, `src/xasl/xasl_unpack_info.cpp`), and execution/evaluation (`src/query/query_executor.c`, `src/query/scan_manager.c`, `src/query/fetch.c`, `src/query/expr_compile.c`, `src/query/query_evaluator.c`, `src/query/query_dump.c`) — along with shared node definitions in `src/xasl/` and `src/query/xasl.h`. See [src/xasl/AGENTS.md](xasl/AGENTS.md), [src/parser/AGENTS.md](parser/AGENTS.md), and [src/query/AGENTS.md](query/AGENTS.md).
  - Stream layout is also an on-disk format: filter-index predicates (`pred_stream`) and function-index expressions (`expr_stream`) are stored in the catalog and read back with `stx_map_stream_to_filter_pred ()` / `stx_map_stream_to_func_pred ()`.
  - XASL clones are reused across executions and sessions: `xcache_retire_clone ()` relies on `qexec_clear_xasl ()` to reset a clone, and parallel scan workers take clones from the same pool. Reset any per-execution state added to a node there.
  - `ER_QPROC_INVALID_XASLNODE` makes the client drop its `xasl_id` and recompile (`src/compat/db_vdb.c`); do not reuse it for unrelated "cannot happen" checks.
- **Storage & transaction changes**: verify page latches vs. transaction locks, latch ordering, WAL/LSAs, crash redo/undo recovery, MVCC visibility, and vacuum (`src/query/vacuum.c`, `src/query/vacuum_oos.cpp`) together. See [src/storage/AGENTS.md](storage/AGENTS.md) and [src/transaction/AGENTS.md](transaction/AGENTS.md).
  - Log and disk formats: add new `LOG_RCVINDEX` entries only at the end of the enum in `src/transaction/recovery.h` (the index is written into log records), and raise `disk_compatibility_level` in `src/base/release_string.c` when an on-disk layout changes.
- **Key coupling points** (workflow checklist, not an exhaustive include graph):
  - `src/base/` and `src/compat/` (`DB_VALUE`) underpin most targets.
  - `src/parser/` ↔ `src/optimizer/`: bidirectional compile/rewrite/planning; note `src/optimizer/histogram/` also builds in `SERVER_MODE`.
  - `src/object/`: shared schema/catalog/workspace headers used across parser, optimizer, query, xasl, method, and sp; `src/object/lob_locator.cpp` pairs with `src/storage/es.c`.
  - `src/query/` ↔ `src/storage/` ↔ `src/transaction/`: runtime and header coupling across scans, fetch, MVCC, WAL, recovery, and vacuum.
  - `src/sp/` ↔ `pl_engine/`: see [src/sp/AGENTS.md](sp/AGENTS.md) and [pl_engine/AGENTS.md](../pl_engine/AGENTS.md).

## Performance-Sensitive Paths

For scans, predicate evaluation, aggregation, or any per-row / per-key loop:

- State a performance claim with its measurement taken on a `release` build, and never compare numbers across build modes.
- Keep plan-fixed work out of the loop: avoid per-row re-tests of plan-time-constant conditions and unconditional out-of-line calls per row.
- A helper split out of a hot loop in `libcubrid` can stay out of line even when marked `inline`; define it `inline __attribute__ ((ALWAYS_INLINE))` (`src/base/porting_inline.hpp`) and confirm with `nm` that its symbol is gone.
- Worker threads accumulate statistics in their own buffer and merge once when they finish (`thread_p->m_px_stats` in `src/base/perf_monitor.h`); the shared per-transaction array is updated without atomics.
