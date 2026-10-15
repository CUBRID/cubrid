# Heap Records, Slotted Pages, And Overflow

This reference covers heap files, row versions, slotted page layout, and multipage overflow records. Read it before
changing heap scans, record insert/update/delete, MVCC visibility, class representation cache, or page slot handling.

## Files

| File | Role |
|------|------|
| `heap_file.c/h` | Heap file manager, object lifecycle, scans, MVCC versions, class representation/attribute helpers |
| `slotted_page.c/h` | Generic slotted-page record layout and slot operations |
| `overflow_file.c/h` | Multipage overflow records used for large heap records and other overflow payloads |

## Heap Concepts

- `HFID` identifies a heap file through a `VFID` plus heap header page.
- `OID` identifies a record by `volid`, `pageid`, and slot.
- Heap records are stored on slotted pages. Slot `0` is header/reserved in several page formats.
- Large records may be represented by overflow records. Heap-level wrappers are named `heap_ovf_*()`.
- MVCC heap records carry insert/delete MVCC metadata and may require previous-version lookup from log.

## Important Entry Points

| Area | Entry Points |
|------|--------------|
| Manager lifecycle | `heap_manager_initialize()`, `heap_manager_finalize()` |
| Address assignment | `heap_assign_address()` |
| Scan cache | `heap_scancache_start()`, `heap_scancache_start_modify()`, `heap_scancache_end*()` |
| Sequential scan | `heap_next()`, `heap_prev()`, `heap_first()`, `heap_last()`, `heap_next_1page()` |
| Range scan | `heap_scanrange_start()`, `heap_scanrange_next()`, `heap_scanrange_end()` |
| Logical mutation | `heap_create_insert_context()`, `heap_insert_logical()`, `heap_update_logical()`, `heap_delete_logical()` |
| Physical mutation | `heap_insert_physical()`, `heap_update_physical()`, `heap_delete_physical()` |
| MVCC visibility | `heap_get_visible_version()`, `heap_scan_get_visible_version()`, `heap_get_last_version()` |
| Vacuum | `heap_vacuum_all_objects()`, `heap_remove_page_on_vacuum()`, `heap_page_get_vacuum_status()` |
| Class representation | `heap_classrepr_get()`, `heap_classrepr_free()`, `heap_classrepr_decache()` |
| Attribute access | `heap_attrinfo_start()`, `heap_attrinfo_read_dbvalues()`, `heap_attrinfo_transform_to_disk()` |
| Check/dump | `heap_check_all_pages()`, `heap_check_heap_file()`, `heap_dump()` |

## Slotted Page Entry Points

| Area | Entry Points |
|------|--------------|
| Initialization | `spage_initialize()`, `spage_boot()`, `spage_finalize()` |
| Free space | `spage_get_free_space()`, `spage_max_space_for_new_record()`, `spage_need_compact()` |
| Insert/update/delete | `spage_insert()`, `spage_insert_at()`, `spage_update()`, `spage_delete()` |
| Partial record edits | `spage_split()`, `spage_append()`, `spage_take_out()`, `spage_put()`, `spage_overwrite()`, `spage_merge()` |
| Scan/get | `spage_next_record()`, `spage_previous_record()`, `spage_get_record()` |
| MVCC/reclaim | `spage_is_mvcc_updatable()`, `spage_vacuum_slot()`, `spage_reclaim()` |
| Validation | `spage_check()`, `spage_check_num_slots()`, `spage_check_slot_owner()` |

## Overflow Entry Points

- `overflow_insert()`
- `overflow_update()`
- `overflow_get()`
- `overflow_get_nbytes()`
- `overflow_get_length()`
- `overflow_get_capacity()`
- `overflow_flush()`
- `overflow_rv_*()` recovery functions

Heap wrappers:

- `heap_ovf_find_vfid()`
- `heap_ovf_insert()`
- `heap_ovf_update()`
- `heap_ovf_delete()`
- `heap_ovf_get()`
- `heap_ovf_get_capacity()`

## Mutation Flow

Use the logical context APIs when changing object lifecycle behavior:

```
heap_create_insert_context/update_context/delete_context
  -> heap_insert_logical / heap_update_logical / heap_delete_logical
    -> location and lock/page preparation
      -> physical slotted-page or overflow operation
      -> log/recovery data and header/free-space updates
```

Direct physical operations are for internal/recovery paths. They are usually not the right entry for SQL-visible object
changes.

## MVCC And Visibility

- Heap scan APIs use `HEAP_SCANCACHE` plus `MVCC_SNAPSHOT`.
- `heap_get_visible_version*()` may need to inspect previous versions from log.
- Update/delete paths must preserve MVCC header state and page vacuum status.
- B-tree MVCC data mirrors selected heap MVCC state; changes to heap MVCC record format can affect `btree.c`.

## Gotchas

- Error paths must unfix all pages and end scan caches.
- `RECDES` may point into a page when using `PEEK`; copy before mutating outside page context.
- Slotted-page free-space hints and heap best-space stats are performance-critical and correctness-adjacent.
- Overflow records are logged and recovered separately from the heap home record.
- Class representation and attribute helpers live in `heap_file.c`; heap is not only row storage.
