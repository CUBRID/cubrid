# Format-aligned Volmap integration (producer 06)

The opt-in integration driver extends the native controlled-state test through
an actual Volmap executable and its HTTP API. The producer remains based on
`e1e651debf6cc100172bde96603b17424f9c135a`; select `feat-oos` explicitly.
Wire compatibility does not establish develop disk-format compatibility.

## Reproduce

Build/install matching Debug and RelWithDebInfo CUBRID configurations with
`UNIT_TEST_PGBUF_INSPECTOR=ON`. Build the matching Volmap consumer at the chosen
revision. Supply a JSON file with explicit inputs:

```json
{
  "producer_source": "/absolute/CUBRID-source",
  "producer_build": "/absolute/CUBRID-build",
  "producer_install": "/absolute/CUBRID-install",
  "consumer_source": "/absolute/volmap-source",
  "consumer_binary": "/absolute/volmap-binary",
  "format_profile": "feat-oos",
  "listen_port": 47180,
  "output_directory": "/absolute/new-evidence-directory"
}
```

Use an unused loopback port, a new output directory, and a short `TMPDIR` on a
filesystem with room for the disposable database and replacement workload.
Run separately for each build mode:

```sh
TMPDIR=/short/private/test-root python3 unit_tests/pgbuf_inspector/controlled_observation.py \
  --fixture /absolute/CUBRID-build/bin/pgbuf_inspector_fixture \
  --volmap-run /absolute/run.json
```

The driver rejects mismatched producer/consumer corpus trees, a mismatched
fixture/build location, mismatched build/install ELF build IDs, missing dynamic
libraries, an unsupported format profile, and occupied HTTP ports. It records
commits, dirty patches, helper/binary hashes, CMake build provenance, actual
library resolution (installation rewrites RUNPATH), native acknowledgements, raw captures and HTTP responses.
The native VPID must belong to the inspected persistent volume set. Explicit
attachment remains on loopback; there is no endpoint discovery by the consumer.
The driver only discovers its own newly created fixture endpoint.

Without `--volmap-run`, the existing temporary-page controller and CTest entry
retain their original six checks. Consumer dependencies are not added to normal
engine builds or CTest. The companion external shell testcase also retains that
interface.

## Independent conditions and observation boundaries

The test-only executable accepts `--permanent`. It creates a private permanent
file and one page through normal file-manager APIs. No SQL/index owner knows
this page, and only the fixture can refix it. Temporary workload pages remain
in a separate file. Abort rolls back the private permanent allocation using the
normal recovery system operation; directly destroying a permanent file outside
that operation is invalid. The executable is not installed and adds no server
command or runtime control endpoint.

| Check | Independent condition | Actual consumer result |
| --- | --- | --- |
| Clean | Successful flush and retained native WRITE fix before/after HTTP | One resident row, dirty false, write latch and fix count 1 |
| Dirty | Native set-dirty under the same retained fix before/after HTTP | A later scan with dirty true and the same latch/fix meaning |
| Evicted complete | Capacity replacement, then native cache-only miss; workload cleanup excludes the target | Complete omission becomes not-resident, without page evidence |
| Evicted partial/shared | Native misses before/after; 512 other pages and controlled transport backpressure | Eight caller epochs share one connection and one real scan; omitted target is unknown |
| Non-server | Database creation runs with the parameter enabled | No inspector endpoint from the standalone utility |
| Shutdown | Bounded stop and normal rollback/temporary-file retirement | Successful fixture exit and removed producer socket |

For the partial/shared check only, a private byte-preserving Unix relay delays
reading after the actual scan header by 150 ms. This is a transport stimulus,
not a state oracle. It never constructs, changes or combines protocol frames.
The producer must actually emit a valid partial footer, or the test fails as an
unestablished precondition. The retained stream checks exact raw record count,
slot bound and incarnation/sequence framing. Every HTTP response must name the
same capture and its own epoch. Relay evidence does not substitute for direct
peer authentication, which the held and complete checks exercise separately.

All HTTP checks retain sanitized no-store responses and compare the ordinary
disk session before/after observation. The driver disables disk following to
keep that comparison within one immutable reading. Restarting the consumer for
the relay establishes a separate disk baseline; the test does not pretend that
fixture workload writes cannot alter disk contents. Native eviction and dirty
preconditions come from acknowledgements, never from returned observations.

The native latch/fix condition and consumer incarnation-scoped LRU bounds are
checked here. Exact packed latch/LRU decoding remains independently exercised
by the producer semantic unit cases; the browser's LRU rendering is a smoke
check, not an independent native-membership measurement. Missing optional page
header fields under a WRITE latch must not become fabricated page facts.

## Joined evidence and delivery scope

The [producer 06 ledger](verification/06/README.md) maps each ticket requirement
to the appropriate boundary. Real shipped-server browser checks remain a
separate suite in Volmap (`playwright.producer.config.ts`), with 12 required
cases per mode. It covers attachment, copied-volume refusal, omitted-parameter
startup, restart while paused, explicit retry, page/Volume/Sector presentation,
and independent tab pause/resume.

Offline corpus and socket tests establish malformed/count/duplicate/version
handling and all resource boundaries. Consumer tests establish age/expiry,
clock discontinuities, cancellation, scope authority, HTTP admission and no
cross-scan merging. These are real implementation tests with controlled peers
or clocks; they are not relabelled as native-engine browser experiments.

Producer 07 must port the completed producer to an isolated develop issue branch
and repeat the required debug/release checks. Windows execution and the full
platform matrix remain explicitly unproven. Performance, density and manual
accessibility release acceptance remain producer/Volmap ticket 08 obligations.
Producer 06 does not wait for completion of Volmap 07 and does not close it.
