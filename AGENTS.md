# AGENTS.md — CUBRID Database Engine

CUBRID is an open-source C/C++17 RDBMS (Apache 2.0) with a Java PL engine for stored procedures. Inspect `VERSION` and `.gitmodules` for release and submodule metadata.

## Read First

Load subsystem guidance before changing code in that area:

- **C/C++ engine (`src/`)** — including error codes, memory/exception handling, build modes, CMake source registration, and formatting: read [src/AGENTS.md](src/AGENTS.md) and the target module's `src/<module>/AGENTS.md` when present.
- **C/C++ unit tests (`unit_tests/`)**: read [unit_tests/AGENTS.md](unit_tests/AGENTS.md), and `unit_tests/oos/AGENTS.md` for GoogleTest OOS tests.
- **Java PL engine (`pl_engine/`)**: read [pl_engine/AGENTS.md](pl_engine/AGENTS.md).
- **Submodules (`cubrid-cci/`, `cubrid-jdbc/`, `cubridmanager/`)**: inspect `.gitmodules` and the submodule's own build and documentation before making changes.

## Common Change Principles

- Verify build targets, presets, flags, and CI triggers from live configuration (`CMakeLists.txt`, `CMakePresets.json`, `build.sh`, `.github/workflows/`) rather than assuming from memory.
- Keep diffs scoped to the task: preserve existing formatting on untouched lines and leave large legacy files intact rather than splitting them for size.
- Leave third-party allocator code (`src/heaplayers/lea_heap.c`) untouched unless explicitly required (`src/heaplayers/` is excluded from `cppcheck` in `.github/workflows/check.yml`).
- Meet PR and license requirements: keep an accepted Apache 2.0 header (`.github/workflows/license_headers/`) on checked extensions (`.c`, `.h`, `.cpp`, `.hpp`, `.C`, `.H`, `.CPP`, `.HPP`, `.ipp`, `.sh`, `.bat`, `.y`, `.l`, `.msg`), format PR titles to match `^\[[A-Z]+-[0-9]+\] .+` (`.github/actions/check-pr-title/action.yml`), and satisfy the CLA before merge.

## Build & Verification Entrypoints

- **Prerequisites**: see `docs/install_build_requirements.md` and `CMakeLists.txt`.
- **Build**: pass an explicit build target — e.g. `./build.sh -m <mode> build` (omitting the target defaults to `all`, which cleans `$prefix_dir/*`; never point `-p` at a live `$CUBRID`; run `./build.sh -h` for options) or `cmake --preset <preset> && cmake --build --preset <preset>` (presets set `CMAKE_INSTALL_PREFIX=$env{CUBRID}`; set `$CUBRID` or override `-DCMAKE_INSTALL_PREFIX=...` and skip install unless targeting a scratch directory).
- **Build modes**: `Release` and `RelWithDebInfo` (`release`) are the only modes compiled with `-Werror` (other than `-Werror=format-security` everywhere) and they compile asserts out (`-DNDEBUG`); `debug` and `optdebug` keep asserts and do not fail on general warnings (`CMakeLists.txt`). Before finalizing engine C/C++ changes, verify both `release` and one assert-enabled mode; during intermediate iterations, state which mode was built.
- **Unit tests**: configure with `-DUNIT_TESTS=ON` or `-DUNIT_TEST_<MODULE>=ON` (see `unit_tests/CMakeLists.txt`), build the test targets, and run `ctest` in the build directory. No CI workflow configures `UNIT_TESTS` or runs `ctest`, so run the affected module's tests locally.
- **PR static checks** (`.github/workflows/check.yml`): `license`, `pr-style`, `code-style` (`.github/workflows/codestyle.sh`), `cppcheck` (using `.github/workflows/cppcheck-spr-list`), and `memory-monitor-check`.
- **Regression CI**: `.github/workflows/gha-ci.yml` (with `.github/workflows/gha-ci-suite.yml` via `workflow_call`, plus `.github/workflows/tc-branch-sync.yml` and `.github/workflows/tc-merge-gate.yml`) builds `release` and `optdebug` (packaged as `debug`) artifacts, running `shell`, `sql`, and `medium` on the `optdebug` build and `sql_by_cci` on `release` for `develop` pushes, PR `/run` comments, nightly `schedule`, and `workflow_dispatch`. Legacy or supplementary configurations also exist in `Jenkinsfile` and `.circleci/config.yml` (its workflow is disabled with `when: false` on `develop`'s lineage); inspect live workflow triggers before assuming which pipeline covers a branch.
- **Regression suites & PR triggers**: the runner is CTP in `CUBRID/cubrid-testtools`; `sql`/`medium` cases live in `CUBRID/cubrid-testcases` and `shell` cases live in private `CUBRID/cubrid-testcases-private-ex`. Testcase changes for PR `<N>` go to the bot-created `tc/pr-<N>` branch (`.github/workflows/tc-branch-sync.yml`), which CI checks out. On PRs targeting `develop` or `feature/*`, comment `/run shell|sql|medium|sql_by_cci|all` (combine suites in one comment—a new `/run` cancels an in-progress run on the same PR) or `/run rerun <run id>`.
- **CTP ends other processes**: CTP cleanup runs `pkill cub` and `kill -9` across the invoking user's processes (`CTP/common/script/util_common.sh` in `cubrid-testtools`). Run CTP only in a container or under a dedicated account, never as a user that owns other CUBRID servers.
- **Proportional verification**: for docs-only edits, run link/anchor checks and `git diff --check` rather than full engine regression suites; for code edits, build affected targets, run relevant local unit tests, and run regression suites via PR `/run` (outward-facing; confirm first) or isolated CTP only.

## Reporting Results

- Report modified files, exact verification commands and observed outcomes, any skipped or unrun checks with reasons, and remaining risks or follow-ups. State only directly observed facts.
