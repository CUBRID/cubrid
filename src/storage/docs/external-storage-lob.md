# External Storage And LOB Backends

This reference covers the external storage API used by LOB-related paths. The storage module owns URI dispatch and backend
file operations; object-level LOB locator behavior lives in `src/object/lob_locator.cpp`.

## Files

| File | Role |
|------|------|
| `es.c/h` | Public external storage API and backend dispatch |
| `es_common.c/h` | URI type parsing, type strings, name hashing, unique number generation |
| `es_posix.c/h` | POSIX/local filesystem backend and local-file helpers |
| `es_owfs.c/h` | OWFS backend or stub implementation when OWFS is disabled |
| `es_list.h` | Small intrusive list helpers used by OWFS state |

## Mode Boundaries

External storage crosses server, standalone, and client builds.

- `es.c`, `es_common.c`, `es_posix.c`, `file_io.c`, `oid.c`, `storage_common.c`, and `tde.c` are in CS/SA/server lists.
- `es.c` includes `network_interface_cl.h` when not in `SERVER_MODE`; client paths can forward requests instead of
  calling server backends directly.
- `es_owfs.c` may compile as stubs returning `ER_ES_GENERAL` when `CUBRID_OWFS` is disabled or on unsupported platforms.

## URI Types

`es_common.c` maps URI prefixes to `ES_TYPE`:

- `ES_POSIX`
- `ES_OWFS`
- `ES_LOCAL`
- `ES_NONE`

Use `es_get_type()` and `es_get_type_string()` rather than open-coding prefix checks.

## Public API

Declared in `es.h`:

- `es_init()`, `es_final()`
- `es_create_file()`
- `es_write_file()`, `es_read_file()`
- `es_delete_file()`
- `es_copy_file()`, `es_copy_file_with_prefix()`
- `es_rename_file()`, `es_move_file_with_prefix()`
- `es_get_file_size()`

These functions dispatch by URI/backend and mode. Preserve URI buffer size assumptions: `ES_URI` is bounded by
`ES_MAX_URI_LEN`.

## POSIX Backend

Start at:

- `es_posix_init()`, `es_posix_final()`
- `xes_posix_create_file()`
- `xes_posix_write_file()`, `xes_posix_read_file()`
- `xes_posix_delete_file()`
- `xes_posix_copy_file()`, `xes_posix_copy_file_with_prefix()`
- `xes_posix_rename_file()`, `xes_posix_move_file_with_prefix()`
- `xes_posix_get_file_size()`
- `es_local_read_file()`, `es_local_get_file_size()`

POSIX paths use generated names, hashed directory components, absolute-path conversion, and platform-specific open flags.
Check path length and directory-creation error paths carefully.

## OWFS Backend

When enabled, start at:

- `es_owfs_init()`, `es_owfs_final()`
- `es_owfs_create_file()`
- `es_owfs_write_file()`, `es_owfs_read_file()`
- `es_owfs_delete_file()`
- `es_owfs_copy_file()`, `es_owfs_rename_file()`
- `es_owfs_get_file_size()`

OWFS maintains backend connection handles behind a mutex and parses path tokens into MDS/service/owner/file fields.

## Gotchas

- External storage is not purely server-side. Check `SERVER_MODE`, `SA_MODE`, and `CS_MODE` paths before changing API
  behavior.
- Local files (`ES_LOCAL`) are read-only helper paths in `es_posix.c`; do not assume they support full POSIX backend
  semantics.
- URI creation/copy/rename operations must keep locator strings valid for object-layer LOB code.
- OWFS disabled builds still compile the file and return errors through stubs; keep stubs in sync with declarations.
