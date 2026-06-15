# 1. Synchronous OOS reclaim inside the vacuum system operation

- Status: Accepted
- Date: 2026-06-15
- Context: CBRD-26668, PR #6986 (discussion r3121789826)

## Context

When vacuum removes a dead heap record that references Out-of-row Overflow Storage
(OOS), the OOS chunks it points to must also be reclaimed, or they leak. The code
path is `vacuum_heap_record` in `src/query/vacuum.c`, which clears the heap slot
(`spage_vacuum_slot`) and then deletes the referenced OOS chunks
(`vacuum_heap_oos_delete_within_sysop` -> `oos_delete`).

Two facts shape the design:

1. **The list of OOS OIDs to delete lives inside the record being deleted.**
   `vacuum_heap_oos_delete_within_sysop` parses the OIDs out of `helper->record`.
   Once the heap slot is vacuumed and that becomes durable, the only on-disk record
   of *which* OOS chunks were referenced is gone.

2. **WAL log records are per-page; one physical redo record covers one page.**
   A single-page operation is therefore atomic on its own. A multi-page operation
   is not: pages flush independently, so a crash can leave it torn. WAL recovery
   only replays what was logged — it does not re-derive missing intent.

Whether an operation is single- or multi-page is governed by two **orthogonal**
axes — where the record body lives (`record_type`) and whether it references OOS:

```
total footprint = body footprint + OOS footprint

  record_type     OOS?   pages touched          path
  -----------     ----   -------------          ---------------------------------
  REC_HOME        no     home only              bulk log, NO sysop
  REC_HOME        yes    home + OOS pages       one sysop   (this is `has_oos`)
  REC_RELOCATION  any    home + fwd (+ OOS)     one sysop
  REC_BIGONE      n/a    home + ovf chain       one sysop   (OOS must not coexist)
```

`record_type` alone is only a proxy for the footprint. OOS is the orthogonal axis
that pushes an otherwise single-page `REC_HOME` into the multi-page (sysop) path —
which is exactly why the `case REC_HOME: if (has_oos)` branch exists.

## Decision

Reclaim OOS chunks **synchronously, inside the same system operation** that logs
the heap-slot removal. `vacuum_heap_record` opens one sysop
(`log_sysop_start`) for any multi-page removal and `log_sysop_commit`s it after the
slot log and the OOS deletes, or `log_sysop_abort`s on failure.

We do **not** defer OOS reclamation to a later garbage-collection pass.

## Rationale

- **Separating the two is unrecoverable.** If the slot removal becomes durable
  before the OOS deletes and a crash intervenes, the OOS OID list is lost forever
  (guaranteed leak, no recovery path). The reverse order yields a dead record
  pointing at freed — possibly reused — OOS storage (corruption). Ordering cannot
  fix this; only atomicity can.

- **A deferred design does not remove the atomicity requirement — it relocates and
  enlarges it.** "Reserve the delete, free later" still needs `{slot removal +
  durable reservation}` to be atomic, plus new machinery: a crash-safe worklist, an
  idempotent re-free, and epoch/generation checks so a later pass cannot free a
  reused OOS slot. Vacuum already runs on the sysop infrastructure, so the
  synchronous approach is the minimum-cost correct design today.

- **The sysop is exactly the right tool.** It is CUBRID's mechanism for bundling
  several per-page log records into one atomic, durable unit, giving the multi-page
  removal all-or-nothing crash semantics.

## Consequences

- OOS reclamation is coupled to heap vacuum: they succeed or fail together, and a
  failed OOS delete aborts the whole record's vacuum for that pass.
- `REC_BIGONE` + OOS is currently unsupported and aborts loudly
  (`vacuum_heap_record`, `case REC_BIGONE`); if that combination becomes valid, an
  OOS-delete loop must be added there under the same sysop.
- If OOS reclamation ever needs to be decoupled (e.g. for vacuum throughput), the
  deferred design above is the path — and it must still make the
  `{slot removal + reservation}` step atomic.
