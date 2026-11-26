# AGENTS.md — CUBRID Database Engine

CUBRID is an open-source C/C++17 RDBMS (Apache 2.0) with a Java PL engine for stored procedures. Inspect `VERSION` and `.gitmodules` for release and submodule metadata.

## Read First

Load subsystem guidance before changing code in that area:

- **C/C++ engine (`src/`)** — including error codes, memory/exception handling, build modes, CMake source registration, and formatting: read [src/AGENTS.md](src/AGENTS.md) and the target module's `src/<module>/AGENTS.md` when present.
- **C/C++ unit tests (`unit_tests/`)**: for GoogleTest OOS tests, read [unit_tests/oos/AGENTS.md](unit_tests/oos/AGENTS.md) first; it overrides the Catch2 default of [unit_tests/AGENTS.md](unit_tests/AGENTS.md), which covers the other modules.
- **Java PL engine (`pl_engine/`)**: read [pl_engine/AGENTS.md](pl_engine/AGENTS.md).
- **Submodules (`cubrid-cci/`, `cubrid-jdbc/`, `cubridmanager/`)**: inspect `.gitmodules` and the submodule's own build and documentation before making changes.
- **Conflicts**: when documents disagree, live code and build files (`CMakeLists.txt`, `build.sh`, `.github/workflows/`) win. A more specific `AGENTS.md` overrides its parent only where it says that it narrows the parent; everywhere else the parent applies. Report each conflict you find with your results.

## Common Change Principles

- Verify build targets, presets, flags, and CI triggers from live configuration (`CMakeLists.txt`, `CMakePresets.json`, `build.sh`, `.github/workflows/`) rather than assuming from memory.
- Keep diffs scoped to the task: preserve existing formatting on untouched lines and leave large legacy files intact rather than splitting them for size.
- Leave third-party allocator code (`src/heaplayers/lea_heap.c`) untouched unless explicitly required (`src/heaplayers/` is excluded from `cppcheck` in `.github/workflows/check.yml`).
- Meet PR and license requirements: keep an accepted Apache 2.0 header (`.github/workflows/license_headers/`) on checked extensions (`.c`, `.h`, `.cpp`, `.hpp`, `.C`, `.H`, `.CPP`, `.HPP`, `.ipp`, `.sh`, `.bat`, `.y`, `.l`, `.msg`), format PR titles to match `^\[[A-Z]+-[0-9]+\] .+` (`.github/actions/check-pr-title/action.yml`), and satisfy the CLA before merge.

## Build & Verification Entrypoints

- **Prerequisites**: see `docs/install_build_requirements.md` and `CMakeLists.txt`.
- **Build**: pass an explicit build target, e.g. `./build.sh -m <mode> build`, or use `cmake --preset <preset> && cmake --build --preset <preset>` (presets are in `CMakePresets.json`).
  - Without a target, `build.sh` runs `all` (`clean build dist` for `release`/`debug`/`optdebug`). `clean` deletes everything inside the `-b` build directory (default `build_x86_64_<mode>`) and the `-p` prefix (default `<build directory>/_install/CUBRID`). Point neither at a directory you want to keep, such as a live `$CUBRID` or another build tree.
  - `./build.sh -h` lists the options, but `-i` and `-a` are not implemented (they print usage and exit 1), and its `-b` default text is out of date.
  - Install only into a scratch directory. Presets install into `$env{CUBRID}`, so before you configure, set `$CUBRID` (and `$CUBRID_DATABASES`) to scratch paths, or pass `-DCMAKE_INSTALL_PREFIX=<scratch>` only for builds without unit tests. Tests that boot a database, such as `unit_tests/oos/`, take `$CUBRID`, `$CUBRID_DATABASES`, and `PATH` as they are when CMake configures (`$ENV{...}` in `unit_tests/oos/CMakeLists.txt`) and edit `$CUBRID/conf/cubrid.conf`, so set both variables to the scratch install before you configure and keep them set when the tests run.
- **Build modes**: `Release` and `RelWithDebInfo` (`release`) are the only modes compiled with `-Werror` (other than `-Werror=format-security` everywhere) and they compile asserts out (`-DNDEBUG`); `debug` and `optdebug` keep asserts and do not fail on general warnings (`CMakeLists.txt`). Before finalizing engine C/C++ changes, verify both `release` and one assert-enabled mode; during intermediate iterations, state which mode was built.
- **Unit tests**: configure with `-DUNIT_TESTS=ON` or `-DUNIT_TEST_<MODULE>=ON` (see `unit_tests/CMakeLists.txt`) and build the test targets. `ctest` runs only modules whose `CMakeLists.txt` calls `add_test`; run any other module's `test_<module>` binary directly (CBRD-28388 tracks the missing registrations). Use `ctest --no-tests=error`: plain `ctest` with no registered test prints "No tests were found" and exits 0. No CI workflow configures `UNIT_TESTS` or runs `ctest`, so run the affected module's tests locally.
- **PR static checks** (`.github/workflows/check.yml`): `license`, `pr-style`, `code-style` (`.github/workflows/codestyle.sh`), `cppcheck` (using `.github/workflows/cppcheck-spr-list`), and `memory-monitor-check`. `pr-style` checks the PR title; the others check only files the PR adds or modifies (`.github/actions/changed-files/action.yml`), so run them yourself on renamed files.
- **Regression CI**: `.github/workflows/gha-ci.yml` (with `.github/workflows/gha-ci-suite.yml` via `workflow_call`) builds `release` and `optdebug` (packaged as `debug`) artifacts: `develop` pushes build and update caches only (`test_run=false`), whereas regression suites (`shell`, `sql`, and `medium` on `optdebug`; `sql_by_cci` on `release`) run on PR `/run` comments, nightly `schedule` (which dispatches one `workflow_dispatch` run per `develop` commit), and manual `workflow_dispatch`. Legacy or supplementary configurations also exist in `Jenkinsfile` and `.circleci/config.yml` (its workflow is disabled with `when: false` on `develop`'s lineage); inspect live workflow triggers before assuming which pipeline covers a branch.
- **Regression suites & PR triggers**: the runner is CTP in `CUBRID/cubrid-testtools`; `sql`/`medium` cases live in `CUBRID/cubrid-testcases` and `shell` cases live in private `CUBRID/cubrid-testcases-private-ex`. On PRs targeting `develop` or `feature/*`, comment `/run shell|sql|medium|sql_by_cci|all` (combine suites in one comment—a new `/run` cancels an in-progress run on the same PR) or `/run rerun <run id>`. `/run` needs an open PR: before one exists, the only regression run is isolated CTP (below), and opening a draft PR to use `/run` is outward-facing, so confirm first.
- **Testcase branches**: testcase changes for PR `<N>` go to the `tc/pr-<N>` branch that `.github/workflows/tc-branch-sync.yml` creates in both testcase repositories when the PR opens; CI checks it out. `.github/workflows/tc-merge-gate.yml` runs `Check TC PRs`, the required check on `develop`. It blocks the merge while `tc/pr-<N>` holds changes that testcase `develop` lacks: the testcase PRs are merged only together with the engine PR, as the last step, following the gate's PR comment. A block is a testcase-PR state, not a regression failure, so do not answer it with `/run`. `.github/workflows/tc-branch-finalize.yml` deletes `tc/pr-<N>` when the engine PR merges or closes, and any change on it that was not merged is lost.
- **CTP ends other processes**: CTP cleanup runs `pkill cub` and `kill -9` across the invoking user's processes (`CTP/common/script/util_common.sh` in `cubrid-testtools`). Run CTP only in a container or under a dedicated account, never as a user that owns other CUBRID servers.
- **Proportional verification**: for docs-only edits, check that every relative link target exists (no link checker is configured) and run `git diff --check` rather than full engine regression suites; for code edits, build affected targets, run relevant local unit tests, and run regression suites via PR `/run` (outward-facing; confirm first) or isolated CTP only.

## Reporting Results

- Report modified files, exact verification commands and observed outcomes, any skipped or unrun checks with reasons, and remaining risks or follow-ups. State only directly observed facts.
