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

## Activation

Compilation succeeded. Installation was deferred by the local build workflow
because the target environment was running `demodb`. No live database process
was stopped by this work. The live viewer subsequently stopped accepting
connections, so live after-install coverage has not been claimed.

Standards and Spec review found no blocking code issues. The backpressure
coverage description was corrected to match resumed sampling during draining.
