# Producer 06 evidence ledger

Recorded 2026-09-11 on Linux x86-64. Review baseline is producer
`f8c068f771ccd141a3c2f08541fe75c84e41ce53`, tree
`890d5c4c6e505ef93f55abb86d66bfe72e136420`, descended from the agreed
`e1e651debf6cc100172bde96603b17424f9c135a` format pin. New work changes the
native test fixture/controller and integration documentation, not the shipped
producer, wire contract or consumer application.

Consumer source for new accepted runs is an isolated checkout of
`5dacafbb248600fd6a27b6aae8b5a1655cfa12c9`. Its locked debug/release builds and
full verification log are retained. The original Volmap worktree has unrelated
uncommitted changes allowing explicit LAN listeners, which conflict with this
ticket's loopback-only boundary; those changes were preserved and are not used
for the final acceptance runs. Generated screenshots are the isolated checkout's
only dirty tracked files after verification.

## Execution summary

Debug and RelWithDebInfo each passed 28/28 CTest entries (61 inspector cases
and six default native checks), 11 native/actual-consumer integration checks,
and 12/12 real shipped-server browser cases (six per Chromium/Firefox).
The actual shared partial scans emitted 214 debug / 219 release raw records;
each served eight HTTP caller epochs through one producer connection/request.

The isolated consumer full gate passed Rust tests, Clippy, static-musl artifact
checks, 73 frontend tests across eight files, and 47 browser cases with one
existing skip. Exact Rust result totals and browser statistics are in
`manifest.json`. No skip is promoted to passed producer evidence.

Standards review found zero blocking violations and one nonblocking suggestion
to share the short HTTP request-object construction. Spec review found zero
implementation defects or scope deviations. Fresh CTP replay also passed one case per mode, zero failures/skips, with
current fixture/helper hashes and the exact external testcase revision captured
in each child process. Producer 06 is complete.

## Requirement mapping

| Producer 06 requirement | Evidence boundary |
| --- | --- |
| Explicit revisions/builds/database/consumer, matched format | `debug-verified/inputs.json`, `release-verified/inputs.json`, build/install and library-resolution logs; fresh private databases, `feat-oos` only |
| Identical independently tested corpus | All 109 files compared at every native/browser preflight; version 1.0 revision 2, aggregate SHA-256 `11dbecc72e4b9dd78e22080f138c801c189c7e047c0db23af8233f44a804d5ce`; producer encoder unit cases and consumer decoder's 39 canonical exchanges run independently |
| Mutual peer/persistent identity, copied/version refusal, sanitized explicit attachment | New direct HTTP native checks and shipped-server browser suite; consumer exact-UID/identity-component/private-mode tests; prior isolated producer credentials (4 cases/41 assertions per mode); real version/refusal transport tests and actual consumer decoder/refusal tests |
| Known VPID, coherent semantics, coverage/count/duplicates/malformed/no merging | Native permanent VPID `1:577`, retained WRITE fix, native cache miss after replacement; actual consumer direct clean/dirty/nonresident and relay partial unknown. Eight caller epochs share one actual producer scan. Producer semantic unit cases separately prove packed latch/LRU mappings; consumer corpus/lifecycle tests prove ambiguous duplicates, strict raw counts, discard and no merged absence |
| Restart/age/stall/pause/hidden/clock/cancellation/disk independence | New unmodified-server browser restart/pause/retry/tab tests and ordinary disk navigation; native HTTP disk-session equality; full consumer gate's real-socket deadline/retained-age/expiry/cancellation tests and controlled clock/hidden-browser checks |
| One shared connection/scan, bounded allocation/admission/scheduling | Native relay records exactly one connection and request for eight actual HTTP callers; producer unit budget cases; consumer eight-waiter/ninth-refusal, accounted owner memory, HTTP scope/admission and scheduler tests. HTTP race repair is already in 5dacafb, with historical 500-pass evidence audited |
| Separate debug and release actual integration/counts/raw logs | New native HTTP and shipped-server Chromium/Firefox runs per mode, alongside producer CTest and independent consumer gate. No develop execution is claimed |
| Ownership and handoff | Producer fixture changes here; consumer application remains at its existing pin. Producer 07 follows producer 06; Volmap 07 is not a predecessor and remains open for develop/external obligations |

Tests with controlled peers/clocks are not native-engine/browser measurements.
The byte-preserving relay changes transport pacing, not protocol fields. Direct
held-state cases authenticate the producer itself. The browser's `0:0` and LRU
rendering checks remain smoke coverage; the independently held permanent target
is established by the native controller and adopted by actual consumer HTTP.
Native tuple mapping is proved by semantic unit cases, not inferred from CSS.

## Audited prior evidence and prerequisites

`history-audit.json` records 142 successful checksum checks from four retained
Volmap evidence directories. Read their README/manifest and raw compressed logs
at the recorded absolute paths. Earlier failures remain historical; the later
`07-http-admission` causal repair supersedes old unresolved-HTTP wording.
All old debug/release producer engine hashes matched at preflight. The original
debug consumer binary did not match its historical browser hash, prompting the
isolated rebuild and new browser executions above.

Producer tickets 04/05 are complete at `e5f3cdf`/`f8c068f`. Their manifests are at
`/home/vimkim/temp/volmap/.scratch/pgbuf-producer-implementation/verification/{04,05}`.
The audited follow-up retains shipped-server SQL-progress/isolation checks and
both exact-UID environments. This new fixture mode does not invalidate those
unchanged transport/engine results.

External testcase checkout:
`/home/vimkim/gh/tc/CBRD-27398-pgbuf-inspector-fixtures`, revision
`a648d78f599504fe0c916628ea51ea3f2b5f7ca7`, case
`shell/_06_issues/_26_2h/cbrd_27398/cases/cbrd_27398.sh`.
September 9 CTP executed one successful case per mode, zero failures/skips.
Those are historical invocations at the old fixture/controller hashes. Fresh
September 11 CTP runs in `ctp-debug/` and `ctp-release/` separately prove the new
default temporary fixture/helper behavior and unmodified-server attachment.
Each child identifies the same testcase revision and current engine/helper
hashes; no historical invocation is relabelled as a fresh run.

## Evidence boundaries retained for the next tickets

Develop port, Windows execution, remaining platform configurations and full
release performance/manual-accessibility gates are unproven here. Linux normal
Debug/RelWithDebInfo server builds, non-server database creation with no endpoint,
and the format-aligned functional consumer integration are the tested scope.
This is not whole-feature release approval and does not close Volmap 07 or 08.

Failed attempts are retained: missing permanent fixture mode (initial red), HTTP
session-field mismatch, occupied port, producer floor refusal, insufficient
`/tmp` space, and debug permanent-file cleanup assertion. The latter is repaired
by native transaction rollback instead of out-of-operation `file_destroy`.
CMake installation rewrites RUNPATH, so raw build/install executable hashes
need not match; matching ELF build IDs, build logs, exact file hashes and loaded
library paths are recorded together. Debug's shared installation was busy with
an existing `demodb`; its build completed but installation was deferred. A
separate installation was made without stopping that database. A short private
`CUBRID_TMP` resolved the pre-test PL socket-path guard.

Use `gzip -dc FILE.log.gz` for raw logs and `sha256sum -c SHA256SUMS` in this
directory to validate the final artifact set. Reproduction commands and input
schema are in [the integration guide](../../volmap-integration.md).

The first CTP attempts passed runtime assertions but lacked the test worktree
Git parent path in the isolated home. Their provenance is incomplete and retained
under `failures/`; the final attempts add that path and copy child logs before
another mode can overwrite testcase outputs. Both final child identity logs
resolve the expected testcase commit without Git errors.
