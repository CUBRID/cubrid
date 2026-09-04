# Disk, File, And Space Management

This reference covers physical volume/sector allocation and logical storage files. Read it for bugs involving free space,
volume extension, `VFID` lifecycle, sector reservation, file page allocation, or dropped/temp files.

## Files

| File | Role |
|------|------|
| `disk_manager.c/h` | Volume formatting, volume extension, sector reservation maps, volume header checks |
| `file_manager.c/h` | Logical file table, `VFID` lifecycle, file page allocation/deallocation |
| `extendible_hash.c/h` | Persistent extendible hash files used internally |
| `ftab_set.cpp/hpp` | Helper set for file-table operations |

## Layering

```
disk_manager: volumes and sectors
  -> file_manager: logical files and file-table pages
    -> heap/btree/catalog/ehash allocate pages by VFID
      -> page_buffer fixes the actual pages
```

Use the highest layer that matches the task. Subsystems should usually allocate through `file_alloc*()` and deallocate
through `file_dealloc()`, not reserve sectors directly.

## File Types

`FILE_TYPE` in `file_manager.h` classifies logical files. Common values:

- `FILE_HEAP`, `FILE_HEAP_REUSE_SLOTS`, `FILE_MULTIPAGE_OBJECT_HEAP`
- `FILE_BTREE`, `FILE_BTREE_OVERFLOW_KEY`
- `FILE_EXTENDIBLE_HASH`, `FILE_EXTENDIBLE_HASH_DIRECTORY`
- `FILE_CATALOG`, `FILE_DROPPED_FILES`, `FILE_VACUUM_DATA`
- `FILE_QUERY_AREA`, `FILE_TEMP`

If you add or reinterpret a file type, audit allocation, recovery, checkdb, dump, TDE, and disk compatibility.

## Important Entry Points

| Area | Entry Points |
|------|--------------|
| Disk startup/shutdown | `disk_manager_init()`, `disk_manager_final()` |
| Volume lifecycle | `disk_format_first_volume()`, `disk_add_volume_extension()`, `disk_unformat()` |
| Sector reservation | `disk_reserve_sectors()`, `disk_unreserve_ordered_sectors()`, `disk_check_sectors_are_reserved()` |
| Disk checking/reporting | `disk_check()`, `disk_dump_all()`, `disk_spacedb()` |
| File startup/shutdown | `file_manager_init()`, `file_manager_final()` |
| File creation | `file_create()`, `file_create_with_npages()`, `file_create_heap()`, `file_create_temp()` |
| Page allocation | `file_alloc()`, `file_alloc_multiple()`, `file_alloc_sticky_first_page()` |
| File/page destruction | `file_postpone_destroy()`, `file_destroy()`, `file_temp_retire()`, `file_dealloc()` |
| Mapping | `file_map_pages()`, `file_get_num_user_pages()`, `file_get_sticky_first_page()` |
| TDE metadata | `file_get_tde_algorithm()`, `file_apply_tde_algorithm()`, `file_rv_set_tde_algorithm()` |

## Descriptors

Logical file descriptors are packed into `FILE_DESCRIPTORS`, currently sized by `FILE_DESCRIPTORS_SIZE`.

- Heap files store class OID and `HFID`.
- B-tree files store class OID and attribute/index identity.
- Overflow B-tree key files store owning `BTID` and class OID.
- Vacuum data files store the first VPID.

The comment in `file_manager.h` is important: changing descriptor size is a disk compatibility change.

## Extendible Hash

`extendible_hash.c` provides persistent hash files through `EHID`. It allocates directory and bucket pages through the
file manager and logs updates through recovery functions declared in `extendible_hash.h`.

Start at:

- `ehash_search()`
- `ehash_insert()`
- `ehash_delete()`
- `ehash_map()`
- `ehash_rv_*()` recovery functions

## Gotchas

- Sector reservation and file allocation are separate concepts. A reserved sector is not automatically a file page.
- Temporary files have different lifetime paths (`file_temp_*`) from permanent files.
- Sticky first pages are used by callers that need a stable header/root page. Preserve that invariant.
- File deallocation interacts with page-buffer deallocation and recovery. Do not just drop file-table entries.
- Space accounting bugs often require checking both disk maps and file-table partial-sector state.
