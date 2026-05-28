# Contributing to CUBRID

First off, thank you for considering contributing to CUBRID.

There are many ways to contribute, from submitting a bug report and a feature
request, improving the documentation, sharing sample codes, writing a blog or
issuing a patch.

- [How to file a bug report or feature request](#how-to-file-a-bug-report-or-feature-request)
- [How to submit a pull request](#how-to-submit-a-pull-request)
- [PR requirements](#pr-requirements)
- [Code style](#code-style)
- [Memory header rule](#memory-header-rule-server-side-files)
- [Build & test](#build--test)
- [Getting help](#getting-help)

## How to file a bug report or feature request

Please use [our issue tracker](http://jira.cubrid.org/browse/CBRD) to
[create an issue](http://jira.cubrid.org/secure/CreateIssue!default.jspa).

### For a bug report

* Please select a proper "Project". Typical choice is "CUBRID (CBRD)".
* Please specify "Issue Type" as "Correct Error" and describe your issue with
  details, especially a reproduction scenario.
* It would be great if you also show us your expected results as well as the
  current results.

### For a feature suggestion

* Please select a proper "Project". Typical choice is "CUBRID (CBRD)".
* Please specify "Issue Type" as "Improve Function/Performance" and let us
  hear your suggestion.

## How to submit a pull request

We use [the standard GitHub fork-and-pull-request workflow](https://gist.github.com/Chaser324/ce0505fbed06b947d962).

Before submitting, please make sure you have signed and submitted
[the contributors license agreement (CLA)](https://github.com/CUBRID/cubrid/wiki/CUBRID-Contributor's-Agreement).

The workflow is essentially:

1. Fork the CUBRID project.
2. Make a topic branch off `develop`. Make your changes to that branch.
3. Open a pull request against the CUBRID repository. The base branch is
   `develop`.

Useful references:

* [Forking a repository](https://help.github.com/articles/fork-a-repo)
* [Using pull requests](https://help.github.com/articles/using-pull-requests/)
* [How to Contribute to an Open Source Project on GitHub](https://egghead.io/series/how-to-contribute-to-an-open-source-project-on-github)

You can also email us at contact at cubrid.org.

## PR requirements

| Check | Requirement |
|---|---|
| **Title** | Must match `^\[[A-Z]+-\d+\]\s.+` — e.g. `[CBRD-12345] Fix btree overflow` |
| **License header** | New `.c/.h/.cpp/.hpp/.C/.H/.CPP/.HPP/.ipp/.sh/.bat/.y/.l/.msg` files need the Apache 2.0 header (copy from any existing file) |
| **Code style** | Each changed file must be a fixed point under the project formatters (see [Code style](#code-style)) |
| **memory_wrapper.hpp** | In `.c`/`.cpp` files compiled into the server, must be the **last** `#include`, immediately preceded by the marker comment (see [Memory header rule](#memory-header-rule-server-side-files)) |
| **cppcheck** | No new `error:` findings (warnings are allowed) |
| **CLA** | Required before merge |

The corresponding CI jobs live in [`.github/workflows/check.yml`](.github/workflows/check.yml).

## Code style

CUBRID dispatches **three formatters by file extension** via a single script,
[`.github/workflows/codestyle.sh`](.github/workflows/codestyle.sh). Always go
through that script — the option flags are not the tool defaults, and the
extension dispatch matters (a `.c` file is formatted with `indent` even though
the build compiles it as C++17).

| Extension | Formatter | Pinned version |
|---|---|---|
| `.c`, `.h`, `.i` | GNU indent | **2.2.11** (build from source — distro is 2.2.12+) |
| `.cpp`, `.hpp`, `.ipp` | astyle | Ubuntu 24.04 package |
| `.java` | google-java-format | **1.7** |

Install: see [docs/install_build_requirements.md#code-formatters](docs/install_build_requirements.md#code-formatters).

Format one file:

```sh
.github/workflows/codestyle.sh path/to/file.c
```

Reproduce the exact CI gate locally (run from the repo root):

```sh
# 1. List files you've changed. Default: unstaged + staged vs. HEAD.
#    For a feature branch, you may instead want everything since the base:
#    files=$(git diff --name-only cub/develop...HEAD)
files=$(git diff --name-only HEAD)

# 2. Format each one (skip deleted / non-existent paths).
echo "$files" | while read -r f; do
  [ -n "$f" ] && [ -f "$f" ] && .github/workflows/codestyle.sh "$f"
done

# 3. Assert the formatter was a no-op on those files specifically.
#    Scoping the final diff to $files avoids tripping on unrelated unstaged
#    edits elsewhere in your working tree.
echo "$files" | xargs -r git diff -- | tee /tmp/gitdiff
test ! -s /tmp/gitdiff   # exit 0 = CI will pass
```

### Why CI sometimes flags lines you didn't touch

GNU indent is **context-sensitive and not strictly idempotent**. Its output
for your hunk can depend on whitespace and comment alignment elsewhere in the
same file. If a file's existing formatting predates `indent 2.2.11`, running
the formatter on it can rewrite regions far from your change — and those
rewrites land in your PR diff.

**Safe workflow for `.c`/`.h`/`.i` edits** (skip for `.cpp`/`.hpp` — astyle
is much closer to idempotent in practice):

```sh
# 1. Save your logical change as a patch.
git diff -- path/to/file.c > /tmp/my-change.patch

# 2. Revert the file to its committed state.
git checkout HEAD -- path/to/file.c

# 3. Format the baseline. Any diff here is pre-existing drift, NOT yours —
#    decide separately whether to commit it (often: not in this PR).
.github/workflows/codestyle.sh path/to/file.c
git diff path/to/file.c

# 4. Re-apply only your logical change on top of the formatted baseline.
git apply /tmp/my-change.patch
# Alternative: have an AI agent re-introduce just the logical edits hunk by hunk.

# 5. Format once more. Drift is now confined to your touched lines.
.github/workflows/codestyle.sh path/to/file.c
```

**Shortcut for tiny edits**: run the formatter, then `git add -p` and stage
only the hunks that match your intent. The cosmetic drift stays unstaged and
never reaches CI.

> Do **not** use `clang-format` — there is no `.clang-format` in this repo,
> and clang-format output will fail CI.

## Memory header rule (server-side files)

For any `.c`/`.cpp` file listed in `cubrid/CMakeLists.txt`,
`memory_wrapper.hpp` must be the **last** `#include`, immediately preceded by
the marker comment:

```cpp
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"
```

Exceptions:

* `strict_warnings_on.hpp` / `strict_warnings_off.hpp` may appear after
  `memory_wrapper.hpp` (see CBRD-25966).
* `src/heaplayers/lea_heap.c` is third-party and exempt.
* `src/base/memory_monitor_sr.cpp` and `src/base/memory_monitor_api.cpp` are
  the implementation of the memory monitoring module itself and are exempt.

Enforced by the `memory-monitor-check` job in
[`.github/workflows/check.yml`](.github/workflows/check.yml).

## Build & test

* Build: see [README.md](README.md#build-from-source)
  (`./build.sh -m debug` is the most common invocation).
* Unit tests: see [unit_tests/AGENTS.md](unit_tests/AGENTS.md).

## Getting help

* JIRA: <http://jira.cubrid.org/browse/CBRD>
* Wiki: <https://github.com/CUBRID/cubrid/wiki>
* Subreddit: <https://www.reddit.com/r/CUBRID/>
