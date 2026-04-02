# src/transaction/ — MVCC, WAL, Locking, Recovery, Boot

Server-side. Largest module by scope.

## Key Files

| File | Role |
|------|------|
| `lock_manager.c` | Lock acquisition, release, escalation, deadlock detection |
| `wait_for_graph.c` | Deadlock detection via wait-for graph |
| `mvcc.c/h` | MVCC snapshot, visibility: `mvcc_satisfies_snapshot()` |
| `log_append.cpp` | WAL log record writing |
| `log_recovery.c` | Crash recovery: analysis, redo, undo phases (ARIES) |
| `log_manager.c` | Log buffer management, checkpoints |
| `log_page_buffer.c` | Log page I/O, archive management |
| `log_record.hpp` | `LOG_RECORD_HEADER`, record types, LSN |
| `log_comm.c` | Log communication (replication support) |
| `log_compress.c` | Log record compression (LZ4) |
| `boot_sr.c` | Server boot sequence, database creation |
| `boot_cl.c` | Client-side boot, database connection |
| `log_tran_table.c` | Transaction table management |
| `transaction_sr.c` | Server-side transaction ops: commit, abort, savepoint |
| `transaction_cl.c` | Client-side transaction control |

## Where to Look

| Task | File |
|------|------|
| Fix deadlock | `lock_manager.c`, `wait_for_graph.c` |
| Fix MVCC visibility | `mvcc.c` — `mvcc_satisfies_snapshot()` |
| Fix WAL/logging | `log_append.cpp` (write), `log_recovery.c` (redo/undo) |
| Fix crash recovery | `log_recovery.c` — ARIES: analysis → redo → undo |
| Fix checkpoint | `log_manager.c` — `logpb_checkpoint()` |
| Fix vacuum/GC | `src/query/vacuum.c` (NOT in this directory) |
| Fix boot/startup | `boot_sr.c` (server), `boot_cl.c` (client) |
| Fix commit/abort | `transaction_sr.c` |

## MVCC Model

- Each row version has `insert_id` and `delete_id` (MVCC IDs)
- `mvcc_satisfies_snapshot()` checks visibility against transaction's snapshot
- Snapshot contains: lowest active MVCC ID, bit array of active transactions
- Vacuum reclaims rows invisible to all active snapshots

## WAL Protocol (ARIES-based)

- **Write-ahead**: Log record written before data page flush
- **Log records**: Identified by `LOG_LSN` (page + offset)
- **Undo/Redo**: Each record has undo data, redo data, or both
- **Recovery phases**: Analysis → Redo (from checkpoint) → Undo (losers)
- **Compensation**: CLR records prevent repeated undo during nested recovery

## Lock Hierarchy

```
Database → Table (SCH-S/SCH-M/IX/IS/S/X) → Row (S/X)
```

- Intent locks (`IX`, `IS`) on table before row locks
- Schema locks (`SCH-S`, `SCH-M`) for DDL
- Deadlock detection runs periodically via wait-for graph
- Lock escalation: many row locks → table lock

## Conventions

- `THREAD_ENTRY *thread_p` first parameter everywhere
- Transaction index (`tran_index`) identifies active transactions — not MVCC ID
- Log records use `LOG_DATA_ADDR` to identify target page + offset
- Boot functions prefixed `boot_` (client: `_cl`, server: `_sr`)

## Gotchas

- `lock_manager.c` and `log_recovery.c` are each ~15K+ lines
- MVCC IDs are 64-bit, monotonically increasing — never reused
- Vacuum is concurrent — runs as daemon, must handle races with active transactions
- Recovery code must handle partially-written log pages (torn writes)
- Boot sequence has strict ordering — server must initialize subsystems in correct order

