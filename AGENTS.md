# AGENTS.md — CUBRID Database Engine

CUBRID is an open-source C/C++17 RDBMS (Apache 2.0) with a Java PL engine for stored procedures.

## Read First

Load subsystem guidance before changing code in that area:

- **C/C++ engine (`src/`)** (build modes, CMake registration, memory and errors, formatting, module checklists): read [src/AGENTS.md](src/AGENTS.md) and the target `src/<module>/AGENTS.md` when present.
- **C/C++ unit tests (`unit_tests/`)**: read [unit_tests/oos/AGENTS.md](unit_tests/oos/AGENTS.md) for the GoogleTest OOS tests, or [unit_tests/AGENTS.md](unit_tests/AGENTS.md) for the other modules.
- **Java PL engine (`pl_engine/`)**: read [pl_engine/AGENTS.md](pl_engine/AGENTS.md).
- **Submodules (`cubrid-cci/`, `cubrid-jdbc/`, `cubridmanager/`)**: inspect `.gitmodules` and the submodule's own build and documentation first.

## Task Guides

- Before you build, install, run unit tests, or change build presets, read [docs/build.md](docs/build.md).
- Before you add a file, push, open or update a PR, comment `/run`, change testcases, or decide which checks to run, read [docs/pull-requests.md](docs/pull-requests.md).

## Precedence and Scope

- Live code and build files (`CMakeLists.txt`, `CMakePresets.json`, `build.sh`, `.github/workflows/`) override documents and memory; report each conflict you find with your results.
- A more specific `AGENTS.md` overrides its parent only where it says that it narrows the parent.
- Keep diffs scoped: do not reformat untouched lines or split large legacy files.

## Hazards

- `build.sh` with no target (or `clean`) deletes the `-b` build directory and the `-p` install prefix; never point either at a directory you want to keep.
- Presets install into `$env{CUBRID}`: set `$CUBRID` and `$CUBRID_DATABASES` to scratch paths before you configure; never install into a live `$CUBRID`.
- CTP cleanup runs `kill -9` on every process of the invoking user (not only `cub*`); run it only in a container or under a dedicated account.
- Pushing, opening a PR, and commenting `/run` are outward-facing; confirm first.
- `ctest` with no registered tests exits 0; use `ctest --no-tests=error`.

## Reporting Results

- Report the verification commands you ran with their observed results, and every check you skipped, with the reason.
