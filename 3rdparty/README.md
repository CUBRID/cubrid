# Third-Party Libraries

This directory contains a CMake build script for third-party softwares that CUBRID uses.

## Dependencies

The following dependencies are required and integrated into the CUBRID build process.  
For more details on how they are built with CUBRID, please refer to the [3rdparty/CMakeLists.txt](CMakeLists.txt):

- libexpat v2.8.2
- Jansson v2.14.1
- Editline (libedit) for CUBRID version, https://github.com/CUBRID/libedit (Linux Only)
- OpenSSL v3.5.7
- unixODBC v2.3.14
- LZ4 v1.10.0
- RapidJSON v1.1.0 (2025-02-05 master snapshot; includes the CVE-2024-38517 fix)
- RE2 2023-03-01 (last release before the Abseil dependency)
- Intel oneTBB v2021.11.0 (server-side only)
- Flex and Bison
  - flex 2.5.34+ and bison 3.0.0+ required (On Linux; provided by the system)
  - winflexbison 2.5.22 (On Windows)
