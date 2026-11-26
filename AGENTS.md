# AGENTS.md — CUBRID Database Engine

CUBRID is an open-source C/C++17 RDBMS (Apache 2.0) with a Java PL engine for stored procedures. Inspect `VERSION` and `.gitmodules` for release and submodule metadata.

## Read First

Load subsystem guidance before changing code in that area:

- **C/C++ engine (`src/`)** — including build modes, CMake source registration, memory/exception handling, formatting, and the pointers to the error-code, recovery-index, and XASL checklists: read [src/AGENTS.md](src/AGENTS.md) and the target module's `src/<module>/AGENTS.md` when present.
- **C/C++ unit tests (`unit_tests/`)**: for GoogleTest OOS tests, read [unit_tests/oos/AGENTS.md](unit_tests/oos/AGENTS.md) first; it overrides the Catch2 default of [unit_tests/AGENTS.md](unit_tests/AGENTS.md), which covers the other modules.
- **Java PL engine (`pl_engine/`)**: read [pl_engine/AGENTS.md](pl_engine/AGENTS.md).
- **Submodules (`cubrid-cci/`, `cubrid-jdbc/`, `cubridmanager/`)**: inspect `.gitmodules` and the submodule's own build and documentation before making changes.

## Task Guides

- Before you build, install, run unit tests, or change build presets, read [docs/build.md](docs/build.md).
- Before you add a file, push, open or update a PR, comment `/run`, change testcases, or decide which checks to run, read [docs/pull-requests.md](docs/pull-requests.md).

## Conflicts

When documents disagree, live code and build files (`CMakeLists.txt`, `build.sh`, `.github/workflows/`) win. A more specific `AGENTS.md` overrides its parent only where it says that it narrows the parent; everywhere else the parent applies. Report each conflict you find with your results.

## Common Change Principles

- Verify build targets, presets, flags, and CI triggers from live configuration (`CMakeLists.txt`, `CMakePresets.json`, `build.sh`, `.github/workflows/`) rather than assuming from memory.
- Keep diffs scoped to the task: preserve existing formatting on untouched lines and leave large legacy files intact rather than splitting them for size.
- Leave third-party allocator code (`src/heaplayers/lea_heap.c`) untouched unless explicitly required (`src/heaplayers/` is excluded from `cppcheck` in `.github/workflows/check.yml`).
- Before finalizing engine C/C++ changes, verify both `release` (the only `-Werror` mode; asserts compiled out) and one assert-enabled mode (`debug` or `optdebug`).

## Hazards

- `build.sh` with no target (or `clean`) deletes the build and install directories; never point `-b` or `-p` at a directory you want to keep.
- Presets install into `$env{CUBRID}`: set `$CUBRID` (and `$CUBRID_DATABASES`) to scratch paths before you configure; never install into a live `$CUBRID`.
- CTP cleanup runs `kill -9` on every process of the invoking user (not only `cub*`); run it only in a container or under a dedicated account.
- Pushing, opening a PR, and commenting `/run` are outward-facing; confirm first.
- `ctest` with no registered tests exits 0; use `ctest --no-tests=error`.

## Reporting Results

- Report modified files, exact verification commands and observed outcomes, any skipped or unrun checks with reasons, and remaining risks or follow-ups. State only directly observed facts.
