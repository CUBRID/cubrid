# 0001 — MVCC OOS reclamation is always vacuum-deferred

> **Status: Accepted — being implemented in PR #6986 (DEC-3, 2026-06-04).** The "remaining gap" below (MVCC `remove_old_forward` relocation leak) is now in scope: a dedicated `RVHF_DELETE_NEWHOME_NOTIFY_VACUUM` WAL record will tag the forward-delete and be admitted to the vacuum forward-walk. This is an on-disk log-format addition (pinned rcvindex + recovery handler).

When an UPDATE orphans an OOS-bearing old record version in MVCC mode, the old OOS records are reclaimed by **vacuum** — never deleted inline at update time — because the old version survives in the undo log (reached via `prev_version_lsa`) and a concurrent snapshot can still resolve its OOS OIDs from the OOS file. Inline deletion is correct only in standalone (SA) mode, which has no snapshots and no vacuum; that is why the eager-delete helpers (`heap_update_home_delete_replaced_oos`, `heap_update_relocation_delete_replaced_oos`) are `!is_mvcc_op`-gated.

## The remaining gap (why this ADR exists)

`heap_update_relocation`'s three `remove_old_forward` sub-paths physically delete the old forward `REC_NEWHOME` via `heap_log_delete_physical` (rcvindex `RVHF_DELETE`). `RVHF_DELETE` is not in `LOG_IS_MVCC_HEAP_OPERATION`, so vacuum neither collects the slot nor lets the forward-walk see its undo — the old forward's OOS records leak permanently in MVCC mode. SA mode is already covered by the eager-delete helper.

The physical delete is **load-bearing**: CUBRID in-place MVCC keeps only the latest version on the heap page. The old forward's data is moved to the home slot and its old copy is preserved in the delete's undo record, whose LSA becomes the new version's `prev_version_lsa` (`heap_update_set_prev_version`). So the old OOS-bearing version exists *only* in that undo record.

## Decision

Tag the `remove_old_forward` forward-delete (MVCC only) with a dedicated notify-vacuum delete rcvindex (e.g. `RVHF_DELETE_NEWHOME_NOTIFY_VACUUM`) and admit it to the vacuum forward-walk, so vacuum reclaims that undo record's OOS once the version is snapshot-dead.

Recommended hook-point: a dedicated forward-walk-only branch in `vacuum_process_log_block`, **not** folding the new rcvindex into `LOG_IS_MVCC_HEAP_OPERATION` (which would make vacuum pointlessly "collect" an already-deleted slot for its REMOVE path). This hook-point is an internal, reversible detail with no on-disk impact.

## Considered options

- **Synchronous `oos_delete` during `remove_old_forward` (all modes).** Rejected: in MVCC mode it deletes OOS that an active snapshot still resolves from the OOS file via the prev-version chain → wrong results / read errors for concurrent transactions. (This was the original TODO-note proposal.)
- **Leave the old forward as a dead version on the page for vacuum's on-page `HAS_OOS` REMOVE path.** Rejected: not how CUBRID in-place MVCC works — old UPDATE versions live in the undo log, not as page slots. The on-page `HAS_OOS` check only fires for final-state DELETEs (the deleted record genuinely stays on the page), never for UPDATE old-versions.
- **Admit the shared `RVHF_DELETE` to the forward-walk gate.** Rejected: `RVHF_DELETE` covers every physical heap delete in the system; the forward-walk would unpack and scan the undo of all of them — broad blast radius for a rare OOS case.

## Consequences

- Adds a new WAL record type (an on-disk log-format addition): the rcvindex value is pinned and not renumberable, like the existing OOS rcvindexes.
- **OID-reuse forward-compat caveat.** The forward-walk (`vacuum_forward_walk_delete_old_oos`) currently deletes *all* old OOS OIDs unconditionally, with no old∩new sharing check — unlike the SA eager-delete helpers, which skip OIDs shared with the new record (`heap_oos_oid_in_vector`). This is safe today because UPDATE always allocates fresh OIDs (`heap_attrinfo_insert_to_oos`). If OOS-OID reuse/dedup is ever implemented — a deferred future improvement (the cancelled M3 plan; **not** in M2) — the forward-walk must gain the same sharing check first, or it will delete an OID the live post-image still references.
