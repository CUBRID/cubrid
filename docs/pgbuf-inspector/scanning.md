# Page-buffer inspector scans

After attachment, send the v1 `scan_request` with the negotiated incarnation.
Each accepted scan has an incarnation-scoped increasing sequence, a wall-clock
start header, resident page records, and an end footer with the exact emitted
record count, visited-slot count and explicit truncation. A repeated VPID counts
again and remains ambiguous. Only a valid footer makes a capture usable.
Complete omission is observed nonresidency; partial omission is unknown. Scans
are never merged and neither a record nor the pool is an atomic snapshot.

## Sampling policy

The implementation clarification accepted for ticket 03 permits unknown header
fields and partial coverage when safe sampling is unavailable. This uses the
frozen v1 optional-field semantics; it does not change the wire schema or corpus.

`make_scan_source` in `page_buffer.c` supplies producer-owned scalar observations.
A single nonblocking BCB mutex attempt protects the VPID from replacement. Failure
marks the scan partial; that slot is not retried. One atomic latch-word load
provides latch mode, waiter presence and fix count. One atomic flags load provides
all state bits and the LRU zone/index tuple. LRU membership is decoded against
the incarnation's shared/private topology, with private indices rebased after
shared indices and invalid membership mapped to invalid/null.

The BCB mutex alone cannot protect page-header fields from an existing fixed
holder. LSA and page kind are therefore read only when the sampled latch is none,
fix count is zero and flushing is false. Fix, promote, replacement and flush
admission require the BCB mutex; lock-free read fixing requires an existing READ
latch, so it cannot enter this idle state. Active holders' header fields are
omitted as unknown, without losing the safely observed residency record. Invalid
LSAs are omitted; the engine null pageid becomes JSON null. OOS's native page
kinds map through the wire-owned OOS vocabulary, never native ordinals.

The BCB/I/O-buffer tables and their links are initialized before inspector startup
and remain alive until its daemon is destroyed, before log/page-buffer teardown.
Relevant source anchors are `pgbuf_initialize_bcb_table`, `pgbuf_claim_bcb_for_fix`,
`pgbuf_latch_bcb_upon_fix`, `pgbuf_lockfree_fix_ro`, `pgbuf_set_lsa`,
`pgbuf_set_page_ptype`, `pgbuf_bcb_flush_with_wal`, and the inspector lifecycle
calls in `boot_sr.c`. Porting must revalidate these assumptions on the target base.

The mutex is released before encoding or socket I/O. Sampling performs no page
fix/load, page-image copy, disk read, DWB/TDE probe, list traversal, accounting
change or hot-path instrumentation. It directly uses the mutex rather than the
optional monitoring wrappers, so observation does not change their counters.

## Bounds and cancellation

The daemon streams a bounded queue per client (at most 64 KiB) and retains no
whole capture. Limits are 65,536 visited slots and emitted records, 1 GiB total
framed scan bytes, 4 KiB per record/control frame including newline, and depth 16.
A maximum control-frame allowance is reserved for the footer before admitting
each record. Limits may yield partial results; reaching a cap at full traversal
can remain complete. The next start advances past the visited span.

A global 100 ms floor applies to scan starts across both admitted clients.
Traversal/serialization expires after 1,500 ms elapsed, checked between slots and
including output backpressure. Pending writes expire after 250 ms without
progress; the whole exchange expires after 2 s, including the footer/drain.
Socket operations never block, and each polling turn bounds traversal work to
approximately 2 ms to allow prompt shutdown. These are scheduling-aware limits,
not real-time guarantees. Disconnect, unexpected in-flight input, timeout or
shutdown cancels the capture and releases client resources; no successful footer
is invented for a broken stream.

## Verification

The configured `test_pgbuf_inspector` executable covers deterministic encoded
samples, topology boundaries, empty/complete/partial scans, duplicate counts,
slot/record caps, footer reservation, rotation and time boundaries, plus real
Unix-socket rate limits, framing, cancellation and stalls. Credential cases use
two real mapped UIDs. The existing v1 conformance corpus remains unchanged.

Run the installed debug or release server harness with:

```sh
python3 unit_tests/pgbuf_inspector/server_attachment.py --scan
```

This verifies nonzero real resident observations and valid bounded footer
completion. It does not establish controlled dirty/eviction behavior, oversized
real-pool fairness or the later cross-repository performance/release gates.
