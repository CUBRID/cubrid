# src/query/ — XASL Execution & Scan Managers

Server-side. Executes deserialized XASL plans.

## Key Files

| File | Role |
|------|------|
| `query_executor.c` | Main executor: `qexec_execute_mainblock()` — XASL tree walker (~27K lines) |
| `scan_manager.c` | Scan open/next/close for heap, index, list, set, method scans |
| `fetch.c` | Tuple fetching, REGU_VARIABLE evaluation |
| `query_manager.c` | Query cache, temp file management, result sets |
| `list_file.c` | Temp list files for intermediate results, sorting |
| `string_opfunc.c` | String function implementations (CONCAT, SUBSTR, etc.) (~28K lines) |
| `xasl.h` | `XASL_NODE`, `REGU_VARIABLE` definitions; includes `src/xasl/xasl_predicate.hpp` for `PRED_EXPR` |
| `arithmetic.c` | Numeric/date/time function implementations |
| `query_opfunc.c` | Aggregate functions (SUM, AVG, COUNT, etc.) |
| `xasl_to_stream.c` | Client-side: XASL → byte stream serialization |
| `stream_to_xasl.c` | Server-side: byte stream → XASL deserialization |
| `query_analytic.cpp` | Window/analytic function execution (OVER clause) |
| `query_hash_scan.c` | Hash join scan implementation |
| `vacuum.c` | MVCC vacuum — garbage collection of old row versions |

## Subdirectory
| Dir | Purpose |
|-----|---------|
| `parallel/` | Parallel query execution: heap scan, hash join, query execute, sort. Core: `px_parallel.hpp`, `px_worker_manager.hpp`. |

## Where to Look

| Task | File |
|------|------|
| Fix query result | `query_executor.c` — trace from `qexec_execute_mainblock()` |
| Fix scan behavior | `scan_manager.c` — `scan_open_*()`, `scan_next_scan()` |
| Fix string function | `string_opfunc.c` |
| Fix numeric/date function | `arithmetic.c` |
| Fix aggregate function | `query_opfunc.c`, `query_aggregate.hpp` |
| Fix window function | `query_analytic.cpp` |
| Fix XASL serialization | `xasl_to_stream.c` / `stream_to_xasl.c` |
| Fix sort/orderby | `list_file.c` |
| Add new function | Implement in `string_opfunc.c` or `arithmetic.c` + wire in `fetch.c` |

## Execution Model

```
XASL_NODE tree (deserialized on server)
  → qexec_execute_mainblock()
    → For each XASL node type:
       BUILDLIST_PROC: Scan + materialize to list file
       BUILDVALUE_PROC: Single-row aggregation
       UNION_PROC/DIFFERENCE_PROC/INTERSECTION_PROC: Set operations
       SCAN_PROC: Open scan → fetch tuples → evaluate predicates
    → Scan types: heap scan, index scan, list scan, hash scan
    → Predicates evaluated via PRED_EXPR tree
    → Values fetched via REGU_VARIABLE evaluation in fetch.c
```

## Key Types

| Type | Purpose |
|------|---------|
| `XASL_NODE` | Executable plan node (SELECT, UPDATE, INSERT, etc.) |
| `PRED_EXPR` | Predicate expression tree (WHERE/HAVING conditions) |
| `REGU_VARIABLE` | Register variable — references columns, constants, expressions |
| `VAL_LIST` | Value list for current tuple |
| `QFILE_LIST_ID` | Temp list file handle for intermediate results |

## Conventions

- `THREAD_ENTRY *thread_p` first parameter on all functions
- Functions prefixed `qexec_` (executor), `scan_` (scan manager), `qfile_` (list files)
- Aggregate state tracked in `AGGREGATE_TYPE` linked list on XASL node
- All function implementations receive `DB_VALUE *` args, write result to `DB_VALUE *`

## Gotchas

- `query_executor.c` is ~27K lines — use function index
- XASL serialization must match exactly between client and server — version mismatches cause crashes
- `REGU_VARIABLE` evaluation is recursive — deep expressions can stack overflow
- List file I/O can be major bottleneck — temp files hit disk for large results
