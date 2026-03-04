# src/storage/ — Buffer Pool, Heap, B-tree, Disk

Server-side (`SERVER_MODE` / `SA_MODE`).

## Key Files

| File | Lines | Role |
|------|-------|------|
| `page_buffer.c` | 17K | Buffer pool: LRU replacement, fix/unfix, dirty tracking, flush |
| `heap_file.c` | 27K | Heap file manager: record insert/update/delete/scan |
| `btree.c` | 37K | B-tree index: find, range search, insert, delete, split/merge |
| `btree_load.c` | — | B-tree bulk loading (sorted insert) |
| `file_manager.c` | — | File/tablespace management, page allocation |
| `disk_manager.c` | — | Disk sector/page allocation, volume management |
| `overflow_file.c` | — | Overflow pages for large records |
| `extendible_hash.c` | — | Extendible hashing (used internally) |
| `slotted_page.c` | — | Slotted page format: slot directory + records |
| `storage_common.h` | — | `VPID`, `OID`, `HFID`, `BTID` — core identifiers |
| `oid.c/h` | — | Object identifier operations |
| `es.c/h` | — | External storage (LOB file storage backend: POSIX, OWFS) |

## Where to Look

| Task | File |
|------|------|
| Fix buffer pool bug | `page_buffer.c` — `pgbuf_fix()`, `pgbuf_unfix()` |
| Fix heap scan | `heap_file.c` — `heap_next()`, `heap_get_record()` |
| Fix index scan | `btree.c` — `btree_find()`, `btree_range_search()` |
| Fix page corruption | `slotted_page.c` — slot directory logic |
| Fix disk space issue | `disk_manager.c`, `file_manager.c` |
| Fix overflow record | `overflow_file.c` |

## Core Identifiers

| Type | Fields | Purpose |
|------|--------|---------|
| `VPID` | `volid`, `pageid` | Volume + page address |
| `OID` | `volid`, `pageid`, `slotid` | Object identifier (row address) |
| `HFID` | `vfid`, `hpgid` | Heap file identifier |
| `BTID` | `vfid`, `root_pageid` | B-tree identifier |
| `LOG_LSN` | `pageid`, `offset` | Log sequence number |

## Buffer Pool Protocol

```c
/* Fix (pin) a page — MUST unfix when done */
page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, ...);
/* ... use page ... */
pgbuf_unfix (thread_p, page);

/* Mark dirty before unfix if modified */
pgbuf_set_dirty (thread_p, page, DONT_FREE);
```

- Every `pgbuf_fix()` MUST have matching `pgbuf_unfix()` — tracked by `resource_tracker`
- Latch modes: `PGBUF_LATCH_READ`, `PGBUF_LATCH_WRITE`
- Page types: `PAGE_HEAP`, `PAGE_BTREE`, `PAGE_OVERFLOW`, etc.

## Conventions

- All page access through buffer pool — never raw disk I/O
- B-tree operations hold page latches, not transaction locks (latches for physical consistency, locks for logical)
- Heap records use slotted page format — slot 0 is header
- `THREAD_ENTRY *thread_p` is first parameter on all server-side functions

## Gotchas

- `btree.c` is enormous (~30K lines) — use function index to navigate
- Page latch ordering matters: parent before child to avoid deadlocks
- Buffer pool uses victim selection via LRU zones — not simple LRU
- `heap_file.c` has MVCC-aware and non-MVCC paths — check which you're in
