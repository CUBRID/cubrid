# src/storage/ — Storage Engine

## OVERVIEW

On-disk data structures: B-tree indexes, heap files, page buffer pool, file I/O, and file manager. 56 files.

## KEY FILES

| File | Lines | Purpose |
|------|-------|---------|
| `btree.c` | 36k | B-tree operations — largest file in project |
| `heap_file.c` | 26k | Heap (row) storage |
| `page_buffer.c` | 16k | Buffer pool: page fetch/unfix/flush |
| `file_io.c` | 12k | Physical I/O, volume management |
| `file_manager.c` | 11k | Logical file/page allocation |
| `statistics_sr.c` | | Table/index statistics (owner: @shparkcubrid) |

## CODEOWNERS

- `statistics*` → @shparkcubrid
- Everything else → @hornetmj

## NOTES

- Page size is configurable at DB creation time
- Buffer pool uses clock-sweep replacement
- `btree.c` handles all index types (unique, FK, covering, etc.) in one file
