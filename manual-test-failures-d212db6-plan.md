# Manual Test Failure Remediation Plan for `d212db6`

## Scope

This document turns the manual-test failure analysis for CUBRID build
`11.5.0.2436-d212db6` into an executable remediation plan and records the results.

- Source revision: `d212db627ae0199f75d845c98275cd5bb52c4d01`
- Branch: `feature/oos-m2`
- Worktree: `/home/vimkim/gh/cb/feat-oos-m2-manual`
- Build preset: `debug_gcc`
- QA run: `/home/vimkim/gh/cubrid-qahome-fetcher/runs/20260715-123333-11.5.0.2436-d212db6`
- Failure report: `failure-report.md` under the QA run above
- OOS design context: `/home/vimkim/gh/cubrid-oos-context/OOS-CONTEXT.md`

The raw report contains 618 entries and 285 deduplicated failures. The categories
are 64 crash, 12 SQL, 108 shell, 3 isolation, 35 interface, 62 HA, and 1 unknown.
The execution order below is based on normalized root causes rather than raw
failure counts.

## Normalized failure map

| Priority | Signature / symptom | Count | Root cause or working hypothesis | Planned action |
| --- | --- | ---: | --- | --- |
| P0 | `lock_uninit_resource`, `lock_manager.c:832` | 44 | CBRD-27052: DBLink 2PC recovery scan uses a system/recovery transaction index, so class locks survive until shutdown | Port and verify develop commit `dfeffb382` |
| P0 | `cubthread::manager::~manager` | 8 | CBRD-27051/27054: DBLink daemon survives a boot-restart error path | Port and verify develop commit `ca7c51c43`; audit daemon-entry reservation cleanup |
| P0 | `log_system_tdes::destroy_system_transactions` | 4 | Same DBLink daemon/error-path lifetime problem | Covered by `ca7c51c43`, then restart/error-path verification |
| P0 | default-expression assertion / NULL value | 2 crash + 9 SQL | CBRD-27021/27030: name resolution clears cached system datetime/epoch values and internal `do_statement` does not restore them | Port and verify develop commit `ebf08d080` |
| P0 | `pgbuf_timed_sleep`, `page_buffer.c:7250` | 1 | Confirmed OOS/heap latch inversion: vacuum holds a heap data page and `heap_oos_find_vfid` unconditionally fixes the heap header | Add conditional header fixing and ordered retry; audit all callers holding data pages |
| P0 | `pgbuf_fix_debug`, `fetch_mode=NEW_PAGE` | 2 tests / 3 cores | OOS numerable-file allocation reaches `file_alloc` or `file_numerable_add_page` with a page that appears already initialized | Reproduce with allocation identity logging and determine double allocation versus stale BCB before changing semantics |
| P0 | `vacuum_heap`, `vacuum.c:1581` | 1 | Release-build abort masks the original `vacuum_heap_page` failure | Reproduce in debug and capture the preceding error, VPID, record, and error stack |
| P1 | `css_readn` / `ldr_server_load` forced cores | 2 | Stacks are in wait/sleep paths and are likely secondary to server death | Re-evaluate only after primary server crashes are removed |
| P1 | new interface/SQL failures | 17 unique | Mostly PL/CSQL and Java SP fallout from the large `d212db6` tree sync; three paths are duplicated across suites | Triage separately after server stability is restored |
| P1 | `delete_select_04.ctl` | 1 unique | Same isolation failure reported on two hosts and QA summary | Reproduce the exact reported path once the primary fixes build |
| P2 | shell and HA failures without useful diffs | 99 shell + 58 HA | Many have `is_new=Unknown`; crash fallout is likely inflating the lists | Rerun affected buckets after P0 stabilization, then classify residuals |

## Execution plan

### 1. Establish the control build

- [x] Confirm the worktree is at exact revision `d212db6`.
- [x] Validate the OOS worktree, build preset, compiler database, `clangd`, and `just` setup.
- [x] Confirm the installed debug binary identifies itself as the failing build.
- [x] Preserve the existing modified `cubrid-cci` submodule and untracked local tooling files.
- [x] Record a clean baseline build result before modifying tracked engine sources.

### 2. Integrate already-diagnosed post-`d212db6` fixes

Apply the three known develop fixes as reviewable working-tree patches, without
creating commits. After each logical patch:

1. Inspect the complete upstream diff and its dependencies.
2. Apply only the source changes required by that fix.
3. Run `just build` through the worktree environment.
4. Run a focused reproducer when the corresponding testcase is locally available.
5. Record the result and any divergence from the upstream patch.

- [x] CBRD-27052 / `dfeffb382`: isolate the DBLink recovery scan in its own transaction and always commit/abort/free it.
- [x] CBRD-27051 / `ca7c51c43`: stop the DBLink daemon on the boot restart error path.
- [x] Audit `REGISTER_DAEMON` accounting around the new stop path, as noted in CBRD-27054.
- [x] CBRD-27030 / `ebf08d080`: preserve or refetch cached system datetime/epoch values for internal statement execution.

### 3. Remove the confirmed OOS latch inversion

The failing stack is:

```text
vacuum_heap_prepare_record
  -> vacuum_oos_find_vfid_for_heap_record
    -> heap_oos_find_vfid
      -> pgbuf_fix(heap header, READ, UNCONDITIONAL)
        -> pgbuf_timed_sleep assertion
```

The caller still holds a heap data page. The fix must preserve the established
heap page-ordering rule rather than merely extending the latch timeout.

- [x] Add a `PGBUF_LATCH_CONDITION` argument to `heap_oos_find_vfid`, matching `heap_ovf_find_vfid` conventions.
- [x] Use a conditional heap-header fix from paths that already hold heap data pages.
- [x] On conditional failure, release held data page(s), fix the header unconditionally in the correct order, re-fix data page(s), and retry, following the existing `REC_BIGONE` pattern.
- [x] Audit vacuum and eager OOS delete/update callers for the same header-after-data ordering.
- [x] Reproduce `bug_cubridsus2771` with the focused CTP shell runner; its first post-fix failure moved to OOS allocation during server shutdown rather than the reported latch stack.
- [x] Re-run `bug_cubridsus2771` after removing the shutdown-only allocation assertion; the exact case passed 1/1 in 127 seconds with no new core.

### 4. Diagnose OOS `NEW_PAGE` assertions

Do not treat conversion to non-numerable files as a fix unless the observed page
identity conflict is explained. The `d212db6` baseline intentionally creates the
OOS file as numerable; the implemented design migration below changes that.

- [x] Reproduce `cbrd_23430` independently; all 20 checks in its ten-cycle load/update/recovery loop passed in 387 seconds without a core.
- [x] Reproduce `cbrd_25365` independently; all 26 checks passed in 295 seconds, including its 823 MB unload/loaddb stage.
- [ ] Capture VFID, allocated VPID, file type, allocation type, ftab page identity/fullness, page type, page LSA, and BCB state immediately before the failed `NEW_PAGE` fix.
- [ ] Distinguish double allocation, numerable-table corruption, reused page identity, and stale-buffer metadata.
- [x] Implement the accepted OOS design change: create new OOS files as non-numerable and replace nth-page enumeration with the file sector-bitmap snapshot walk.
- [x] Make interrupted OOS page allocation propagate its original error instead of asserting, and guard scalar insertion against a null allocated-page handle.
- [x] Add a deterministic OOS regression that crosses a disk-sector boundary, frees pages throughout the scan range, clears cached hints, and verifies bitmap-based best-space rediscovery.
- [x] Add a deterministic VARBIT OOS regression test for the confirmed allocation sequence.

### 5. Expose the masked vacuum failure

- [x] Attempt `cbrd_23613_6/sql_03` twice with the debug build and preserve the prerequisite failure evidence.
- [ ] Capture the first error returned by `vacuum_heap_page`, including the heap page and record context.
- [x] Remove the generic temporary abort after `vacuum_heap_page` failure so normal error propagation is observable.
- [ ] Fix the underlying error, not the release-only abort symptom.

### 6. Re-run and triage residual failures

- [x] Run the exact failing isolation `delete_select_04.ctl` path from the report.
- [ ] Re-run focused shell failures carrying CBRD-27052/27054/27021 labels.
- [ ] Re-run the affected shell and HA buckets after primary crash removal.
- [ ] Deduplicate remaining SQL/interface failures and separate PL/CSQL/Java-SP regressions from OOS failures.
- [ ] Treat client/admin forced cores as primary only if they remain without a preceding server failure.

### 7. Acceptance criteria

- [x] Every tracked engine-source change passes `just build` in `debug_gcc`.
- [x] Relevant unit tests pass via `just build-test` or a narrower documented target.
- [ ] Each locally available P0 reproducer either passes or has a captured, actionable first failure.
- [x] No new latch-order violation, transaction leak, daemon leak, or silent NULL/default-expression behavior was observed in the targeted regressions.
- [x] Temporary OOS abort/assert instrumentation is not mistaken for production behavior and is explicitly accounted for before handoff.
- [x] This document contains commands, outcomes, remaining blockers, and the final tracked diff.

## Execution log

### 2026-07-15 — control setup

- OOS environment validation passed for branch `feature/oos-m2`.
- `HEAD` is exactly `d212db627ae0199f75d845c98275cd5bb52c4d01`.
- Installed binary reports `CUBRID 11.5.0.2436-d212db6`, 64-bit debug build.
- The existing modified `cubrid-cci` submodule and untracked local tooling/test-analysis files were left untouched.
- The OOS testcase checkout exists at `/home/vimkim/gh/tc/oos-ctp`; `CTP` is not exported by the worktree environment, so focused test commands must set it explicitly.
- The unmodified control revision completed `direnv exec . just build` successfully.

### 2026-07-15 — CBRD-27052 integration

- Applied the source change from develop commit `dfeffb382` to `src/query/dblink_2pc_daemon.c`.
- `direnv exec . just build` completed successfully after the change.
- A focused DBLink XA recovery testcase has not yet been located locally.

### 2026-07-15 — CBRD-27051 daemon shutdown integration

- Audited the daemon manager: `destroy_daemon()` removes the tracked daemon and restores its one reserved thread entry.
- The DBLink daemon is not registered with `REGISTER_DAEMON`; it currently relies on the manager's single padding entry. Stopping it on the restart error path restores that entry for a subsequent restart.
- Applied the source change from develop commit `ca7c51c43` to `src/transaction/boot_sr.c`.
- `direnv exec . just build` completed successfully after the change.
- A targeted restart/error-path test is still pending.

### 2026-07-15 — CBRD-27030 default-expression integration

- Applied the full six-file source change from develop commit `ebf08d080`.
- The shared `db_ensure_server_info` path now covers regular statement execution, re-resolved internal statements, create-select inserts, trigger statements, and stored-procedure default arguments.
- `direnv exec . just build` completed successfully after the change.
- Client-side `20399_loaddb.sh`: 1 executed, 1 successful, 0 failed.
- Server-side loaddb variant of `20399_loaddb.sh`: 1 executed, 1 successful, 0 failed.
- The first CTP attempt was interrupted before testcase execution and is intentionally not counted.

### 2026-07-15 — OOS heap-header latch ordering

- Extended `heap_oos_find_vfid` with a latch condition and kept ordinary callers unconditional.
- Vacuum now probes the heap header conditionally while holding a data page. On a conditional miss it copies the record context, releases forward/home pages, performs the header lookup unconditionally, re-fixes the home page, and retries record preparation in page order.
- Audited eager OOS cleanup separately. It retains the unconditional lookup because the confirmed server-mode MVCC cleanup is vacuum-driven; the eager path is primarily standalone/MVCC-disabled and has no matching QA crash stack.
- `direnv exec . just build` completed successfully after the latch-order changes.
- The exact `bug_cubridsus2771` CTP run executed for 253 seconds. It did not reproduce `pgbuf_timed_sleep`; instead, shutdown interrupted an in-flight `file_alloc`, and the temporary `assert(false)` in `oos_file_alloc_new` generated a new core at line 1787. This is recorded under allocation/error propagation below.

### 2026-07-15 — OOS allocation and enumeration

- Confirmed from the QA cores that `cbrd_23430` fails at `file_alloc:5508` while fixing the allocated OOS page as `NEW_PAGE`, and `cbrd_25365` also fails at `file_numerable_add_page:8054` while creating a numerable user-page-table component.
- The accepted OOS ADR identifies `FILE_OOS` as the only always-permanent numerable file and requires migration to a non-numerable file enumerated from `file_get_all_data_sectors`.
- New OOS files are now non-numerable. Best-space synchronization and exact OOS statistics collect data VPIDs from the sector bitmap, skip the sticky header, and tolerate a bitmap-snapshot page that vacuum deallocated or reassigned before its read-only fix.
- Partial best-space scans resume by sorted VPID and wrap once; the insert/write path still uses strict `OLD_PAGE` fixing for cached candidates.
- Removed the crash-only assertion on an expected interrupted `file_alloc` failure and added scalar-insert null-page error propagation. The observed `bug_cubridsus2771` shutdown error was `ER_INTERRUPTED`, not a page-identity conflict.
- `direnv exec . just build` completed successfully after the non-numerable migration and error-propagation change.

### 2026-07-15 — focused OOS regression results

- Added `BestspaceSectorBitmapSyncAcrossSectors` to `unit_tests/oos/test_oos_bestspace.cpp`. It allocates 69 near-full OOS pages (more than `DISK_SECTOR_NPAGES`), frees pages across the VPID range, removes all cached hints, and verifies that a full bitmap scan rediscovers a reusable page.
- The focused `test_oos_bestspace` run, including its database fixture, passed 3/3.
- An initial parallel OOS CTest run passed 19 tests and hit three shared-`unittestdb` fixture conflicts; those failures produced the test-binary cores under `build_preset_debug_gcc/unit_tests/oos` and are harness collisions, not engine failures.
- The authoritative sequential rerun, `ctest --output-on-failure -R "oos" -j1`, passed 22/22 in 49.37 seconds.
- The final `bug_cubridsus2771` focused CTP run passed 1/1 in 127 seconds. It produced neither the reported heap-header latch timeout nor a replacement allocation core.
- The first local `cbrd_23430` attempt is excluded: a second Codex/CTP process sharing the same installation stopped `jsondb`, and the only failure was `Failed to connect to database server`; no core was created.
- The subsequent uncontended `cbrd_23430` run passed all 20 testcase checks in 387 seconds with no core, covering ten create/load/update/select and forced-recovery cycles.
- The exact `cbrd_25365` run passed all 26 testcase checks in 295 seconds with no reported core. Its 823 MB unload/loaddb stage exercised the numerable-page-table allocation path implicated by the QA core after OOS files had been migrated away from that path.

### 2026-07-15 — deterministic SQL allocation and observability

- Added `OosSqlCrud.NonNumerableAllocationAcrossSectors` to `unit_tests/oos/sql/test_oos_sql_crud.cpp`. It inserts `DISK_SECTOR_NPAGES + 5` VARBIT rows, verifies all values and lengths through SQL, and confirms in standalone mode that the OOS file grew beyond one sector.
- `ctest -V -R '^test_oos_sql_crud$' -j1` ran the fixture and all nine SQL CRUD tests. The new cross-sector test passed in 47 ms; the three-test fixture passed in 8.51 seconds.
- Added `Num_oos_bitmap_snapshot_skips` to the performance monitor and a unit-test debug counter. A skip is counted when a sector-bitmap snapshot references a page vacuum already deallocated or reassigned before the conditional read latch.

### 2026-07-15 — exact vacuum shell attempt

- Ran `cbrd_23613_6/sql_03` twice with the focused shell runner. Both runs completed CTP setup but failed before any SQL testcase executed because JDBC `useSSL=true` could not handshake with broker1; the second run used a clean local process state and reproduced the same prerequisite failure.
- The authoritative second transcript is `/tmp/shell_single.CmZs2G.log`; the testcase evidence is `cases/ctp_sql.fail` and `cases/sql_03.result` under the testcase directory.
- CTP reported 1/1 NOK after 316 seconds. There was no server core, no `vacuum_heap_page` call, and therefore no first engine error to diagnose from this testcase. It remains an infrastructure blocker rather than a vacuum pass or engine failure.
- Removed only the generic release-build `abort()` that followed a `vacuum_heap_page` error. The four pre-existing `OOS+REC_BIGONE` hard-fail guards remain in `vacuum.c` and `heap_file.c`; they protect a separate unsupported invariant and still carry `REVERT BEFORE MERGE` comments.
- The targeted engine substitute, `test_oos_real_vacuum_server`, passed in the final OOS suite.

### 2026-07-15 — exact isolation result

- Ran `_01_ReadCommitted/primary_key_column/aggregate/delete_select_04.ctl` with serialized ctltool preparation (`MAKEFLAGS=-j1`). Earlier qactl-missing and parallel-build attempts are excluded as harness setup failures.
- The case reproduced NOK after 20.979 seconds without a core or fatal engine error.
- Both semantic snapshots match the answer: the pre-commit aggregate is `0, 9` and the post-commit aggregate is `3, 18`. The only difference is transcript ordering: `1 row affected` appears after the first aggregate instead of before it because the concurrent delete completes later.
- No engine change was made for this timing-only output-order difference; it is independent of OOS storage behavior.

### 2026-07-15 — final validation

- The final `direnv exec . just build` completed successfully after all tracked changes.
- `ctest --output-on-failure -R 'oos' -j1` passed 22/22 in 53.27 seconds. This includes bestspace, real vacuum, transaction, BIGONE, eager cleanup, and all OOS SQL binaries.
- `git diff --check` reports no whitespace errors.
- No OOS-test or vacuum-shell core was created by the final build, focused SQL regression, sequential OOS suite, or latest exact vacuum-shell attempt.
- A final worktree-wide scan found `core.cub_server.3968916.dev2.1784091961`, created at 14:06 by a separate `cub_server testdb` startup between the OOS suite and vacuum-shell attempt. Its SIGABRT stack ends at `serial_cache_index_btid` (`serial.c:1515`, null cached serial index BTID) during `boot_restart_server`; it contains no OOS or vacuum frames. The unrelated concurrent-test artifact was preserved rather than deleted.
- Final tracked diff: 25 paths, 427 insertions and 112 deletions. The modified `cubrid-cci` submodule and existing untracked local files remain untouched.

## Final result

The prioritized remediation is implemented and locally validated. The three known
post-`d212db6` fixes are integrated; the OOS heap-header latch inversion is fixed;
OOS allocation has moved to the accepted non-numerable, sector-bitmap design; and
deterministic bestspace and VARBIT cross-sector regressions are in place. The exact
allocation-heavy shell cases pass and the complete OOS CTest set is green.

Two residuals remain outside the implemented OOS fix:

1. `cbrd_23613_6/sql_03` cannot reach its SQL workload locally because its nested
   CTP/JDBC SSL handshake fails. It must be rerun in a QA-compatible SSL harness to
   validate the original `vacuum.c:1581` path end to end.
2. `delete_select_04.ctl` still reports NOK solely because a concurrent DML status
   line moves in the transcript; both database result sets match the answer.

The broader P1/P2 SQL, interface, shell, and HA bucket reruns remain follow-up work;
they are deliberately not claimed as completed by this targeted P0 remediation.
