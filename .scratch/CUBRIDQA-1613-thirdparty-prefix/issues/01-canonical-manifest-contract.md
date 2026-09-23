# 01: Canonical manifest, contract module, and standalone seam

**Type:** task

**What to build:** Establish one executable source of truth for the Linux x86_64
`build_rl8.10` third-party specification, plus a standalone CMake/CTest seam that
directly exercises the production contract without configuring or building the
CUBRID engine.

**Blocked by:** None (can start immediately)

**Status:** resolved

## Authority

- Parent: <http://jira.cubrid.org/browse/CUBRIDQA-1613>
- Spec: `/home/vimkim/gh/my-cubrid-jira/issues/CUBRIDQA-1613-cubrid-thirdparty-prefix_c63a3b9_codex.md`
- Baseline: `c63a3b993be552ef6ad3ce244c386d5081147958`

## Acceptance criteria

- [x] A canonical `cubrid-thirdparty-manifest-v1` manifest declares all eight
      dependencies in graph order: expat, CUBRID libedit, LZ4, OpenSSL,
      unixODBC, RapidJSON, RE2, and oneTBB.
- [x] The manifest covers source URL/SHA-256, patches, build recipe and semantic
      arguments, fixed environment/flags, normalized outputs, linkage/SONAME,
      platform variant, and license source/destination/SHA-256.
- [x] Canonical-format validation enforces UTF-8 without BOM, LF, two-space
      indentation, recursively sorted object keys, the defined array ordering,
      integer-only `recipe_revision`, and exactly one final LF.
- [x] Closed-schema validation rejects unknown keys, missing fields, duplicate
      dependency names, unsupported recipes/linkage values, and malformed data.
- [x] The exact manifest bytes, including the final LF, are the lowercase
      SHA-256 specification fingerprint; no runtime JSON reserialization is
      used to compute compatibility.
- [x] Existing ExternalProject recipes consume manifest values directly, or a
      temporary equality guard fails configure on any duplicated-value drift.
- [x] A production contract module owns manifest parsing, fingerprinting, mode
      selection, and exported third-party variables so producer and consumer do
      not grow separate interpretations.
- [x] A standalone project under `3rdparty/tests` includes that production
      module and registers a `thirdparty-contract` CTest suite using only local,
      deterministic fixtures.
- [x] The fixture contains a header-only dependency, a static library, and a
      versioned shared payload with relative linker/SONAME symlinks, plus an
      ExternalProject marker/sentinel.
- [x] The suite runs without the root project, engine, JDK, submodules, Catch2,
      or network access and initially proves canonical/schema checks plus
      unset/`EXTERNAL` route compatibility.

## Verification commands

```sh
rm -rf build-thirdparty-contract
cmake -S 3rdparty/tests -B build-thirdparty-contract -G Ninja
ctest --test-dir build-thirdparty-contract --output-on-failure
git diff --check
```

The CTest driver must assert subprocess exit codes and stable diagnostics; a
negative case must not rely on `WILL_FAIL` alone.

## Non-goals

- Do not build or package the real eight-dependency prefix in this ticket.
- Do not switch the full CUBRID build to `CI_PREBUILT` in this ticket.

## Comments

## Answer

Implemented the canonical eight-dependency manifest and production CMake
contract module. The ExternalProject graph now consumes manifest source
identity, semantic arguments, and fixed flags directly; temporary output and
recipe equality guards reject drift. The exact manifest fingerprint is
`4c19d1957a6a4df946ab6020a6e18a8bc4688a195221ca5e6c6abac27520c126`.

Added a standalone 20-case CTest suite covering canonical byte-format and
closed-schema failures, exact raw-byte fingerprinting, unset/`EXTERNAL`
routing, exported output/linkage/SONAME data, and locally built real static/ELF
fixtures with relative shared-library symlinks. A root Debug CMake configure and
all ticket verification commands pass. The final Standards and Spec reviews
both passed with zero findings.
