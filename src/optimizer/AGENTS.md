# src/optimizer/ — Cost-Based Query Planning

Client-side (`#if !defined(SERVER_MODE)`).

## Key Files

| File | Role |
|------|------|
| `query_planner.c` | Core planner: generates QO_PLAN tree from QO_ENV |
| `query_graph.c` | Query graph construction (nodes = tables, edges = joins) |
| `plan_generation.c` | Plan enumeration and cost-based selection |
| `query_bitset.c` | Bitset operations for plan enumeration |

## Subdirectory

| Dir | Purpose |
|-----|---------|
| `rewriter/` | Query rewriting transformations (before optimization) |

## Where to Look

| Task | File |
|------|------|
| Fix join ordering | `query_planner.c`, `plan_generation.c` |
| Fix cost estimation | `plan_generation.c` |
| Fix query graph | `query_graph.c` |
| Fix query rewriting | `rewriter/` |
| Fix index selection | `query_planner.c` — index scan cost vs sequential scan |

## Pipeline Position

```
parser/ (PT_NODE tree) → optimizer/ (QO_PLAN tree) → parser/xasl_generation.c (XASL_NODE)
```

## Key Types

| Type | Purpose |
|------|---------|
| `QO_ENV` | Optimization environment — tables, predicates, statistics |
| `QO_PLAN` | Physical plan node — scan, join, sort operators |
| `QO_NODE` | Table/class reference in query graph |
| `QO_TERM` | Predicate term (join condition or filter) |
| `QO_INFO` | Cost/cardinality statistics |

## Conventions

- Functions prefixed `qo_` (query optimizer)
- Plan enumeration: bottom-up dynamic programming over join orderings
- Cost model considers: index selectivity, page I/O, sort cost
- Statistics from catalog: `qo_get_class_info()` fetches table/index stats

## Gotchas

- Optimizer runs **client-side** — statistics fetched from server, planning done locally
- Small module but high complexity — plan enumeration is exponential in join count
- Cost model accuracy depends on up-to-date statistics — stale stats = bad plans
- `query_planner.c` handles both simple queries and complex multi-way joins
