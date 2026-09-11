# Longer traversal budget validation

The live Volume view previously received only a few sectors of resident pages
because its producer cut traversal off after 100 ms, including serialization
and socket backpressure. The user requested a longer scan budget. The producer
now allows 1,500 ms of traversal, below the existing two-second whole exchange
limit. Scan-start cadence, 250 ms write-stall cancellation, two-millisecond
poll work slices, queue size, slot/record caps, and memory ownership are unchanged.

## Evidence

- `socket-before.log`: the production Unix socket path with 32,768 slots and
  4,096 resident pages stops after 584 records under the old budget. Its virtual
  scheduler advances five milliseconds per polling turn; delivery takes 115 ms.
- `socket-after.log`: the same case returns all 4,096 resident pages and visits
  all 32,768 slots in 900 ms of virtual polling time. This is deterministic
  scheduling evidence, not a wall-clock throughput benchmark.
- `scan-socket-tests.log`: all 37 selected scan/socket cases pass, including
  traversal boundaries, the whole exchange deadline, output stalls and cancellation.
  The separately privileged credential testcase is outside this selection.
- `native-tests.log` and `native-timing.json`: the actual native engine fixture
  with a 64 MiB pool completes its populated scan (565 resident records, 4,096
  visited slots) in 296.774 ms. A throttled reader still produces a validated
  partial footer. Nonresidency is established only from complete scans.

The native fixture was run with the newly built `build_preset_debug_gcc/cubrid`
library first in `LD_LIBRARY_PATH`; its default runtime path otherwise selects
the older installed library. Raw native captures are retained alongside their
summary. These are debug-build correctness measurements on controlled fixtures;
they do not establish completion for every pool size, contention level or host.

The old partial fixture used a single 150 ms read pause. That now completes,
so partial-coverage tests sustain periodic 120 ms pauses after each 8 KiB read
until the traversal budget elapses, while preserving write progress within the
250 ms stall limit. Both direct and Volmap relay helpers use this mechanism.
The direct native path passed; the full provenance-pinned cross-repository
Volmap qualification was not rerun here.

## Live activation and verification

The tested build was installed after the user closed the standalone csql
session. Installed and built `libcubrid.so` ELF build IDs match:
`3a367c1270296dedcbfc1918ede57cae766658a8`.
Installation restored the stock cubrid.conf, so `enable_pgbuf_inspector=yes`
was restored before the final restart. The managed start recipe also passed
its runtime lock into the daemon, blocking managed stop on its own server;
direct graceful CUBRID stop/start completed the restart. The new server
published the expected private inspector socket and accepted SQL connections.

A read-only `select * from pgbuf_lru_demo` populated the existing demo table's
buffer pages. The live HTTP Volume requests in `live-scans.json` then measured:

| Sectors requested | Producer time | Resident | Not resident | Partial omissions |
| --- | ---: | ---: | ---: | ---: |
| 0–63 | 577.284 ms | 610 | 3,486 | 0 |
| 64–127 | 577.352 ms | 0 | 4,096 | 0 |
| 0–63, repeat | 577.060 ms | 610 | 3,486 | 0 |

All three captures report `producer_complete: true`, with 4,096 requested and
4,096 evaluated pages each. Requests for the two halves use separate captures;
they are not combined into a single observation. The original live symptom
(roughly 100 ms scans with thousands of partial omissions) no longer occurs
in these warmed-workload checks. Nonresident pages have no current LRU zone;
complete coverage does not imply that all disk pages are in the buffer pool.

Standards and Spec review found no blocking code issues. The backpressure
coverage description was corrected to match resumed sampling during draining.

The live Chromium viewport check (`browser-check.json`, [screenshot](live-volume.png))
received two complete 1,536-page viewport captures, rendered 13 resident cells
with `1S`/`3P` labels, and rendered no unknown glyphs. This viewport covers
fewer sectors than the explicit 64-sector HTTP measurements above; its resident
count is not a whole-volume count.
