# Third-Party Libraries

This directory contains a CMake build script for third-party softwares that CUBRID uses.

## Dependencies

The following dependencies are required and integrated into the CUBRID build process.  
For more details on how they are built with CUBRID, please refer to the [3rdparty/CMakeLists.txt](CMakeLists.txt):

- libexpat v2.8.2
- Editline (libedit) for CUBRID version, https://github.com/CUBRID/libedit (Linux Only)
- OpenSSL v3.5.7
- unixODBC v2.3.14 (Linux only)
- LZ4 v1.10.0
- RapidJSON v1.1.0 (2025-02-05 master snapshot; includes the CVE-2024-38517 fix)
- RE2 2023-03-01 (last release before the Abseil dependency)
- Intel oneTBB v2021.11.0 (server-side only)
- Flex and Bison
  - flex 2.5.34+ and bison 3.0.0+ required (On Linux; provided by the system)
  - winflexbison 2.5.22 (On Windows)

The versions above are what the Linux build compiles from source. The Windows
build does not use them: it links prebuilt libraries committed under
`win/3rdparty`, which this upgrade does not touch.

| Library  | Windows prebuilt | Location                                     |
| -------- | ---------------- | -------------------------------------------- |
| libexpat | 2.2.5            | `win/3rdparty/include`, `win/3rdparty/lib64` |
| OpenSSL  | 1.1.1f           | `win/3rdparty/openssl`                       |
| LZ4      | 1.9.2            | `win/3rdparty/lz4`                           |
| RE2      | unversioned      | `win/3rdparty/RE2`                           |

Upgrading the Windows prebuilt libraries is tracked separately.

## Reuse a completed prefix on Linux

A third-party prefix contains the headers and libraries that CUBRID needs.
Prepare it once, then share it between worktrees on the same host and compiler environment.
The producer requires CMake 3.21 or later, Ninja, make, ccache, and the dependency build prerequisites.
CUBRID retains its existing build prerequisites, including the JDK, Flex, Bison, and selected submodules.

Run the producer from the repository root:

```bash
bash 3rdparty/build.sh
```

The default state directory is `.thirdparty/`.
It contains the producer's `build/`, its `ccache/`, and the completed `prefix/`.
CUBRID clean does not remove these directories when they are separate from its build and installation directories.

Use the prefix with matching compilers:

```bash
cmake --preset release \
  -DCMAKE_C_COMPILER=/usr/bin/gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/reuse-install" \
  -DCUBRID_3RDPARTY_ROOT="$PWD/.thirdparty/prefix" \
  -DUNIT_TEST_JSON_BUILDER=ON
cmake --build --preset release --parallel 8
cmake --install build_preset_release
./build_preset_release/bin/test_json_builder
```

Pass an absolute path to `CUBRID_3RDPARTY_ROOT`.
An empty value selects the ordinary build.
A specified prefix must contain all required headers, libraries, licenses, and unixODBC source material.
Missing files or an incompatible fingerprint cause a configure error.
The consumer creates no dependency download or build targets.

The producer accepts these environment variables:

| Variable | Default | Meaning |
| --- | --- | --- |
| `CUBRID_3RDPARTY_STATE` | `$PWD/.thirdparty` | Separate producer state and prefix |
| `CUBRID_3RDPARTY_CC` | `/usr/bin/gcc` | C compiler |
| `CUBRID_3RDPARTY_CXX` | `/usr/bin/g++` | C++ compiler |
| `CUBRID_3RDPARTY_JOBS` | `8` | Jobs within one dependency |
| `CCACHE_DIR` | `<state>/ccache` | Dependency compiler cache |

The producer builds one dependency at a time to limit total parallel work.
Configure CUBRID with the same compiler pair that produced the prefix.
Both Release and OptDebug use the existing per-library dependency options.

Treat a completed prefix as read-only while CUBRID builds use it.
For a dependency change, select a new state directory and prepare a new prefix.
Do not run multiple producers against the same state directory.

### Compatibility

The fingerprint compares the recipe revision, C/C++ compiler kinds and versions, OS name, CPU, and pointer size.
Change the revision in [Fingerprint.cmake](standalone/Fingerprint.cmake) when dependency versions, build options, patches, or the package layout change.
Comments, formatting, and test selection do not require a new revision.

This feature supports native builds on the same host and compiler environment.
It does not check libc, sysroot, or arbitrary compiler flags.
Reuse across hosts or distributions, cross compilation, sanitizer builds, and LTO are outside this support scope.

### Installed Gateway

The prefix build installs its unixODBC driver manager library with CUBRID.
Gateway uses the copy in the installation's `lib/` directory.
The installation also contains the unixODBC source archive and rebuild instructions under `3rdparty/unixODBC/`.
The installation includes the LGPL text in `3rdparty/libodbc_license.txt`.
After installation, Gateway does not need the producer build directory or the original prefix.

Provide the database-specific ODBC driver and register it in `odbcinst.ini`.
Set `CGW_LINK_ODBC_DRIVER_NAME` to the registered driver name.
Before Gateway starts, set `ODBCSYSINI` to the directory that contains `odbcinst.ini`.
For example, `ODBCSYSINI=/etc` selects `/etc/odbcinst.ini`.

### Focused checks

Check successful configuration and rejected incomplete or incompatible prefixes:

```bash
python3 3rdparty/tests/check-package.py .thirdparty/prefix
```

Compile, link, and execute a probe that uses the eight dependencies:

```bash
cmake -S 3rdparty/tests/probe -B .thirdparty/probe -G Ninja \
  -DCubridThirdParty_DIR="$PWD/.thirdparty/prefix/lib/cmake/CubridThirdParty"
cmake --build .thirdparty/probe --parallel 2
ctest --test-dir .thirdparty/probe --output-on-failure
```

To check clean reuse, record prefix and producer stamp snapshots with `tests/snapshot.py`.
Clean the CUBRID build and installation directories, then repeat CUBRID configure, build, and install without the producer.
Compare file contents, modification times, and symlink targets in the snapshots.
Check the installed Gateway's loaded libraries and an actual external database query separately.
