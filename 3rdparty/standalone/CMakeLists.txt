# Build dependencies independently of the CUBRID build tree.
cmake_minimum_required(VERSION 3.21)
project(CubridThirdPartyProducer LANGUAGES C CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "The third-party producer supports Linux only")
endif()

get_filename_component(CUBRID_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(PREPEND CMAKE_MODULE_PATH "${CUBRID_SOURCE_ROOT}/cmake")
set(CUBRID_3RDPARTY_PRODUCER ON)
set(CUBRID_3RDPARTY_ROOT "")
set(CUBRID_3RDPARTY_JOBS 8 CACHE STRING "Jobs within one dependency")
if(NOT CUBRID_3RDPARTY_JOBS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "CUBRID_3RDPARTY_JOBS must be a positive integer")
endif()
find_program(THIRDPARTY_MAKE NAMES gmake make REQUIRED)
find_program(THIRDPARTY_CCACHE NAMES ccache REQUIRED)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/wrappers")
foreach(language C CXX)
  set(THIRDPARTY_COMPILER "${CMAKE_${language}_COMPILER}")
  configure_file(compiler.in "wrappers/${language}" @ONLY)
  file(CHMOD "${CMAKE_BINARY_DIR}/wrappers/${language}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
endforeach()
set(THIRDPARTY_CC "${CMAKE_BINARY_DIR}/wrappers/C")
set(THIRDPARTY_CXX "${CMAKE_BINARY_DIR}/wrappers/CXX")
add_subdirectory("${CUBRID_SOURCE_ROOT}/3rdparty" recipes)

# The developer preset also needs Catch2; it is header-only in CUBRID's tests.
include(FetchContent)
FetchContent_Declare(thirdparty_catch2
  URL https://codeload.github.com/catchorg/Catch2/tar.gz/255aa5f
  URL_HASH SHA256=954d6dd8e53435aefd804d811bb03e0c697d2966cb662d03796b80fb5af4ed87
  DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads"
  DOWNLOAD_NAME catch2-255aa5f.tar.gz)
FetchContent_GetProperties(thirdparty_catch2)
if(NOT thirdparty_catch2_POPULATED)
  FetchContent_Populate(thirdparty_catch2)
endif()

include(Fingerprint.cmake)
cubrid_thirdparty_identity(THIRDPARTY_IDENTITY)
configure_file(CubridThirdPartyConfig.cmake.in CubridThirdPartyConfig.cmake @ONLY)
configure_file(unixODBC-README.txt.in unixODBC-README.txt @ONLY)
configure_file(Package.cmake.in Package.cmake @ONLY)
add_custom_target(prefix ALL
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/Package.cmake"
  DEPENDS ${EP_TARGETS} ${TBB_TARGETS}
  COMMENT "Assembling standalone CubridThirdParty prefix"
  VERBATIM)
