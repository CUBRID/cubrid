# Overload and lifecycle verification

The producer's isolation checks use the production `endpoint` implementation
over real Unix stream sockets, with controlled scalar sources and clocks where
exact boundary conditions are needed. The installed-server check also exercises
unmodified `cub_server` with two stalled scan clients while SQL commits and excess
connections receive `busy`. Run both layers in Debug and RelWithDebInfo builds.

```sh
ctest --test-dir BUILD_DIRECTORY --output-on-failure
BUILD_DIRECTORY/bin/test_pgbuf_inspector '[socket]~[.credential]'
unshare --map-auto --map-root-user BUILD_DIRECTORY/bin/test_pgbuf_inspector '[.credential]'
python3 unit_tests/pgbuf_inspector/server_attachment.py --scan --isolation
```

Select the installed build through `CUBRID`, `PATH` and the library environment
before running the Python harness. Its isolated registry, configuration and
master port prevent interference with other databases. `TMPDIR` must provide
sufficient space and a short Unix-socket pathname. Evidence stays in the printed
directory: `commands.log`, `isolation-worker.log`, `isolation.json`, and captures.
The isolation workload disables string compression in that disposable database
so that repeated test strings establish enough resident pages for backpressure.
It requires an independently drained capture larger than 128 KiB, two accepted
scan headers, two live sockets, successful SQL update/commit/readback and `busy`
refusals during the same interval. Linux `ss` socket diagnostics must identify
both accepted server sockets by peer inode and show charged send memory at or
above their send-buffer budgets both before and after SQL. Install `ss` from
iproute2; unavailable diagnostics are missing evidence. Failure to establish these preconditions is
inconclusive evidence and exits unsuccessfully.

## Requirement mapping

| Requirement | Executable evidence |
| --- | --- |
| Two admitted clients and excess demand | `Admission overload leaves both admitted clients usable` checks 128 refusals and subsequent scans on both clients; installed-server isolation records refusals during committed SQL |
| Global 100 ms floor | `The scan floor applies across both clients and preserves increasing sequences`: 99,999 / 100,000 / 100,001 microseconds; installed server also uses sequential accepted scans |
| Fragmentation, coalescing, malformed requests and EOF | `Fragmented controls...`, `Malformed scan controls...`, `Controls cannot exceed...`, negotiation and premature-request cases; coalesced requests violate the sequential request contract and close without starting a scan |
| 4 KiB frames, 64 KiB handshake, depth 16 | Socket hello and scan boundary cases plus `Frame and depth limits hold below at and above their boundaries`; complete identity or explicit refusal is exercised over real I/O |
| 65,536 records/slots, 1 GiB scan and 1,500 ms traversal | Deterministic scan and wire boundary cases at limit minus one, limit, limit plus one; lower scan-byte budgets isolate footer reservation since ordinary scalar frames do not reach 1 GiB before the record cap |
| 64 KiB output and footer under backpressure | `Backpressure bounds queued bytes and reserves a truthful footer` establishes sampling saturation, accounts generated wire bytes minus bytes in the kernel, then resumes sampling as output drains and validates a partial footer |
| 250 ms write stall | Real stalled handshake/scan tests plus `Write stall deadline...` at 249,999 / 250,000 / 250,001 microseconds; incomplete bytes never publish a capture |
| 500 ms handshake | Real idle greeting expiration and delayed metadata tests, plus `Handshake deadline...` at 499,999 / 500,000 / 500,001 microseconds; incoming fragments cannot reset the deadline |
| 2 s whole exchange | Deterministic before-write checks at 1,999,999 / 2,000,000 / 2,000,001 microseconds; `Real slow draining...` establishes saturation before 100 ms and observes at least ten real write-progress events with gaps below 250 ms, then closure at 2 s without publication |
| Disconnect and shutdown cancellation | Sampling stops after disconnect/shutdown; repeated lifecycle tests check peer EOF, bounded stop and unchanged descriptor count; real shutdown closes both stalled exchanges and removes its own socket |
| Exact UID, no root/group exceptions | Explicit credential suite requires two mapped UIDs; the different-UID child retains the server group; root connecting to a non-root endpoint is refused before data exchange |
| Filesystem safety | Unsafe directory modes, symlinks, regular files, wrong-owner sockets/directories, active sockets, confirmed stale sockets and backlog-full `EAGAIN` probes; checked identities remain preserved |
| Replacement, failed activation, restart | Endpoint and installed-server pathname replacement; SQL remains usable after failed activation and after removing the conflict without rebinding; restart changes incarnation, retains persistent identity and refuses an old expectation |
| Sanitization and capture validity | Canonical wire corpus rejects malformed/count/sequence/incarnation/EOF captures; actual scan framing is validated before publication; stable refusals carry no paths or OS error text |

The output accounting test measures queued wire bytes, not allocator/RSS overhead
or the separate kernel send buffer. The 2 s real-I/O test checks its timing
preconditions explicitly; a heavily delayed run fails instead of turning a stall
timeout into whole-exchange evidence. Deadlines remain scheduling-aware bounds.

## Page protection and evidence boundaries

The native adapter in `page_buffer.c:make_scan_source` uses one
`pthread_mutex_trylock`, never waits for page protection, and returns unavailable
on contention. Every successful lock path unlocks before returning owned scalar
values. The transport then serializes and writes those values. The deterministic
unavailable-slot and cancellation cases complement this source-level argument;
the installed-server workload proves SQL progress and shutdown during real
backpressure. This does not claim a controlled native held-mutex experiment or
the known-page dirty/eviction oracles owned by ticket 05.

Socket absence alone remains unavailable attachment evidence. The harness knows
the parameter setting because it writes the startup configuration and checks the
stable activation diagnostic; it does not infer `parameter-off` from absence.

Preserve source commit/tree, build type, binary hashes, corpus revision/hash,
commands, named executed counts, credential maps, preconditions and raw logs in
the gate manifest. Keep failed or inconclusive attempts alongside passing final
runs. These checks do not discharge the external testcase, consumer integration,
develop-port or dedicated-host performance gates.
