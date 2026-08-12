# B-tree Indexes

This reference covers B+tree search, range scan, mutation, MVCC index records, bulk load, unique statistics, and external
sort support. `btree.c` is one of the largest files in the engine; use anchors instead of reading it top-to-bottom.

## Files

| File | Role |
|------|------|
| `btree.c/h` | B+tree manager: search, range scan, insert/delete, uniqueness, MVCC index data, recovery |
| `btree_load.c/h` | Bulk index load and B-tree page/header initialization helpers |
| `btree_unique.cpp/hpp` | Unique/global unique statistics helpers |
| `external_sort.c/h` | External sort support used by bulk index build and other storage/query paths |

## Concepts

- `BTID` identifies an index file through a `VFID` plus root page.
- B-tree pages are slotted pages with B-tree-specific records.
- Leaf records store keys and one or more object identifiers. Overflow pages may hold long key/OID lists.
- MVCC index records carry enough MVCC data to choose visible objects during index scans and undo/vacuum operations.
- Unique indexes have special first-visible-object and delete-MVCCID rules; do not generalize non-unique behavior into
  unique paths without checking those helpers.

## Public Entry Points

| Area | Entry Points |
|------|--------------|
| Lookup | `btree_find_key()`, `btree_keyval_search()`, `btree_find_min_or_max_key()` |
| Range scan | `btree_prepare_bts()`, `btree_range_scan()`, `btree_range_scan_select_visible_oids()` |
| Scan info | `btree_get_next_key_info()`, `btree_get_next_node_info()`, `btree_index_*_scan()` |
| Insert/delete/update | `btree_insert()`, `btree_mvcc_delete()`, `btree_physical_delete()`, `btree_update()` |
| Vacuum/undo | `btree_vacuum_insert_mvccid()`, `btree_vacuum_object()`, `btree_undo_mvcc_delete()` |
| Creation | `btree_create_file()`, `btree_initialize_new_page()` |
| Check/dump | `btree_check_by_class_oid()`, `btree_check_all()`, `btree_dump()`, `btree_dump_capacity()` |
| Statistics | `btree_get_stats()`, `btree_get_unique_statistics()`, `btree_index_capacity()` |
| Online index | `btree_online_index_dispatcher()`, `btree_online_index_list_dispatcher()` |

## Internal Navigation Anchors

| Area | Anchors |
|------|---------|
| Root fixing | `btree_fix_root_with_info()`, `btree_fix_root_for_insert()`, `btree_fix_root_for_delete()` |
| Key location | `btree_locate_key()`, `btree_search_nonleaf_page()`, `btree_find_key_from_leaf()` |
| Leaf traversal | `btree_find_lower_bound_leaf()`, `btree_find_boundary_leaf()`, `btree_find_next_index_record()` |
| Split/merge | `btree_split_node()`, `btree_split_root()`, `btree_merge_node()`, `btree_merge_root()` |
| Insert internals | `btree_insert_internal()`, `btree_key_insert_new_key()`, `btree_key_append_object_*()` |
| Delete internals | `btree_delete_internal()`, `btree_key_delete_remove_object()`, `btree_key_remove_object()` |
| MVCC packing | `btree_pack_mvccinfo()`, `btree_unpack_mvccinfo()`, `btree_or_put_object()`, `btree_or_get_object()` |
| Visibility | `btree_record_satisfies_snapshot()`, `btree_select_visible_object_for_range_scan()` |
| Unique locking | `btree_key_find_and_lock_unique()`, `btree_check_locking_for_insert_unique()` |
| Overflow | `btree_create_overflow_key_file()`, `btree_load_overflow_key()`, `btree_get_next_overflow_vpid()` |
| Recovery | `btree_rv_*()` functions, especially keyval, node record, and page record variants |

## Range Scan Flow

```
scan manager opens index scan
  -> btree_prepare_bts()
  -> btree_range_scan()
    -> find boundary leaf
    -> read leaf records
    -> select visible OIDs / process objects
    -> advance to next/previous leaf or overflow
```

Range scan bugs usually involve one of: boundary comparison, leaf/overflow advancement, MVCC visibility, OID buffer
management, or latch release during restart/resume.

## Insert/Delete Flow

```
btree_insert() / btree_mvcc_delete()
  -> root fix and search
  -> leaf record inspection
  -> unique/non-unique specific object handling
  -> split/merge if needed
  -> page dirtying and recovery logging
```

For visible SQL changes, start at the public entry points. Internal helpers often assume a root/leaf latch, initialized
helper struct, current system operation, or prepared undo/redo buffers.

## Bulk Load And Sort

- `btree_load.c` builds pages from sorted input and initializes B-tree root/node/overflow headers.
- `external_sort.c` provides disk-backed sorting for large inputs.
- Bulk load paths have different latch/logging assumptions from row-by-row insert paths. Do not mix helpers casually.

## Gotchas

- Latches protect page structure; locks protect logical object/index conflicts. Unique paths use lock helpers in addition
  to B-tree latches.
- B-tree records use slotted-page operations. Page corruption fixes often require reading `slotted_page.c` too.
- MVCC index data must stay consistent with heap MVCC headers and vacuum behavior.
- Recovery functions are numerous and specialized. When changing a logged record format, update dump/check paths too.
- Online index states use MVCCID-like flag values; use the `btree_online_index_*` helpers rather than open-coding tests.
