# src/transaction/ — MVCC, WAL, Locking, Recovery, Boot

Follow [src/AGENTS.md](../AGENTS.md) for build modes, error handling, CMake registration, and the storage and
transaction change checklist. This file adds transaction-specific navigation and rules.

Before you add a recovery index (`LOG_RCVINDEX`, `RV_fun[]`), read [Adding a Recovery Index](#adding-a-recovery-index) below.

## Build Modes

Most files build only in `SERVER_MODE`/`SA_MODE`. A client subset also builds into `cubridcs` (`TRANSACTION_SOURCES` in
`cs/CMakeLists.txt`): `boot_cl.c`, `client_credentials.cpp`, `flashback_cl.c`, `locator.c`, `locator_cl.c`,
`lock_table.c`, `log_comm.c`, `log_compress.c`, `log_lsa.cpp`, `log_writer.c`, `transaction_cl.c`, and on UNIX
`log_applier.c` and `log_applier_sql_log.c`. Of these, `flashback_cl.c`, `log_applier.c`, and `log_applier_sql_log.c`
build only in `cubridcs`, and `boot_cl.c`, `locator_cl.c`, and `transaction_cl.c` also build in `cubridsa` but not in
`cubrid`. Code in these files outside `SERVER_MODE`/`SA_MODE` guards must build in `CS_MODE`.

## Where to Look

| Task | Start here |
|------|------------|
| Lock acquire, release, escalation | `lock_manager.c`: `lock_object()`, `lock_escalate_if_needed()` |
| Deadlock detection | `lock_manager.c`: `lock_detect_local_deadlock()`, run by the daemon from `lock_deadlock_detect_daemon_init()` |
| Lock modes and compatibility | `LOCK` in `lock_table.h`; `lock_Comp` and `lock_Conv` in `lock_table.c` |
| MVCC visibility | `mvcc.c`: `mvcc_satisfies_snapshot()` |
| Snapshot build, oldest visible MVCCID | `mvcc_table.cpp` (`mvcctable::build_mvcc_info()`, `mvcctable::compute_oldest_visible_mvccid()`), `mvcc_active_tran.cpp`; callers get a snapshot from `logtb_get_mvcc_snapshot()` (`log_tran_table.c`) |
| Append a log record | Declared in `log_manager.h`, defined in `log_manager.c`: `log_append_undoredo_data*()`, `log_append_undo_data*()`, `log_append_redo_data*()`, `log_append_compensate()`. `log_append.cpp` is the prior-list layer under them (`prior_lsa_*`, LSA assignment) |
| System operations | `log_manager.c`: `log_sysop_start()`, `log_sysop_commit()`, `log_sysop_abort()` |
| Recovery index and redo/undo function table | `recovery.h` (`LOG_RCVINDEX`, `struct rvfun`), `recovery.c` (`RV_fun[]`) |
| Crash recovery | `log_recovery.c`: `log_recovery()` calls `log_recovery_analysis()`, `log_recovery_redo()`, `log_recovery_undo()`; redo workers in `log_recovery_redo*.cpp` |
| Log record types | `log_record.hpp` (`LOG_RECTYPE`, `LOG_RECORD_HEADER`); the log address type is in `log_lsa.hpp` |
| Checkpoint | `log_page_buffer.c`: `logpb_checkpoint()`; daemon and trigger in `log_manager.c` (`log_checkpoint_execute()`) |
| Log page I/O, archives | `log_page_buffer.c` |
| Commit, abort | `log_manager.c`: `log_commit()`, `log_abort()`, `log_complete()`. `transaction_sr.c` holds the `xtran_*` request entry points and `tran_server_unilaterally_abort*()` |
| Transaction table | `log_tran_table.c` (`logtb_*`) |
| Object insert/update/delete from client requests | `locator_sr.c`: `xlocator_force()`, `locator_insert_force()`, `locator_update_force()`, `locator_delete_force()` |
| Replication, HA log apply | `replication.c`, `log_applier.c` |
| Vacuum | `src/query/vacuum.c`, `src/query/vacuum_oos.cpp` (outside this directory) |
| Boot | `boot_sr.c` (`boot_restart_server()`, `xboot_initialize_server()`), `boot_cl.c` (client) |

`wait_for_graph.c/h` is compiled out in full (`#if defined(ENABLE_UNUSED_FUNCTION)`). Fix deadlock behavior in
`lock_manager.c`.

## MVCC

- The row header is `MVCC_REC_HEADER` (`mvcc.h`): `mvcc_ins_id`, `mvcc_del_id`, and `prev_version_lsa`.
- For snapshot contents, read `struct mvcc_snapshot` (`mvcc.h`). Visibility uses `lowest_active_mvccid`,
  `highest_completed_mvccid`, and the active set `m_active_mvccs`.
- `MVCCID` is 64-bit (`UINT64`, `storage_common.h`). It is not the transaction index: `tran_index` is a slot in the
  transaction table.
- Vacuum daemons run concurrently with transactions and reclaim versions that no snapshot can see.

## WAL and Recovery

- The log address type is `LOG_LSA` (`log_lsa.hpp`: `pageid` + `offset`).
- A data page is flushed only after the log up to its page LSA is flushed. See
  [buffer-io-durability.md](../storage/docs/buffer-io-durability.md) for the page-buffer side.
- `LOG_DATA_ADDR` (`log_append.hpp`) names the target of a record: `vfid`, `pgptr`, and `offset`.
- Rollback writes `LOG_COMPENSATE` records, so undo that repeats after a crash does not undo a change twice.

## Adding a Recovery Index

The checklist for adding a recovery index (`LOG_RCVINDEX`, `RV*`) with its recovery functions.
Read this before you add a recovery index or its `RV_fun[]` entry.

A new log record type (`LOG_RECTYPE` in `src/transaction/log_record.hpp`) is a different, larger change, because the recovery passes, the log dumps, and `src/transaction/log_applier.c` switch on it.

- `src/transaction/recovery.h`: add `RVFOO = <last value + 1>` just before the `RV_LAST_LOGID = ...` alias and point the alias at it; `RV_NOT_DEFINED = 999` stays last. Never renumber or insert between existing values: logs store the numbers, and `RV_fun[]` is indexed by position. The comment above `RVOOS_INSERT` about renumbering the OOS block describes the former feat/oos branch and does not apply on `develop`.
- `src/transaction/recovery.c`: add the `RV_fun[]` entry at the same position, `{RVFOO, "RVFOO", undofun, redofun, dump_undofun, dump_redofun}`. Recovery functions take `(THREAD_ENTRY *, LOG_RCV *)`, not `const LOG_RCV *`; a dump function may be `NULL`. `rv_check_rvfuns ()` checks the order only when an assert-enabled server or SA boot runs `log_initialize_internal ()`, so boot one.
- `RCV_IS_*` macros (`recovery.h`):
  - `RCV_IS_LOGICAL_LOG` when rollback and postpone must not fix the logged page, because the function fixes pages and logs its own changes (a null VPID counts as logical already).
  - `RCV_IS_NOTHING_TO_UNDO` when rollback must skip the record.
  - `RCV_IS_LOGICAL_COMPENSATE_MANUAL` / `RCV_IS_LOGICAL_RUN_POSTPONE_MANUAL` when the undo / postponed redo function logs its own compensation / run-postpone record from `rcv->reference_lsa`.
  - `RCV_IS_POSTPONE_NOT_UNDOABLE` when the log does not cover the postponed effect.
- TDE: add the index to `LOG_MAY_CONTAIN_USER_DATA` (`src/storage/tde.h`) when its data can hold user row or key data; otherwise that data is logged in plaintext for encrypted tables.
- Parallel redo: add it to `log_rv_need_sync_redo ()` (`src/transaction/log_recovery.c`) when its redo must finish before later records are redone, as for volume or sector changes.
- Page-less records: when the record is appended with a NULL `pgptr`, add the index to `LOG_IS_PAGELESS_RCVINDEX` (`src/transaction/log_manager.c`); otherwise `log_append_undoredo_data ()` / `log_append_redo_data ()` set a page LSA on the NULL page.
- Replication records (`RVREPL_*`) also need `log_dump_record_replication ()` (`src/transaction/log_manager.c`), `src/transaction/replication.c`, and `src/transaction/log_applier.c`.
- Compatibility: an older binary cannot recover a log that holds the new index, so say so in the PR. Never bump `disk_compatibility_level` (`src/base/release_string.c`) in a feature PR—it is a release-level version bumped once per incompatible release with release/upgrade coordination; instead, verify compatibility and flag breaking on-disk changes to release owners.

## Locks

- `LOCK` (`lock_table.h`) lists every mode, including `NULL_LOCK`, `BU_LOCK`, and `SIX_LOCK`. Its values index
  `lock_Comp` and `lock_Conv`. Keep the enum order and both tables in step.
- A class lock is taken on the class OID with `oid_Root_class_oid` as its class. An instance lock is taken on the
  object OID with its class OID.
- Plain MVCC reads take no row locks; they use the snapshot. MVCC heap insert, delete, and update take the transaction's
  MVCCID self-lock (`logtb_ensure_mvccid_self_lock()`) before they stamp the record, and later writers wait on it.
- `lock_escalate_if_needed()` replaces many instance locks with one class lock.

## Conventions

- Server functions that use per-thread state (latches, locks, private heap, error context) take
  `THREAD_ENTRY *thread_p` first. Pure helpers and client code do not; C++ code may take `cubthread::entry &`. Follow
  the surrounding file.
- Client boot code is in `boot_cl.c` and server boot code is in `boot_sr.c`. Function names carry no `_cl`/`_sr`
  suffix. Server request handlers are named `xboot_*`.

## Gotchas

- `log_manager.c`, `locator_sr.c`, `lock_manager.c`, and `log_page_buffer.c` are very large. Use `rg` on function
  names instead of reading them top to bottom.
- `boot_restart_server()` and `xboot_initialize_server()` initialize subsystems in dependency order. Put a new
  subsystem's initialization after everything it uses.
