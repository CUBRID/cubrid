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

The script prints a temporary artifact directory. It derives both release positions from the source, checks supported code against the sync commit, compiles the actual `vacuum_heap_record` with mocked dependencies, and runs TLC separately for each slot. Read every model result with `/statechart`. Inspect the generated `.tla`, corresponding `.cfg`, `.out`, and `.replay.out` files. Exit 0 means both models pass. Exit 1 means at least one model finding. Exit 2 means `OUT OF SYNC` or a model, compiler, or tool error. A finding is a model counterexample, not a reproduced engine bug. A mock replay executes the extracted function, but its page, logger, competing commit, crash, and recovery boundaries are stubs. Verify allocator eligibility and the full recovery path in the engine before reporting an engine defect.

At sync, home passes and forward has a model finding. Vacuum logs the forward slot and releases its latch before its sysop commit. The model allows an INSERT to reuse that slot, commit and flush its WAL, and then a crash before the sysop commit is durable. Restart undo then targets the committed row's slot. The replay injects this interleaving at the release boundary; it does not run the allocator, WAL, or restart. `heap_add_bestpage` and `spage_find_free_slot` support the reuse assumption, but do not establish the entire concurrent execution.

## Bounds and assumptions

One `REC_RELOCATION` sysop, one home slot and one forward slot on distinct pages, one competing INSERT, and one crash/restart are modeled. The home slot is assumed reusable. Each INSERT is assumed able to select the target page and slot, with sufficient space. Page lifetime, bytes, non-reusable home slots, losing INSERT undo order, other record types, multiple workers, replication, and HA are outside scope. The model retains the committed insert at restart. It treats its commit as flushing all earlier WAL, and allows a later vacuum log or sysop commit to be either durable or lost only where WAL order permits. Sysop commit completion means its end record was appended; it does not require a forced flush. If that append precedes the INSERT log and the INSERT commits, its flush also makes the sysop end durable. The baseline finding requires the INSERT commit to flush before the sysop end is appended. The model overapproximates page selection and persistence; a pass establishes only these modeled cases. Ordinary relocation has no explicit abort after the forward release in the covered branch; this check covers crash undo.

To test sensitivity, run `python3 .agents/skills/verify-heap-vacuum-reuse/scripts/check.py --self-test`. It copies covered source into a temporary mirror, changes the real home release from `DONT_FREE` to `FREE`, checks both independent findings, restores the source, and checks the baseline again. It also moves the forward release after sysop commit in that mirror and checks the safe model shape. No engine source in the checkout is modified. The home mutation also exposes a stale-page-use question that the mock does not resolve.

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
