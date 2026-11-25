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
- Meet PR and license requirements: keep an accepted Apache 2.0 header (`.github/workflows/license_headers/`) on checked extensions (`.c`, `.h`, `.cpp`, `.hpp`, `.ipp`, `.sh`, `.bat`, `.y`, `.l`, `.msg`), format PR titles to match `^\[[A-Z]+-[0-9]+\] .+` (`.github/actions/check-pr-title/action.yml`), and satisfy the CLA before merge.

## Build & Verification Entrypoints

- **Prerequisites**: see `docs/install_build_requirements.md` and `CMakeLists.txt`.
- **Build**: run `./build.sh -m <mode> build` (run `./build.sh -h` for modes and options) or `cmake --preset <preset> && cmake --build build_preset_<preset>` (see `CMakePresets.json`).
- **Unit tests**: configure with `-DUNIT_TESTS=ON` or `-DUNIT_TEST_<MODULE>=ON` (see `unit_tests/CMakeLists.txt`), build the test targets, and run `ctest` in the build directory.
- **PR static checks** (`.github/workflows/check.yml`): `license`, `pr-style`, `code-style` (`.github/workflows/codestyle.sh`), `cppcheck` (using `.github/workflows/cppcheck-spr-list`), and `memory-monitor-check`.
- **Regression CI**: `.github/workflows/gha-ci.yml` (with `.github/workflows/gha-ci-suite.yml` via `workflow_call`, plus `.github/workflows/tc-branch-sync.yml` and `.github/workflows/tc-merge-gate.yml`) builds release/debug artifacts and runs `shell`, `sql`, `medium`, and `sql_by_cci` suites on `develop` pushes, PR `/run` comments, nightly `schedule`, and `workflow_dispatch`. Legacy or supplementary configurations also exist in `Jenkinsfile` and `.circleci/config.yml`; inspect live workflow triggers before assuming which pipeline covers a branch.
- **Proportional verification**: for docs-only edits, run link/anchor checks and `git diff --check` rather than full engine regression suites; for code edits, build all affected targets and run relevant unit and regression tests.

## Reporting Results

- Report modified files, exact verification commands and observed outcomes, any skipped or unrun checks with reasons, and remaining risks or follow-ups. State only directly observed facts.
