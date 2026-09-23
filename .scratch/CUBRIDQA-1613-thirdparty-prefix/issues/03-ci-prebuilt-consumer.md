# 03: Matching CI_PREBUILT consumer

**Type:** task

**What to build:** Allow a matching relocated prefix to satisfy the existing
CUBRID third-party interface in explicit `CI_PREBUILT` mode without declaring
or running any ExternalProject, while leaving the default developer route
unchanged.

**Blocked by:** 02 — Complete relocatable prefix producer

**Status:** resolved

## Authority and input

- Parent: <http://jira.cubrid.org/browse/CUBRIDQA-1613>
- Input is a successfully validated prefix produced by ticket 02.
- Reuse the contract module and standalone seam established by ticket 01.

## Acceptance criteria

- [x] Unset mode and explicit `EXTERNAL` select the existing ExternalProject
      route and preserve its current target graph and behavior.
- [x] Explicit `CI_PREBUILT` requires `CUBRID_3RDPARTY_ROOT` and selects the
      prefix route before `include(ExternalProject)` or any dependency target is
      declared.
- [x] Cache variables and same-named environment variables are both supported;
      equal duplicate values are accepted.
- [x] After successful validation, all existing dependency include/library
      variables and their link order point into the prefix.
- [x] `EP_INCLUDES`, `EP_LIBS`, and TBB-specific variables keep the interface
      expected by current consumers, while `EP_TARGETS` and `TBB_TARGETS` are
      empty in `CI_PREBUILT` mode.
- [x] The standalone matching-prefix case moves the produced fixture prefix,
      then compiles and links a tiny consumer against its header-only, static,
      and shared artifacts.
- [x] The success case proves the ExternalProject/network sentinel is untouched.
- [x] Unset and `EXTERNAL` standalone cases prove the legacy marker is created.
- [x] The default developer build remains source-compatible and does not require
      any new mode/root setting.

## Verification commands

```sh
rm -rf build-thirdparty-contract
cmake -S 3rdparty/tests -B build-thirdparty-contract -G Ninja
ctest --test-dir build-thirdparty-contract \
  -L thirdparty-contract --output-on-failure

export CUBRID_3RDPARTY_ROOT=/absolute/path/to/a/ticket-02-prefix
cmake -S . -B build-ci-prebuilt-smoke -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCUBRID_3RDPARTY_MODE=CI_PREBUILT \
  -DCUBRID_3RDPARTY_ROOT="$CUBRID_3RDPARTY_ROOT"
cmake --build build-ci-prebuilt-smoke --target cubrid
git diff --check
```

The standalone suite is the primary gate. The top-level `cubrid` target is a
smoke integration, not the Release/OptDebug qualification performed by ticket
05.

## Non-goals

- Do not silently recover from an invalid explicit `CI_PREBUILT` request.
- Do not replace the existing plain variables with imported targets.

## Comments

## Answer

Implemented the matching `CI_PREBUILT` consumer before any ExternalProject
declaration. Cache-only, environment-only, and equal duplicate inputs select a
relocated matching prefix; the existing dependency variables preserve include
paths and library order while `EP_TARGETS` and `TBB_TARGETS` remain empty.
Existing consumers now tolerate those empty dependency lists, including the
pinned CCI submodule's defined-versus-empty compatibility boundary.

The standalone suite has 33 passing cases. Its three consumer cases build a
real fixture prefix, relocate it, compile/link/run against header-only, static,
and versioned shared artifacts, assert every exported interface, and prove the
ExternalProject sentinel is untouched. Unset and explicit `EXTERNAL` cases
still create the legacy marker. A top-level default Release configure retained
all eight ExternalProject targets; a top-level `CI_PREBUILT` configure against a
freshly rebuilt real eight-dependency prefix declared none.

The real prefix producer completed 66/66 steps. The exact host-GCC 11.5 Release
`cubrid` smoke reached engine compilation but stopped on pre-existing engine
warnings promoted by `-Werror`; after demoting the first warning it reached
157/322 before another pre-existing warning. The independent Spec review
accepted this as an environment-limited smoke because the standalone suite is
the ticket-03 primary gate and ticket 05 owns full Release/OptDebug
qualification. Final Standards and Spec reviews passed with zero findings.
