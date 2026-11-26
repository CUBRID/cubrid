# Adding a Recovery Index

The checklist for adding a recovery index (`LOG_RCVINDEX`, `RV*`) with its recovery functions.
Read this before you add or renumber a recovery index or its `RV_fun[]` entry.

A new log record type (`LOG_RECTYPE` in `src/transaction/log_record.hpp`) is a different, larger change, because the recovery passes, the log dumps, and `src/transaction/log_applier.c` switch on it.

- `src/transaction/recovery.h`: add `RVFOO = <last value + 1>` just before the `RV_LAST_LOGID = ...` alias and point the alias at it; `RV_NOT_DEFINED = 999` stays last.
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

Transaction-specific navigation is in [src/transaction/AGENTS.md](../src/transaction/AGENTS.md).
