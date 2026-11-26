# src/optimizer/ — Cost-Based Query Planning

Builds client-side only (`CS_MODE`/`SA_MODE`; `optimizer.h` has `#error Does not belong to server module`), **except** `histogram/`. See "Build-Mode Boundaries" in [src/AGENTS.md](../AGENTS.md#build-mode-boundaries).

## Key Files

| File | Role |
|------|------|
| `query_graph.c` | `qo_optimize_query ()` entry; query graph (nodes, terms, segments); `qo_get_class_info ()` loads statistics |
| `query_planner.c` | Plan search (`qo_planner_search ()`, `qo_search_planner ()`, `qo_search_partition_join ()`), all `qo_*cost*` functions, index scan and join plan generation |
| `plan_generation.c` | `QO_PLAN` → XASL (`qo_to_xasl ()`, called from `src/parser/xasl_generation.c`) |
| `query_bitset.c` | Bitsets used by the planner |
| `optimizer.h` | `QO_ENV`, `QO_PLAN`, `QO_NODE`, `QO_TERM`, `QO_INFO` |

## Subdirectories

| Dir | Purpose | Builds in |
|-----|---------|-----------|
| `rewriter/` | Query rewrites before planning | `CS_MODE`, `SA_MODE` |
| `histogram/` | Column histograms: build, read, sample | `histogram_builder.cpp`, `histogram_reader.cpp`: all three modes. `histogram_sampler_sr.cpp`: `SERVER_MODE`, `SA_MODE`. `*_cl.cpp`: `CS_MODE`, `SA_MODE` |

Code in `histogram/` that `libcubrid` links must not use optimizer or parser headers (`optimizer.h` raises `#error` in `SERVER_MODE`). Check the owning `CMakeLists.txt` lists before you add an include.

## Where to Look

| Task | File |
|------|------|
| Join order, plan search | `query_planner.c` |
| Cost estimation, index vs. sequential scan | `query_planner.c` (`qo_*cost*`) |
| Query graph, statistics lookup | `query_graph.c` |
| Plan → XASL | `plan_generation.c` |
| Query rewriting | `rewriter/` |
| Histograms | `histogram/` |

## Pipeline Position

```
parser/view_transform.c → mq_rewrite () (rewriter/) → PT_NODE
  → parser/xasl_generation.c → qo_optimize_query () (query_graph.c) → query_planner.c (QO_PLAN)
  → qo_to_xasl () (plan_generation.c) → XASL_NODE
```

## Gotchas

- Statistics come from the server through the catalog. Planning runs on the client, so stale statistics give bad plans.
- Plan search cost grows quickly with the number of joined tables.
