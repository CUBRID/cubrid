# CUBRIDQA-1613: CI prebuilt third-party prefix

**Status:** ready-for-agent

## Authority

- Parent issue: <http://jira.cubrid.org/browse/CUBRIDQA-1613>
- Production spec: `/home/vimkim/gh/my-cubrid-jira/issues/CUBRIDQA-1613-cubrid-thirdparty-prefix_c63a3b9_codex.md`
- CUBRID worktree: `/home/vimkim/gh/cb/CUBRIDQA-1613-thirdparty-prefix`
- Source baseline: `c63a3b993be552ef6ad3ce244c386d5081147958`
- cubridci integration: <https://github.com/CUBRID/cubridci/pull/125>
- cubridci POC baseline: `8f1855ddab659eab7af149419a9992743751662c`

The production spec is authoritative if a ticket summary is ambiguous. Do not
modify or close CUBRIDQA-1613 while working these local tickets.

## Dependency graph

```text
01 Canonical manifest, contract module, and standalone seam
  -> 02 Complete relocatable prefix producer
    -> 03 Matching CI_PREBUILT consumer
      -> 04 Fail-closed validation and diagnostics
        -> 05 Release/OptDebug linkage qualification
          -> 06 Activate cubridci PR #125
            -> 07 Measure CI performance
              -> 08 Record rollout decision
```

This is a blocker-ordered linear chain. Work only the frontier: the first
`ready-for-agent` ticket whose blockers are all resolved.

## Tickets

| ID | Ticket | Blocked by | Status |
|----|--------|------------|--------|
| 01 | [Canonical manifest, contract module, and standalone seam](issues/01-canonical-manifest-contract.md) | None | resolved |
| 02 | [Complete relocatable prefix producer](issues/02-relocatable-prefix-producer.md) | 01 | resolved |
| 03 | [Matching CI_PREBUILT consumer](issues/03-ci-prebuilt-consumer.md) | 02 | resolved |
| 04 | [Fail-closed validation and diagnostics](issues/04-fail-closed-validation.md) | 03 | resolved |
| 05 | [Release/OptDebug linkage qualification](issues/05-release-optdebug-qualification.md) | 04 | ready-for-agent |
| 06 | [Activate cubridci PR #125](issues/06-activate-cubridci-pr-125.md) | 05 | ready-for-agent |
| 07 | [Measure CI performance](issues/07-measure-ci-performance.md) | 06 | ready-for-agent |
| 08 | [Record rollout decision](issues/08-record-rollout-decision.md) | 07 | ready-for-agent |

## Frontier

- `05` can start immediately.
- `06` through `08` remain blocked until the preceding ticket is resolved.

## Decisions so far

- CUBRID owns the canonical manifest and hashes its exact source-controlled bytes.
- The primary behavioral seam is a standalone CMake/CTest black-box suite that
  does not require the engine, JDK, submodules, Catch2, or network downloads.
- The first production scope is Linux x86_64 `build_rl8.10`.
- Release and OptDebug share one prefix.
- Explicit `CI_PREBUILT` is fail-closed and never falls back to ExternalProject.
- Static/shared linkage stays unchanged; unixODBC remains shared with SONAME
  `libodbc.so.2`.
- Image tag, rollout order, and deployment owner remain undecided until the
  implementation and measurements are complete.
- [Ticket 01](issues/01-canonical-manifest-contract.md) resolved the canonical
  manifest/contract seam with raw fingerprint
  `4c19d1957a6a4df946ab6020a6e18a8bc4688a195221ca5e6c6abac27520c126`;
  ticket 02 can build the relocatable producer against these exported values.
- [Ticket 02](issues/02-relocatable-prefix-producer.md) resolved the atomic,
  relocatable eight-dependency producer with recipe revision 2 and raw
  fingerprint
  `58a2f6e3383ab366c60efd66996e11ac421836c227b702ed32bb705e72dd185e`;
  ticket 03 can consume and validate this exact prefix contract.
- [Ticket 03](issues/03-ci-prebuilt-consumer.md) resolved matching relocated
  `CI_PREBUILT` consumption with cache/environment parity, the complete legacy
  variable interface, empty dependency targets, and no ExternalProject
  declarations; ticket 04 can harden the same path's fail-closed validation and
  diagnostic contract.
- [Ticket 04](issues/04-fail-closed-validation.md) resolved the fail-closed
  validator and stable eight-field diagnostics with a 54-case labeled negative
  matrix covering interface conflicts, manifests, provenance, artifacts, path
  containment, symlink chains, and exact SONAME validation; ticket 05 can run
  Release/OptDebug linkage qualification against this hardened consumer.

## Comments

- 2026-09-23: The user approved this eight-ticket breakdown and dependency graph.
