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
