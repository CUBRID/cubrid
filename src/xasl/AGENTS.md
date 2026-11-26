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

- Any change to a serialized member follows the "XASL changes" checklist in [src/AGENTS.md](../AGENTS.md). It covers generation, `xts_process_*` / `xts_sizeof_*`, deserialization, execution, the catalog-stored filter and function-index streams, and the clone reset in `qexec_clear_xasl ()`.
- Mode guards: a serialized member must sit outside every mode guard, so its layout is the same in all modes. A server-only runtime member that is not serialized goes inside `#if defined (SERVER_MODE) || defined (SA_MODE)` (see the "not serialized" members in `src/query/xasl.h`). Keep the existing guards.
- If parallel hash join workers read a new pointer, regu, or predicate member, copy it in `xasl_spawner.cpp`. If the tree walkers must visit it, update `xasl_iteration.cpp`.
