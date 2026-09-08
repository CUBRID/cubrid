# Driver request histogram

Start thin csql with `communication_histogram=yes`. Histogram commands require
the authenticated user to belong to the DBA group for collection and access to
collected data. An inactive local collector still reports `OFF` to a non-DBA
after a refused `on`, as before. Without a target commands operate
on the current connection:

```text
;.hist on
SELECT * FROM a_table;
;.dump_hist
;.x_hist
;.hist off
```

`.dump_hist` preserves the collected data. `.clear_hist` resets it without
printing, and `.x_hist` prints before resetting. Starting collection after an
`off` begins a new interval. Repeating `on` preserves the current interval.

Use the `Session_id` from `SHOW SESSION STATUS` to inspect any connected driver
session, including JDBC and CCI. These commands use the existing csql session
command transport; drivers need no protocol changes:

```text
;.hist on session 123
;.dump_hist session 123
;.clear_hist session 123
;.hist off session 123
```

The request table counts completed, valid protocol requests by function. Thin
csql separates execution/rendering (`csql_execute`) from session commands
(`csql_session_command`). Ordinary driver functions such as prepare, execute and
fetch retain their actual function names. Histogram control/inspection requests
do not count themselves.

`Sent size` and `Recv size` are byte totals from the client's perspective,
including protocol framing and any streamed payload. They count successfully
transferred protocol bytes, not TCP/IP headers or TLS encryption overhead.
`Server time (s)` is accumulated elapsed processing time on the server: from
dispatch after reading the request through processing and automatic transaction
completion, before the final reply write. It includes lock and callback waits;
it is neither client RTT nor CPU time. The following execution-statistics section
uses the target connection's existing per-transaction-index perfmon counters,
preserving the existing counter/gauge meanings across commits.

For another session, a dump reads the last completed request snapshot. Enabling
or disabling takes effect at that session's next request boundary. A reset
concurrent with an in-flight request excludes that request from both counters
and execution statistics once it completes. Closed sessions disappear from the
registry; new connections start with collection disabled.

Detailed counter buffers are allocated only after DBA opt-in. Disabled sessions
do not read clocks or copy perfmon buffers. Enabled snapshots and administrative
updates synchronize through a per-session mutex; the registry lock only protects
session lookup/lifetime. Server-wide statdump and client-side process globals are
not used as substitutes for connection execution counters.

`unit_tests/server_compile/probe_histogram.py BROKER_PORT DBNAME` checks live
permissions, two-session isolation, reset semantics and wire byte equality against
an already running isolated database and broker. The caller owns their lifecycle.

`unit_tests/server_compile/measure_histogram.py BROKER_PORT DBNAME` measures seven
alternating on/off pairs of ordinary version requests on the same release binary.
It reports client elapsed time per request with median and range, separately from
the server's histogram time. This measures the optional collection cost for that
small-request workload, not an engine throughput regression against another build.
