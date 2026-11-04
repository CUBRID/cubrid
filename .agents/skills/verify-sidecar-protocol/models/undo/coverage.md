# Physical undo destination

## Promise and scope

When a deleted image must be physically restored at its original address, undo must find the same allocated page generation, the slot empty, and enough bytes available. It must preserve a committed competing insert. A violation can overwrite committed data, exhaust the page, or restore into a deallocated or reused page. `UndoSlotSafe`, `UndoBytesSafe`, `UndoPageSafe`, and `CommittedPreserved` check these distinct consequences. The first three record violations at the actual undo attempt, including abort and restart. They do not disable the attempt when the address is invalid.

This is a proposed protocol model, not a claim about current CUBRID code. It corresponds to the physical-undo prerequisite in `sidecar/codex-20261104/design.md`, especially its reservation, latch, and WAL ordering candidates. Source context is CUBRID `e64a04a79a5842827a124b3f8313516d0da2594c` and design commit `12b1d0d20a0669ff0401c8290e672d65198aaf0d`. The source grounding in `grounding.md` identifies vacuum's released slot and byte space, recovery insertion, and page lifetime as separate obligations. It does not establish a reachable production failure. The existing relocation vacuum model is independent and unchanged.

## State and boundaries

- `phase` distinguishes ready, running system operation, end appended, abort, restart, and completion. `recoveryStep` names loser undo, deletion undo, and recovery completion.
- `page` stores allocation, generation, each slot's physical occupant, and used bytes. Available bytes are `Capacity - page.used`; the deleted image needs two bytes. `competitor` stores its status, slot, and size.
- `wal` is an ordered sequence; `durable` is its durable prefix length. `latch` and `reservation` represent the two alternative proposed policies. `crashed` bounds crashes to one. `turn` records the faulty recovery worker's alternating retry state only in the liveness mutant. `saw` records reachability witnesses; `violations` records an attempted invalid undo.
- `BeginDelete` atomically logs and forces the deletion and releases its slot and bytes. This deliberately assumes the delete log is durable before its dirty page can survive a crash. It is an abstraction, not a required immediate force in an implementation.
- `Insert` atomically appends its insert record and updates the page; `CommitCompetitor` appends commit and flushes the entire preceding WAL prefix. `Flush` can independently flush the current prefix. Insertion after an appended end can be uncommitted and unflushed. A crash drops its unlogged dirty page effect, while a durable uncommitted insert survives to loser undo. A committed insert survives.
- `AppendEnd` appends without forcing and releases the latch or reservation. Commit after this append necessarily flushes the earlier end. `Abort` chooses rollback; `Rollback` performs the fixed-address undo as a separate atomic action. Competing insertion and commit remain enabled in rollback, subject to the held protection. `DropProtection` releases the policy resource early only in its respective mutant.
- `Crash` keeps the durable WAL prefix and the page effects permitted by WAL. `RecoverLoser` undoes a durable losing competitor before `RecoverUndo` tests for a durable end and, if absent, restores the deleted image. `RecoverFinish` completes recovery. Loser-before-deletion-undo is imported and unproved. An independent reverse-order probe also passed at all three safe bounds, so this model does not establish that this ordering matters. The loser witness proves the cleanup action is reachable, not that the ordering is necessary or matches the engine. Repeat-crash idempotence is outside the model.
- `Finish` closes a completed system operation once the competing insert is resolved. `Done` stutters only after `phase = "done"`. Crashes after this abstract completion are represented by the identical pre-completion crash state, but no new work starts after completion.

An acquired reservation protects the original slot and two undo bytes. An acquired latch blocks competing page access. Both held resources block deallocation. `Deallocate` considers only empty pages and is modeled only before end append, during running or rollback; the page mutant bypasses held-resource protection. Safe executions retain the resource through rollback, so they do not deallocate the page in that interval. Deallocation after end append and its WAL, durability, and restart handling are excluded. Neither policy proves how reservations survive a crash, how latches are ordered, or how the allocator finds protected resources. After end append, insertion can reuse the slot and bytes. A later competitor commit flushes the earlier end, and the model removes a durable losing insert before deletion undo if the end was lost.

## Imported and exported guarantees

There are no TLA+ module imports from the other Sidecar models. This model assumes the page address and its old generation identify one fixed undo image, WAL ordering and commit flush are correct, logged losing inserts are undone before this image, and no external allocator bypasses the modeled page policy. It does not model page deallocation after end append. Its exported contract is the four named semantic invariants under either proposed guard, not an engine-conformance result or a general composition proof. An integration model must map its physical deletion, end append, competitor commit, crash, and restore events to these atomic actions and establish those assumptions separately.

## Fairness and progress

`RecoveryFinishes` requires every reached recovery state eventually to reach `done`. `RecoveryAvailable` checks that `Recover` is enabled in every recovery state, for the safe and livelock cases. The model assumes recovery resources remain available. Weak fairness on `Recover` requires the continuously enabled recovery worker to run. In the livelock mutant, `RecoverLoser` stays enabled and executes, but toggles `turn` without advancing `recoveryStep`. The resulting two-state cycle satisfies weak fairness and violates `RecoveryFinishes`. `NoRecoveryWitness` demonstrates that the antecedent is reachable. This property makes no claim that normal admission or transaction completion is fair.

## Cases and results

All counts below are TLC 1.7.4 with one worker and the jar SHA-256 `936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88`. Deadlock checks are enabled. No state or depth constraints or symmetry reductions are used. A negative or witness count is the explored prefix before the named violation, except the temporal negative explores its complete graph.

| Case | Bounds `(MaxSlots, Capacity)` | Expected and observed result | States generated / distinct |
|---|---:|---|---:|
| safe | (2, 3) | Reservation, all invariants and progress pass | 306 / 203 |
| safeLatch | (2, 3) | Latch through end append, all checks pass | 204 / 132 |
| larger | (3, 4) | Reservation qualification, all checks pass | 706 / 472 |
| unsafeSlot | (2, 3) | Slot guard removed, `UndoSlotSafe` fails | 42 / 37 |
| unsafeBytes | (2, 3) | Byte guard removed, `UndoBytesSafe` fails | 56 / 50 |
| unsafePage | (2, 3) | Empty page deallocated despite protection, `UndoPageSafe` fails | 69 / 64 |
| unsafeClobber | (2, 3) | Slot guard removed, `CommittedPreserved` fails | 98 / 86 |
| unsafeLivelock | (2, 3) | Fair worker repeats without progress, `RecoveryFinishes` fails | 253 / 150 |
| unsafeUnavailable | (2, 3) | Recovery resource unavailable, `RecoveryAvailable` fails | 6 / 6 |
| unsafeNoLatch | (2, 3) | Latch acquisition omitted, `UndoSlotSafe` fails | 59 / 55 |
| unsafeNoReservation | (2, 3) | Reservation acquisition omitted, `UndoSlotSafe` fails | 59 / 55 |
| unsafeDropLatch | (2, 3) | Latch released during rollback, `UndoSlotSafe` fails | 107 / 80 |
| unsafeDropReservation | (2, 3) | Reservation released during rollback, `UndoSlotSafe` fails | 166 / 125 |
| witnessRecovery | (2, 3) | `NoRecoveryWitness` fails | 6 / 6 |
| witnessBeforeEnd | (2, 3) | `NoBeforeEndWitness` fails after another-slot commit before end | 7 / 7 |
| witnessEndLost | (2, 3) | `NoEndLostWitness` fails after crash loses unflushed end | 18 / 17 |
| witnessEndCovered | (2, 3) | `NoEndCoveredWitness` fails after later commit flushes end | 26 / 25 |
| witnessShared | (2, 3) | `NoSharedWitness` fails when reservation permits another allocation | 3 / 3 |
| witnessLoserUndo | (2, 3) | `NoLoserUndoWitness` fails after durable losing insert cleanup | 88 / 76 |

`unsafeUnavailable` deliberately removes the recovery-availability premise and
checks `RecoveryAvailable`. It is an assumption-sensitivity control. It is
separate from `unsafeLivelock`, which preserves worker availability and weak
fairness while violating progress.

Each mutant changes one protection or scheduling rule and checks the same named property as its safe case. `unsafeClobber` repeats the slot fault to check its separate committed-data consequence. The witness checks negate deliberately reachable facts. An incomplete run or a different error is not a successful control.

The /statechart safe run reached 203 states and found no broken rules. The unsafe slot run reached 274 states after continuing past the error. Its shortest path is original record, delete, competitor inserts into the released slot, abort, then undo finds the occupied slot. The chart suppresses transitions whose source and destination have the same reader-facing label. The chart is an interpretation of the model, not a real-engine replay.

## Corrections and limits

The independent review found that phase-based guards hid missing resource acquisitions. The repaired guards consult the held latch or reservation, and the four acquisition and early-release controls expose the resulting undo-slot failure. The original liveness control alternated worker availability. The revised control keeps the worker enabled and demonstrates a fair cycle in its faulty effect. The initial crash abstraction also allowed a page insert to survive without its WAL record. The final model drops that dirty effect when the insert is absent from the durable prefix, and separately permits flushing the insert so loser undo is exercised. These are modeling corrections, not demonstrated changes to engine code.

The model has one deleted image, one competitor, one page, one crash, and at most one competitor commit. Slot capacities and record sizes are abstract units. It omits repeat crashes, page redo, multiple losers, logging of compensation records, buffer/page flush races, allocator metadata, lock acquisition and deadlock, and concurrency inside a listed atomic action. An abort can finish while an unrelated uncommitted competitor remains active; that competitor’s eventual outcome is outside this model. A failed undo is recorded and its abstract page mutation proceeds even if used bytes exceed capacity, so the counterexample is visible instead of being blocked. In particular, it imports loser-before-sysop undo and WAL/page correspondence rather than proving either against CUBRID recovery. It does not establish page lifetime after end append or its interaction with crash recovery. The qualification run is finite and does not establish arbitrary-size correctness.

## Reproduce

Run each config from this directory, with a distinct temporary metadata directory. For example:

```sh
mkdir -p /tmp/sidecar-undo-tlc/tmp-safe /tmp/sidecar-undo-tlc/tmp-slot
java -Djava.io.tmpdir=/tmp/sidecar-undo-tlc/tmp-safe -XX:+UseParallelGC -cp "$HOME/.cache/tlaplus/tla2tools-1.7.4.jar" tlc2.TLC -workers 1 -metadir /tmp/sidecar-undo-tlc/reproduce-safe -config Safe.cfg SidecarUndo.tla
java -Djava.io.tmpdir=/tmp/sidecar-undo-tlc/tmp-slot -XX:+UseParallelGC -cp "$HOME/.cache/tlaplus/tla2tools-1.7.4.jar" tlc2.TLC -workers 1 -metadir /tmp/sidecar-undo-tlc/reproduce-slot -config UnsafeSlot.cfg SidecarUndo.tla
```

Run all remaining `.cfg` files with separate temporary and metadata directories and compare against `cases.json`. The /statechart reports and Mermaid graphs for the repaired model are in `/tmp/sidecar-tla-20261104/repair-undo/chart`. The chart omits transitions that keep the same reader-facing label.

## Final common qualification receipt

The common qualification run completed all 19 undo cases with their
expected verdicts. Its report is
`/mnt/nvme/data-5/tmp/sidecar-tlc-7ff52zr4/report.json`.
Report SHA-256: `8d18505c1655fd20a1b8aba36f9fbee89606a988fb47bcd0953c8944ea66e9e8`.
Safe state counts are exhausted searches; negative and witness counts are
prefixes ending at their named counterexamples. All quick cases are included.
The final run used the frozen runner and requirements, without concurrent
edits to executable inputs. The table above is reconciled with this receipt.
