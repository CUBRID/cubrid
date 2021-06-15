#
#  Copyright 2008 Search Solution Corporation
#  Copyright 2016 CUBRID Corporation
# 
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
# 
#       http://www.apache.org/licenses/LICENSE-2.0
# 
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
# 
#
message(STATUS "======== CMakeLists.txt")

cmake_minimum_required(VERSION 2.8)

function(with_unit_tests res) # checks for -DUNIT_TESTS=ON or -DUNIT_TEST_???=ON
  get_cmake_property(vars VARIABLES)
  foreach(var ${vars})
    if(var MATCHES "^(UNIT_TESTS|UNIT_TEST_.*)$")
      if(${${var}} STREQUAL "ON")
        set(${res} 1 PARENT_SCOPE)  #at least one unit test was requested
      endif()
    endif()
  endforeach()
endfunction(with_unit_tests)

with_unit_tests(AT_LEAST_ONE_UNIT_TEST)
message("    ==== with_unit_tests() = ${AT_LEAST_ONE_UNIT_TEST}")

#also searches for modules onto cubrid/cmake
# add other Find[LIBRARY].cmake scripts in cubrid/cmake
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Debug")
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (default Debug)")
endif()

project(CUBRID)

#TODO: see why the lines below have no effect
#list(APPEND CMAKE_CXX_SOURCE_FILE_EXTENSIONS "c")
#list(REMOVE_ITEM CMAKE_C_SOURCE_FILE_EXTENSIONS "c")

if(UNIX)
  message(STATUS "  OS = UNIX")
elseif(WIN32)
  message(STATUS "  OS = WIN32")
else()
  message(FATAL_ERROR "OS not supported")
endif()

# options
if(UNIX)
  set(ENABLE_SYSTEMTAP_DEFAULT ON)
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
else(UNIX)
  set(ENABLE_SYSTEMTAP_DEFAULT OFF)
endif(UNIX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
message(STATUS "  CMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

if(WIN32)
  set(WINDOWS_BASE_DIR ${CMAKE_SOURCE_DIR}/win)
  set(WINDOWS_EXTERNAL_DIR ${WINDOWS_BASE_DIR}/external)
  set(WINDOWS_EXTERNAL_LIB_DIR ${WINDOWS_EXTERNAL_DIR}/lib64)
  set(WINDOWS_EXTERNAL_INCLUDE_DIR ${WINDOWS_EXTERNAL_DIR}/include)
  # I could not load debug dll for jansson. disabled for now.
  # if(CMAKE_BUILD_TYPE MATCHES "Debug")
  #   set(WINDOWS_EXTERNAL_DLL_DIR ${WINDOWS_EXTERNAL_DIR}/dll_debug)
  # else()
    set(WINDOWS_EXTERNAL_DLL_DIR ${WINDOWS_EXTERNAL_DIR}/dll)
  # endif()
endif(WIN32)

macro(_EXPAND_EXTERNAL_DEPENDENCY depender prefix_dependee)
  message(STATUS "${depender} depends on external project ${prefix_dependee}")

  set(DEPENDEE_INCLUDE_DIR ${${prefix_dependee}_INCLUDES})
  if(DEFINED DEPENDEE_INCLUDE_DIR)
    target_include_directories(${depender} PRIVATE ${DEPENDEE_INCLUDE_DIR})
  else()
    message(WARNING "include directory variable(${prefix_dependee}_INCLUDES) is not set for target ${prefix_dependee}")
  endif()

  set(DEPENDEE_LIBS_DIR ${${prefix_dependee}_LIBS})
  if(DEFINED DEPENDEE_LIBS_DIR)
    target_link_libraries(${depender} LINK_PRIVATE ${DEPENDEE_LIBS_DIR})
  else()
    message(WARNING "libraries directory variable(${prefix_dependee}_LIBS) is not set for target ${prefix_dependee}")
  endif()

  if (TARGET ${${prefix_dependee}_TARGET})
    add_dependencies(${depender} ${${prefix_dependee}_TARGET})
  else(TARGET ${${prefix_dependee}_TARGET})
    message(STATUS "target ${prefix_dependee}_TARGET=${${prefix_dependee}_TARGET} doesn't exist")
  endif(TARGET ${${prefix_dependee}_TARGET})
endmacro(_EXPAND_EXTERNAL_DEPENDENCY)

function(target_external_dependencies depender)
  foreach (arg IN LISTS ARGN)
    _EXPAND_EXTERNAL_DEPENDENCY(${depender} ${arg})
  endforeach()
endfunction(target_external_dependencies)

# FIXME: linux 32bit build mode not working now
option(ENABLE_32BIT "Build for 32-bit banaries (on 64-bit platform)" OFF)
option(ENABLE_SYSTEMTAP "Enable dynamic tracing support using systemtap" ${ENABLE_SYSTEMTAP_DEFAULT})
option(USE_DUMA "Use Detect Unintended Memory Access library" OFF)
option(USE_CUBRID_ENV "Use CUBRID environment variables" ON)
option(WITH_JDBC "Build with JDBC driver" ON)
option(WITH_CMSERVER "Build with Manager server" ON)
option(UNIT_TESTS "Unit tests" OFF)

# options for external libraries (BUNDLED, EXTERAL or SYSTEM)
# expat library sources URL
set(WITH_LIBEXPAT_URL "https://github.com/libexpat/libexpat/releases/download/R_2_2_5/expat-2.2.5.tar.bz2")

# jansson library sources URL
set(WITH_LIBJANSSON_URL "http://www.digip.org/jansson/releases/jansson-2.10.tar.gz")

# editline library sources URL
set(WITH_LIBEDIT_URL "http://thrysoee.dk/editline/libedit-20170329-3.1.tar.gz")

# rapidjson library sources URL
set(WITH_RAPIDJSON_URL "https://github.com/Tencent/rapidjson/archive/v1.1.0.tar.gz")

# openssl library sources URL
set(WITH_LIBOPENSSL_URL "https://www.openssl.org/source/old/1.1.1/openssl-1.1.1f.tar.gz")

set(WITH_EXTERNAL_PREFIX "EXTERNAL" CACHE STRING "External library prefix (default: EXTERNAL)")
set(WITH_BUNDLED_PREFIX "BUNDLED" CACHE STRING "Bundled library prefix (default: BUNDLED)")
set(WITH_SYSTEM_PREFIX "SYSTEM" CACHE STRING "System library prefix (default: SYSTEM)")

set(WITH_LIBEXPAT     "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with expat library (default: EXTERNAL)")
set(WITH_LIBJANSSON   "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with jansson library (default: EXTERNAL)")
set(WITH_LIBEDIT      "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with editline library (default: EXTERNAL)")
set(WITH_LIBNCURSES   "${WITH_SYSTEM_PREFIX}" CACHE STRING "Build with ncurses library (default: SYSTEM)")
set(WITH_LIBOPENSSL   "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with openssl library (default: EXTERNAL)")

message(STATUS "Build with Curses library: ${WITH_LIBNCURSES}")
message(STATUS "Build with editline library: ${WITH_LIBEDIT}")
message(STATUS "Build with expat library: ${WITH_LIBEXPAT}")
message(STATUS "Build with jansson library: ${WITH_LIBJANSSON}")
message(STATUS "Build with openssl library: ${WITH_LIBOPENSSL}")

# Version info
# TODO: remove BUILD_NUMBER file and replace with VERSION file
if(EXISTS ${CMAKE_SOURCE_DIR}/VERSION)
  set(VERSION_FILE VERSION)
elseif(EXISTS ${CMAKE_SOURCE_DIR}/VERSION-DIST)
  set(VERSION_FILE VERSION-DIST)
else(EXISTS ${CMAKE_SOURCE_DIR}/VERSION)
  message(FATAL_ERROR "Could not find a VERSION file")
endif(EXISTS ${CMAKE_SOURCE_DIR}/VERSION)
message(STATUS "Get version information from ${VERSION_FILE}")
# Generate the same file in other directory to trigger cmake re-configure when the file changes
configure_file(${VERSION_FILE} ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${VERSION_FILE}.check_cache COPYONLY)
file(STRINGS ${VERSION_FILE} VERSION_STR)
if(VERSION_STR MATCHES "[0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?(-[a-zA-Z0-9_]+)?")
  string(REGEX MATCHALL "[0-9]+|-([A-Za-z0-9_]+)" VERSION_MATCHES ${VERSION_STR})
  list(LENGTH VERSION_MATCHES VERSION_MATCHES_LENGTH)
  list(GET VERSION_MATCHES 0 CUBRID_MAJOR_VERSION)
  list(GET VERSION_MATCHES 1 CUBRID_MINOR_VERSION)
  list(GET VERSION_MATCHES 2 CUBRID_PATCH_VERSION)
else()
  message(FATAL_ERROR "Failed to parse a version string from ${VERSION_FILE} file")
endif()

if(VERSION_MATCHES_LENGTH GREATER 3)
  list(GET VERSION_MATCHES 3 CUBRID_EXTRA_VERSION)
  if(VERSION_MATCHES_LENGTH GREATER 4)
    list(GET VERSION_MATCHES 4 CUBRID_HASH_TAG)
  endif(VERSION_MATCHES_LENGTH GREATER 4)
else(VERSION_MATCHES_LENGTH GREATER 3)
  find_package(Git)
  if(EXISTS "${CMAKE_SOURCE_DIR}/.git" AND GIT_FOUND)
    set(CUBRID_MAJOR_START_DATE "2019-12-12")
    execute_process(COMMAND ${GIT_EXECUTABLE} --version
    COMMAND awk "{ printf \"%s\", $3 }"
    OUTPUT_VARIABLE GIT_VERSION
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    if(GIT_VERSION VERSION_GREATER 1.7.6)
      execute_process(COMMAND ${GIT_EXECUTABLE} rev-list --count --after ${CUBRID_MAJOR_START_DATE} HEAD
        COMMAND awk "{ printf \"%04d\", $1 }"
        OUTPUT_VARIABLE commit_count RESULT_VARIABLE git_result
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    else(GIT_VERSION VERSION_GREATER 1.7.6)
      # If you have trouble with execute_process on Windows, it is likely an issue caused by git installation.
      # <git-installation-directory>/bin has two files, bash.exe and sh.exe, that messes up CMake's execute_process
      #
      # There are two possible fixes for this issue:
      # 1. Add <git-installation-directory>/usr/bin to %PATH% environment variable
      # 2. Remove <git-installation-directory>/bin from %PATH% environment variable
      # You may prefer the first option if you want to use git command in command prompt.
      #
      # If both fixes fail, just replace execute_process(...) with set (commit_count 0). It is not a critical step to generate the project.
      # Also you may send us a message at https://www.cubrid.org/contact
      execute_process(COMMAND ${GIT_EXECUTABLE} log --after ${CUBRID_MAJOR_START_DATE} --oneline
        COMMAND wc -l
        COMMAND awk "{ printf \"%04d\", $1 }"
        OUTPUT_VARIABLE commit_count RESULT_VARIABLE git_result
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    endif(GIT_VERSION VERSION_GREATER 1.7.6)
    if(git_result)
      message(FATAL_ERROR "Could not get count information from Git")
    endif(git_result)
    set(CUBRID_EXTRA_VERSION ${commit_count})
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
      OUTPUT_VARIABLE commit_hash RESULT_VARIABLE git_result
      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    if(git_result)
      message(FATAL_ERROR "Could not get hash information from Git")
    endif(git_result)
    set(CUBRID_HASH_TAG -${commit_hash})
    # Generate the same file in other directory to trigger cmake re-configure when the HEAD changes
    configure_file(".git/HEAD" ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/HEAD.check_cache COPYONLY)
  else(EXISTS "${CMAKE_SOURCE_DIR}/.git" AND GIT_FOUND)
    set(CUBRID_EXTRA_VERSION 0)
  endif(EXISTS "${CMAKE_SOURCE_DIR}/.git" AND GIT_FOUND)
endif(VERSION_MATCHES_LENGTH GREATER 3)
set(CUBRID_VERSION ${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}.${CUBRID_PATCH_VERSION}.${CUBRID_EXTRA_VERSION}${CUBRID_HASH_TAG})

set(MAJOR_RELEASE_STRING ${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION})
set(PACKAGE_STRING "${PROJECT_NAME} ${MAJOR_RELEASE_STRING}")
set(PRODUCT_STRING "${MAJOR_RELEASE_STRING}")
# TODO: change to string type for RELEASE_STRING, BUILD_NUMBER, BUILD_OS
# TODO: remove #if defined (VERSION_STRING) in src/base/release_string.c
set(RELEASE_STRING ${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}.${CUBRID_PATCH_VERSION})
# BUILD_NUMBER (digital only version string) for legacy codes
set(BUILD_NUMBER ${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}.${CUBRID_PATCH_VERSION}.${CUBRID_EXTRA_VERSION})
set(BUILD_OS ${CMAKE_SYSTEM_NAME})

# BUILD_TYPE
if(CMAKE_BUILD_TYPE MATCHES "Debug")
  set(BUILD_TYPE "debug")
elseif(CMAKE_BUILD_TYPE MATCHES "Coverage|Profile")
  string(TOLOWER "${CMAKE_BUILD_TYPE} debug" BUILD_TYPE)
elseif(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
  set(BUILD_TYPE "release")
else(CMAKE_BUILD_TYPE MATCHES "Debug")
  set(BUILD_TYPE "unknown")
endif(CMAKE_BUILD_TYPE MATCHES "Debug")

if(MSVC)
  configure_file(cmake/version.rc.cmake version.rc)
  # TODO: remove version.rc files from win/*
endif(MSVC)

# Language setting
#enable_language(C CXX)

# System check
if(UNIX)
  if(CMAKE_COMPILER_IS_GNUCC)
    if(ENABLE_32BIT AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.3.4)
      message(SEND_ERROR "Cannot compile 32-bit program with gcc version (${CMAKE_CXX_COMPILER_VERSION})")
      message(FATAL_ERROR "4.3.4 or higher version is required. (ref. https://gcc.gnu.org/bugzilla/show_bug.cgi?id=16185)")
    endif(ENABLE_32BIT AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.3.4)

    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.4.7)
      message(SEND_ERROR "Cannot compile with gcc version ${CMAKE_CXX_COMPILER_VERSION}. Require at least 4.4.7.")
    endif(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.4.7)

    # C flags for both debug and release build
    set(CMAKE_C_COMMON_FLAGS "-ggdb -fno-omit-frame-pointer")
    # C++ flags for both debug and release build
    set(CMAKE_CXX_COMMON_FLAGS "${CMAKE_C_COMMON_FLAGS} -std=c++17")

    # C flags for debug build
    set(CMAKE_C_DEBUG_FLAGS "-Wall -fno-inline ${CMAKE_C_COMMON_FLAGS}")
    # C++ flags for debug build
    set(CMAKE_CXX_DEBUG_FLAGS "-Wall -fno-inline ${CMAKE_CXX_COMMON_FLAGS}")

    # C flags for release build
    set(CMAKE_C_RELEASE_FLAGS "-O2 -DNDEBUG -finline-functions ${CMAKE_C_COMMON_FLAGS}")
    # C++ flags for release build
    set(CMAKE_CXX_RELEASE_FLAGS "-O2 -DNDEBUG -finline-functions ${CMAKE_CXX_COMMON_FLAGS}")

    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${CMAKE_C_DEBUG_FLAGS}")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_DEBUG_FLAGS}")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${CMAKE_C_RELEASE_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_CXX_RELEASE_FLAGS}")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} ${CMAKE_C_RELEASE_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_CXX_RELEASE_FLAGS}")

    if(WITH_SOURCES)
      set(DEBUG_PREFIX_MAP_FLAG "-fdebug-prefix-map=${CMAKE_SOURCE_DIR}/src=${CMAKE_INSTALL_PREFIX}/src")

      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${DEBUG_PREFIX_MAP_FLAG}")
      set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${DEBUG_PREFIX_MAP_FLAG}")
      set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${DEBUG_PREFIX_MAP_FLAG}")
      set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${DEBUG_PREFIX_MAP_FLAG}")
      set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} ${DEBUG_PREFIX_MAP_FLAG}")
      set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${DEBUG_PREFIX_MAP_FLAG}")
    endif(WITH_SOURCES)

    # set has-style
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--hash-style=both")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--hash-style=both")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--hash-style=both")

  else(CMAKE_COMPILER_IS_GNUCC)
    message(FATAL_ERROR "We currently do not support ${CMAKE_CXX_COMPILER_ID} compiler")
  endif(CMAKE_COMPILER_IS_GNUCC)
else(UNIX)
  # turn off LinkIncremental option for release build

  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "INCREMENTAL:YES" "INCREMENTAL:NO" replaceIncFlag ${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO})
  set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/INCREMENTAL:NO ${replaceIncFlag}" )

  string(REPLACE "INCREMENTAL:YES" "INCREMENTAL:NO" replaceIncFlag ${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO})
  set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "/INCREMENTAL:NO ${replaceIncFlag}" )

  #set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX /wd4820 /showIncludes" )
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /wd4820 /std:c++17" )
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W3" )
  set(CMAKE_CXX_STANDARD 17)
endif(UNIX)

# check target platform
# Test 32/64 bits
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(TARGET_PLATFORM_BITS 64)
else()
  message(FATAL_ERROR "Target platform 32bit is no longer supported")
endif()

# FIXME: remove unused checking rule
#include(CheckSymbolExists)
#if(WIN32)
#  check_symbol_exists("_M_AMD64" "" TARGET_ARCH_X64)
#  if(NOT TARGET_ARCH_X64)
#    check_symbol_exists("_M_IX86" "" TARGET_ARCH_X86)
#  endif(NOT TARGET_ARCH_X64)
#else(WIN32)
#  check_symbol_exists("__i386__" "" TARGET_ARCH_X86)
#  check_symbol_exists("__x86_64__" "" TARGET_ARCH_X64)
#endif(WIN32)

message(STATUS "Build ${PROJECT_NAME} ${CUBRID_VERSION} ${TARGET_PLATFORM_BITS}bit ${CMAKE_BUILD_TYPE} on ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")

# generate parser and lexer
set(CSQL_GRAMMAR_INPUT ${CMAKE_SOURCE_DIR}/src/parser/csql_grammar.y)
set(CSQL_GRAMMAR_OUTPUT ${CMAKE_BINARY_DIR}/csql_grammar.c)
set(CSQL_LEXER_INPUT ${CMAKE_SOURCE_DIR}/src/parser/csql_lexer.l)
set(CSQL_LEXER_OUTPUT ${CMAKE_BINARY_DIR}/csql_lexer.c)
set(LOADER_GRAMMAR_INPUT ${CMAKE_SOURCE_DIR}/src/loaddb/load_grammar.yy)
set(LOADER_GRAMMAR_OUTPUT ${CMAKE_BINARY_DIR}/load_grammar.cpp)
set(LOADER_LEXER_INPUT ${CMAKE_SOURCE_DIR}/src/loaddb/load_lexer.l)
set(LOADER_LEXER_OUTPUT ${CMAKE_BINARY_DIR}/load_lexer.cpp)
set(ESQL_GRAMMAR_INPUT ${CMAKE_SOURCE_DIR}/src/executables/esql_grammar.y)
set(ESQL_GRAMMAR_OUTPUT ${CMAKE_BINARY_DIR}/esql_grammar.c)
set(ESQL_LEXER_INPUT ${CMAKE_SOURCE_DIR}/src/executables/esql_lexer.l)
set(ESQL_LEXER_OUTPUT ${CMAKE_BINARY_DIR}/esql_lexer.c)

# bison and flex check
if(WIN32)
  set(WIN_FLEX_BISON_VERSION "2.5.20")
  set(WIN_FLEX_BISON_URL "https://github.com/lexxmark/winflexbison/releases/download/v${WIN_FLEX_BISON_VERSION}/win_flex_bison-${WIN_FLEX_BISON_VERSION}.zip")
  set(WIN_FLEX_BISON_FILENAME "${CMAKE_BINARY_DIR}/external/Download/win_flex_bison.zip")
  file(DOWNLOAD ${WIN_FLEX_BISON_URL} ${WIN_FLEX_BISON_FILENAME})

  set(WIN_FLEX_BISON_INSTALL_DIR "${CMAKE_BINARY_DIR}/external/Install/win_flex_bison")
  file(MAKE_DIRECTORY ${WIN_FLEX_BISON_INSTALL_DIR})
  execute_process(COMMAND ${CMAKE_COMMAND} -E tar -xf ${WIN_FLEX_BISON_FILENAME} WORKING_DIRECTORY ${WIN_FLEX_BISON_INSTALL_DIR})
  list(APPEND CMAKE_PREFIX_PATH ${WIN_FLEX_BISON_INSTALL_DIR})
endif(WIN32)

include(FindBISON)
find_package(BISON 3.0.0)

if(NOT BISON_FOUND)
  if(UNIX)
    message(FATAL_ERROR "Please install bison and flex.")
  else()
    message(
      FATAL_ERROR
      "Please install bison and flex.\nDownload the latest archive from"
      " https://github.com/lexxmark/winflexbison/releases"
      " extract it to a directory in the Path (e.g. %USERPROFILE%\\AppData\\Local\\Microsoft\\WindowsApps)"
      " and retry running cmake."
    )
  endif()
endif(NOT BISON_FOUND)

include(FindFLEX)
find_package(FLEX 2.5.34 REQUIRED)

# CSQL FLEX/BISON targets
# replace old bison directives with new ones
# use add_custom_command because we want to regenerate csql_grammar.yy
# whenever csql_grammar.y is modified
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/csql_grammar.yy
  COMMAND ${CMAKE_COMMAND} -DGRAMMAR_INPUT_FILE=${CMAKE_SOURCE_DIR}/src/parser/csql_grammar.y -DGRAMMAR_OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/csql_grammar.yy -P ${CMAKE_SOURCE_DIR}/cmake/upgrade_bison_grammar.cmake
  MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/src/parser/csql_grammar.y
)

set(CSQL_GRAMMAR_INPUT ${CMAKE_CURRENT_BINARY_DIR}/csql_grammar.yy)

# because csql_grammar.yy are generated through a custom command
# whenever csql_grammar.y is modified, bison and flex will be
# run to create new outputs

bison_target(csql_grammar ${CSQL_GRAMMAR_INPUT} ${CSQL_GRAMMAR_OUTPUT}
  COMPILE_FLAGS "--no-lines --name-prefix=csql_yy -d -r all")
flex_target(csql_lexer ${CSQL_LEXER_INPUT} ${CSQL_LEXER_OUTPUT}
  COMPILE_FLAGS "--noline --never-interactive --prefix=csql_yy")
add_flex_bison_dependency(csql_lexer csql_grammar)
add_custom_target(gen_csql_grammar DEPENDS ${BISON_csql_grammar_OUTPUTS})
add_custom_target(gen_csql_lexer DEPENDS ${FLEX_csql_lexer_OUTPUTS})

# Loader FLEX/BISON targets
bison_target(loader_grammar ${LOADER_GRAMMAR_INPUT} ${LOADER_GRAMMAR_OUTPUT}
  COMPILE_FLAGS "-d -r all")
flex_target(loader_lexer ${LOADER_LEXER_INPUT} ${LOADER_LEXER_OUTPUT}
  COMPILE_FLAGS "--noline")
add_flex_bison_dependency(loader_lexer loader_grammar)
add_custom_target(gen_loader_grammar DEPENDS ${BISON_loader_grammar_OUTPUTS})
add_custom_target(gen_loader_lexer DEPENDS ${FLEX_loader_lexer_OUTPUTS})

# ESQL FLEX/BISON targets
# replace old bison directives with new ones
# use add_custom_command because we want to regenerate esql_grammar.yy
# whenever esql_grammar.y is modified
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/esql_grammar.yy
  COMMAND ${CMAKE_COMMAND} -DGRAMMAR_INPUT_FILE=${CMAKE_SOURCE_DIR}/src/executables/esql_grammar.y -DGRAMMAR_OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/esql_grammar.yy -P ${CMAKE_SOURCE_DIR}/cmake/upgrade_bison_grammar.cmake
  MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/src/executables/esql_grammar.y
)

set(ESQL_GRAMMAR_INPUT ${CMAKE_CURRENT_BINARY_DIR}/esql_grammar.yy)

# because esql_grammar.yy are generated through a custom command
# whenever esql_grammar.y is modified, bison and flex will be
# run to create new outputs

bison_target(esql_grammar ${ESQL_GRAMMAR_INPUT} ${ESQL_GRAMMAR_OUTPUT}
  COMPILE_FLAGS "--no-lines --name-prefix=esql_yy -d -r all")
flex_target(esql_lexer ${ESQL_LEXER_INPUT} ${ESQL_LEXER_OUTPUT}
  COMPILE_FLAGS "--noline --never-interactive --prefix=esql_yy")
add_flex_bison_dependency(esql_lexer esql_grammar)
add_custom_target(gen_esql_grammar DEPENDS ${BISON_esql_grammar_OUTPUTS})
add_custom_target(gen_esql_lexer DEPENDS ${FLEX_esql_lexer_OUTPUTS})

# For java stored procedure server
include(FindJNI)
find_package(JNI 1.5)
if(JNI_FOUND)
  set(JAVA_INC ${JAVA_INCLUDE_PATH} ${JAVA_INCLUDE_PATH2})
endif(JNI_FOUND)

# Build types
if(CMAKE_BUILD_TYPE MATCHES "Coverage")
  set(CMAKE_CXX_FLAGS_COVERAGE "-g -O0 --coverage -fprofile-arcs -ftest-coverage -std=c++17"
    CACHE STRING "Flags used by the c++ compiler during coverage build." FORCE)
  set(CMAKE_C_FLAGS_COVERAGE "-g -O0 --coverage -fprofile-arcs -ftest-coverage"
    CACHE STRING "Flags used by the c compiler during coverage build." FORCE)
  set(CMAKE_EXE_LINKER_FLAGS_COVERAGE ""
    CACHE STRING "Flags used for linking binaries during coverage build." FORCE)
  set(CMAKE_SHARED_LINKER_FLAGS_COVERAGE ""
    CACHE STRING "Flags used for shared libraries during coverage build." FORCE)

  mark_as_advanced(
    CMAKE_CXX_FLAGS_COVERAGE
    CMAKE_C_FLAGS_COVERAGE
    CMAKE_EXE_LINKER_FLAGS_COVERAGE
    CMAKE_SHARED_LINKER_FLAGS_COVERAGE)
endif(CMAKE_BUILD_TYPE MATCHES "Coverage")

if(CMAKE_BUILD_TYPE MATCHES "Profile")
  set(CMAKE_CXX_FLAGS_PROFILE "-g -pg -std=c++17"
    CACHE STRING "Flags used by the c++ compiler during profile build." FORCE)
  set(CMAKE_C_FLAGS_PROFILE "-g -pg"
    CACHE STRING "Flags used by the c compiler during profile build." FORCE)

  mark_as_advanced(CMAKE_CXX_FLAGS_PROFILE CMAKE_C_FLAGS_PROFILE)
endif(CMAKE_BUILD_TYPE MATCHES "Profile")


# source directories
set(API_DIR                  ${CMAKE_SOURCE_DIR}/src/api)
set(BASE_DIR                 ${CMAKE_SOURCE_DIR}/src/base)
set(BROKER_DIR               ${CMAKE_SOURCE_DIR}/src/broker)
set(CCI_DIR                  ${CMAKE_SOURCE_DIR}/src/cci)
set(CM_COMMON_DIR            ${CMAKE_SOURCE_DIR}/src/cm_common)
set(COMMUNICATION_DIR        ${CMAKE_SOURCE_DIR}/src/communication)
set(COMPAT_DIR               ${CMAKE_SOURCE_DIR}/src/compat)
set(CONNECTION_DIR           ${CMAKE_SOURCE_DIR}/src/connection)
set(EXECUTABLES_DIR          ${CMAKE_SOURCE_DIR}/src/executables)
set(HEAPLAYERS_DIR           ${CMAKE_SOURCE_DIR}/src/heaplayers)
set(JSP_DIR                  ${CMAKE_SOURCE_DIR}/src/jsp)
set(LOADDB_DIR               ${CMAKE_SOURCE_DIR}/src/loaddb)
set(MONITOR_DIR              ${CMAKE_SOURCE_DIR}/src/monitor)
set(OBJECT_DIR               ${CMAKE_SOURCE_DIR}/src/object)
set(ODBC_DIR                 ${CMAKE_SOURCE_DIR}/src/odbc)
set(OLEDB_DIR                ${CMAKE_SOURCE_DIR}/src/oledb)
set(OPTIMIZER_DIR            ${CMAKE_SOURCE_DIR}/src/optimizer)
set(PARSER_DIR               ${CMAKE_SOURCE_DIR}/src/parser)
set(QUERY_DIR                ${CMAKE_SOURCE_DIR}/src/query)
set(REPLICATION_DIR          ${CMAKE_SOURCE_DIR}/src/replication)
set(SESSION_DIR              ${CMAKE_SOURCE_DIR}/src/session)
set(STORAGE_DIR              ${CMAKE_SOURCE_DIR}/src/storage)
set(THREAD_DIR               ${CMAKE_SOURCE_DIR}/src/thread)
set(TOOLS_DIR                ${CMAKE_SOURCE_DIR}/src/tools)
set(TRANSACTION_DIR          ${CMAKE_SOURCE_DIR}/src/transaction)
set(WIN_TOOLS_DIR            ${CMAKE_SOURCE_DIR}/src/win_tools)
set(XASL_DIR                 ${CMAKE_SOURCE_DIR}/src/xasl)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(
  include
  src/api
  src/base
  src/broker
  src/cci
  src/communication
  src/compat
  src/connection
  src/executables
  src/heaplayers
  src/jsp
  src/loaddb
  src/monitor
  src/object
  src/optimizer
  src/parser
  src/replication
  src/query
  src/session
  src/storage
  src/thread
  src/tools
  src/transaction
  src/xasl
  )

include(CheckFunctionExists)
check_function_exists(asprintf HAVE_ASPRINTF)
check_function_exists(vasprintf HAVE_VASPRINTF)
check_function_exists(basename HAVE_BASENAME)
check_function_exists(dirname HAVE_DIRNAME)
check_function_exists(ctime_r HAVE_CTIME_R)
check_function_exists(localtime_r HAVE_LOCALTIME_R)
check_function_exists(drand48_r HAVE_DRAND48_R)
check_function_exists(gethostbyname_r HAVE_GETHOSTBYNAME_R)
check_function_exists(getopt_long HAVE_GETOPT_LONG)
check_function_exists(open_memstream HAVE_OPEN_MEMSTREAM)
check_function_exists(strdup HAVE_STRDUP)
check_function_exists(strlcpy HAVE_STRLCPY)

if(WIN32)
  set(NOMINMAX 1)
endif(WIN32)

include(CheckIncludeFile)
check_include_file(err.h HAVE_ERR_H)
check_include_file(getopt.h HAVE_GETOPT_H)
check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(libgen.h HAVE_LIBGEN_H)
check_include_file(limits.h HAVE_LIMITS_H)
if(NOT HAVE_LIMITS_H)
  set(PATH_MAX 512)
  set(NAME_MAX 255)
  set(LINE_MAX 2048)
endif(NOT HAVE_LIMITS_H)
if(WIN32)
  set(LINE_MAX 2048)
endif(WIN32)

check_include_file(memory.h HAVE_MEMORY_H)
check_include_file(nl_types.h HAVE_NL_TYPES_H)
check_include_file(regex.h HAVE_REGEX_H)
check_include_file(rpc/des_crypt.h HAVE_RPC_DES_CRYPT_H)
check_include_file(stdbool.h HAVE_STDBOOL_H)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(stdlib.h HAVE_STDLIB_H)
check_include_file(stddef.h HAVE_STDDEF_H)
check_include_file(string.h HAVE_STRING_H)
check_include_file(strings.h HAVE_STRINGS_H)
check_include_file(sys/param.h HAVE_SYS_PARAM_H)
check_include_file(sys/stat.h HAVE_SYS_STAT_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)
check_include_file(unistd.h HAVE_UNISTD_H)
if(HAVE_STDLIB_H AND HAVE_STDDEF_H)
  set(STDC_HEADERS 1)
endif(HAVE_STDLIB_H AND HAVE_STDDEF_H)

include(CheckTypeSize)
check_type_size(char SIZEOF_CHAR)
check_type_size(short SIZEOF_SHORT)
check_type_size(int SIZEOF_INT)
check_type_size(long SIZEOF_LONG)
check_type_size("long long" SIZEOF_LONG_LONG)
check_type_size("void *" SIZEOF_VOID_P)
check_type_size(byte_t BYTE_T)
check_type_size(int8_t INT8_T)
check_type_size(int16_t INT16_T)
check_type_size(int32_t INT32_T)
check_type_size(int64_t INT64_T)
check_type_size(intptr_t INTPTR_T)
check_type_size(uint8_t UINT8_T)
check_type_size(uint16_t UINT16_T)
check_type_size(uint32_t UINT32_T)
check_type_size(uint64_t UINT64_T)
check_type_size(uintptr_t UINTPTR_T)
check_type_size("off_t" SIZEOF_OFF_T)
if(NOT SIZEOF_OFF_T)
  set(off_t "long int")
endif()
check_type_size("size_t" SIZEOF_SIZE_T)
if(NOT SIZEOF_SIZE_T)
  set(size_t "unsinged int")
endif()
check_type_size("pid_t" SIZEOF_PID_T)
if(NOT SIZEOF_PID_T)
  # TODO: util_sa.c:79 remove typedef int pid_t
  if(NOT WIN32)
    set(pid_t "int")
  endif(NOT WIN32)
endif()
set(CMAKE_REQUIRED_FLAGS "-D_LARGEFILE64_SOURCE -finline-functions")
check_type_size("off64_t" SIZEOF_OFF64_T)
unset(CMAKE_REQUIRED_FLAGS)

include(CheckPrototypeDefinition)
if(HAVE_GETHOSTBYNAME_R)
  check_prototype_definition(gethostbyname_r
    "int gethostbyname_r(const char *name, struct hostent *hp, char *buf, size_t buflen, struct hostent **result, int *herr)"
    "0"
    "netdb.h"
    HAVE_GETHOSTBYNAME_R_GLIBC)
  check_prototype_definition(gethostbyname_r
    "struct hostent *gethostbyname_r(const char *name, struct hostent *hp, char *buf, size_t buflen, int *herr)"
    "NULL"
    "netdb.h"
    HAVE_GETHOSTBYNAME_R_SOLARIS)
  check_prototype_definition(gethostbyname_r
    "int gethostbyname_r(const char *name, struct hostent *hp, struct hostent_data *hdata)"
    "0"
    "netdb.h"
    HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
endif(HAVE_GETHOSTBYNAME_R)

include(CheckSymbolExists)
if(NOT HAVE_STDBOOL_H)
  check_symbol_exists( "_Bool" stdbool.h HAVE__BOOL)
endif(NOT HAVE_STDBOOL_H)

include(CheckCSourceCompiles)
check_c_source_compiles("#include <sys/time.h>\n#include <time.h>\nmain(){}" TIME_WITH_SYS_TIME)
check_c_source_compiles("#include <stdint.h>
int main() {
  uint64_t i, j, r;
  i = 100; j = 200; r = __sync_bool_compare_and_swap(&i, 100, j);
  if (r == 0 || i != 200) { return(1); }
  i = 100; j = 200; r = __sync_bool_compare_and_swap(&i, 101, j);
  if (r != 0 || i != 100) { return(1); }
  i = 100; j = 200; r = __sync_add_and_fetch(&i, j);
  if (r != 300 || i != 300) { return(1); }
  i = 100; r = __sync_lock_test_and_set(&i, 200);
  if (r != 100 || i != 200) { return(1); }
  return(0);
}"
  HAVE_GCC_ATOMIC_BUILTINS)

include(TestBigEndian)
test_big_endian(WORDS_BIGENDIAN)

if(UNIX)
  include(FindThreads)
endif(UNIX)


configure_file(cmake/config.h.cmake config.h)
# TODO: merge version.h into config.h ?
configure_file(cmake/version.h.cmake version.h)

# platform specifie configurations
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  # Definitions for system
  add_definitions(-DGCC -DLINUX -D_GNU_SOURCE -DI386 -DX86)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wwrite-strings -Wno-cast-qual -Wmissing-prototypes -Wredundant-decls -Wextra -Wno-unused -Wno-format-security -pthread")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wwrite-strings -Wno-cast-qual -Wredundant-decls -Wextra -Wno-unused -Wno-unused-parameter -Wno-format-security -pthread")

  if(SIZEOF_OFF64_T)
    add_definitions( -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64)
  endif(SIZEOF_OFF64_T)

  if(ENABLE_32BIT AND ${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64")
    set(CMAKE_C_COMPILER_ARG1 "-m32 -march=i686")
    set(CMAKE_CXX_COMPILER_ARG1 "-m32 -march=i686")
    #set(CMAKE_C_FLAGS "-m32 ${CMAKE_C_FLAGS}")
    #set(CMAKE_CXX_FLAGS "-m32 ${CMAKE_CXX_FLAGS}")
  endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "AIX")
  # Definitions for system
  add_definitions(-DGCC -DAIX -D_GNU_SOURCE -DPPC -fsigned-char -D_THREAD_SAFE)

  if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64")
    add_definitions(-DAIX_PPC64 -D__WORDSIZE=64)
    set(CMAKE_C_COMPILER_ARG1 "-pthread -maix64")
    set(CMAKE_CXX_COMPILER_ARG1 "-pthread -maix64")
    set(CMAKE_EXE_LINKER_FLAGS "-Wl,-bbigtoc -Wl,-brtl -Wl,-bnortllib -Wl,-bnosymbolic -Wl,-bnoautoexp -Wl,-bexpall -Wl,-bmaxdata:0x200000000 -Wl,-bnoipath" CACHE INTERNAL "" FORCE)
  else()
    add_definitions(-D_LARGE_FILES -DAIX_PPC32 -D__WORDSIZE=32)
    set(CMAKE_C_COMPILER_ARG1 "-pthread -maix32")
    set(CMAKE_C_COMPILER_ARG1 "-pthread -maix32")
    set(CMAKE_EXE_LINKER_FLAGS "-Wl,-bbigtoc -Wl,-brtl -Wl,-bnortllib -Wl,-bnosymbolic -Wl,-bnoautoexp -Wl,-bexpall -Wl,-bmaxdata:0x80000000 -Wl,-bnoipath" CACHE INTERNAL "" FORCE)
  endif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64")
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
  add_definitions(-DWIN -DWINDOWS -D_CRT_SECURE_NO_WARNINGS /wd4274 /wd4996)
  if(TARGET_PLATFORM_BITS EQUAL 64)
    string(REPLACE "/DWIN32" "/D_WIN64" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  endif(TARGET_PLATFORM_BITS EQUAL 64)
  if(MSVC_VERSION GREATER 1400)
    # experimental option (it can lead the build system to a halt or creating a bad object files)
    set(PARALLEL_JOBS "0" CACHE STRING "Specifies the number of jots to build parallel. 0 means OFF")
    if(PARALLEL_JOBS GREATER $ENV{NUMBER_OF_PROCESSORS})
      add_definitions(/MP)
    elseif(PARALLEL_JOBS GREATER 0)
      add_definitions(/MP${PARALLEL_JOBS})
    endif(PARALLEL_JOBS GREATER $ENV{NUMBER_OF_PROCESSORS})
  endif(MSVC_VERSION GREATER 1400)
endif(CMAKE_SYSTEM_NAME MATCHES "Linux")

# common definitions
set(COMMON_DEFS SYSV MAXPATHLEN=1024 -D_REENTRANT)

# For systemtap options
if(ENABLE_SYSTEMTAP)
  find_program(DTRACE dtrace)
  if(NOT DTRACE)
    message(FATAL_ERROR "The dtrace tool was not found. check PATH or install dtrace")
  endif(NOT DTRACE)
  mark_as_advanced(DTRACE)
  execute_process(COMMAND ${DTRACE} -V OUTPUT_VARIABLE dtrace_version)
  if(dtrace_version MATCHES "Sun D" OR dtrace_version MATCHES "Oracle D")
    message(WARNING "Sun or Oracle D is not supported")
    message(FATAL_ERROR "Please use sourceware.org's systemtap [https://sourceware.org/systemtap/]")
  endif(dtrace_version MATCHES "Sun D" OR dtrace_version MATCHES "Oracle D")
  check_include_file(sys/sdt.h HAVE_SYS_SDT_H)
  if(NOT HAVE_SYS_SDT_H)
    message(FATAL_ERROR "The systemtap support needs sys/sdt.h header")
  endif(NOT HAVE_SYS_SDT_H)

  set(PROBES_SOURCE ${CMAKE_SOURCE_DIR}/contrib/systemtap/probes.d)
  set(PROBES_HEADER ${CMAKE_BINARY_DIR}/probes.h)
  set(PROBES_OBJECT ${CMAKE_BINARY_DIR}/probes.o)
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/probes.h ${PROBES_OBJECT}
    COMMAND ${DTRACE} -C -h -s ${PROBES_SOURCE} -o ${PROBES_HEADER}
    COMMAND ${DTRACE} -C -G -s ${PROBES_SOURCE} -o ${PROBES_OBJECT}
    DEPENDS ${PROBES_SOURCE}
    )
  add_custom_target(gen_probes DEPENDS ${PROBES_SOURCE} ${PROBES_HEADER} ${PROBES_OBJECT})
  set_target_properties(gen_probes PROPERTIES LINKER_LANGUAGE C)
endif(ENABLE_SYSTEMTAP)


# For duma option
if(USE_DUMA)
  find_library(DUMA_LIBRARIES duma)
  if(NOT DUMA_LIBRARIES)
    message(FATAL_ERROR "Cannot find DUMA library")
  endif(NOT DUMA_LIBRARIES)
  message(STATUS "Use DUMA library")
endif(USE_DUMA)


# For CUBRID environment variable option
if(USE_CUBRID_ENV)
  message(STATUS "Default install prefix: ${CMAKE_INSTALL_PREFIX}")
  set(CUBRID_PREFIXDIR .)
  set(CUBRID_BINDIR bin)
  set(CUBRID_LIBDIR lib)
  set(CUBRID_SRCDIR src)
  set(CUBRID_INCLUDEDIR include)
  set(CUBRID_VARDIR var)
  set(CUBRID_DATADIR share)
  set(CUBRID_CONFDIR conf)
  set(CUBRID_JAVADIR java)
  set(CUBRID_LOCALEDIR msg)
  set(CUBRID_LOGDIR log)
  set(CUBRID_TMPDIR tmp)
  set(CUBRID_SHLOCALESDIR locales)
  set(CUBRID_TZDIR timezones)
  set(CUBRID_DEMODIR demo)
  set(CUBRID_COMPATDIR compat)
else(USE_CUBRID_ENV)
  message(WARNING "This build does not use CUBRID environment variables")
  list(APPEND COMMON_DEFS DO_NOT_USE_CUBRIDENV)

  set(CUBRID_PREFIXDIR ${CMAKE_INSTALL_PREFIX} CACHE STRING "The prefix directory for CUBRID (default: ${CMAKE_INSTALL_PREFIX}")
  list(APPEND DIR_DEFS CUBRID_PREFIXDIR="${CUBRID_PREFIXDIR}")
  set(CUBRID_BINDIR bin CACHE STRING "The directory for binary files")
  if(IS_ABSOLUTE ${CUBRID_BINDIR})
    list(APPEND DIR_DEFS CUBRID_BINDIR="${CUBRID_BINDIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_BINDIR="${CUBRID_PREFIXDIR}/${CUBRID_BINDIR}")
  endif()
  set(CUBRID_LIBDIR lib CACHE STRING "The directory for library files")
  if(IS_ABSOLUTE ${CUBRID_LIBDIR})
    list(APPEND DIR_DEFS CUBRID_LIBDIR="${CUBRID_LIBDIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_LIBDIR="${CUBRID_PREFIXDIR}/${CUBRID_LIBDIR}")
  endif()
  set(CUBRID_SRCDIR src CACHE STRING "The directory for source files")
  if(IS_ABSOLUTE ${CUBRID_SRCDIR})
    list(APPEND DIR_DEFS CUBRID_SRCDIR="${CUBRID_SRCDIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_SRCDIR="${CUBRID_PREFIXDIR}/${CUBRID_SRCDIR}")
  endif()
  set(CUBRID_VARDIR var CACHE STRING "The directory for variable data files")
  if(IS_ABSOLUTE ${CUBRID_VARDIR})
    list(APPEND DIR_DEFS CUBRID_VARDIR="${CUBRID_VARDIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_VARDIR="${CUBRID_PREFIXDIR}/${CUBRID_VARDIR}")
  endif()
  set(CUBRID_DATADIR share CACHE STRING "The directory for shared data files")
  if(IS_ABSOLUTE ${CUBRID_DATADIR})
    list(APPEND DIR_DEFS CUBRID_DATADIR="${CUBRID_DATADIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_DATADIR="${CUBRID_PREFIXDIR}/${CUBRID_DATADIR}")
  endif()
  set(CUBRID_CONFDIR conf CACHE STRING "The directory for configuration files")
  if(IS_ABSOLUTE ${CUBRID_CONFDIR})
    list(APPEND DIR_DEFS CUBRID_CONFDIR="${CUBRID_CONFDIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_CONFDIR="${CUBRID_PREFIXDIR}/${CUBRID_CONFDIR}")
  endif()
  set(CUBRID_JAVADIR java CACHE STRING "The directory for JSP file")
  if(IS_ABSOLUTE ${CUBRID_JAVADIR})
    list(APPEND DIR_DEFS CUBRID_JAVADIR="${CUBRID_JAVADIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_JAVADIR="${CUBRID_PREFIXDIR}/${CUBRID_JAVADIR}")
  endif()
  set(CUBRID_LOCALEDIR msg CACHE STRING "The directory for message files")
  if(IS_ABSOLUTE ${CUBRID_LOCALEDIR})
    list(APPEND DIR_DEFS CUBRID_LOCALEDIR="${CUBRID_LOCALEDIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_LOCALEDIR="${CUBRID_PREFIXDIR}/${CUBRID_LOCALEDIR}")
  endif()
  set(CUBRID_LOGDIR log CACHE STRING "The directory for log files")
  if(IS_ABSOLUTE ${CUBRID_LOGDIR})
    list(APPEND DIR_DEFS CUBRID_LOGDIR="${CUBRID_LOGDIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_LOGDIR="${CUBRID_PREFIXDIR}/${CUBRID_LOGDIR}")
  endif()
  set(CUBRID_TMPDIR tmp CACHE STRING "The directory for temporary files")
  if(IS_ABSOLUTE ${CUBRID_TMPDIR})
    list(APPEND DIR_DEFS CUBRID_TMPDIR="${CUBRID_TMPDIR}")
  else()
    list(APPEND DIR_DEFS CUBRID_TMPDIR="${CUBRID_PREFIXDIR}/${CUBRID_TMPDIR}")
  endif()
  # directories for install
  set(CUBRID_INCLUDEDIR include CACHE STRING "The directory for header files")
  set(CUBRID_SHLOCALESDIR locales CACHE STRING "The directory for locale files")
  set(CUBRID_TZDIR timezones CACHE STRING "The directory for timezone files")
  set(CUBRID_DEMODIR demo CACHE STRING "The directory for demo data files")
  set(CUBRID_COMPATDIR compat CACHE STRING "The directory for compatible files")
endif(USE_CUBRID_ENV)


# For external library options
include(ExternalProject)
set_property(DIRECTORY PROPERTY EP_BASE "${CMAKE_BINARY_DIR}/external")
set(DEFAULT_CONFIGURE_OPTS <SOURCE_DIR>/configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/external --enable-static --disable-shared --with-pic CFLAGS=-w CXXFLAGS=-w)

# WITH_LIBEXPAT can have multiple values with different meanings
# on Linux:
# * "EXTERNAL" - (default) builds expat library from URL stored in ${WITH_LIBEXPAT_URL} uses the library created by the build
# * "SYSTEM"   - use expat library located in:
#                  ${WITH_EXTERNAL_LIBS_PATH}
#                  ${WITH_LIBEXPAT_ROOT_PATH}/lib
#                use expat headers located in:
#                  ${WITH_EXTERNAL_HEADERS_PATH}
#                  ${WITH_LIBEXPAT_ROOT_PATH}/include
# on Windows:
# * "EXTERNAL" - (default) uses the prebuilt library from cubrid/win/external
if(WITH_LIBEXPAT STREQUAL "EXTERNAL")
  set(LIBEXPAT_TARGET libexpat)
  list(APPEND EP_TARGETS ${LIBEXPAT_TARGET})
  if(UNIX)
    externalproject_add(${LIBEXPAT_TARGET}
      URL                  ${WITH_LIBEXPAT_URL}
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    ${DEFAULT_CONFIGURE_OPTS} --without-xmlwf  --without-docbook
      BUILD_COMMAND        make AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
    set(LIBEXPAT_LIBS ${CMAKE_CURRENT_BINARY_DIR}/external/lib/libexpat.a)
    set(LIBEXPAT_INCLUDES ${CMAKE_CURRENT_BINARY_DIR}/external/include/expat)
  elseif(WIN32)
    set(LIBEXPAT_LIBS ${WINDOWS_EXTERNAL_LIB_DIR}/libexpat.lib)
    set(LIBEXPAT_INCLUDES ${WINDOWS_EXTERNAL_INCLUDE_DIR})
    add_custom_target(${LIBEXPAT_TARGET}
      COMMAND ${CMAKE_COMMAND} -E echo "  copying ${WINDOWS_EXTERNAL_DLL_DIR}/libexpat64.dll to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/libexpat.dll"
      COMMAND ${CMAKE_COMMAND} -E copy ${WINDOWS_EXTERNAL_DLL_DIR}/libexpat64.dll ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/libexpat.dll
    )
  endif(UNIX)
elseif(WITH_LIBEXPAT STREQUAL "SYSTEM")
  if(UNIX)
    find_library(LIBEXPAT_LIBS NAMES libexpat.a PATHS ${WITH_EXTERNAL_LIBS_PATH} ${WITH_LIBEXPAT_ROOT_PATH}/lib REQUIRED)
    find_path(LIBEXPAT_INCLUDES NAMES expat.h expat_config.h expat_external.h PATHS ${WITH_EXTERNAL_HEADERS_PATH} ${WITH_LIBEXPAT_ROOT_PATH}/include REQUIRED)

    message(STATUS "Found expat library: ${LIBEXPAT_LIBS}")
    message(STATUS "Found expat headers: ${LIBEXPAT_INCLUDES}")
  endif(UNIX)
endif()
list(APPEND EP_INCLUDES ${LIBEXPAT_INCLUDES})
list(APPEND EP_LIBS ${LIBEXPAT_LIBS})

# WITH_LIBJANSSON can have multiple values with different meanings
# on Linux:
# * "EXTERNAL" - (default) builds jansson library from URL stored in ${WITH_LIBJANSSON_URL} uses the library created by the build
# * "SYSTEM"   - use jansson library located in:
#                  ${WITH_EXTERNAL_LIBS_PATH}
#                  ${WITH_LIBJANSSON_ROOT_PATH}/lib
#                use jansson headers located in:
#                  ${WITH_EXTERNAL_HEADERS_PATH}
#                  ${WITH_LIBJANSSON_ROOT_PATH}/include
# on Windows:
# * "EXTERNAL" - (default) uses the prebuilt library from cubrid/win/external
if(WITH_LIBJANSSON STREQUAL "EXTERNAL")
  set(LIBJANSSON_TARGET libjansson)
  list(APPEND EP_TARGETS ${LIBJANSSON_TARGET})
  if(UNIX)
    externalproject_add(${LIBJANSSON_TARGET}
      URL                  ${WITH_LIBJANSSON_URL}
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        make all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
    set(LIBJANSSON_LIBS ${CMAKE_CURRENT_BINARY_DIR}/external/lib/libjansson.a)
    set(LIBJANSSON_INCLUDES ${CMAKE_CURRENT_BINARY_DIR}/external/include/jansson)
  else(UNIX)
    # WIN32
    set(LIBJANSSON_LIBS ${WINDOWS_EXTERNAL_LIB_DIR}/jansson64.lib)
    set(LIBJANSSON_INCLUDES ${WINDOWS_EXTERNAL_INCLUDE_DIR})
    add_custom_target(${LIBJANSSON_TARGET}
      COMMAND ${CMAKE_COMMAND} -E echo "  copying ${WINDOWS_EXTERNAL_DLL_DIR}/jansson64.dll to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/jansson.dll"
      COMMAND ${CMAKE_COMMAND} -E copy ${WINDOWS_EXTERNAL_DLL_DIR}/jansson64.dll ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/jansson.dll
      COMMAND ${CMAKE_COMMAND} -E copy ${WINDOWS_EXTERNAL_DLL_DIR}/jansson64.dll ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/jansson64.dll
    )
  endif(UNIX)
elseif(WITH_LIBJANSSON STREQUAL "SYSTEM")
  if(UNIX)
    find_library(LIBJANSSON_LIBS NAMES libjansson.a PATHS ${WITH_EXTERNAL_LIBS_PATH} ${WITH_LIBJANSSON_ROOT_PATH}/lib REQUIRED)
    find_path(LIBJANSSON_INCLUDES NAMES jansson.h jansson_config.h PATHS ${WITH_EXTERNAL_HEADERS_PATH} ${WITH_LIBJANSSON_ROOT_PATH}/include REQUIRED)

    message(STATUS "Found jansson library: ${LIBJANSSON_LIBS}")
    message(STATUS "Found jansson headers: ${LIBJANSSON_INCLUDES}")
  endif(UNIX)
endif()
list(APPEND EP_INCLUDES ${LIBJANSSON_INCLUDES})
list(APPEND EP_LIBS ${LIBJANSSON_LIBS})

# WITH_LIBNCURSES can have multiple values with different meanings
# on Linux:
# * "SYSTEM"   - uses ncurses installed in the system (for more info check https://cmake.org/cmake/help/v3.10/module/FindCurses.html)
set(LIBNCURSES_TARGET libncurses)
if(UNIX)
  if(WITH_LIBNCURSES STREQUAL "SYSTEM")
    include(FindCurses)
    find_package(Curses REQUIRED)
    CHECK_LIBRARY_EXISTS("${CURSES_LIBRARIES}" wtimeout "" LIBNCURSES_HAS_WTIMEOUT)
    set(LIBNCURSES_INCLUDES "${CURSES_INCLUDE_DIRS}")
    set(LIBNCURSES_LIBS "${CURSES_LIBRARIES}")

    if(NOT LIBNCURSES_HAS_WTIMEOUT)
      message(STATUS "search for tinfo library")
      find_library(TINFO_LIBRARY NAMES tinfo)
      if(TINFO_LIBRARY)
        message(STATUS "found tinfo library")
        CHECK_LIBRARY_EXISTS("${TINFO_LIBRARY}" wtimeout "" TINFO_HAS_WTIMEOUT)
        if(TINFO_HAS_WTIMEOUT)
          set(CURSES_LIBRARIES ${CURSES_LIBRARIES} ${TINFO_LIBRARY})
        else(TINFO_HAS_WTIMEOUT)
          message(FATAL_ERROR "tinfo library does not have wtimeout")
        endif(TINFO_HAS_WTIMEOUT)
      else(TINFO_LIBRARY)
        message(FATAL_ERROR "no tinfo library")
      endif(TINFO_LIBRARY)
    endif(NOT LIBNCURSES_HAS_WTIMEOUT)
  else(WITH_LIBNCURSES STREQUAL "SYSTEM")
    message(FATAL_ERROR "Invalid WITH_LIBNCURSES option value ${WITH_LIBNCURSES}")
  endif(WITH_LIBNCURSES STREQUAL "SYSTEM")
endif(UNIX)

# WITH_LIBEDIT can have multiple values with different meanings
# on Linux:
# * "EXTERNAL" - (default) builds editline library from URL stored in ${WITH_LIBEDIT_URL} uses the library created by the build
# * "SYSTEM"   - use editline library located in:
#                  ${WITH_EXTERNAL_LIBS_PATH}
#                  ${WITH_LIBEDIT_ROOT_PATH}/lib
#                use editline headers located in:
#                  ${WITH_EXTERNAL_HEADERS_PATH}
#                  ${WITH_LIBEDIT_ROOT_PATH}/include
if(UNIX)
  if(WITH_LIBEDIT STREQUAL "EXTERNAL")
    set(LIBEDIT_TARGET libedit)
    externalproject_add(${LIBEDIT_TARGET}
      URL                  ${WITH_LIBEDIT_URL}
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    "LDFLAGS=-L${CMAKE_CURRENT_BINARY_DIR}/external/lib" ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        make all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
    if(TARGET ${LIBNCURSES_TARGET})
      add_dependencies(${LIBEDIT_TARGET} ${LIBNCURSES_TARGET})
    endif()
    set(LIBEDIT_LIBS ${CMAKE_CURRENT_BINARY_DIR}/external/lib/libedit.a)
    set(LIBEDIT_INCLUDES ${CMAKE_CURRENT_BINARY_DIR}/external/include)
    list(APPEND EP_TARGETS ${LIBEDIT_TARGET})
  elseif(WITH_LIBEDIT STREQUAL "SYSTEM")
    find_library(LIBEDIT_LIBS NAMES libedit.a PATHS ${WITH_EXTERNAL_LIBS_PATH} ${WITH_LIBEDIT_ROOT_PATH}/lib REQUIRED)
    find_path(LIBEDIT_INCLUDES NAMES editline/readline.h histedit.h PATHS ${WITH_EXTERNAL_HEADERS_PATH} ${WITH_LIBEDIT_ROOT_PATH}/include REQUIRED)

    message(STATUS "Found editline library: ${LIBEDIT_LIBS}")
    message(STATUS "Found editline headers: ${LIBEDIT_INCLUDES}")
  endif()
endif(UNIX)
list(APPEND EP_LIBS ${LIBEDIT_LIBS})
list(APPEND EP_INCLUDES ${LIBEDIT_INCLUDES})

# LZ4
#
set(LZ4_TARGET lz4)
if(WIN32) # msvc-built lz4 v1.9.2 libraries 
  set(LZ4_INCLUDES ${WINDOWS_EXTERNAL_DIR}/lz4/include)
  set(LZ4_LIBS ${WINDOWS_EXTERNAL_DIR}/lz4/bin/liblz4.lib)
  set(LZ4_DLL ${WINDOWS_EXTERNAL_DIR}/lz4/bin/liblz4.dll)

  add_custom_target(${LZ4_TARGET}
    COMMAND ${CMAKE_COMMAND} -E echo "  copying ${LZ4_DLL} to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/liblz4.dll"
    COMMAND ${CMAKE_COMMAND} -E copy ${LZ4_DLL} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/liblz4.dll
  )
else()
  externalproject_add(${LZ4_TARGET}
    GIT_REPOSITORY https://github.com/lz4/lz4
    GIT_TAG fdf2ef5             # https://github.com/lz4/lz4/releases/tag/v1.9.2
    CONFIGURE_COMMAND ""        # no configure
    BUILD_IN_SOURCE true        # lz4 Makefile is designed to run locally
    BUILD_COMMAND make CFLAGS="-fPIC"  # to allow static linking in shared library
    INSTALL_COMMAND ""          # suppress install
  )
  set(LZ4_INCLUDES ${CMAKE_BINARY_DIR}/external/Source/lz4/lib)
  set(LZ4_LIBS ${CMAKE_BINARY_DIR}/external/Source/lz4/lib/liblz4.a)
endif()
list(APPEND EP_LIBS ${LZ4_LIBS})
list(APPEND EP_INCLUDES ${LZ4_INCLUDES})

# WITH_LOBOPENSSL can have multiple values with different meanings
# on Linux:
# * "EXTERNAL" - (default) builds openssl library from URL stored in ${WITH_LIBOPENSSL_URL} uses the library created by the build
# * "SYSTEM"   - use openssl library located in:
#                  ${WITH_EXTERNAL_LIBS_PATH}
#                  ${WITH_LIBOPENSSL_ROOT_PATH}/lib
#                use openssl headers located in:
#                  ${WITH_EXTERNAL_HEADERS_PATH}
#                  ${WITH_LIBOPENSSL_ROOT_PATH}/include
# on Windows:
# * "EXTERNAL" - (default) uses the prebuilt library from cubrid/win/external
if(WITH_LIBOPENSSL STREQUAL "EXTERNAL")
  set(LIBOPENSSL_TARGET libopenssl)
  if(UNIX)
    #compile OpenSSL library given an internet url to a OpenSSL archive
    #e.g. https://www.openssl.org/source/openssl-1.1.1f.tar.gz
    externalproject_add(${LIBOPENSSL_TARGET}
      URL                  ${WITH_LIBOPENSSL_URL}
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    <SOURCE_DIR>/config --prefix=${CMAKE_CURRENT_BINARY_DIR}/external no-shared # ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        $(MAKE) all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      $(MAKE) install_sw AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
    set(LIBOPENSSL_LIBS ${CMAKE_CURRENT_BINARY_DIR}/external/lib/libssl.a ${CMAKE_CURRENT_BINARY_DIR}/external/lib/libcrypto.a)
    set(LIBOPENSSL_INCLUDES ${CMAKE_CURRENT_BINARY_DIR}/external/include)
    list(APPEND EP_TARGETS ${LIBOPENSSL_TARGET})
  else(UNIX)
    set(LIBOPENSSL_LIBS ${WINDOWS_EXTERNAL_DIR}/openssl/lib/libssl.lib)
    list(APPEND LIBOPENSSL_LIBS ${WINDOWS_EXTERNAL_DIR}/openssl/lib/libcrypto.lib)
    list(APPEND LIBOPENSSL_LIBS Crypt32 Ws2_32)
    set(LIBOPENSSL_INCLUDES ${WINDOWS_EXTERNAL_DIR}/openssl/include)
  endif(UNIX)
else(WITH_LIOPENSSL STREQUAL "SYSTEM")
  if(UNIX)
    find_library(LIBOPENSSL_LIBS NAMES libssl.a libcrypto.a PATHS ${WITH_EXTERNAL_LIBS_PATH} ${WITH_LIBOPENSSL_ROOT_PATH}/lib REQUIRED)
    find_path(LIBOPENSSL_INCLUDES NAMES openssl/conf.h openssl/evp.h PATHS ${WITH_EXTERNAL_HEADERS_PATH} ${WITH_LIBOPENSSL_ROOT_PATH}/include REQUIRED)
    message(STATUS "Found openssl library: ${LIBOPENSSL_LIBS}")
    message(STATUS "Found openssl headers: ${LIBOPENSSL_INCLUDES}")
  endif(UNIX)
endif()
list(APPEND EP_INCLUDES ${LIBOPENSSL_INCLUDES})
list(APPEND EP_LIBS ${LIBOPENSSL_LIBS})

# For JVM libraries
if(WIN32)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    set(JVM_LIBS ${JAVA_JVM_LIBRARY})
  else(TARGET_PLATFORM_BITS EQUAL 32)
    set(JVM_LIBS ${CMAKE_SOURCE_DIR}/win/external/lib64/jvm.lib)
  endif(TARGET_PLATFORM_BITS EQUAL 32)
  list(APPEND JVM_LIBS -DELAYLOAD:jvm.dll)
endif(WIN32)

# rapidjson
set(RAPIDJSON_TARGET rapidjson)
externalproject_add(${RAPIDJSON_TARGET}
  # tried URL but archive gets downloaded every time. URL_MD5 may help
  URL                  ${WITH_RAPIDJSON_URL}
  TIMEOUT              600
  DOWNLOAD_NO_PROGRESS 1
  # don't install
  INSTALL_COMMAND      ""
  # don't build test, doc and examples
  CMAKE_CACHE_ARGS     -DRAPIDJSON_BUILD_TESTS:STRING=off
                       -DRAPIDJSON_BUILD_DOC:STRING=off
                       -DRAPIDJSON_BUILD_EXAMPLES:STRING=off
)
# add to include directories
include_directories(${CMAKE_BINARY_DIR}/external/Source/rapidjson/include)
# add to external project targets
list(APPEND EP_TARGETS ${RAPIDJSON_TARGET})

if(WITH_JDBC AND EXISTS ${CMAKE_SOURCE_DIR}/cubrid-jdbc/src)
  message("Build JDBC Driver from cubird-jdbc submodule")

  set(JDBC_DIR ${CMAKE_SOURCE_DIR}/cubrid-jdbc)
  set(JDBC_SOURCE ${JDBC_DIR}/src)

  # clean and build JDBC
  if(UNIX)
    execute_process(COMMAND bash build.sh clean
      WORKING_DIRECTORY ${JDBC_DIR})
    execute_process(COMMAND bash build.sh
      WORKING_DIRECTORY ${JDBC_DIR})
  else(UNIX)
    execute_process(COMMAND cmd /c build.bat clean
      WORKING_DIRECTORY ${JDBC_DIR}
    )
    execute_process(COMMAND cmd /c build.bat
      WORKING_DIRECTORY ${JDBC_DIR}
    )
  endif(UNIX)

  # dist JDBC
  if(USE_CUBRID_ENV)
    set(CUBRID_JDBCDIR jdbc)
  else(USE_CUBRID_ENV)
    set(CUBRID_JDBCDIR jdbc CACHE STRING "The directory for JDBC driver file")
  endif(USE_CUBRID_ENV)

  install(DIRECTORY ${JDBC_DIR}/
    DESTINATION ${CUBRID_JDBCDIR} COMPONENT JDBC
    FILES_MATCHING PATTERN "*.jar"
  )
endif()

# include subdirectories
add_subdirectory(cas)
add_subdirectory(sa)
add_subdirectory(cs)
add_subdirectory(cubrid)
add_subdirectory(util)
add_subdirectory(cci)
add_subdirectory(broker)
add_subdirectory(cm_common)
add_subdirectory(conf)
add_subdirectory(msg)
add_subdirectory(demo)
add_subdirectory(contrib)
add_subdirectory(tools)
add_subdirectory(locales)
add_subdirectory(timezones)
add_subdirectory(jsp)
if(AT_LEAST_ONE_UNIT_TEST)
  add_subdirectory(unit_tests)
endif()

if(WIN32)
  add_subdirectory(win)
endif(WIN32)

if(WITH_CMSERVER AND EXISTS ${CMAKE_SOURCE_DIR}/cubridmanager/server)
  get_target_property(CCI_PUBLIC_HEADERS cascci PUBLIC_HEADER)
  get_target_property(CMDEP_PUBLIC_HEADERS cmdep PUBLIC_HEADER)
  get_target_property(CMSTAT_PUBLIC_HEADERS cmstat PUBLIC_HEADER)
  if(TARGET_PLATFORM_BITS EQUAL 64)
    list(APPEND CMS_CONFIGURE_OPTS --enable-64bit)
  else(TARGET_PLATFORM_BITS EQUAL 64)
    list(APPEND CMS_CONFIGURE_OPTS --disable-64bit)
  endif(TARGET_PLATFORM_BITS EQUAL 64)
  add_custom_target(pre_cubridmanager)
  if(UNIX)
    set(CMS_DEPENDS cubridcs cascci cmstat cmdep)
    externalproject_add(cubridmanager
      DEPENDS pre_cubridmanager ${CMS_DEPENDS}
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cubridmanager/server
      CONFIGURE_COMMAND sh <SOURCE_DIR>/autogen.sh
      COMMAND sh <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --with-cubrid-libdir=<TMP_DIR> --with-cubrid-includedir=<TMP_DIR> ${CMS_CONFIGURE_OPTS}
      BUILD_COMMAND make all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
  else(UNIX)
    set(CMS_DEPENDS cascci cmstat cmdep)
    externalproject_add(cubridmanager
      DEPENDS pre_cubridmanager ${CMS_DEPENDS}
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cubridmanager/server
      CONFIGURE_COMMAND ""
      BUILD_COMMAND cmd /c build.bat --prefix <INSTALL_DIR> --with-cubrid-libdir <TMP_DIR> --with-cubrid-includedir <TMP_DIR> ${CMS_CONFIGURE_OPTS}
      INSTALL_COMMAND ""
      BUILD_IN_SOURCE 1
      )
  endif(UNIX)
  externalproject_get_property(cubridmanager TMP_DIR)
  foreach(required_target ${CMS_DEPENDS})
    add_custom_command(TARGET pre_cubridmanager PRE_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_LINKER_FILE:${required_target}> ${TMP_DIR})
  endforeach(required_target)
  foreach(required_header ${CCI_PUBLIC_HEADERS} ${CMDEP_PUBLIC_HEADERS} ${CMSTAT_PUBLIC_HEADERS})
    add_custom_command(TARGET pre_cubridmanager PRE_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${required_header} ${TMP_DIR})
  endforeach(required_header)

  externalproject_get_property(cubridmanager INSTALL_DIR)
  install(DIRECTORY ${INSTALL_DIR}/
    DESTINATION ${CUBRID_PREFIXDIR} COMPONENT Manager
    USE_SOURCE_PERMISSIONS)
endif(WITH_CMSERVER AND EXISTS ${CMAKE_SOURCE_DIR}/cubridmanager/server)


# create empty directories
install(DIRECTORY DESTINATION ${CUBRID_LOGDIR})
install(DIRECTORY DESTINATION ${CUBRID_VARDIR})
install(DIRECTORY DESTINATION ${CUBRID_TMPDIR})
if(USE_CUBRID_ENV)
  install(FILES
    ${CMAKE_SOURCE_DIR}/COPYING
    ${CMAKE_SOURCE_DIR}/CREDITS
    ${CMAKE_SOURCE_DIR}/README
    DESTINATION ${CUBRID_PREFIXDIR})
endif(USE_CUBRID_ENV)

if(WITH_SOURCES)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/ DESTINATION ${CUBRID_SRCDIR})
endif(WITH_SOURCES)

# for packaging
if(UNIX)
  set(CPACK_GENERATOR "STGZ;TGZ")
  set(CPACK_SOURCE_GENERATOR "TGZ")
else(UNIX)
  set(CPACK_GENERATOR "ZIP")
endif(UNIX)

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CUBRID Open Source RDBMS")
set(CPACK_PACKAGE_VENDOR "CUBRID Corporation")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/COPYING")
# for WiX (txt file extension is required)
configure_file(COPYING LICENSE.txt)
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_BINARY_DIR}/LICENSE.txt")
set(CPACK_PACKAGE_VERSION_MAJOR "${CUBRID_MAJOR_VERSION}")
set(CPACK_PACKAGE_VERSION_MINOR "${CUBRID_MINOR_VERSION}")
set(CPACK_PACKAGE_VERSION_PATCH "${CUBRID_PATCH_VERSION}")
set(CPACK_PACKAGE_VERSION "${CUBRID_VERSION}")
if(CMAKE_BUILD_TYPE MATCHES "Debug")
  set(CUBRID_PACKAGE_FILE_SUFFIX "-debug")
elseif(CMAKE_BUILD_TYPE MATCHES "Coverage")
  set(CUBRID_PACKAGE_FILE_SUFFIX "-coverage")
elseif(CMAKE_BUILD_TYPE MATCHES "Profile")
  set(CUBRID_PACKAGE_FILE_SUFFIX "-profile")
else(CMAKE_BUILD_TYPE MATCHES "Debug")
  set(CUBRID_PACKAGE_FILE_SUFFIX "")
endif(CMAKE_BUILD_TYPE MATCHES "Debug")
if(WIN32)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${CMAKE_SYSTEM_NAME}-x86-${CUBRID_VERSION}${CUBRID_PACKAGE_FILE_SUFFIX}")
    set(CCI_PACKAGE_FILE_NAME "${PROJECT_NAME}-CCI-${CMAKE_SYSTEM_NAME}-x86-${CUBRID_VERSION}${CUBRID_PACKAGE_FILE_SUFFIX}")
  else(TARGET_PLATFORM_BITS EQUAL 32)
    set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${CMAKE_SYSTEM_NAME}-x64-${CUBRID_VERSION}${CUBRID_PACKAGE_FILE_SUFFIX}")
    set(CCI_PACKAGE_FILE_NAME "${PROJECT_NAME}-CCI-${CMAKE_SYSTEM_NAME}-x64-${CUBRID_VERSION}${CUBRID_PACKAGE_FILE_SUFFIX}")
  endif(TARGET_PLATFORM_BITS EQUAL 32)
else(WIN32)
  set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${CUBRID_VERSION}-${CMAKE_SYSTEM_NAME}.${CMAKE_HOST_SYSTEM_PROCESSOR}${CUBRID_PACKAGE_FILE_SUFFIX}")
  set(CCI_PACKAGE_FILE_NAME "${PROJECT_NAME}-CCI-${CUBRID_VERSION}-${CMAKE_SYSTEM_NAME}.${CMAKE_HOST_SYSTEM_PROCESSOR}${CUBRID_PACKAGE_FILE_SUFFIX}")
endif(WIN32)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${PROJECT_NAME}-${CUBRID_VERSION}-src")

set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)

configure_file(cmake/CPackOptions.cmake.in CPackOptions.cmake)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_BINARY_DIR}/CPackOptions.cmake")

if(WIN32)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    set(WIX_PROGRAMFILESFOLDER ProgramFilesFolder)
    set(WIX_SUPPORTED_PLATFORM 32bit)
    set(WIX_SUPPORTED_PLATFORM_CONDITION "Not VersionNT64")
    if(MSVC90)
      set(WIX_VCREDIST_MSM Microsoft_VC90_CRT_x86.msm)
      set(WIX_VCREDIST_MFC_MSM Microsoft_VC90_MFC_x86.msm)
    elseif(MSVC10)
      set(WIX_VCREDIST_MSM Microsoft_VC100_CRT_x86.msm)
      set(WIX_VCREDIST_MFC_MSM Microsoft_VC100_MFC_x86.msm)
    elseif(MSVC_VERSION GREATER_EQUAL 1910)
      set(WIX_VCREDIST_MSM Microsoft_VC141_CRT_x86.msm)
      set(WIX_VCREDIST_MFC_MSM Microsoft_VC141_MFC_x86.msm)
    else()
      message(WARNING "Cannot generate MSI installer with unsupported compiler version")
    endif()
  else(TARGET_PLATFORM_BITS EQUAL 32)
    set(WIX_PROGRAMFILESFOLDER ProgramFiles64Folder)
    set(WIX_SUPPORTED_PLATFORM 64bit)
    set(WIX_SUPPORTED_PLATFORM_CONDITION "VersionNT64")
    if(MSVC90)
      set(WIX_VCREDIST_MSM Microsoft_VC90_CRT_x86_x64.msm)
      set(WIX_VCREDIST_MFC_MSM Microsoft_VC90_MFC_x86_x64.msm)
    elseif(MSVC10)
      set(WIX_VCREDIST_MSM Microsoft_VC100_CRT_x64.msm)
      set(WIX_VCREDIST_MFC_MSM Microsoft_VC100_MFC_x64.msm)
    elseif(MSVC_VERSION GREATER_EQUAL 1910) # ref. https://cmake.org/cmake/help/v3.9/variable/MSVC_VERSION.html
      set(WIX_VCREDIST_MSM Microsoft_VC141_CRT_x64.msm)
      set(WIX_VCREDIST_MFC_MSM Microsoft_VC141_MFC_x64.msm)
    else()
      message(WARNING "Cannot generate MSI installer with unsupported compiler version")
    endif()
  endif(TARGET_PLATFORM_BITS EQUAL 32)
  if(CMAKE_VERSION VERSION_GREATER "3.1")
    set_property(INSTALL "${CUBRID_BINDIR}/$<TARGET_FILE_NAME:cubridtray>"
      PROPERTY CPACK_START_MENU_SHORTCUTS "CUBRID Service Tray")
    configure_file(cmake/CPackWixPatch.cmake.in wix_patch.xml @ONLY)
  else(CMAKE_VERSION VERSION_GREATER "3.1")
    # TODO: msi package will not working on CMAKE version 3.5 or less
    set(CPACK_PACKAGE_EXECUTABLES cubridtray "CUBRID Service Tray")
  endif(CMAKE_VERSION VERSION_GREATER "3.1")
endif(WIN32)

include(CPack)
