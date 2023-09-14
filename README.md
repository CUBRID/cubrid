# ![CUBRID LOGO](docs/tangram_24.svg) CUBRID Database Management System

CUBRID is a DBMS supported by an active community of open source developers 
and provides better performance and features necessary for Web services.

Below you will see the brief list of sections to guide you to easily get started.

## Download CUBRID

- http://www.cubrid.org/downloads
- http://ftp.cubrid.org

## Getting Started

Follow tutorials at: 
- http://cubrid.org/getting_started

## Build from Source

### Build Requirements

- A modern C++ compiler capable of C++ 17 is required:
  - GCC 8.3 or newer (devtoolset-8 is recommended)
  - Visual Studio 2017 version 15.0 or newer
- A Java Developer Kit (JDK) 1.8 or newer required
- CMake 2.8 or newer
  - To use ninja build system, CMake 3.16.3 or later is required
- For more information about 3rdparty libraries, see [3rdparty/README.md](3rdparty/README.md)

### How to Install the Build Requiements

  Please refer to the following link:

- [install_build_requirements.md](docs/install_build_requirements.md)

### How to Build CUBRID

On Linux:
```
./build.sh
```
To see usage, run `./build.sh -h`

On Windows:
```
.\win\build.bat /out ${OUTPUT_DIR} dist
```
To see usage, run `.\win\build.bat /h`

## Major References

- CUBRID Official Site:
  - http://www.cubrid.org
  - http://www.cubrid.com
- CUBRID Developer Guide:
  - https://dev.cubrid.org/dev-guide/v/dg-en/
  - https://github.com/CUBRID/cubrid/wiki
- CUBRID Issue Tracker: http://jira.cubrid.org/browse/CBRD
- CUBRID Manuals: http://cubrid.org/manuals

## License

CUBRID is distributed under two licenses according to its component:
- Database engine is under Apache license 2.0 and APIs
- Connectors are under BSD license.

Copyright and license information can be found in the file COPYING.

For more information about CUBRID license policy, visit https://www.cubrid.org/cubrid

## Getting Help

- https://www.reddit.com/r/CUBRID/
      
  If you encounter any difficulties getting started, or just have some questions, or find bugs, or have some suggestions, we kindly ask you to post your thoughts on our subreddit

Sincerely,
Your CUBRID Development Team.
