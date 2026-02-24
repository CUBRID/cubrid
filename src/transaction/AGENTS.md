# src/transaction/ — Transaction Manager

## OVERVIEW

WAL logging, lock manager, MVCC, recovery, and locator (object-to-page resolution). 80 files.

## KEY FILES

| File | Lines | Purpose |
|------|-------|---------|
| `log_manager.c` | 15k | WAL write, checkpoint, archive |
| `log_page_buffer.c` | 11k | Log buffer pool |
| `lock_manager.c` | 9k | Lock table, deadlock detection |
| `locator_sr.c` | 14k | Server-side object locator |
| `locator_cl.c` | ~8k | Client-side object locator |
| `mvcc.c` | | MVCC snapshot, visibility |

## CODEOWNERS

All files → @hornetmj

## NOTES

- Recovery uses ARIES-style WAL protocol
- Lock escalation: row → page → table
- MVCC snapshot isolation is default
