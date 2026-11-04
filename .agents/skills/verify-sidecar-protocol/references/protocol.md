# Protocol checked by the Sidecar models

The goal is to preserve exact large-column values through transactions,
rollback, snapshots, cleanup, retained historical logs, and replica restart.
These requirements refine the proposed Sidecar design, not implemented engine
behavior. The design basis is docs revision
`12b1d0d20a0669ff0401c8290e672d65198aaf0d`; engine context is CUBRID
`e64a04a79a5842827a124b3f8313516d0da2594c`.

## Lifetime

One row column owns each immutable value ID. Updates to other columns keep
that ID. Replacement creates a fresh ID, retires old chunks, and publishes
the new parent in one transaction and statement rollback scope. Effective
transaction-family identity must agree for parent and chunks.

Foreign snapshots select committed parent and chunk versions consistently.
Own reads occur at completed statement boundaries. Captured row views cannot
outlive a storage operation. Before reentrant mutation, copy requested values
or close the view. Abort and rollback close it without requiring allocation.
Copied values remain owned and stable.

Rollback reverses individual effects. Its expected retained statement or
savepoint prefix is stored independently of the undo journal and undo target.
Restoring both data and the checking oracle to an incorrect earlier prefix
must fail. Skipping inserted-chunk undo must leave detectable unowned data.
After transactions settle and snapshots close, no eligible retired chunks
may remain forever under the declared collector fairness. No unowned
physical chunk remains once cleanup drains.

## Historical WAL and readers

Each operation's before/after image has canonical input tokens. Writer
constructors and decoders are implementations of that input contract, not
their own expected-value oracle. A manifest names every fragment, ordinal,
image side, payload, and earliest required record. Historical readers record
what they actually reconstructed and compare it with canonical input.

WAL is ordered. A flush makes a prefix durable. Successful acknowledgement
requires a durable transaction commit. Savepoint-cancelled operations emit no
committed logical event.

Pin from first fragment append through durable metadata handoff. Retention
covers the transaction dependency interval, including cancellation and commit
records, until every registered consumer passes it. Keeping surviving image
fragments without the cancellation metadata is insufficient. Restart restores
pin/dependency state before archive deletion resumes.

The history model checks fixed registered consumers. Late registration,
rewinding an expired history request, and the public unavailable-history
error path remain outside its scope. Those POC obligations are not waived.

## Replica transaction

Reconstruct complete values from retained WAL, independent of live source
chunks. Stage all surviving operations of the source transaction. Publish the
whole local transaction and its durable apply progress in one commit. A
valid operation prefix is insufficient if readers can see only part of a
committed source transaction. Crash discards uncommitted staging. Restart
and retry preserve exactly one committed logical effect.

## Undo resources

Until a fixed-address undo becomes impossible, its slot, required bytes, and
page generation need actual protection. In the latch policy, the held latch
prevents allocation. In the reservation policy, the held reservation protects
the slot and bytes while allowing unrelated capacity to be used. Removing
resource acquisition must not leave hidden phase-based protection.

A sysop end append is separate from forcing WAL. Later competing commit
flushes preceding records, including an earlier end. Undo must detect an
occupied slot, insufficient capacity, or a lost page generation rather than
being disabled when the target is invalid. It must preserve committed rows.

The physical model has one deleted image, page, competitor and crash. It
assumes WAL/page correspondence and a recovery order; it does not prove page
redo, repeated recovery, allocator metadata, compensation logging, or
post-end page-deallocation recovery. The proposed protections are not
established current CUBRID guarantees.

## Integration and exclusions

The boundary model synchronizes parent/chunk readiness, a retained savepoint
prefix, cancellation, durable history, recovery gates, and whole-transaction
replica commit. It imports the actual historical predicates. Lifetime and
physical-undo steps have a documented correspondence and imported guarantees,
not a machine-checked action-refinement proof.

Every completed safe configuration explores its own finite domain completely.
Controls and witnesses stop at their expected counterexamples. Separate
larger cases do not establish their combined product or arbitrary-size
correctness. These models omit encryption, record-format compatibility,
arithmetic overflow, actual memory safety, full utility behavior, performance,
and engine implementation conformance. The POC and release gates still owe
those checks. The legacy relocation-vacuum model finding remains separate.
