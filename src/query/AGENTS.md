# src/query/ — Query Execution Engine

## OVERVIEW

Query executor, scan manager, hash join/scan, string operations, and parallel query framework. ~82 files at top level + `parallel/` subdir.

## KEY FILES

| File | Lines | Purpose |
|------|-------|---------|
| `query_executor.c` | 26k | Core executor: scan, join, aggregation |
| `string_opfunc.c` | 28k | String/comparison built-in functions |
| `execute_statement.c` | 21k | DML/DDL statement dispatch |
| `execute_schema.c` | 15k | DDL execution (CREATE, ALTER, etc.) |
| `scan_manager.c` | ~8k | Scan types: heap, index, list |
| `query_hash_scan.c` | | Hash scan implementation |
| `query_hash_join.c` | | Hash join implementation |
| `subquery_cache.c` | | Subquery result caching |
| `vacuum.c` | | MVCC garbage collection |

## PARALLEL FRAMEWORK (`parallel/`)

```
parallel/
├── px_heap_scan/     # Parallel heap scan (16 files)
├── px_hash_join/     # Parallel hash join (6 files)
├── px_query_execute/ # Parallel query dispatch (7 files)
└── 13 top-level files (coordinator, partitioner, etc.)
```

Owner: @shparkcubrid

## CODEOWNERS

- `query_executor.*`, `query_hash_scan.*`, `query_hash_join.*`, `scan_manager.*`, `subquery_cache.*`, `memoize.*`, `parallel/` → @shparkcubrid
- `vacuum.*` → @hornetmj
- Everything else → @beyondykk9
