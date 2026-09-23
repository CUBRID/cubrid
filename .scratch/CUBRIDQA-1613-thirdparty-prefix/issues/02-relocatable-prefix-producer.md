# 02: Complete relocatable prefix producer

**Type:** task

**What to build:** Make the public `cubrid_thirdparty_prefix` target build the
real manifest-defined dependencies and publish an atomic, relocatable prefix
containing only the headers, libraries, licenses, manifest, and provenance that
ordinary CUBRID builds consume.

**Blocked by:** 01 — Canonical manifest, contract module, and standalone seam

**Status:** resolved

## Authority and input

- Parent: <http://jira.cubrid.org/browse/CUBRIDQA-1613>
- Begin from the resolved output of ticket 01 and its production contract module.
- Initial platform is Linux x86_64 `build_rl8.10` only.

## Acceptance criteria

- [x] `CUBRID_3RDPARTY_PREFIX_OUTPUT` is mandatory, absolute, and rejected when
      it is the filesystem root, source root, binary root, or an ancestor of
      either project root.
- [x] `cubrid_thirdparty_prefix` depends on all eight real dependency targets
      and publishes only after every dependency and validation step succeeds.
- [x] The producer normalizes installed and source-tree outputs into
      `include/`, `lib/`, `licenses/`, and `share/cubrid-thirdparty/`.
- [x] The final prefix contains every manifest-declared artifact and no
      ExternalProject `Download`, `Source`, `Build`, `Stamp`, object, cache, or
      build-tool state.
- [x] The exact canonical manifest is copied byte-for-byte into the prefix and
      its raw hash matches the source manifest.
- [x] Provenance records schema, fingerprint, full producer commit, dirty state,
      timestamp, declared and observed platform, compiler identity/path/version/
      target, CMake version/generator, optional image references/digests, and
      regular-file/symlink digest inventory.
- [x] Optional provenance image values are explicit `null` when unavailable and
      never affect the specification fingerprint.
- [x] unixODBC retains a relative in-prefix chain from `libodbc.so` through
      `libodbc.so.2` to a non-empty versioned payload whose ELF SONAME is
      `libodbc.so.2`; unused `libodbcinst` and `libodbccr` are excluded.
- [x] A temporary sibling stage is fully validated before publication; a failed
      run cannot leave a consumable-looking partial output.
- [x] Moving the completed prefix to a different absolute path preserves all
      checks, and neither file contents nor symlink targets contain producer
      source, build, staging, or output paths.
- [x] The standalone suite exercises the real target name and covers producer
      layout, atomic failure, unsafe output paths, symlinks, and relocation.

## Verification commands

```sh
producer_root="$(mktemp -d)"
prefix_dir="$producer_root/prefix"
cmake -S . -B "$producer_root/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCUBRID_3RDPARTY_PREFIX_OUTPUT="$prefix_dir"
cmake --build "$producer_root/build" --target cubrid_thirdparty_prefix

cmake -S 3rdparty/tests -B build-thirdparty-contract -G Ninja
ctest --test-dir build-thirdparty-contract \
  -L thirdparty-contract --output-on-failure

find "$prefix_dir" -mindepth 1 -maxdepth 1 -printf '%f\n' | LC_ALL=C sort
test "$(sha256sum 3rdparty/manifest.json | cut -d' ' -f1)" = \
  "$(sha256sum "$prefix_dir/share/cubrid-thirdparty/manifest.json" | cut -d' ' -f1)"
readelf -d "$prefix_dir/lib/libodbc.so.2" | grep 'SONAME.*libodbc.so.2'
git diff --check
```

The expected top-level `find` output is exactly `include`, `lib`, `licenses`,
and `share`.

## Non-goals

- Do not consume the prefix from a full CUBRID build yet.
- Do not add archive, compression, shared-volume, restore, or publisher logic.

## Comments

## Answer

Implemented the real `cubrid_thirdparty_prefix` target for all eight manifest
dependencies. It assembles a sibling staging tree from named JSON artifact
records, validates layout, manifest bytes, licenses, shared-library chains,
SONAMEs, provenance, inventory digests, and forbidden producer paths, then
atomically publishes the validated prefix. The relocation-safe recipes use
manifest revision 2 with fingerprint
`58a2f6e3383ab366c60efd66996e11ac421836c227b702ed32bb705e72dd185e`.

The standalone contract suite now has 30 cases, including real target success,
atomic failure, unsafe output locations, relocation, and clean repeated builds
to an output below the source tree. All 30 pass. A Release build of the actual
eight-dependency graph also passed; the result had exactly `include`, `lib`,
`licenses`, and `share`, matching source/prefix manifest hashes, the required
relative unixODBC chain and `libodbc.so.2` SONAME, no unused ODBC libraries, and
no embedded source/build/stage/output path. Final Standards and Spec reviews
both passed with zero findings.
