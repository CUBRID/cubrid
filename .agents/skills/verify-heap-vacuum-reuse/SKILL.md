---
name: verify-heap-vacuum-reuse
description: Check physical undo of an ordinary CUBRID relocation vacuum against committed insert reuse. Use when changes affect relocation vacuum latch release, heap slot reuse, WAL durability, or restart undo at the covered source files. This bounded check does not cover OOS.
---

# Verify relocation vacuum slot reuse

A physical undo must find the original slot empty and preserve a competing committed row. If a reused slot is occupied at restart undo, recovery's empty-slot assertion can fail or the committed row can be overwritten.

From the checkout root run:

```sh
python3 .agents/skills/verify-heap-vacuum-reuse/scripts/check.py
```

The command requires Python 3, a C compiler as `cc`, Java, and TLA+ tools 1.7.4. It verifies SHA-256 `936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88`. Place the jar at `~/.cache/tlaplus/tla2tools-1.7.4.jar` or set `TLA2TOOLS_JAR`. Set `JAVA` to select Java. The command does not download tools. `--repo PATH` checks a source mirror; the pinned baseline must remain available in this checkout's Git history.

The script prints a temporary artifact directory. It derives both release positions from the source, checks supported code against the sync commit, compiles the actual `vacuum_heap_record` with mocked dependencies, and runs TLC separately for each slot. Read every model result with `/statechart`. Inspect the generated `.tla`, corresponding `.cfg`, `.out`, and `.replay.out` files. Exit 0 means both models pass. Exit 1 means at least one model finding. Exit 2 means `OUT OF SYNC`, model/replay disagreement, or a model, compiler, or tool error. A finding is a model counterexample, not a reproduced engine bug. A mock replay executes the extracted function, but its page, logger, competing commit, crash, and recovery boundaries are stubs. Verify allocator eligibility and the full recovery path in the engine before reporting an engine defect.

At sync, home passes and forward has a model finding. Vacuum logs the forward slot and releases its latch before its sysop commit. The model allows an INSERT to reuse that slot, commit and flush its WAL, and then a crash before the sysop commit is durable. Restart undo then targets the committed row's slot. The replay injects this interleaving at the release boundary; it does not run the allocator, WAL, or restart. `heap_add_bestpage` and `spage_find_free_slot` support the reuse assumption, but do not establish the entire concurrent execution.

## Engine replay

The model finding has an engine replay. From a built debug worktree with an initialized `cub-workenv`, run:

```sh
.agents/skills/verify-heap-vacuum-reuse/scripts/engine_replay.sh OUTDIR
```

It needs `gdb`, and it creates a new disposable database for each run. It signals only this environment's `cub_master` and `cub_server`, which it selects by `/proc/PID/exe`. It relocates one row, deletes the row and commits. It then stops the vacuum worker under gdb in non-stop mode just before `log_sysop_commit` in the `REC_RELOCATION` branch, which it locates in the current source. While the worker is stopped, it commits a competing INSERT of the same size, SIGKILLs the master and the server, restarts the server and runs SA `checkdb`. Exit 0 means the restart and `checkdb` succeeded, and the competing row survived if its INSERT committed. Exit 1 is a finding, and `OUTDIR/restart-gdb.out` holds the restart backtrace. Exit 2 is a setup or harness error. Pass `DONT_REUSE_OID` as the second argument to test a non-reuse heap.

Observed on 2026-11-10, debug_gcc:

- Baseline `vacuum.c` at the sync commit: exit 1. The competing INSERT commits into the vacuumed forward slot 3. Restart undo of `RVVAC_HEAP_RECORD_VACUUM` (`vacuum_rv_undo_vacuum_heap_record` → `heap_rv_redo_insert`) fails the assertion `slot_p->offset_to_record == SPAGE_EMPTY_OFFSET` in `spage_insert_for_recovery`. Under `cub_master` auto-restart, the server crash-loops.
- With the CBRD-27935 fix, which keeps the forward page fixed until the sysop ends: exit 0. The competing INSERT waits on the forward page while the worker is stopped, and never commits. The restart succeeds with the 14 committed rows, and `checkdb` passes.

### Commit saved-space window (CBRD-27936)

The heap-slot-undo model reports a related byte window. `log_commit_local` frees a transaction's saved page space before it appends the commit record. To replay it, run:

```sh
.agents/skills/verify-heap-vacuum-reuse/scripts/engine_replay_commit.sh OUTDIR
```

The script uses the same process and database rules. It puts two rows of 8000 and 7000 bytes on one page, which leaves 128 free bytes. Session U shrinks row 1 to 100 bytes in place. That is an MVCC UPDATE with no postpone, so it works on the default REUSE_OID heap. U then commits. gdb stops U's worker on the statement after `spage_free_saved_spaces` in `log_commit_local`, and the script reports whether the commit record is appended before that point. Session T grows row 2 to 12000 bytes in place, which fits only with U's released bytes, and commits. The script then SIGKILLs the master and the server, and restarts. The exit codes are the same as those of `engine_replay.sh`.

Observed on 2026-11-10, debug_gcc:

- Free before the commit record (sync commit with the CBRD-27935 fix): exit 1. T commits in place. U is a loser at restart. Undo of U's `RVHF_UPDATE_NOTIFY_VACUUM` (`heap_rv_undo_update` → `heap_rv_undoredo_update` → `heap_update_physical`) gets `SP_DOESNT_FIT` from `spage_update`. It has to write 7972 bytes into a 140-byte slot, and the page has 3232 free bytes. The debug build stops at `assert (false)` in `heap_update_physical`. The DB cannot restart.
- With the CBRD-27936 fix, which frees saved spaces after the commit record is appended: exit 0. T's commit flushes U's commit record, so restart treats U as committed. Row 1 is 100 bytes, row 2 is 12000 bytes, and `checkdb` passes.

Neither replay covers release builds, the DELETE form of the CBRD-27936 window, or 2PC.

The replay covers the slot form of the window on a reuse heap. It does not cover the byte-only form, a non-reuse heap, release builds (where the occupied slot would be overwritten instead of asserting), or a competitor that the fixed latch sends to another page. After the fix, `check.py` reports `OUT OF SYNC` because its source matcher does not recognize the `DONT_FREE`, commit, unfix shape. Update the matcher before relying on the model for the fixed source.

## Bounds and assumptions

One `REC_RELOCATION` sysop, one home slot and one forward slot on distinct pages, one competing INSERT, and one crash/restart are modeled. The home slot is assumed reusable. Each INSERT is assumed able to select the target page and slot, with sufficient space. Page lifetime, bytes, non-reusable home slots, losing INSERT undo order, other record types, multiple workers, replication, and HA are outside scope. The model retains the committed insert at restart. It treats its commit as flushing all earlier WAL, and allows a later vacuum log or sysop commit to be either durable or lost only where WAL order permits. Sysop commit completion means its end record was appended; it does not require a forced flush. If that append precedes the INSERT log and the INSERT commits, its flush also makes the sysop end durable. The baseline finding requires the INSERT commit to flush before the sysop end is appended. The model overapproximates page selection and persistence; a pass establishes only these modeled cases. Ordinary relocation has no explicit abort after the forward release in the covered branch; this check covers crash undo.

To test sensitivity, run `python3 .agents/skills/verify-heap-vacuum-reuse/scripts/check.py --self-test`. It copies covered source into a temporary mirror and derives expected results from the current release positions. It forces early home release, restores the input, forces both releases after sysop end, forces early forward release with the home held, and restores the input again. Every unsafe target must produce both a model finding and a mock injection. The controls work when the forward release has already been corrected. No engine source in the checkout is modified. The home mutation also exposes a stale-page-use question that the mock does not resolve.

## Update the check

`OUT OF SYNC` means a covered source changed beyond the supported release shapes, a covered file or sync commit is missing, or extraction failed. Read the source and update both the model and the source checks for the new behavior. Confirm WAL, latch, slot, and recovery mappings, then rerun the scratch mutation and restore check. Move the sync commit only after confirming every covered file still matches the model. Use `/maintain-formal-verification-skill` for a full freshness review. Never classify an unsupported change as a pass.

## Coverage record

Sync commit: `e64a04a79a5842827a124b3f8313516d0da2594c`.

- `src/query/vacuum.c`, `vacuum.h`: `vacuum_heap_record`, page caller, undo logger and undo callback.
- `src/storage/slotted_page.c`, `slotted_page.h`: `spage_vacuum_slot`, `spage_find_free_slot`, and `spage_insert_for_recovery`.
- `src/storage/heap_file.c`, `heap_file.h`: best-page selection and `heap_rv_redo_insert`.
- `src/storage/page_buffer.c`, `page_buffer.h`: page latch and dirty/unfix behavior.
- `src/transaction/recovery.c`, `recovery.h`: `RVVAC_HEAP_RECORD_VACUUM` mapping.
- `src/transaction/log_manager.c`, `log_recovery.c`, `log_recovery.h`, `log_append.cpp`, `log_append.hpp`, `log_page_buffer.c`, `transaction_sr.c`: sysop logging, commit, flush, and restart undo.

The command compares whole covered files to the sync commit, except the two supported release-position edits in `vacuum_heap_record` and whitespace at line boundaries. This is a conservative freshness fence, not a complete dependency analysis. A change outside these files can invalidate the assumptions and still require a review.
