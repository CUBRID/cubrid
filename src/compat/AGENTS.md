# src/compat/ — Client API & DB_VALUE

Public API layer — bridges client and server sides.

## Key Files

| File | Role |
|------|------|
| `db_macro.c` | `db_make_*()` functions — construct DB_VALUE from C types |
| `db_value_printer.cpp` | DB_VALUE → string formatting |
| `db_set.c` | Set/multiset/sequence DB_VALUE operations |
| `db_obj.c` | Object-level API (find, create, put, get) |
| `db_admin.c` | Database admin API (create, start, shutdown) |
| `db_query.c` | Query execution API |
| `db_vdb.c` | Virtual database (view) support API |
| `db_date.c` | Date/time DB_VALUE operations |
| `db_json.cpp` | JSON DB_VALUE support |
| `db_elo.c` | LOB (External Large Object) API |
| `dbi_compat.h` | Client-visible error codes (mirrors `error_code.h`) |
| `dbtype_def.h` | `DB_VALUE` union definition, `DB_TYPE` enum |
| `dbtype_function.h` | DB_VALUE accessor/mutator declarations |
| `db_set_function.h` | Set operation function declarations |

## Where to Look

| Task | File |
|------|------|
| Add DB_VALUE constructor | `db_macro.c` — follow `db_make_*()` pattern |
| Fix type conversion | `db_macro.c`, `dbtype_def.h` |
| Fix date/time value | `db_date.c` |
| Fix JSON handling | `db_json.cpp` |
| Fix LOB API | `db_elo.c` |
| Fix set operations | `db_set.c`, `db_set_function.h` |
| Add client-visible error | `dbi_compat.h` (must mirror `error_code.h`) |

## DB_VALUE Structure

```c
/* Union-based value container — used EVERYWHERE */
typedef struct db_value DB_VALUE;
/* Key fields: */
/*   db_type: DB_INT, DB_STRING, DB_FLOAT, DB_DATE, DB_JSON, etc. */
/*   data: union of typed values */
/*   need_clear: whether data needs deallocation */
```

## DB_VALUE Patterns

```c
/* Construction */
db_make_int (&val, 42);
db_make_string (&val, "hello");
db_make_null (&val);

/* Access */
int i = db_get_int (&val);
const char *s = db_get_string (&val);
DB_TYPE t = DB_VALUE_DOMAIN_TYPE (&val);

/* Cleanup */
db_value_clear (&val);     /* Free internal data if need_clear is set */
pr_clear_value (&val);     /* Alias used in server-side code */
```

## Conventions

- Functions prefixed `db_` (public API), `pr_` (primitive, internal)
- `db_make_*()` for construction, `db_get_*()` for access
- Always `db_value_clear()` after done with non-trivial DB_VALUES (strings, sets, JSON)
- `dbi_compat.h` and `error_code.h` must stay in sync — add errors to both

## Gotchas

- `DB_VALUE` is used on both client and server — but access patterns differ
- `need_clear` flag: if set, `db_value_clear()` frees data; if not, data is borrowed
- Some `db_make_*()` copy data, others reference — check per type
- LOB values require special handling: external storage, not inline in DB_VALUE

