# Catalog, Statistics, And Maintenance

This reference covers storage-side catalog persistence, CATALOG class maintenance, optimizer statistics, and compactdb
entry points. Read it for bugs involving class representation metadata, class info records, statistics refresh/readback,
or storage maintenance utilities.

## Files

| File | Role |
|------|------|
| `system_catalog.c/h` | Persistent catalog for disk representations and class info |
| `catalog_class.c/h` | Maintains catalog class rows and server compatibility metadata |
| `statistics.h` | Shared statistics structures and client-facing declarations |
| `statistics_cl.c` | Client-side statistics fetch/unpack/dump helpers |
| `statistics_sr.c/h` | Server-side statistics collection, update, and class stats dump |
| `compactdb_sr.c` | Server-side compactdb/heap compaction support |

## Mode Boundaries

- `system_catalog.c`, `catalog_class.c`, `statistics_sr.c`, and `compactdb_sr.c` are server/SA code.
- `statistics_cl.c` builds in CS and SA.
- Standalone includes both client and server statistics code.

## System Catalog

The system catalog stores class representation metadata and class info used by heap/schema/statistics code.

Important entry points:

- `catalog_initialize()`, `catalog_finalize()`
- `catalog_create()`, `catalog_destroy()`
- `catalog_add_representation()`, `catalog_get_representation()`, `catalog_drop_old_representations()`
- `catalog_add_class_info()`, `catalog_get_class_info()`, `catalog_update_class_info()`
- `catalog_insert()`, `catalog_update()`, `catalog_delete()`
- `catalog_get_cardinality()`, `catalog_get_cardinality_by_name()`
- `catalog_check_consistency()`, `catalog_dump()`
- `catalog_rv_*()` recovery functions
- `catalog_start_access_with_dir_oid()`, `catalog_end_access_with_dir_oid()`

`CATALOG_ACCESS_INFO` tracks class OID, representation directory OID, lock/update state, and debug system-op state. Keep
start/end balanced on every error path.

## Catalog Class

`catalog_class.c` maintains catalog class records and compatibility metadata.

Start at:

- `catcls_compile_catalog_classes()`
- `catcls_insert_catalog_classes()`
- `catcls_delete_catalog_classes()`
- `catcls_update_catalog_classes()`
- `catcls_find_and_set_cached_class_oid()`
- `catcls_get_server_compat_info()`
- `catcls_get_db_collation()`
- `catcls_update_class_stats()`

Catalog-class changes often cross into `src/object/` schema code. Verify both storage persistence and object-layer
install/query specs when behavior changes.

## Statistics

Client-side:

- `stats_get_statistics()`
- `stats_client_unpack_statistics()`
- `stats_free_statistics()`
- `stats_dump()`
- `stats_ndv_dump()`
- `stats_get_ndv_by_query()`

Server-side:

- `stats_get_time_stamp()`
- `stats_dump_class_statistics()`
- `stats_update_partitioned_statistics()`
- B-tree statistics collection calls into `btree_get_stats()` and related B-tree paths.

Statistics structures combine class-level data, attribute NDV/cardinality, and B-tree statistics. If changing serialized
statistics buffers, update client unpacking and dump code together.

## Maintenance

`compactdb_sr.c` is a server-side maintenance entry for compacting heap data. It touches heap, B-tree, locking, and
catalog metadata through storage APIs rather than owning those formats directly.

## Gotchas

- Catalog metadata is persistent. Changes can require recovery, compatibility, checkdb, and dump updates.
- Class representation cache lives mostly in `heap_file.c`; catalog changes may need heap cache invalidation.
- Statistics are split by mode. A server-side format change can break client-side unpack/dump code.
- Catalog access helpers manage locks/system operations; do not return early without ending access.
