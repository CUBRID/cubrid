# src/storage/ - Buffer Pool, Heap, B-tree, Disk

Mostly server/standalone storage code. A small client subset is also compiled into `cubridcs`: `byte_order.c`,
`es.c`, `es_common.c`, `es_posix.c`, `file_io.c`, `oid.c`, `statistics_cl.c`, `storage_common.c`, and `tde.c`
(plus `es_owfs.c` on UNIX builds). Core managers such as B-tree, heap, page buffer, disk/file manager, slotted
pages, overflow files, catalog, and server statistics are `SERVER_MODE` / `SA_MODE` only.

For project-wide style, error handling, memory rules, build commands, and CI policy, inherit the root
[AGENTS.md](../../AGENTS.md) and [src/AGENTS.md](../AGENTS.md). This file only adds storage-specific navigation and
safety rules.

## Reference Docs

| Topic | Read This |
|-------|-----------|
| Shared identifiers, OID helpers, record descriptors | [docs/storage-foundations.md](docs/storage-foundations.md) |
| Page buffer, raw I/O, double-write buffer, TDE | [docs/buffer-io-durability.md](docs/buffer-io-durability.md) |
| Volumes, sectors, files, VFID lifecycle | [docs/disk-file-space.md](docs/disk-file-space.md) |
| Heap files, slotted pages, overflow records | [docs/heap-record-pages.md](docs/heap-record-pages.md) |
| B-tree indexes, range scans, bulk load | [docs/btree-indexes.md](docs/btree-indexes.md) |
| System catalog, catalog classes, statistics, compactdb | [docs/catalog-statistics-maintenance.md](docs/catalog-statistics-maintenance.md) |
| External storage / LOB backends | [docs/external-storage-lob.md](docs/external-storage-lob.md) |

## Key Files

| File | Role |
|------|------|
| `storage_common.h`, `oid.c/h`, `record_descriptor.cpp/hpp` | Core storage identifiers and record descriptors |
| `page_buffer.c/h` | Buffer pool: fix/unfix, latches, dirty pages, flushing (~17K lines) |
| `file_io.c/h`, `double_write_buffer.cpp/hpp`, `tde.c/h` | Raw volume I/O, torn-write protection, page/log encryption |
| `disk_manager.c/h`, `file_manager.c/h` | Volumes/sectors and logical file allocation |
| `heap_file.c/h` | Heap file object lifecycle, scans, MVCC versions, class representation cache (~27K lines) |
| `slotted_page.c/h`, `overflow_file.c/h` | Page slot layout and multipage record storage |
| `btree.c/h` | B+tree search, range scan, insert/delete, MVCC index records (~37K lines) |
| `btree_load.c/h`, `btree_unique.cpp/hpp`, `external_sort.c/h` | Bulk index build, unique stats, sort support |
| `system_catalog.c/h`, `catalog_class.c/h`, `statistics*.c/h` | Catalog persistence and optimizer statistics |
| `es*.c/h`, `es_list.h` | External storage URI API and POSIX/OWFS backends |

## Where To Look

| Task | Start Here |
|------|------------|
| Buffer pool pin/leak/latch issue | `page_buffer.c`: `pgbuf_fix*()`, `pgbuf_unfix*()`, `pgbuf_set_dirty*()` |
| Flush/WAL/page LSA issue | `page_buffer.c`, `file_io.h`, `double_write_buffer.cpp`, `tde.c` |
| Volume/sector space issue | `disk_manager.c`: `disk_reserve_sectors()`, `disk_check()` |
| Logical file allocation issue | `file_manager.c`: `file_create*()`, `file_alloc*()`, `file_dealloc()` |
| Heap scan visibility issue | `heap_file.c`: `heap_next()`, `heap_get_visible_version*()`, `heap_scancache_*()` |
| Heap insert/update/delete issue | `heap_file.c`: `heap_insert_logical()`, `heap_update_logical()`, `heap_delete_logical()` |
| Slotted page corruption | `slotted_page.c`: `spage_insert()`, `spage_update()`, `spage_delete()`, `spage_check()` |
| Overflow record issue | `overflow_file.c`, plus `heap_ovf_*()` in `heap_file.c` |
| Index lookup/range scan issue | `btree.c`: `btree_find_key()`, `btree_keyval_search()`, `btree_range_scan()` |
| Index insert/delete/MVCC issue | `btree.c`: `btree_insert()`, `btree_mvcc_delete()`, `btree_physical_delete()` |
| Bulk index build | `btree_load.c`, `external_sort.c` |
| Catalog/statistics inconsistency | `system_catalog.c`, `catalog_class.c`, `statistics_sr.c`, `statistics_cl.c` |
| External storage / LOB path issue | `es.c`, `es_posix.c`, `es_owfs.c`, `src/object/lob_locator.cpp` |

## Core Model

| Type | Meaning |
|------|---------|
| `VPID` | Physical page address: volume + page |
| `VFID` | Logical file identifier: volume + file |
| `HFID` | Heap file identifier: `VFID` + header page |
| `BTID` | B-tree identifier: `VFID` + root page |
| `OID` | Object address: volume + page + slot |
| `EHID` | Extendible hash identifier: `VFID` + directory/root page |
| `RECDES` | Record buffer descriptor used by heap, B-tree, catalog, and slotted page code |
| `PAGE_PTR` | Buffer-pool page pointer; access pages through `pgbuf_*`, not raw disk I/O |

## Buffer Pool Protocol

```c
page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
/* read or modify page */
pgbuf_unfix (thread_p, page);

page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
/* modify page */
pgbuf_set_dirty (thread_p, page, DONT_FREE);
pgbuf_unfix (thread_p, page);
```

- Every successful `pgbuf_fix*()` must be matched by `pgbuf_unfix*()` or `pgbuf_set_dirty(..., FREE)`.
- Mark modified pages dirty before unfixing.
- Use `pgbuf_unfix_and_init()` when the pointer should be nulled after release.
- Ordered heap/overflow page fixes use `PGBUF_WATCHER`; do not bypass watcher state for code that already uses it.

## Storage Rules

- All permanent page access goes through the buffer pool. `file_io.c` is the low-level volume I/O layer, not a shortcut
  for heap/B-tree/catalog page edits.
- Page latches protect physical consistency. Transaction locks protect logical consistency. Do not substitute one for
  the other.
- Respect latch ordering. Parent/ancestor pages are generally fixed before child pages; heap/overflow ordered fixes carry
  group/rank state through `PGBUF_WATCHER`.
- When changing on-disk descriptors or page layouts, check disk compatibility, recovery records, TDE flags, and
  check/dump code in the same subsystem.
- There is no `src/storage/CMakeLists.txt`; source ownership is in `cubrid/CMakeLists.txt`, `sa/CMakeLists.txt`, and
  `cs/CMakeLists.txt`.
- `.c` files are compiled as C++17. Do not introduce C++ exceptions in engine code.

## Gotchas

- `btree.c`, `heap_file.c`, and `page_buffer.c` are intentionally huge; use the reference docs and `rg` for function
  anchors instead of trying to read them top-to-bottom.
- The client target does not build core storage managers. If a header says `#error Belongs to server module`, keep that
  boundary.
- `statistics_cl.c` is client-side, `statistics_sr.c` is server-side, and standalone builds both.
- `es.c` crosses modes: client/SA paths may go through `network_interface_cl.h`, while server paths call backend
  functions directly.
- `LOG_LSA` is owned by transaction/WAL, but storage pages carry page LSA fields. Flush code must preserve the WAL rule.
