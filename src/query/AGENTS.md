# src/query/ — XASL Execution & Scan Managers

Executes deserialized XASL plans. Most files build in `SERVER_MODE`/`SA_MODE`, but a client subset also builds in `CS_MODE`: see `QUERY_SOURCES` in `cs/CMakeLists.txt` (for example `execute_statement.c`, `execute_schema.c`, `arithmetic.c`, `string_opfunc.c`, `expr_compile.c`, `domain_rules.c`, `query_dump.c`, `regu_var.cpp`, `xasl_to_stream.c`). Before you add a server-only call or include to one of these files, check its guards. General mode rules: "Build-Mode Boundaries" in [src/AGENTS.md](../AGENTS.md#build-mode-boundaries).

## Key Files

| File | Role |
|------|------|
| `query_executor.c` | Executor: `qexec_execute_mainblock ()` walks the XASL tree; `qexec_clear_xasl ()` resets a node for reuse |
| `scan_manager.c` | `scan_open_*` / `scan_next_scan ()` for heap, index, list, set, method, and other scans |
| `fetch.c` | `REGU_VARIABLE` evaluation; `fetch_peek_arith ()` fetches operands, then calls the opcode body |
| `fetch_arith_ops.c` | One body function per arithmetic opcode, selected by `fetch_arith_op ()` |
| `expr_compile.c` | Compiles regu variable lists into step programs; must give the same results as the interpreted path |
| `query_evaluator.c` | `PRED_EXPR` evaluation |
| `query_aggregate.cpp` | Aggregate functions (`qdata_initialize_aggregate_list`, `qdata_evaluate_aggregate_list`, ...) |
| `query_analytic.cpp` | Window/analytic functions |
| `query_opfunc.c` | `qdata_*` value and arithmetic helpers |
| `string_opfunc.c`, `arithmetic.c` | String, numeric, and date/time function implementations |
| `query_hash_join.c` | Hash join (parallel version in `parallel/px_hash_join/`) |
| `query_hash_scan.c` | Hash list scan (key lookup in list files), not hash join |
| `list_file.c` | Temp list files, `qfile_sort_list ()` |
| `query_manager.c` | Server-side query entries, temp files, and results (`xqmgr_*`). The XASL cache is `xasl_cache.c` |
| `xasl.h` | `XASL_NODE` and the proc structs |
| `regu_var.hpp` | `REGU_VARIABLE` definition |
| `xasl_to_stream.c` / `stream_to_xasl.c` | XASL serialization (client side) and deserialization (server side) |
| `execute_statement.c` | Client-side statement execution (DDL, DML, serial) |
| `vacuum.c` | MVCC vacuum |
| `parallel/` | Parallel heap scan, hash join, merge join, query execute, and sort |

## Where to Look

| Task | File |
|------|------|
| Wrong query result | `query_executor.c`, from `qexec_execute_mainblock ()` |
| Scan behavior | `scan_manager.c` |
| Aggregate function | `query_aggregate.cpp` |
| Window function | `query_analytic.cpp` |
| Hash join | `query_hash_join.c`, `parallel/px_hash_join/` |
| Sort / ORDER BY | `list_file.c` |
| XASL layout or serialization | Follow [XASL Changes](../xasl/AGENTS.md#xasl-changes) in `src/xasl/AGENTS.md` |

## Adding an arithmetic opcode (new SQL function)

An opcode that has no body in `fetch_arith_op ()` silently keeps the cleared value (NULL) and returns no error. Touch every point:

1. Opcode value: append the new `T_*` at the end of `OPERATOR_TYPE` in `src/storage/storage_common.h`; never insert it in the middle. The XASL stream stores the ordinal (`xts_process_arith_type ()`), and the function-index and filter-index streams in the catalog keep it, so renumbering breaks existing indexes. Parser side (`PT_*` op, grammar or `keyword.c`, `type_checking.c`, and the `PT_*` → `T_*` mapping in `pt_to_regu_variable ()` in `xasl_generation.c`): see [src/parser/AGENTS.md](../parser/AGENTS.md).
2. Implementation: `string_opfunc.c` or `arithmetic.c`.
3. `fetch.c`: add the opcode to the operand fetch switch in `fetch_peek_arith ()`.
4. `fetch_arith_ops.c`: add a body function and its case in `fetch_arith_op ()`.
5. `expr_compile.c`: add the opcode to `expr_generic_operand_spec ()` with the same operand fetch rule as step 3. If you leave it out, the node stays interpreted.
6. `domain_rules.c`: check its opcode switches. For example, `domain_character_rule ()` must list every operator that returns one of its operands.
7. `query_dump.c`: the opcode name.
8. `regu_var.hpp`: if the result can change while the operands stay the same (a random value, a serial, a session variable, a statistic, or `CONNECT BY` state), add the opcode to `fetch_opcode_caches ()` so that it returns false. Otherwise a node whose operands are all constant (or absent) is flagged `REGU_VARIABLE_FETCH_ALL_CONST` after its first evaluation, the expression compiler computes it once per execution, and every row gets the first value.

## Conventions

- Functions that may touch per-thread state (allocation, `er_set`, page fixes) take `THREAD_ENTRY *thread_p` first. Pure helpers do not.
- Prefixes: `qexec_` (executor), `scan_` (scan manager), `qfile_` (list files), `qdata_` (value helpers).

## Gotchas

- `query_executor.c`, `fetch.c`, and `string_opfunc.c` are very large. Use symbol search.
- `REGU_VARIABLE` evaluation is recursive. The depth guard is `PRM_ID_MAX_RECURSION_SQL_DEPTH` in `fetch.c`.
- A large intermediate result goes to temp files on disk.
