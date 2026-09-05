# Buffer, I/O, And Durability

This reference covers the page buffer, raw volume I/O, double-write buffer, and TDE touchpoints. Read it before changing
page fix/unfix behavior, dirty-page flushing, page LSA handling, volume read/write paths, or page encryption metadata.

## Files

| File | Role |
|------|------|
| `page_buffer.c/h` | Buffer pool, page latches, LRU/victim selection, dirty tracking, flush daemon |
| `file_io.c/h` | Raw volume/log/backup file I/O, page reserved area, page watermark/LSA helpers |
| `double_write_buffer.cpp/hpp` | Torn-write protection for data page flushes |
| `tde.c/h` | Transparent data encryption helpers for pages/logs and TDE algorithm metadata |

`page_buffer.c` and `double_write_buffer.cpp` are server/SA code. `file_io.c` and `tde.c` also build in the client
target because client code needs volume metadata and encryption helpers.

## Page Fix Protocol

Normal read:

```c
PAGE_PTR page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
/* read page */
pgbuf_unfix (thread_p, page);
```

Normal update:

```c
PAGE_PTR page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
/* update page */
pgbuf_set_dirty (thread_p, page, DONT_FREE);
pgbuf_unfix (thread_p, page);
```

Rules:

- Every successful `pgbuf_fix*()` must be matched by an unfix/free path.
- Modified pages must be marked dirty before release.
- Prefer `pgbuf_unfix_and_init()` and `pgbuf_set_dirty_and_free()` when the pointer should not be reused.
- Debug builds route many calls through `*_debug` macros that record call-site information. Preserve those macros.

## Important Entry Points

| Area | Entry Points |
|------|--------------|
| Startup/shutdown | `pgbuf_initialize()`, `pgbuf_finalize()`, `pgbuf_thread_variables_init()` |
| Fix/unfix | `pgbuf_fix_with_retry()`, `pgbuf_fix*()`, `pgbuf_unfix*()`, `pgbuf_unfix_all()` |
| Dirty/LSA | `pgbuf_set_dirty*()`, `pgbuf_get_lsa()`, `pgbuf_set_lsa*()`, `pgbuf_set_lsa_as_temporary()` |
| Flush | `pgbuf_flush()`, `pgbuf_flush_with_wal()`, `pgbuf_flush_checkpoint()`, `pgbuf_flush_all*()` |
| Validation | `pgbuf_is_valid_page()`, `pgbuf_check_page_ptype()`, `pgbuf_has_perm_pages_fixed()` |
| Ordered fixes | `pgbuf_ordered_fix*()`, `pgbuf_ordered_unfix*()`, `PGBUF_WATCHER` |
| Deallocation | `pgbuf_dealloc_page()`, `pgbuf_dealloc_temp_page()`, `pgbuf_fix_if_not_deallocated*()` |

## Latches And Watchers

- `PGBUF_LATCH_READ` allows readers; `PGBUF_LATCH_WRITE` is required for page mutation.
- `PGBUF_LATCH_FLUSH` is an internal block mode, not a normal fix mode.
- Ordered heap/overflow page access uses `PGBUF_WATCHER` to carry group/rank/latch state and avoid latch-order
  deadlocks. Do not replace watcher code with plain `pgbuf_fix()` unless the caller no longer participates in ordered
  fixing.

## Dirty Pages, WAL, And Page LSA

- The WAL rule is enforced at flush time: log records must be durable before the corresponding data page is written.
- `FILEIO_PAGE_RESERVED` in `file_io.h` stores page `lsa`, `pageid`, `volid`, page type, flags, and TDE nonce.
- `fileio_set_page_lsa()` writes both the reserved LSA and the page-end watermark. `fileio_is_page_sane()` checks the
  duplicate LSA.
- `pgbuf_flush_with_wal()` is the page-buffer entry point that coordinates flush with WAL state.

## Raw I/O Layer

`file_io.c` owns physical files, volume labels, page reads/writes, backups, and low-level path handling. It is below the
buffer pool. Heap/B-tree/catalog code should not bypass `pgbuf_*` to edit permanent pages through `fileio_*` calls.

## Double-Write Buffer

The double-write buffer protects against torn data-page writes. `double_write_buffer.cpp` sits on the flush path, not on
normal page mutation paths. If a change alters page flush ordering or page image contents, check DWB assumptions too.

## TDE Touchpoints

- `FILEIO_PAGE_FLAG_ENCRYPTED_AES` and `FILEIO_PAGE_FLAG_ENCRYPTED_ARIA` mark encrypted pages in `file_io.h`.
- `pgbuf_set_tde_algorithm()` stores page-level TDE metadata through the buffer pool.
- `file_get_tde_algorithm()` / `file_apply_tde_algorithm()` in `file_manager.c` handle logical file TDE metadata.
- Heap and B-tree creation query class TDE state before applying file/page algorithms.

## Gotchas

- A clean compile does not prove fix/unfix balance. Use resource-tracker assertions and inspect all error paths.
- Do not ignore `PGBUF_CONDITIONAL_LATCH` behavior; callers may rely on a non-blocking failure path.
- Page validation and TDE state are easy to lose when copying page images or adding recovery paths.
- `LOG_LSA` belongs to transaction/WAL, but storage pages carry LSA fields. Treat page LSA updates as durability work.
