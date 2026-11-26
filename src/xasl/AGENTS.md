# src/xasl/ — XASL Node Type Definitions

Shared XASL plan types. The client generates and serializes them, and the server deserializes and executes them. The main node, `XASL_NODE`, is in `src/query/xasl.h`. `REGU_VARIABLE` is in `src/query/regu_var.hpp`.

## Key Files

| File | Role |
|------|------|
| `xasl_predicate.hpp` | `PRED_EXPR`, `COMP_EVAL_TERM` |
| `xasl_aggregate.hpp`, `xasl_analytic.hpp` | Aggregate and analytic nodes |
| `access_spec.hpp` | Access on tables, lists, and indexes |
| `xasl_sp.hpp` | Stored procedure call nodes |
| `xasl_stream.hpp`, `xasl_stream.cpp` | Stream constants and shared `stx_*` helpers |
| `xasl_unpack_info.hpp`, `xasl_unpack_info.cpp` | Deserialization context (buffers and allocation), not the per-node unpack code |
| `xasl_spawner.hpp`, `xasl_spawner.cpp` | Deep copies of XASL parts (regu variables, `outptr_list`, `val_list`, ...) for parallel hash join workers |
| `xasl_iteration.hpp`, `xasl_iteration.cpp` | `iterate_xasl_tree ()` / `iterate_regu_var ()` walkers used by parallel query and memoize |

## Where to Look

| Task | File |
|------|------|
| Predicate structure | `xasl_predicate.hpp` |
| Aggregate or analytic node | `xasl_aggregate.hpp`, `xasl_analytic.hpp` |
| Per-node serialization | `src/query/xasl_to_stream.c` (`xts_process_*` and the matching `xts_sizeof_*`) |
| Per-node deserialization | `src/query/stream_to_xasl.c` (`stx_build_*`, for example `stx_build_xasl_node ()`) |

## Rules

- Any change to a serialized member follows the checklist in [XASL Changes](#xasl-changes) below. It covers generation, `xts_process_*` / `xts_sizeof_*`, deserialization, execution, the catalog-stored filter and function-index streams, and the clone reset in `qexec_clear_xasl ()`.
- Mode guards: a serialized member must sit outside every mode guard, so its layout is the same in all modes. A server-only runtime member that is not serialized goes inside `#if defined (SERVER_MODE) || defined (SA_MODE)` (see the "not serialized" members in `src/query/xasl.h`). Keep the existing guards.
- If parallel hash join workers read a new pointer, regu, or predicate member, copy it in `xasl_spawner.cpp`. If the tree walkers must visit it, update `xasl_iteration.cpp`.

## XASL Changes

The checklist for changing an XASL node, its serialized layout, or how it executes.
Read this before you add, remove, or change a member of an XASL node, a regu variable, or a predicate.


### Four Pipeline Stages

Check all four stages together, along with shared node definitions in `src/xasl/` and `src/query/xasl.h`:

1. Generation: `src/parser/xasl_generation.c`, `src/parser/xasl_regu_alloc.cpp`, `src/optimizer/plan_generation.c`.
2. Serialization: `src/query/xasl_to_stream.c`.
3. Deserialization: `src/query/stream_to_xasl.c`, `src/xasl/xasl_unpack_info.hpp`, `src/xasl/xasl_unpack_info.cpp`.
4. Execution and evaluation: `src/query/query_executor.c`, `src/query/scan_manager.c`, `src/query/fetch.c`, `src/query/expr_compile.c`, `src/query/query_evaluator.c`, `src/query/query_dump.c`.

Module guidance: [src/parser/AGENTS.md](../parser/AGENTS.md), [src/optimizer/AGENTS.md](../optimizer/AGENTS.md), and [src/query/AGENTS.md](../query/AGENTS.md).

### Checklist

- Each `xts_sizeof_*` must count every byte that its matching `xts_process_*` writes (`src/query/xasl_to_stream.c`), and the `stx_*` reader (`src/query/stream_to_xasl.c`) must read the same fields in the same order. Only assert-enabled builds check the size (`assert (buf <= buf_p + size)` in `xts_save_*`), so run a query that uses the new member on `debug` or `optdebug`.
- The stream has no version: client and server must come from the same build. `SA_MODE` compiles both `xasl_to_stream.c` and `stream_to_xasl.c`, so standalone runs use the same pack/unpack path.
- Parallel hash join workers copy parts of a node through `cubxasl::spawner` (`src/xasl/xasl_spawner.cpp`, `SERVER_MODE` only). If workers read the new member (a pointer, regu variable, value list, or predicate), add it to the spawner copy.
- Stream layout is also an on-disk format: filter-index predicates (`pred_stream`) and function-index expressions (`expr_stream`) are stored in the catalog and read back with `stx_map_stream_to_filter_pred ()` / `stx_map_stream_to_func_pred ()`.
- XASL clones are reused across executions and sessions: `xcache_retire_clone ()` relies on `qexec_clear_xasl ()` to reset a clone, and parallel scan workers take clones from the same pool. Reset any per-execution state added to a node there.
- `ER_QPROC_INVALID_XASLNODE` makes the client drop its `xasl_id` and recompile (`src/compat/db_vdb.c`); do not reuse it for unrelated "cannot happen" checks.
