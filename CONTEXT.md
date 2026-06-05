# CONTEXT — OOS vacuum-reclaim glossary

> Scope: the language of the OOS (Out-of-row Overflow Storage) cleanup work on branch `oos-vacuum` (PR #6986 / CBRD-26668). Glossary only — no implementation detail. Decisions live in `docs/adr/`.

## Terms

- **OOS record** — an attribute value too large for the heap row, stored out-of-line in the OOS file and referenced from the heap record's variable-offset table by an **OOS OID**. The heap record's MVCC header carries the **HAS_OOS flag** when any such reference is present.

- **OOS OID** — the identifier of one OOS record. **Freshly allocated per heap record, per transform** (every INSERT/UPDATE that writes an OOS column gets brand-new OIDs); never shared or aliased across rows. This uniqueness is what makes unconditional reclaim safe (see *delete-all-safe*).

- **OOS chunk / chunk chain** — one OOS record is stored as a linked chain of chunks across OOS-file pages, each chunk pointing to the next. A record's chunks are **not** guaranteed co-located on one page.

- **Record shape** — how a heap row physically lives:
  - **REC_HOME** — the whole row, including its OOS references, sits in the home slot.
  - **REC_RELOCATION → REC_NEWHOME** — the home slot holds only an 8-byte forwarding OID; the real row (and its OOS references) lives in a **REC_NEWHOME** record on a forward page.
  - **REC_BIGONE** — the row body lives on overflow pages. **Invariant: REC_BIGONE never carries OOS.**

- **Eager cleanup** — deleting orphaned OOS records *inline* at DELETE/UPDATE time. Correct **only in SA (standalone) mode**, which has no MVCC snapshots and no vacuum. Gated on `!is_mvcc_op`.

- **Vacuum forward-walk** — vacuum replaying the log forward; for an OOS-bearing UPDATE pre-image it reclaims the old OOS records the live row no longer references. The only path that can reach OOS orphaned by an MVCC UPDATE (the old version exists only in the undo log).

- **Idempotency probe** — a "does this OOS chunk still exist?" check before deleting, so that re-running a vacuum block (after an earlier partial attempt committed some deletes) skips already-gone chunks instead of erroring.

- **Bounded leak vs. vacuum wedge** — two failure modes for an un-reclaimable OOS record. A **bounded leak** leaves the OOS record on disk but vacuum stays live and makes progress. A **vacuum wedge** is when a block can never complete, so vacuum re-runs it forever and `oldest_unvacuumed_mvccid` never advances — stalling MVCC garbage collection and log-archive removal. A wedge is strictly worse than a leak.

- **Block / block retry** — vacuum processes the log in fixed **blocks**. A vacuum worker may only abandon a block during **shutdown**; any other incomplete block is retried from the same `start_lsa`.
