# 0002 — A vacuum-worker OOS reclaim failure degrades to a bounded, logged leak

## Status

Accepted (PR #6986 / CBRD-26668). Supersedes the earlier "propagate as block failure" resolution of the Greptile G12/G13 review comments.

## Context

CUBRID vacuum processes the transaction log in fixed **blocks** on worker threads. The codebase enforces a hard invariant: **a vacuum worker may only abandon a block during shutdown.**

- `vacuum_finished_block_vacuum` asserts `thread_p->shutdown` (and `is_shutdown_requested()`) on the *incomplete* branch — `src/query/vacuum.c:4188-4192` (debug builds).
- `vacuum_check_shutdown_interruption` asserts a worker only errors with `thread_p->shutdown && error_code == ER_INTERRUPTED` — `src/query/vacuum.c:8388`.
- There is **no retry cap**: an incomplete block is re-marked AVAILABLE+INTERRUPTED and re-dispatched from the same `start_lsa` forever. A block that always fails wedges vacuum — `oldest_unvacuumed_mvccid` never advances, stalling MVCC GC and log-archive removal.

Every other non-shutdown worker error in this file follows the same pattern: `assert_release(false)` + `er_clear()` + continue (b-tree vacuum `3973-3987`, heap-object collection `3804-3812`, LOB delete `3994-3997`). They swallow the error, log it, and finish the block.

The PR's OOS forward-walk cleanup violated this invariant: on an `oos_delete` failure it did `error_code = oos_err; goto end;` (`vacuum.c:3869`). Verified consequences if that path is ever exercised by a real I/O/buffer error: (1) a debug server aborts on the `4190` assert; (2) a release server retries the block forever (wedge). A *bounded leak* (an OOS record left on disk) is strictly safer than an *unbounded wedge*.

## Decision

On **any** OOS reclaim failure in the vacuum worker — a transient `heap_oos_find_vfid` lookup failure during the forward-walk gate, **or** an `oos_delete` chunk failure inside `vacuum_forward_walk_delete_old_oos` — the worker:

1. logs the failure loudly via `vacuum_er_log_error` (so the leak is **detectable** in the vacuum error log),
2. `er_clear()`s the error,
3. continues, letting the block **complete** (`error_code` stays `NO_ERROR`).

It does **not** `goto end` / fail the block for OOS errors. The per-block idempotency probe (`vacuum_oos_chunk_exists`) is retained — it is still correct and useful when a block is retried for other (legitimate) reasons.

This applies both to the new transient-lookup case (review comment G16) and as a **retrofit** of the existing `oos_delete`-failure path at `vacuum.c:3869` (review comments G12/G13).

## Considered options

- **Fail the block + idempotent retry** (the original G12/G13 resolution). Rejected: trips the shutdown-only assert at `4190` in debug builds and wedges vacuum with no retry cap on a permanent failure.
- **Fail the block, but relax the `4188-4192` assert and add a bounded retry cap.** Rejected: invasive surgery on shared vacuum control-flow and a load-bearing invariant, to buy aggressive retry of a rare OOS error — the cap still ends in "give up and leak," i.e. the same end state as the chosen option but with more churn and more blast radius.
- **Bounded, logged leak (chosen).** Consistent with how b-tree/heap/LOB vacuum errors are already handled; preserves vacuum liveness; makes the leak observable.

## Consequences

- On rare OOS-file I/O/buffer errors, a small number of OOS records may remain un-reclaimed. They are logged and can be reconciled out-of-band; they do not block vacuum.
- Vacuum liveness and the "worker abandons a block only on shutdown" invariant are preserved.
- This reverses the literal ask of Greptile G12 ("propagate as block failure"); the review threads will be answered with this rationale.
- A future hard guarantee (zero OOS leak) would require a durable retry/repair queue outside the vacuum worker, which is out of scope here.
