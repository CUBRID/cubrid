# Pull Requests and CI

What a CUBRID engine PR must satisfy, which CI runs on it, and how to run regression suites.
Read this before you add a file, push, open or update a PR, comment `/run`, change testcases, or decide which checks to run.

## PR Title, License Headers, and CLA

- Format PR titles to match `^\[[A-Z]+-[0-9]+\] .+` (`.github/actions/check-pr-title/action.yml`).
- Keep an accepted Apache 2.0 header (`.github/workflows/license_headers/`) on checked extensions (`.c`, `.h`, `.cpp`, `.hpp`, `.C`, `.H`, `.CPP`, `.HPP`, `.ipp`, `.sh`, `.bat`, `.y`, `.l`, `.msg`). List a third-party file that keeps its own license in `.github/workflows/license_headers/license_check_ignore_files.txt`.
- Satisfy the CLA before merge.

## PR Static Checks

`.github/workflows/check.yml` runs `license`, `pr-style`, `code-style` (`.github/workflows/codestyle.sh`), `cppcheck` (using `.github/workflows/cppcheck-spr-list`), and `memory-monitor-check`. `pr-style` checks the PR title; the others check only files the PR adds or modifies (`.github/actions/changed-files/action.yml`), so run them yourself on renamed files (CBRD-28409).

## Regression CI

`.github/workflows/gha-ci.yml` (with `.github/workflows/gha-ci-suite.yml` via `workflow_call`) builds `release` and `optdebug` (packaged as `debug`) artifacts:

- `develop` pushes build and update caches only (`test_run=false`).
- Regression suites (`shell`, `sql`, and `medium` on `optdebug`; `sql_by_cci` on `release`) run on PR `/run` comments, nightly `schedule` (which dispatches one `workflow_dispatch` run per `develop` commit), and manual `workflow_dispatch`.

Legacy or supplementary configurations also exist in `Jenkinsfile` and `.circleci/config.yml` (its workflow is disabled with `when: false` on `develop`'s lineage); inspect live workflow triggers before assuming which pipeline covers a branch.

## Regression Suites and `/run`

- The runner is CTP in `CUBRID/cubrid-testtools`; `sql`/`medium` cases live in `CUBRID/cubrid-testcases` and `shell` cases live in private `CUBRID/cubrid-testcases-private-ex`.
- On PRs targeting `develop` or `feature/*`, comment `/run shell|sql|medium|sql_by_cci|all` or `/run rerun <run id>`. Combine suites in one comment: a new `/run` cancels an in-progress run on the same PR.
- `/run` needs an open PR. Before one exists, the only regression run is isolated CTP (see [Running CTP Locally](#running-ctp-locally)), and opening a draft PR to use `/run` is outward-facing, so confirm first.

## Testcase Branches

- Testcase changes for PR `<N>` go to the `tc/pr-<N>` branch that `.github/workflows/tc-branch-sync.yml` creates in both testcase repositories when the PR opens; CI checks it out.
- `.github/workflows/tc-merge-gate.yml` runs `Check TC PRs`, the required check on `develop`. It blocks the merge while a testcase PR for `tc/pr-<N>` is open in either testcase repository, even a draft with no changes (so it fails on every new engine PR); after those PRs are closed or merged, it blocks while `tc/pr-<N>` still holds files that testcase `develop` lacks. Close an empty testcase PR, or merge one with changes, only together with the engine PR as the last step, following the gate's PR comment. A block is a testcase-PR state, not a regression failure, so do not answer it with `/run`.
- `.github/workflows/tc-branch-finalize.yml` deletes `tc/pr-<N>` when the engine PR merges or closes, and any change on it that was not merged is lost.

## Running CTP Locally

CTP cleanup runs `pkill cub` and then `kill -9` on every process of the invoking user except its own ancestors (shells, editors, terminal multiplexers, and agents included) (`CTP/common/script/util_common.sh` in `cubrid-testtools`). Run CTP only in a container or under a dedicated account, never as a user that owns other CUBRID servers.

## Proportional Verification

- For docs-only edits, check that every relative link target exists and that every `#anchor` in a relative link matches a heading in its target file (GitHub heading slug); no link checker is configured. When you rename a heading, search the repository for links to its old anchor. Run `git diff --check` rather than full engine regression suites.
- For code edits, build affected targets, run relevant local unit tests (see [build.md](build.md#unit-tests)), and run regression suites via PR `/run` (outward-facing; confirm first) or isolated CTP only.
