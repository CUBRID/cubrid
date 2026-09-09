# Controlled native page observations

`unit_tests/pgbuf_inspector/pgbuf_inspector_fixture.cpp` is a test-only executable
linked to the normal server engine. Normal server boot starts the production
inspector daemon. The fixture adds no installed command, protocol message,
production endpoint, per-fix accounting or engine instrumentation.

The Python controller reads the shipped Unix socket. Its separate stdin/stdout
channel controls the workload and records native acknowledgements; inspector
responses never establish the fixture preconditions.

## Preconditions and checks

The fixture creates a private temporary file and allocates a known VPID with a
WRITE fix. Only the fixture thread knows and accesses this page. The page is not
unfixed during the resident-state observations.

| Case | Independent native condition | Required socket observation |
| --- | --- | --- |
| clean-held | Successful `pgbuf_flush_with_wal`, retained WRITE fix | Complete scan contains exactly one matching VPID, `dirty=false` |
| dirty-held | `pgbuf_set_dirty(DONT_FREE)` after writing a private canary, retained WRITE fix | Complete scan contains exactly one matching VPID, `dirty=true` |
| partial-held | Same retained fix, 512 additional private pages create output pressure | Valid partial footer; target remains independently held |
| evicted-complete | Target flushed/unfixed, capacity-driven replacement, native cache-only miss before and after scan | Complete omission supports observed nonresidency |
| evicted-partial | Same private allocated target remains absent, no workload can refix it | Partial omission remains unknown to an observer |
| shutdown | Controller sends stop and waits for successful native cleanup | Process exits successfully and socket pathname disappears |

Before and after each held observation, native `pgbuf_get_fix_count`,
`pgbuf_get_latch_mode` and `pgbuf_get_vpid_ptr` checks confirm the same page and
single WRITE fix. These checks execute in Debug and Release builds. A held WRITE
latch prevents replacement and another thread's page modification/flush during
the observation. The socket record must report that latch/fix state, omit unsafe
page-header fields, and never contain the private page-byte canary.

The controller bounds native acknowledgements to eight seconds (twenty-five seconds
for the replacement workload). The fixture
bounds the entire next stdin command, including incomplete input, to ten seconds.
A missing acknowledgement or failed precondition is a failed/inconclusive test;
it does not count as producer semantic evidence. Native cleanup retires the
temporary file using the normal temporary-file lifecycle.

The deliberate 150 ms read pause follows a received scan header and consumes the
producer's traversal budget under output pressure. It establishes no page-state
precondition. The testcase requires an actual `truncated=true` footer and fails
if that condition cannot be produced. Complete state assertions require an actual
complete footer. All captures check framing, sequence/incarnation, counts and
wire budgets before semantic assertions.

Eviction uses actual capacity-driven buffer replacement. The owner flushes and
unfixes the target, then allocates and flushes private temporary pages in batches
of 4096 (the configured buffer count). It checks for removal between batches,
with a 32768-page cap and a fifteen-second workload deadline checked between
allocations. No explicit invalidation or file deallocation removes the target. After the
native target miss, the owner removes only the other workload pages from cache,
with a separate five-second cleanup deadline. Native invalidation may decline a
protected victim; cleanup retries within that same deadline and succeeds only
when a cache-only native miss is observed. It rejects the target VPID from
that cleanup list. This reduces scan output enough for the complete capture to
finish within the unchanged producer budget. The cleanup uses native cache-only
fix/invalidate operations valid for these private temporary pages; it cannot
serve as the target eviction proof.
Failure to establish removal fails the testcase.

`pgbuf_fix(OLD_PAGE_IF_IN_BUFFER, READ, CONDITIONAL)` is the independent native
probe. A hit is unfixed; a null result counts only with `er_errid()==NO_ERROR`.
In `page_buffer.c`, this fetch mode returns immediately on a hash miss without
claiming a buffer or reading the page. The target remains allocated in the
fixture's private file, so deallocation cannot masquerade as eviction. A probe
hit may promote the target; checking only between whole-pool batches avoids
continually refreshing it during the workload.

Across the complete capture the single owner performs no allocations or loading
fixes. Before the subsequent partial capture it allocates 512 fresh private
pages solely to establish output pressure; none uses the still-allocated target
VPID. No SQL/session has access to this private page. Native misses
before and after each scan confirm the held absence condition. Complete and
partial captures both omit the target; the recorded wire conclusions are
`observed-nonresident` and `unknown`, respectively. The partial capture cannot
inherit the fixture's independent knowledge. This proves producer observation
semantics and does not implement or validate an external consumer's state model.

## Reproduction

Configure a Linux build with `UNIT_TEST_PGBUF_INSPECTOR=ON`, build it and install
its matching server libraries/utilities. With that installation's usual `CUBRID`,
`PATH` and `LD_LIBRARY_PATH` environment loaded, run:

```sh
python3 unit_tests/pgbuf_inspector/controlled_observation.py \
  --fixture BUILD/bin/pgbuf_inspector_fixture
ctest --test-dir BUILD -R '^test_pgbuf_inspector_native$' --output-on-failure
```

`BUILD` is the chosen CMake build directory. Python optimization must be disabled.
The controller creates a fresh database/temporary/configuration directory and
prints its evidence path. `synchronization.json`, each scan's JSONL, `native.log`
and `commands.log` remain there after the run. Keep the directory with the build
mode, source revision and binary hashes. Use a short temporary root with enough
space for a test database; `TMPDIR` selects that root.

The companion case lives in the actual external testcase repository at
`shell/_06_issues/_26_2h/cbrd_27398/cases/cbrd_27398.sh`. It runs this controller,
then independently runs `server_attachment.py --scan` against an unmodified
`cub_server` to check default-off absence, normal socket observations and restart.
Its README documents the standard CTP configuration and required matching-helper
environment. Ticket 05 requires both Debug and Release evidence, plus that
separate unmodified Release server run, before it can be completed.
