# .github/workflows/ — CI Automation

GitHub Actions workflows and supporting scripts for CUBRID CI checks.

## Key Files

| File | Trigger | Purpose |
|------|---------|---------|
| `check.yml` | PR to `develop`, `release/11.**`, `feature/**` | License headers, PR title format, code style, cppcheck, memory_wrapper include order |
| `comment_trigger.yml` | Issue/PR comment events | Comment-triggered automation |
| `codestyle.sh` | Called by `check.yml` | Runs `indent` (C/H), `astyle` (C++/HPP), `google-java-format` (Java) |
| `cppcheck-spr-list` | Used by cppcheck job | Suppression list for known/approved cppcheck warnings |
| `license_headers/` | Used by license job | Reference license header files for comparison |
| `memory_header_include/` | Used by memory-monitor-check job | Reference include patterns for `memory_wrapper.hpp` validation |

## Jobs in `check.yml`

| Job | What it checks |
|-----|----------------|
| `license` | All added/modified `.c/.h/.cpp/.hpp/.sh/.bat/.y/.l/.msg` files must have Apache 2.0 header |
| `pr-style` | PR title must match `^\[[A-Z]+-\d+\]\s.+` (e.g. `[CBRD-12345] Fix ...`) |
| `code-style` | Code formatting via indent/astyle/google-java-format — fails if `git diff` is non-empty after |
| `cppcheck` | Static analysis on changed C/C++ files — fails on any `error:` line |
| `memory-monitor-check` | `.c/.cpp` files in `cubrid/CMakeLists.txt` must include `memory_wrapper.hpp` as the last include with the required comment |

## Conventions for Adding New Workflows

- Target branches: `develop`, `release/11.**`, or `feature/**`
- Use `jitterbit/get-changed-files@v1` to scope checks to only changed files
- Skipped file types / directories: `src/heaplayers/`, backup files (`.c~`, `.cpp.orig`)
- Suppressing a cppcheck warning requires adding it to `cppcheck-spr-list` (requires approval)
- New license header formats go in `license_headers/`

## Gotchas

- `check.yml` uses `continue-on-error: true` on `get-changed-files` — workflows still run even if the file list step fails
- `indent` version is pinned to 2.2.11 (2.2.12 produces different output)
- The memory-monitor-check has exceptions: `lea_heap.c`, `memory_monitor_sr.cpp`, `memory_monitor_api.cpp`, and files not in `cubrid/CMakeLists.txt`
