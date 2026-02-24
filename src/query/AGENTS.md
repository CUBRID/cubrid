# src/query/ — XASL Execution & Scan Managers

82 code files. Server-side. Executes deserialized XASL plans.

## Key Files

| File | Lines | Role |
|------|-------|------|
| `query_executor.c` | 27K | Main executor: `qexec_execute_mainblock()` — XASL tree walker |
| `scan_manager.c` | 9K | Scan open/next/close for heap, index, list, set, method scans |
| `fetch.c` | — | Tuple fetching, REGU_VARIABLE evaluation |
| `query_manager.c` | — | Query cache, temp file management, result sets |
| `list_file.c` | — | Temp list files for intermediate results, sorting |
| `string_opfunc.c` | 28K | String function implementations (CONCAT, SUBSTR, etc.) |
| `arithmetic.c` | 7K | Numeric/date/time function implementations |
| `query_opfunc.c` | — | Aggregate functions (SUM, AVG, COUNT, etc.) |
| `crypt_opfunc.c` | — | Crypto functions (MD5, SHA, AES, etc.) |
| `numeric_opfunc.c` | — | NUMERIC/DECIMAL arbitrary-precision arithmetic |
| `xasl_to_stream.c` | — | Client-side: XASL → byte stream serialization |
| `stream_to_xasl.c` | — | Server-side: byte stream → XASL deserialization |
| `query_aggregate.hpp` | — | Aggregate accumulator classes (C++) |
| `query_analytic.cpp` | — | Window/analytic function execution (OVER clause) |
| `query_hash_scan.c` | — | Hash join scan implementation |
| `query_cl.c` | — | Client-side query API |
| `vacuum.c` | — | MVCC vacuum — garbage collection of old row versions |
| `xasl.h` | — | `XASL_NODE`, `PRED_EXPR`, `REGU_VARIABLE` definitions |

## Subdirectory
|-----|---------|
| `parallel/` | Parallel query execution framework (owner: @shparkcubrid) |

### parallel/ Structure

```
parallel/
├── px_parallel.hpp/cpp         # Core parallel execution framework
├── px_worker_manager.hpp/cpp    # Worker thread management
├── px_callable_task.hpp/cpp     # Callable task abstraction
├── px_thread_safe_queue.hpp     # Thread-safe queue for tasks
├── px_interrupt.hpp             # Interrupt handling
├── px_sort.c/h                  # Parallel sort
├── px_heap_scan/                # Parallel heap scan (8 files)
│   ├── px_heap_scan.hpp/cpp     # Main heap scan
│   ├── px_heap_scan_task.*      # Scan task units
│   ├── px_heap_scan_result_*    # Result handling
│   └── px_heap_scan_slot_*      # Slot iteration
├── px_hash_join/                # Parallel hash join (4 files)
│   ├── px_hash_join.hpp/cpp     # Main hash join
│   └── px_hash_join_*_manager.* # Task/spawn management
└── px_query_execute/            # Parallel query execution (4 files)
    ├── px_query_executor.*      # Query executor wrapper
    ├── px_query_task.*           # Query task units
    └── px_query_checker.*        # Execution checks
```

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

- `query_executor.c` is ~25K lines — use function index
- XASL serialization must match exactly between client and server — version mismatches cause crashes
- `REGU_VARIABLE` evaluation is recursive — deep expressions can stack overflow
- List file I/O can be major bottleneck — temp files hit disk for large results

## Owners

CODEOWNERS: @beyondykk9 (general), per-file overrides:
- `query_executor.*`, `query_hash_scan.*`, `query_hash_join.*`, `scan_manager.*`, `subquery_cache.*`, `memoize.*`, `parallel/` → @shparkcubrid
- `vacuum.*` → @hornetmj
