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
| `statistics_ndv.c` | Sample-based NDV estimator |
| `compactdb_sr.c` | Server-side compactdb/heap compaction support |

## Mode Boundaries

- `system_catalog.c`, `catalog_class.c`, `statistics_sr.c`, `statistics_ndv.c`, and `compactdb_sr.c` are server/SA code.
- `statistics_cl.c` builds in CS and SA.
- Standalone includes both client and server statistics code.

## System Catalog

The system catalog stores class representation metadata and class info used by heap/schema/statistics code.
It also caches representation locations and directory OIDs by class OID and representation ID.
`catalog_get_dir_oid_from_cache()` falls back to the class record. Preserve this cache's invalidation separately from
the heap class-representation cache; see [system_catalog.c](../system_catalog.c).

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
start/end cleanup balanced and pass the actual operation error to end access. Update access aborts its system operation
on error; success commits or attaches it to the outer operation according to mode and schema lock. Before restart or
with a null directory OID, start access returns without acquiring a lock.

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
- `stats_client_unpack_statistics()`, internal to `statistics_cl.c`
- `stats_free_statistics()`
- `stats_dump()`
- `stats_ndv_dump()`

Server-side:

- `stats_get_time_stamp()`
- `xstats_update_statistics()`
- `stats_dump_class_statistics()`, built only with `CUBRID_DEBUG`
- `stats_update_partitioned_statistics()`, internal to `statistics_sr.c`
- B-tree statistics collection calls into `btree_get_stats()` and related B-tree paths.

Server updates reuse supplied NDVs when they cover every supported column. Otherwise they call
`xstats_collect_ndv_by_fullscan_reservoir()` in
[histogram_sampler_sr.cpp](../../optimizer/histogram/histogram_sampler_sr.cpp).
`stats_estimate_ndv_from_sample()` in [statistics_ndv.c](../statistics_ndv.c) estimates NDV from a sample.
Parent partition statistics merge HLL sketches when available; the fallback sums partition NDV estimates and clamps
the result to the total row count. Distinct counts are not generally additive across partitions.

Statistics structures combine class-level data, attribute NDV/cardinality, and B-tree statistics. If changing serialized
statistics buffers, update client unpacking and dump code together.

## Maintenance

`boot_compact_db()` in [compactdb_sr.c](../compactdb_sr.c) processes locked instances, clears invisible OID references,
and rewrites outdated representations. When requested, it drops old catalog representations only after a complete
class pass with no failed objects, an unchanged representation ID, and a granted class X lock.
`boot_heap_compact_pages()` separately delegates heap page compaction.

## Gotchas

- Catalog metadata is persistent. Changes can require recovery, compatibility, checkdb, and dump updates.
- Class representation cache lives mostly in `heap_file.c`; catalog changes may need heap cache invalidation.
- Statistics are split by mode. A server-side format change can break client-side unpack/dump code.
- Catalog access helpers manage locks/system operations; do not return early without ending access.
