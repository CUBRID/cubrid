# CBRD-27074: Removing fixed delays from `csql -S` PL startup

Date: 2026-07-15
Branch: `csql-sa-pl-startup`
Baseline commit: `4caf6b952701f13b1b6f08cfb1ae04f98b38dad8`

## Executive summary

`csql -S` paid about two seconds of fixed delay while starting and stopping the Procedural Language (PL) server:

1. the standalone monitor probed a stale PL endpoint and then slept for one second;
2. the parent slept for another second immediately after forking `cub_pl`;
3. the Linux `cub_pl` process checked its parent once per second, which did not block successful startup but delayed child cleanup after `csql` exited.

The sleeps were synchronization workarounds added across several PL lifecycle fixes. They are no longer needed when synchronization is expressed directly:

- readiness is now detected by polling the child-specific PL info and ping endpoint with short exponential backoff;
- Linux asks the kernel to kill `cub_pl` when its creating parent exits by setting `PR_SET_PDEATHSIG` before `exec`;
- the post-fork sleep and the standalone preflight sleep are removed;
- Linux `cub_pl` blocks in `pause()` instead of waking every second to compare PPIDs.

The measured cold `csql -S` PL call improved from approximately 2.51 seconds to a 0.50-0.55 second range (0.527-second mean over ten runs). That removes about 1.98 seconds, a roughly 79% reduction or 4.8x speedup for this test. Thirty rapid standalone pairs, ten TCP-mode standalone runs, server restart and shutdown tests, abnormal server termination, invalid-JVM cleanup, and the CBRD-25908 shell regression all passed.

## Scope and conclusion

The report answers two separate questions that were initially conflated:

1. **Why is `csql -S` slow?** Two one-second sleeps in the standalone monitor's critical path account for almost all avoidable latency.
2. **Why does `cub_pl` contain `sleep(1)` in `main()`?** It polls for a parent-PPID change so the child terminates with the DB process. It is not the main startup delay, but it is another timing workaround and delays orphan cleanup by up to one second.

The correct change is therefore not a bare deletion of `sleep(1)`. The readiness sleeps must become condition-driven retry, and the Linux lifetime sleep must become a kernel-enforced parent-death relationship.

## Observed behavior before the change

### Cold standalone timeline

Tracing a cold PL function invocation showed this sequence:

```text
csql -S process
  |
  | do_check_state(STOPPED)
  |   `- do_check_connection(1)
  |        |- ping stale/nonexistent PL endpoint -> failure
  |        `- sleep 1 second                       [fixed delay #1]
  |
  | fork cub_pl
  |   |- child starts JVM
  |   `- parent sleep 1 second                     [fixed delay #2]
  |
  | ping/bootstrap PL server
  `- execute SQL and exit

cub_pl process
  `- compare current PPID with initial PPID
       `- sleep 1 second between comparisons       [cleanup delay]
```

The JVM itself became ready in roughly 0.39 seconds on the test host. The parent nevertheless waited the full fixed second after `fork`, so a faster JVM could not produce a faster startup.

### The standalone preflight always slept after a miss

The old call `do_check_connection(1)` was intended to determine whether a PL process from a previous standalone invocation was still shutting down. Its counter-based loop performed a ping and then slept for 1,000 ms after a failed first attempt. A normal cold invocation has no reusable PL endpoint, so it paid this sleep on every launch.

The state check only needs one immediate observation. Waiting here does not make the new child safer because the subsequent monitor already owns the retry and initialization logic.

### The post-fork sleep was readiness by elapsed time

`server_monitor_task::do_monitor()` assigned the child PID and called `sleep(1)` before entering initialization. This was added by CBRD-25796 when PL startup was moved earlier in DB boot to avoid `fork()` failure after large allocations. It gave the child time to initialize, but did not establish that any particular child state was true after one second.

The later `do_check_connection()` already represented the real condition: the child must publish its endpoint and answer a PL ping. The fixed sleep duplicated that wait at coarse resolution.

### The `cub_pl` main-loop sleep was a lifetime workaround

On Linux, `cub_pl` saved its initial PPID and checked once per second whether the PPID had changed. CBRD-25931 generalized the older `getppid() == 1` check for WSL, where reparenting behavior can differ.

This polling loop had two properties:

- it ran concurrently with the JVM server, so it was not the one-second parent startup delay;
- it detected parent death zero to one second late and woke for the entire process lifetime.

The requirement is event-driven: terminate the PL process when its creating DB process exits. Linux already exposes that relationship through `prctl(PR_SET_PDEATHSIG, ...)`.

## Relevant change history

| Ticket | Commit | Intent | Relationship to this change |
|---|---|---|---|
| CBRD-25660 | `362c4cbe1` | Make the PL server dependent on, and started with, the DB server | Defines the parent-child lifetime invariant that must be preserved |
| CBRD-25712 | `02d864a4b` | Keep an abnormal/dummy PL process when JVM initialization repeatedly fails | Prevents treating every non-listening child as a simple process-start failure |
| CBRD-25796 | `52f22607b` | Start PL before allocation-heavy DB boot and handle `fork()` failure | Introduced the post-fork one-second sleep as a stabilization workaround |
| CBRD-25908 | `8c8e8cbb3` | Prevent an SA utility from terminating the PL server of a running DB | Requires stale/current PL identity to remain correct across SA execution |
| CBRD-25925 | `e9d408e21` | Retry SA startup when a previous PL process is still shutting down | Requires readiness retry, but not fixed one-second polling |
| CBRD-25931 | `17975b365` | Terminate Linux/WSL PL when its initial parent changes | Introduced PPID polling in `cub_pl`; replaced here with a Linux kernel event |

The sleeps were reasonable incremental safeguards when these lifecycle fixes landed. The current code now has enough identity and endpoint information to replace them with explicit conditions.

## Required invariants

The fix preserves the behavior established by the tickets above:

1. Every `cub_pl` launched by the DB process is owned by that creating parent.
2. Normal or abnormal parent termination must not leave a PL/JVM process behind.
3. The monitor must not connect to a stale endpoint published by an earlier PL PID.
4. UDS and random TCP endpoint publication must both work.
5. A successfully forked child whose JVM fails may remain as the intentional abnormal/dummy process until the parent or operator terminates it.
6. `cubrid pl restart` must replace the PL PID and restore PL execution.
7. SA execution while a CS server is active must not terminate the server-owned PL process.
8. Windows behavior and non-Linux Unix fallback behavior must remain defined.

## Implemented design

### 1. Make Linux child lifetime explicit before `exec`

`create_child_process()` now accepts `set_parent_death_signal`. The PL monitor passes `true`; no other caller exists.

In the Linux child, immediately after `fork()` and before file redirection or `exec`:

```text
parent captures getpid()
  `- fork()
       `- child: prctl(PR_SET_PDEATHSIG, SIGKILL)
            |- failure -> _exit(EXIT_FAILURE)
            |- parent already changed -> _exit(EXIT_SUCCESS)
            `- exec cub_pl
```

The post-`prctl` `getppid()` comparison closes the standard process-exit race: if the parent exits between `fork()` and `prctl()`, Linux does not retroactively deliver the configured signal, so the child must detect that case itself.

The configuration is applied before `exec` because `PR_SET_PDEATHSIG` is preserved across an ordinary `execve()`. See the Linux [`PR_SET_PDEATHSIG` documentation](https://man7.org/linux/man-pages/man2/PR_SET_PDEATHSIG.2const.html).

`SIGKILL` is deliberate:

- the lifetime contract is strict; the PL JVM must not outlive the DB process;
- the existing `SIGTERM` handler has PL-info-dependent behavior and can return without exiting when the info file changes;
- the previous PPID-change path returned directly from `main()` without attempting JVM shutdown, so this does not remove an existing graceful-cleanup guarantee.

### 2. Replace the parent post-fork sleep with readiness polling

The monitor no longer sleeps after assigning `m_pid`. It proceeds directly to `do_initialize()`, which waits up to ten seconds for the actual readiness condition.

The new loop uses `std::chrono::steady_clock` and backoff intervals of:

```text
10 ms -> 20 ms -> 40 ms -> 80 ms -> 160 ms -> 320 ms -> 500 ms (capped)
```

Each iteration:

1. initializes a fresh `PL_SERVER_INFO` value;
2. reads the PL info file;
3. requires a successful read and a non-disabled PID; initialized sentinel values make a partial file read fail safely;
4. after a fork, rejects info whose PID does not equal `m_pid`;
5. creates or updates the connection pool from that same endpoint snapshot;
6. sends the existing PL ping;
7. returns immediately on success or if the child has terminated;
8. otherwise sleeps only until the next backoff interval or the deadline.

Reading PID and port into one initialized snapshot avoids mixing identity from one file read with a port from another. The PID equality check is especially important in TCP mode because a previous random port can remain in the info file briefly.

The overall initialization budget remains ten seconds, matching the old ten one-second attempts. Failure semantics are therefore preserved while successful startup becomes responsive to actual readiness.

### 3. Make the SA stale-process probe non-blocking

The SA `STOPPED` state now calls `do_check_connection(0)`. A zero timeout still performs one immediate info read and ping, but never sleeps.

This preserves the CBRD-25925 state distinction for a previous PL process that is still reachable. A normal cold start proceeds immediately to `fork`.

### 4. Remove Linux PPID polling from `cub_pl`

Linux `cub_pl` now calls `pause()` in its lifetime loop. Parent death is delivered as `SIGKILL` by the kernel, so no periodic PPID check or timer is required.

Platform handling is:

| Platform | Parent-death mechanism |
|---|---|
| Linux, including WSL kernels that support `prctl` | `PR_SET_PDEATHSIG(SIGKILL)` configured before `exec`; `cub_pl` blocks in `pause()` |
| Windows | Existing process-handle wait; invalid handles and `WAIT_FAILED` now terminate the wait loop safely |
| Other Unix | Existing initial-PPID comparison and one-second polling retained as fallback |

## Files changed

### `src/base/process_util.h`

- Extends the internal child-process API with the parent-death option.

### `src/base/process_util.c`

- Includes `<sys/prctl.h>` for Linux builds.
- Configures `PR_SET_PDEATHSIG` in the child before `exec`.
- Checks for parent exit in the `fork`/`prctl` race window.
- Leaves Windows and other Unix behavior unchanged when the option is not supported.

### `src/sp/pl_sr.cpp`

- Removes the unconditional post-fork `sleep(1)`.
- Makes the SA stale-process probe immediate.
- Replaces counter-based one-second retry with deadline-based exponential backoff.
- Validates the published PID against the child PID.
- Builds the ping connection from one consistent PL-info snapshot.

### `src/executables/pl.cpp`

- Replaces Linux PPID polling with `pause()`.
- Retains the polling fallback for non-Linux Unix.
- Handles Windows parent-handle acquisition and wait failures without looping indefinitely.

No SQL syntax, catalog, storage, wire-protocol, or Java PL implementation changes are involved.

## Race and failure analysis

### Parent exits before the child configures the death signal

The child compares `getppid()` with the PID captured before `fork`. A mismatch exits before `exec`, preventing an orphan.

Linux defines the "parent" for `PR_SET_PDEATHSIG` as the thread that created the child. SERVER mode creates one dedicated `pl-monitor` daemon thread in `server_manager::start()` and normally destroys it during `server_manager` teardown; the main execution thread owns the SA lifetime. If the server architecture later replaces that daemon thread while keeping the process alive, the PL parent association must be revisited. The narrow case where the creating thread disappears before `prctl` while the containing process remains alive is not distinguishable by `getppid()`; it remains a theoretical Linux thread-level race, not observed in the lifecycle tests.

### PL info file contains a previous PID or endpoint

After `fork`, readiness requires `pl_info.pid == m_pid`. The monitor does not ping or adopt the endpoint until the current child publishes it. This prevents a stale UDS owner or random TCP port from being accepted as the new child.

### Child exits during readiness polling

Each failed iteration checks `is_terminated_process(m_pid)` and returns immediately. The monitor state machine retains its existing retry/failure policy.

### JVM library cannot load

The child still publishes its abnormal state and remains alive according to CBRD-25712. The monitor continues using its existing retry policy. If the SA parent is externally terminated, the kernel kills the dummy child immediately; a later invocation can start normally.

### Parent exits normally or receives `SIGKILL`

Both cases cause the Linux kernel to deliver `SIGKILL` to `cub_pl`. This was verified for normal utility shutdown and direct abnormal server termination.

### Endpoint is initially present but not listening

The monitor retries. A TCP trace observed refused connections to the initially published port, followed by successful connections to the ready random port. No fixed delay or stale-PID acceptance is needed.

## Verification

### Build and static checks

| Check | Result |
|---|---|
| Project code formatter on all four changed source/header files | Pass |
| `git diff --check` | Pass |
| Debug GCC build and install, including SA, CS, SERVER, `cub_pl`, and Java PL artifacts | Pass |
| Unit-test build/install gate | Pass |
| CTest discovery | No tests registered in this build tree |
| Existing local SQL build-test smoke gate | Pass |

The final reinstall completed successfully after the CTP run restored the active installation from project configuration.

### Standalone functional and performance checks

Test database function:

```sql
SELECT tf();
-- 42
```

| Scenario | Result |
|---|---|
| Cold UDS PL invocation, 10 runs | 0.50-0.55 s; mean 0.527 s; all returned 42 |
| Rapid SA cycle: `SELECT 1` followed by `SELECT tf()`, 30 pairs | 60/60 commands passed; no leftover `testdb` PL process |
| TCP PL transport, 10 cold runs | 10/10 passed; trace confirmed `AF_INET` endpoint retry and connection |
| Invalid `CUBRID_JAVA_HOME`, externally limited to 15 s | Expected timeout (exit 124); no PL process left behind |
| Normal invocation after invalid-JVM test | Passed and returned 42; no PL process left behind |

Ten measured UDS cold runs:

```text
0.50  0.51  0.54  0.53  0.55
0.54  0.53  0.53  0.52  0.52 seconds
```

### Process-level trace

The post-change trace showed:

```text
child:  prctl(PR_SET_PDEATHSIG, SIGKILL) = 0
child:  execve(.../cub_pl, ["cub_pl", "testdb"], ...)
child:  pause()
parent: exited with 0
child:  killed by SIGKILL
```

The traced parent exited at `1784100573.030312`; the traced PL main process was reported killed at `1784100573.037552`, about 7.2 ms later under `strace`. The trace contained no one-second sleep in the Linux startup/lifetime path.

### SERVER-mode lifecycle checks

The server tests were isolated on a temporary unused master port because three unrelated local CUBRID installations were already using the default port. The temporary configuration was restored afterward.

| Scenario | Result |
|---|---|
| Initial server PL call | Passed; returned 42 in 0.11 s client elapsed time |
| `cubrid pl restart testdb` | Passed in 2.049 s; PL PID changed; next call returned 42 |
| Normal `cubrid server stop testdb` | Passed in 1.029 s; child absent when the command returned |
| Direct server `SIGTERM` cleanup | Server and child gone within 54 ms |
| Server `SIGKILL` | Old PL child gone within 12 ms |
| Master automatic server recovery after `SIGKILL` | New server PID and PL PID created; subsequent PL call returned 42 |

### Existing shell regression

The CTP CBRD-25908 case was run against the installed build:

```text
Test Category: shell
Total Case: 1
Total Execution Case: 1
Total Success Case: 1
Total Fail Case: 0
Elapsed: 16 seconds
```

This case verifies that an SA-mode utility does not terminate the PL server owned by a running DB server, then verifies a PL function through CS mode.

## Before/after result

| Metric | Before | After | Change |
|---|---:|---:|---:|
| Cold `csql -S` PL invocation | ~2.51 s | 0.527 s mean | ~1.98 s removed; ~79% faster elapsed time |
| Parent post-fork minimum wait | 1,000 ms | 0 ms fixed; readiness-driven | Fixed delay removed |
| SA stale-process miss wait | 1,000 ms | One immediate probe | Fixed delay removed |
| Linux parent-death detection | 0-1,000 ms polling delay | Kernel event; 7-12 ms observed under trace/polling | Periodic wakeup removed |

The remaining roughly half-second is dominated by process launch and JVM initialization. Further reduction would require a different PL lifetime or JVM reuse design and is outside CBRD-27074.

## Limitations and reviewer focus

1. **Linux-specific lifetime primitive**: `PR_SET_PDEATHSIG` is used only under `LINUX`. Non-Linux Unix retains the old one-second fallback.
2. **Creating-thread semantics**: Linux associates the death signal with the thread that called `fork`, not an abstract process owner. The current persistent monitor-thread model is compatible, but this should remain visible in review.
3. **Hard termination**: `SIGKILL` intentionally favors the no-orphan invariant over JVM cleanup. This matches the previous parent-loss behavior, which did not perform an orderly JVM shutdown.
4. **Windows runtime**: Windows error handling was made finite, but this Linux host did not provide a Windows runtime test.
5. **Failure-duration policy**: The full repeated invalid-JVM monitor exhaustion was not awaited; the test used a 15-second external limit to verify child cleanup and subsequent recovery. The existing intentional abnormal-child policy is unchanged.
6. **Automated unit coverage**: the build tree currently registers no CTest cases for this process-lifecycle path. Runtime coverage therefore comes from targeted process tests and CTP CBRD-25908.
7. **Branch freshness**: at report time, the baseline branch was nine commits behind `origin/develop`. The change should be rebased or merged with the current base before final publication and CI.

Primary review points:

- confirm `SIGKILL` is the desired Linux parent-death signal for the PL ownership contract;
- confirm the server monitor task's creating thread remains persistent for the server lifetime;
- review the fork-to-`prctl` race handling and child-only placement before `exec`;
- review PID/port snapshot validation for UDS and random TCP modes;
- confirm the ten-second total readiness budget preserves intended failure behavior.

## Final status

The fixed-delay removal plan is implemented and locally verified. The source change is limited to four files, formats cleanly, builds in all Linux CUBRID modes, passes targeted lifecycle checks, and passes the directly related CBRD-25908 shell regression.

Suggested PR title:

```text
[CBRD-27074] Remove fixed delays from standalone PL startup
```
