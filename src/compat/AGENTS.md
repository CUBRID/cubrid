# src/compat/ — Client API & DB_VALUE

The public `db_*` client API and the `DB_VALUE` container. Mode membership: see "Build-Mode Boundaries" in [src/AGENTS.md](../AGENTS.md).

## Key Files

| File | Role |
|------|------|
| `dbtype_def.h` | `struct db_value`, the `DB_TYPE` enum (`DB_TYPE_INTEGER`, `DB_TYPE_STRING`, ...), and per-type value typedefs (`DB_DATE`, `DB_NUMERIC`, `DB_JSON`, ...) |
| `dbtype_function.h` | Public declarations of `db_make_*` / `db_get_*` and the other value functions |
| `dbtype_function.i` | Definitions of `db_make_*` / `db_get_*`: inlined where `dbtype.h` includes the file; `dbtype_function.c` compiles the exported copies |
| `db_macro.c` | `db_value_domain_init`, `db_value_put`, `db_value_clear`, and the `db_value_coerce` wrapper |
| `db_vdb.c` | SQL statement API: `db_open_buffer`, `db_compile_statement`, `db_execute_statement`, and the recompile after `ER_QPROC_INVALID_XASLNODE` |
| `db_value_printer.cpp` | `DB_VALUE` → string formatting |
| `db_set.c` | Set/multiset/sequence operations |
| `db_date.c`, `db_json.cpp`, `db_elo.c` | Date/time, JSON, and LOB values |
| `dbi_compat.h` | Public client header (installed as `dbi.h`). It includes `error_code.h` and defines no `ER_*` macros |

## Where to Look

| Task | File |
|------|------|
| Add a `db_make_*` / `db_get_*` function | `dbtype_function.i`: a `STATIC_INLINE ... __attribute__ ((ALWAYS_INLINE))` declaration in the `#if !defined (_NO_INLINE_DBTYPE_FUNCTION_)` block and the definition below it; plus an `extern` declaration in `dbtype_function.h`. Follow a neighbouring function |
| Fix type conversion, CAST, or coercion | `src/object/object_domain.c` (`tp_value_coerce`, `tp_value_cast`, `tp_value_auto_cast`) and `src/object/object_domain_convert.cpp` |
| Fix the client compile/execute path | `db_vdb.c` |
| Add an error code | Only `src/base/error_code.h` and the message catalogs. Follow "Adding a new error code" in [src/AGENTS.md](../AGENTS.md) |

## DB_VALUE

```c
struct db_value { DB_DOMAIN_INFO domain; DB_DATA data; need_clear_type need_clear; };

db_make_int (&val, 42);
DB_TYPE t = DB_VALUE_DOMAIN_TYPE (&val);   /* compare with DB_TYPE_INTEGER, not DB_INT or DB_DATE */
int i = db_get_int (&val);
db_value_clear (&val);                     /* calls pr_clear_value () */
```

- `need_clear` set: `db_value_clear ()` frees `data`. Not set: the data is borrowed.
- Constructors differ in ownership: `db_make_string ()` borrows the caller's buffer (`need_clear` false), `db_make_string_copy ()` copies it, and `db_make_json ()` takes an explicit `need_clear` argument. Check the constructor before you free the source.
- Clear every value that may own memory (strings, sets, JSON, LOB locators) on all paths, including error returns.
