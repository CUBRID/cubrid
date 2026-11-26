# XASL Changes

The checklist for changing an XASL node, its serialized layout, or how it executes.
Read this before you add, remove, or change a member of an XASL node, a regu variable, or a predicate.

## Four Pipeline Stages

Check all four stages together, along with shared node definitions in `src/xasl/` and `src/query/xasl.h`:

1. Generation: `src/parser/xasl_generation.c`, `src/parser/xasl_regu_alloc.cpp`, `src/optimizer/plan_generation.c`.
2. Serialization: `src/query/xasl_to_stream.c`.
3. Deserialization: `src/query/stream_to_xasl.c`, `src/xasl/xasl_unpack_info.hpp`, `src/xasl/xasl_unpack_info.cpp`.
4. Execution and evaluation: `src/query/query_executor.c`, `src/query/scan_manager.c`, `src/query/fetch.c`, `src/query/expr_compile.c`, `src/query/query_evaluator.c`, `src/query/query_dump.c`.

Module guidance: [src/xasl/AGENTS.md](../src/xasl/AGENTS.md), [src/parser/AGENTS.md](../src/parser/AGENTS.md), [src/optimizer/AGENTS.md](../src/optimizer/AGENTS.md), and [src/query/AGENTS.md](../src/query/AGENTS.md).

## Checklist

- Each `xts_sizeof_*` must count every byte that its matching `xts_process_*` writes (`src/query/xasl_to_stream.c`), and the `stx_*` reader (`src/query/stream_to_xasl.c`) must read the same fields in the same order. Only assert-enabled builds check the size (`assert (buf <= buf_p + size)` in `xts_save_*`), so run a query that uses the new member on `debug` or `optdebug`.
- The stream has no version: client and server must come from the same build. `SA_MODE` compiles both `xasl_to_stream.c` and `stream_to_xasl.c`, so standalone runs use the same pack/unpack path.
- Parallel hash join workers copy parts of a node through `cubxasl::spawner` (`src/xasl/xasl_spawner.cpp`, `SERVER_MODE` only). If workers read the new member (a pointer, regu variable, value list, or predicate), add it to the spawner copy.
- Stream layout is also an on-disk format: filter-index predicates (`pred_stream`) and function-index expressions (`expr_stream`) are stored in the catalog and read back with `stx_map_stream_to_filter_pred ()` / `stx_map_stream_to_func_pred ()`.
- XASL clones are reused across executions and sessions: `xcache_retire_clone ()` relies on `qexec_clear_xasl ()` to reset a clone, and parallel scan workers take clones from the same pool. Reset any per-execution state added to a node there.
- `ER_QPROC_INVALID_XASLNODE` makes the client drop its `xasl_id` and recompile (`src/compat/db_vdb.c`); do not reuse it for unrelated "cannot happen" checks.
