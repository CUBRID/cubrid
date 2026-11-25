# src/ — CUBRID Engine Source

Follow [the root AGENTS.md](../AGENTS.md) for repository-wide build, CI, and reporting rules. Consult the target module's `AGENTS.md` (and `src/storage/docs/` when working in `src/storage/`) before changing subsystem behavior.

**Compilation language**: most `.c` sources in engine targets compile as C++17 (`-std=c++17` in `CMakeLists.txt`) via per-directory `set_source_files_properties(... PROPERTIES LANGUAGE CXX)` in CMake (a few exceptions such as `cm_broker_jni.c` and `gencat.c` compile as C). Note that `c_to_cpp.sh` only renames `.c` files to `.cpp` with `git mv`; always check every owning target `CMakeLists.txt` for actual language settings.

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
2. Add the file (and tracked headers where listed) to each owning target's `CMakeLists.txt`, ensuring `PROPERTIES LANGUAGE CXX` is set in every owning directory for C++-compiled `.c` files.
3. If the file is listed in `cubrid/CMakeLists.txt`, satisfy the `memory_wrapper.hpp` include check below.
4. Build every affected target (`libcubrid`/`cub_server`, `libcubridsa`, `libcubridcs`, utilities, broker, or unit tests).

## Resource, Memory, and Error Handling

- **Allocator/deallocator pairing** (`src/base/memory_alloc.h`, `src/parser/parser.h`):
  - `db_private_alloc (thread_p, size)` ↔ `db_private_free_and_init (thread_p, ptr)`
  - `malloc`/`calloc`/`realloc` ↔ `free_and_init (ptr)`
  - `os_malloc`/`os_calloc` ↔ `os_free_and_init (ptr)`
  - `parser_alloc (parser, len)` → parser-context arena lifetime (never freed manually)
  - Verify ownership and cleanup on every error and early-return path. Existing C++ RAII helpers (`src/base/scope_exit.hpp`, `src/base/mem_block.hpp`, `std::unique_ptr`, `std::lock_guard`) are used where appropriate; follow the surrounding module's conventions.
  - `db_private_alloc` / `db_private_free` use the private heap of the passed `thread_p` argument in `SERVER_MODE` (`db_private_get_heapid_from_thread ()`, asserting `size > 0`): always pass the current thread's own `thread_p` (or `NULL`), never another thread's entry, and free on the same thread. Allocate cross-thread buffers (parallel worker ↔ requester) with `malloc`/`free_and_init` (never cross-free with `db_private_free`). In `CS_MODE`, `db_private_alloc` delegates to `db_ws_alloc`; in `SA_MODE`, the private heap is process-wide.
  - `PEEK` results are borrowed, not owned: a peeked `RECDES` points into the fixed page (see `src/storage/docs/heap-record-pages.md`), a peeked `QFILE_TUPLE_RECORD` points into the current page (or scan-owned `scan_id->tplrec` for overflow tuples) with `size == 0` (`qfile_retrieve_tuple ()`), and `fetch_peek_dbval ()` returns a non-owned value. Never free or realloc a peeked pointer; use `COPY` / `fetch_copy_dbval ()` when data must outlive the page fix or the next fetch.
- **Error-code interfaces vs. throwing STL/C++ operations**:
  - Engine interfaces use negative error codes in `src/base/error_code.h` (`NO_ERROR = 0`) and report errors with `er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CODE, ...)`. Do not let C++ exceptions escape across C-style engine interfaces.
  - Prefer `.cpp` files for new STL-heavy code. Whenever calling throwing STL operations (in `.cpp` or C++-compiled `.c`), catch exceptions at or before the C-interface boundary and translate them via `er_set (...)` and an error-code return (see `src/storage/heap_file.c`, `src/optimizer/histogram/histogram_sampler_sr.cpp`, and `src/query/string_regex_std.cpp`).
  - The error context is per thread (`cuberr::context`): an `er_set` on a worker thread is not visible to the thread answering the client. In parallel query workers (`src/query/parallel/`), propagate errors explicitly via `parallel_query::err_messages_with_lock::move_top_error_message_to_this ()` (`src/query/parallel/px_interrupt.hpp`), and swap them into the requester's context after `wait_workers ()`.
- **Adding a new error code**: update `src/base/error_code.h` (definition and `ER_LAST_ERROR`) and both `msg/en_US.utf8/cubrid.msg` and `msg/ko_KR.utf8/cubrid.msg` under `$set 5 MSGCAT_SET_ERROR` using positive message ID `-ER_CODE` (`src/compat/dbi_compat.h` already includes `error_code.h` and defines no `ER_*` macros). If a client-facing error must also be exposed in CCI, open a separate PR in `CUBRID/cubrid-cci` for `src/cci/base_error_code.h` and bump the submodule separately—do not modify files inside `cubrid-cci/` in an engine PR.

## Formatting, Includes, and Static Checks

- **Formatters** (`.github/workflows/codestyle.sh`): `.c`/`.h`/`.i` use GNU `indent -l120 -lc120` (CI pins GNU `indent` 2.2.11—2.2.12+ produces different output—and runs `codestyle.sh` across each entire modified file; check `indent --version` locally to avoid unrelated diffs); `.cpp`/`.hpp`/`.ipp` use `astyle` (GNU style, 2-space indent, 120-char line limit).
- **Protecting C++ syntax in `.c`/`.h` files**: GNU `indent` misformats C++ syntax (`try`/`catch`, templates, lambdas). Wrap C++-only blocks inside legacy `.c`/`.h` files with:

  ```c
  /* *INDENT-OFF* */
  /* C++ syntax code */
  /* *INDENT-ON* */
  ```

- **`memory_wrapper.hpp` include order** (`.github/workflows/check.yml`, `memory-monitor-check` job):
  - Applies to `.c`/`.cpp` files whose basename appears in `cubrid/CMakeLists.txt`: `src/base/memory_monitor_sr.cpp`, `src/base/memory_monitor_api.cpp`, and `src/base/memory_wrapper.cpp` are exempt, and `src/heaplayers/lea_heap.c` is exempt from the position and comment checks.
  - Place `#include "memory_wrapper.hpp"` as the last `^#include` line (including conditional includes inside `#if` blocks), with exact comment `// XXX: SHOULD BE THE LAST INCLUDE HEADER` (no trailing space) on the immediately preceding line.
  - **Checked exception**: `src/debugging/strict_warnings_on.hpp` and `src/debugging/strict_warnings_off.hpp` may follow `memory_wrapper.hpp`.
  - Keep `memory_wrapper.hpp` out of header files (`.h`/`.hpp`); use `src/base/memory_cwrapper.h` in headers when allocation wrappers are needed.
- **Conventions & static analysis**: in new `.c` files, include `"config.h"` first and use `#ifndef _FILENAME_H_` / `_FILENAME_HPP_` header guards matching neighboring files (do not retrofit untouched legacy files); avoid inline `cppcheck` suppressions without justification in the PR.

## Cross-Module Impact Checks

- **XASL changes**: check all four pipeline stages together — generation (`src/parser/xasl_generation.c`, `src/parser/xasl_regu_alloc.cpp`), serialization (`src/query/xasl_to_stream.c`), deserialization (`src/query/stream_to_xasl.c`, `src/xasl/xasl_unpack_info.hpp`, `src/xasl/xasl_unpack_info.cpp`), and execution/evaluation (`src/query/query_executor.c`, `src/query/scan_manager.c`, `src/query/fetch.c`, `src/query/expr_compile.c`, `src/query/query_evaluator.c`, `src/query/query_dump.c`) — along with shared node definitions in `src/xasl/` and `src/query/xasl.h`. See [src/xasl/AGENTS.md](xasl/AGENTS.md), [src/parser/AGENTS.md](parser/AGENTS.md), and [src/query/AGENTS.md](query/AGENTS.md).
  - Stream layout is also an on-disk format: filter-index predicates (`pred_stream`) and function-index expressions (`expr_stream`) are stored in the catalog and read back with `stx_map_stream_to_filter_pred ()` / `stx_map_stream_to_func_pred ()`.
  - XASL clones are reused across executions and sessions: `xcache_retire_clone ()` relies on `qexec_clear_xasl ()` to reset a clone, and parallel scan workers take clones from the same pool. Reset any per-execution state added to a node there.
  - `ER_QPROC_INVALID_XASLNODE` makes the client drop its `xasl_id` and recompile (`src/compat/db_vdb.c`); do not reuse it for unrelated "cannot happen" checks.
- **Storage & transaction changes**: verify page latches vs. transaction locks, latch ordering, WAL/LSAs, crash redo/undo recovery, MVCC visibility, and vacuum (`src/query/vacuum.c`, `src/query/vacuum_oos.cpp`) together. See [src/storage/AGENTS.md](storage/AGENTS.md) and [src/transaction/AGENTS.md](transaction/AGENTS.md).
  - Log and disk formats: when adding a recovery record, append to `LOG_RCVINDEX` at the end of the enum and update `RV_LAST_LOGID` (`src/transaction/recovery.h`), insert the matching positional entry in `RV_fun[]` (`src/transaction/recovery.c`; ordering is asserted only in `!NDEBUG` builds via `rv_check_rvfuns ()`), and update `RCV_IS_*` classification macros if applicable. Never bump `disk_compatibility_level` (`src/base/release_string.c`) in a feature PR—it is a release-level version bumped once per incompatible release with release/upgrade coordination; instead, verify compatibility and flag breaking on-disk changes to release owners.
- **Key coupling points** (workflow checklist, not an exhaustive include graph):
  - `src/base/` and `src/compat/` (`DB_VALUE`) underpin most targets.
  - `src/parser/` ↔ `src/optimizer/`: bidirectional compile/rewrite/planning; note `src/optimizer/histogram/` also builds in `SERVER_MODE`.
  - `src/object/`: shared schema/catalog/workspace headers used across parser, optimizer, query, xasl, method, and sp; `src/object/lob_locator.cpp` pairs with `src/storage/es.c`.
  - `src/query/` ↔ `src/storage/` ↔ `src/transaction/`: runtime and header coupling across scans, fetch, MVCC, WAL, recovery, and vacuum.
  - `src/sp/` ↔ `pl_engine/`: see [src/sp/AGENTS.md](sp/AGENTS.md) and [pl_engine/AGENTS.md](../pl_engine/AGENTS.md).

## Performance-Sensitive Paths

For scans, predicate evaluation, aggregation, or any per-row / per-key loop:

- State a performance claim with its measurement taken on a `release` build, and never compare numbers across build modes.
- Keep plan-fixed work out of the loop: avoid per-row re-tests of plan-time-constant conditions, and avoid unnecessary per-row out-of-line calls when a `release` profile shows overhead.
- Only when a `release` (`-DNDEBUG`) profile shows missed inlining on a small hot-path helper in `libcubrid`, declare it `STATIC_INLINE ... __attribute__ ((ALWAYS_INLINE))` (`src/base/porting_inline.hpp` — expands only under `__GNUC__ && NDEBUG`, empty in `debug`/`optdebug`) and verify with `nm -C` (not `-D`) on the `release` `libcubrid.so`. Do not force-inline cold or large helpers.
- Parallel worker threads accumulate statistics in their own buffer when `m_uses_px_stats` and `m_px_orig_thread_entry` are set (`thread_p->m_px_stats` in `src/base/perf_monitor.h`) and merge once on finish; the shared per-transaction array is updated without atomics.
