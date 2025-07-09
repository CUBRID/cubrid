# Third-Party Libraries

This directory contains a CMake build script for third-party softwares that CUBRID uses.

## Dependencies

The following dependencies are required and integrated into the CUBRID build process.  
For more details on how they are built with CUBRID, please refer to the [3rdparty/CMakeLists.txt](CMakeLists.txt):

- libexpat v2.2.5
- Jansson v2.10
- Editline (libedit) for CUBRID version, https://github.com/CUBRID/libedit (Linux Only)
- OpenSSL v1.1.1f
- unixODBC v2.3.9
- LZ4 v1.9.2
- RapidJSON v1.1.0
- Flex and Bison
  - flex 2.5.34 and bison 3.0.0 (On Linux)
  - winflexbison 2.5.22 (On Windows)
