# B-tree Indexes

This reference covers B+tree search, range scan, mutation, MVCC index records, bulk load, unique statistics, and external
sort support. `btree.c` is one of the largest files in the engine; use anchors instead of reading it top-to-bottom.

## Files

| File | Role |
|------|------|
| `btree.c/h` | B+tree manager: search, range scan, insert/delete, uniqueness, MVCC index data, recovery |
| `btree_load.c/h` | Bulk index load and B-tree page/header initialization helpers |
| `btree_sort.c/h`, `btree_sort_merge.cpp`, `btree_sort_radix.cpp/hpp` | Dedicated index-build external sort, merge, and radix sort |
| `btree_lock.cpp/hpp` | Server-mode writer waits and transaction-end waits used by uniqueness checks |
| `btree_unique.cpp/hpp` | Unique/global unique statistics helpers |
| `external_sort.c/h` | General external sort for query and other callers; index builds use `btree_sort*` |

## Concepts

- `BTID` identifies an index file through a `VFID` plus root page.
- B-tree pages are slotted pages with B-tree-specific records.
- Leaf records store keys and one or more object identifiers. Long keys use an overflow-key file, separate from the
  overflow pages holding OID lists.
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
| Vacuum | `btree_vacuum_insert_mvccid()`, `btree_vacuum_object()` |
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
| MVCC delete stamping | `btree_key_find_and_insert_delete_mvccid()` through `btree_insert_internal()` |
| Delete internals | `btree_delete_internal()`, `btree_key_delete_remove_object()`, `btree_key_remove_object()` |
| MVCC packing | `btree_pack_mvccinfo()`, `btree_unpack_mvccinfo()`, `btree_or_put_object()`, `btree_or_get_object()` |
| Visibility | `btree_record_satisfies_snapshot()`, `btree_select_visible_object_for_range_scan()` |
| Unique locking | `btree_key_find_and_lock_unique()`, `btree_check_locking_for_insert_unique()` |
| Writer waits | `btree_key_lock_object()`, `btree_key_wait_out_conflicting_writer()`, `btree_key_wait_for_tran_end()` |
| Overflow | `btree_create_overflow_key_file()`, `btree_load_overflow_key()`, `btree_get_next_overflow_vpid()`, `btree_ovf_dir_find_oid()` |
| Recovery | `btree_rv_*()` functions and the static `btree_undo_mvcc_delete()` helper |

New non-unique OID overflow chains keep OIDs ordered and use a separator directory for two or more data pages. Unique
and legacy chains use legacy handling. Directory structural changes hold the leaf write latch and run in a system
operation with logged record changes. Read the `btree_ovf_dir_*` invariants in [btree.c](../btree.c).

## Range Scan Flow

```
scan manager opens index scan
  -> btree_prepare_bts()
  -> btree_range_scan()
    -> start at the bound, find an extreme leaf if unbounded, or resume
    -> read leaf records
    -> process eligible keys through the supplied callback
    -> advance to next/previous leaf or overflow
```

`btree_keyval_search()` supplies the visible-OID selection callback; `btree_range_scan()` also supports other key
callbacks. See `btree_range_scan_start()` and `btree_range_scan()` in [btree.c](../btree.c).

Range scan bugs usually involve one of: boundary comparison, leaf/overflow advancement, MVCC visibility, OID buffer
management, or latch release during restart/resume.

## Insert/Delete Flow

| Operation | Internal path |
|-----------|---------------|
| `btree_insert()` | `btree_insert_internal()` inserts a new object |
| `btree_mvcc_delete()` | `btree_insert_internal()` stamps a delete MVCCID on the existing object |
| `btree_physical_delete()` | Normally `btree_delete_internal()` removes the object; server serial-class deletion uses delete marking through the insert path |

Insert paths can split nodes during descent before searching the target leaf. Physical-delete paths can merge nodes
during descent. Leaf handling, recovery logging, and dirty marking follow each operation's protocol. The shared
traversal is `btree_search_key_and_apply_functions()` in [btree.c](../btree.c).

For visible SQL changes, start at the public entry points. Internal helpers often assume a root/leaf latch, initialized
helper struct, current system operation, or prepared undo/redo buffers.

## Bulk Load And Sort

- `btree_load.c` calls `btree_index_sort()`, which uses `btree_sort()` to feed leaf construction from sorted key/OID input.
  The loader also builds non-leaf pages and initializes root/node/overflow headers.
- [btree_sort.c](../btree_sort.c) owns the dedicated index sorter, with merge and radix implementations in the files
  above. `external_sort.c` owns the general sorter.
- Bulk load paths have different latch/logging assumptions from row-by-row insert paths. Do not mix helpers casually.
- In server mode, do not hold the build system operation's topop mutex while waiting for parallel sort workers; workers
  need it for their own system operations. Logged and parallel no-redo builds finish their system operations differently.
  Check `xbtree_load_index()` in [btree_load.c](../btree_load.c) before moving system-operation boundaries.

## Gotchas

- Latches protect page structure; locks protect logical object/index conflicts. Unique paths use lock helpers in addition
  to B-tree latches.
- Blocking object or writer waits release leaf/overflow pages before waiting and revalidate or restart afterward.
  The transaction-end waiting helpers are server-only; see [btree_lock.cpp](../btree_lock.cpp).
- B-tree records use slotted-page operations. Page corruption fixes often require reading `slotted_page.c` too.
- MVCC index data must stay consistent with heap MVCC headers and vacuum behavior.
- Recovery functions are numerous and specialized. When changing a logged record format, update dump/check paths too.
- Online index states use MVCCID-like flag values; use the `btree_online_index_*` helpers rather than open-coding tests.
