# 04: Fail-closed validation and diagnostics

**Type:** task

**What to build:** Make every invalid or incompatible explicit `CI_PREBUILT`
request fail during configure, before any ExternalProject or network-capable
fallback is touched, with one stable diagnostic contract that identifies the
failed check.

**Blocked by:** 03 — Matching CI_PREBUILT consumer

**Status:** resolved

## Authority and input

- Parent: <http://jira.cubrid.org/browse/CUBRIDQA-1613>
- Extend the matching consumer path from ticket 03; do not add a second validator.

## Acceptance criteria

- [x] Unknown mode, partial root-only configuration, missing root, relative
      root, and conflicting cache/environment values fail at configure time.
- [x] Root validation requires an existing absolute directory.
- [x] Source and prefix manifests must be non-empty regular files with equal raw
      SHA-256 fingerprints.
- [x] Provenance schema, full producer revision, recorded fingerprint, and
      declared-versus-observed platform are validated.
- [x] Every manifest-declared header, library, and license exists and resolves
      inside the root; missing, empty, non-regular, or escaping artifacts fail.
- [x] Static libraries are non-empty regular files.
- [x] Shared libraries have a relative, non-dangling, in-root symlink chain that
      ends in a non-empty regular payload with the declared ELF SONAME.
- [x] Tests cover missing root/manifest/provenance and each artifact class,
      empty files, dangling/absolute/escaping symlinks, byte mutation, a
      different valid canonical manifest, malformed JSON, unsupported schema,
      and provenance fingerprint mismatch.
- [x] Every failure emits `mode`, `root`, `source_manifest`, `prefix_manifest`,
      `expected_fingerprint`, `actual_fingerprint`, `producer_revision`, and a
      precise `failed_check` value.
- [x] Every negative case asserts both the intended subprocess failure and the
      expected diagnostic fragments; `WILL_FAIL` alone is not sufficient.
- [x] Every negative case proves the ExternalProject/network sentinel remains
      untouched, with no source-build fallback.

## Verification commands

```sh
rm -rf build-thirdparty-contract
cmake -S 3rdparty/tests -B build-thirdparty-contract -G Ninja
ctest --test-dir build-thirdparty-contract \
  -L thirdparty-contract-negative --output-on-failure
ctest --test-dir build-thirdparty-contract \
  -L thirdparty-contract --output-on-failure
git diff --check
```

The test registration must expose the `thirdparty-contract-negative` label so
the complete fail-closed matrix can be rerun independently.

## Non-goals

- Do not add a warning-and-fallback mode.
- Do not make provenance observations part of the specification fingerprint.

## Comments

## Answer

Implemented one fail-closed validator on the existing `CI_PREBUILT` path. It
validates cache/environment parity, the absolute existing root, exact source
and prefix manifest bytes, provenance identity and platform, all declared
headers/static libraries/licenses, and the complete relative unixODBC symlink
chain through an in-root non-empty ELF payload with exact SONAME equality.
Every failure uses the stable eight-field diagnostic contract with a precise
`failed_check`; source-manifest validation preserves its concrete cause.

Registered 54 black-box cases under `thirdparty-contract-negative`. Every case
asserts a failing nested configure, exact diagnostic field values, an untouched
ExternalProject/network sentinel, and no fallback. The final clean verification
passed 54/54 negative cases and 87/87 total contract cases. First-pass Spec
findings for EXTERNAL root conflicts, indirect symlink escapes, regex SONAME
matching, and incomplete diagnostics were repaired; the final Standards and
Spec reviews both returned zero findings.
