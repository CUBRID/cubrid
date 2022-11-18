#
#
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

set(WITH_LIBEXPAT_URL "https://github.com/libexpat/libexpat/releases/download/R_2_2_5/expat-2.2.5.tar.bz2") # expat library sources URL
set(WITH_LIBJANSSON_URL "http://www.digip.org/jansson/releases/jansson-2.10.tar.gz")                        # jansson library sources URL
set(WITH_LIBEDIT_URL "http://thrysoee.dk/editline/libedit-20170329-3.1.tar.gz")                             # editline library sources URL
set(WITH_RAPIDJSON_URL "https://github.com/Tencent/rapidjson/archive/v1.1.0.tar.gz")                        # rapidjson library sources URL
set(WITH_LIBOPENSSL_URL "https://www.openssl.org/source/old/1.1.1/openssl-1.1.1f.tar.gz")                   # openssl library sources URL
set(WITH_LIBUNIXODBC_URL "http://www.unixodbc.org/unixODBC-2.3.9.tar.gz")                                   # unixODBC library sources URL

# options for external libraries (BUNDLED, EXTERAL or SYSTEM)
set(WITH_EXTERNAL_PREFIX "EXTERNAL" CACHE STRING "External library prefix (default: EXTERNAL)")
set(WITH_BUNDLED_PREFIX "BUNDLED" CACHE STRING "Bundled library prefix (default: BUNDLED)")
set(WITH_SYSTEM_PREFIX "SYSTEM" CACHE STRING "System library prefix (default: SYSTEM)")

set(WITH_LIBEXPAT     "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with expat library (default: EXTERNAL)")
set(WITH_LIBJANSSON   "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with jansson library (default: EXTERNAL)")
set(WITH_LIBEDIT      "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with editline library (default: EXTERNAL)")
set(WITH_LIBNCURSES   "${WITH_SYSTEM_PREFIX}" CACHE STRING "Build with ncurses library (default: SYSTEM)")
set(WITH_LIBOPENSSL   "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with openssl library (default: EXTERNAL)")
set(WITH_LIBUNIXODBC  "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with unixODBC library (default: EXTERNAL)")

message(STATUS "Build with Curses library: ${WITH_LIBNCURSES}")
message(STATUS "Build with editline library: ${WITH_LIBEDIT}")
message(STATUS "Build with expat library: ${WITH_LIBEXPAT}")
message(STATUS "Build with jansson library: ${WITH_LIBJANSSON}")
message(STATUS "Build with openssl library: ${WITH_LIBOPENSSL}")
message(STATUS "Build with unixODBC library: ${WITH_LIBUNIXODBC}")

# For external library options
include(ExternalProject)

set (3RDPARTY_LIBS_DIR ${CMAKE_BINARY_DIR}/3rdparty)
set_property(DIRECTORY PROPERTY EP_BASE "${3RDPARTY_LIBS_DIR}")
set(DEFAULT_CONFIGURE_OPTS <SOURCE_DIR>/configure --prefix=${CMAKE_BINARY_DIR}/3rdparty --enable-static --disable-shared --with-pic CFLAGS=-w CXXFLAGS=-w)

# For Windows
if(WIN32)
  # directory path for Windows
  set(WINDOWS_BASE_DIR ${CMAKE_SOURCE_DIR}/win)
  set(WINDOWS_EXTERNAL_DIR ${WINDOWS_BASE_DIR}/3rdparty)
  set(WINDOWS_EXTERNAL_LIB_DIR ${WINDOWS_EXTERNAL_DIR}/lib64)
  set(WINDOWS_EXTERNAL_INCLUDE_DIR ${WINDOWS_EXTERNAL_DIR}/include)
  # I could not load debug dll for jansson. disabled for now.
  # if(CMAKE_BUILD_TYPE MATCHES "Debug")
  #   set(WINDOWS_EXTERNAL_DLL_DIR ${WINDOWS_EXTERNAL_DIR}/dll_debug)
  # else()
    set(WINDOWS_EXTERNAL_DLL_DIR ${WINDOWS_EXTERNAL_DIR}/dll)
  # endif()

  # bison and flex for Windows
  set(WIN_FLEX_BISON_VERSION "2.5.20")
  set(WIN_FLEX_BISON_URL "https://github.com/lexxmark/winflexbison/releases/download/v${WIN_FLEX_BISON_VERSION}/win_flex_bison-${WIN_FLEX_BISON_VERSION}.zip")
  set(WIN_FLEX_BISON_FILENAME "${3RDPARTY_LIBS_DIR}/Download/win_flex_bison.zip")
  file(DOWNLOAD ${WIN_FLEX_BISON_URL} ${WIN_FLEX_BISON_FILENAME})

  set(WIN_FLEX_BISON_INSTALL_DIR "${3RDPARTY_LIBS_DIR}/Install/win_flex_bison")
  file(MAKE_DIRECTORY ${WIN_FLEX_BISON_INSTALL_DIR})
  execute_process(COMMAND ${CMAKE_COMMAND} -E tar -xf ${WIN_FLEX_BISON_FILENAME} WORKING_DIRECTORY ${WIN_FLEX_BISON_INSTALL_DIR})
  list(APPEND CMAKE_PREFIX_PATH ${WIN_FLEX_BISON_INSTALL_DIR})
endif(WIN32)

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
# * "EXTERNAL" - (default) uses the prebuilt library from cubrid/win/3rdparty
set(LIBEXPAT_TARGET libexpat)
if(WITH_LIBEXPAT STREQUAL "EXTERNAL")
  if(UNIX)
    externalproject_add(${LIBEXPAT_TARGET}
      URL                  ${WITH_LIBEXPAT_URL}
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    ${DEFAULT_CONFIGURE_OPTS} --without-xmlwf  --without-docbook
      BUILD_COMMAND        make AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
    set(LIBEXPAT_LIBS ${3RDPARTY_LIBS_DIR}/lib/libexpat.a)
    set(LIBEXPAT_INCLUDES ${3RDPARTY_LIBS_DIR}/include/expat)
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
list(APPEND EP_TARGETS ${LIBEXPAT_TARGET})
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
# * "EXTERNAL" - (default) uses the prebuilt library from cubrid/win/3rdparty
set(LIBJANSSON_TARGET libjansson)
if(WITH_LIBJANSSON STREQUAL "EXTERNAL")
  list(APPEND EP_TARGETS ${LIBJANSSON_TARGET})
  if(UNIX)
    externalproject_add(${LIBJANSSON_TARGET}
      URL                  ${WITH_LIBJANSSON_URL}
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        make all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
    set(LIBJANSSON_LIBS ${3RDPARTY_LIBS_DIR}/lib/libjansson.a)
    set(LIBJANSSON_INCLUDES ${3RDPARTY_LIBS_DIR}/include/jansson)
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
set(LIBCURSES_TARGET libncurses)
if(UNIX)
  if(WITH_LIBNCURSES STREQUAL "SYSTEM")
    include(FindCurses)
    find_package(Curses REQUIRED)
    CHECK_LIBRARY_EXISTS("${CURSES_LIBRARIES}" wtimeout "" LIBNCURSES_HAS_WTIMEOUT)
    set(LIBCURSES_INCLUDES "${CURSES_INCLUDE_DIRS}")
    set(LIBCURSES_LIBS "${CURSES_LIBRARIES}")

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
set(LIBEDIT_TARGET libedit)
if(UNIX)
  if(WITH_LIBEDIT STREQUAL "EXTERNAL")
    externalproject_add(${LIBEDIT_TARGET}
      URL                  ${WITH_LIBEDIT_URL}
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    "LDFLAGS=-L${3RDPARTY_LIBS_DIR}/lib" ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        make all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
    if(TARGET ${LIBCURSES_TARGET})
      add_dependencies(${LIBEDIT_TARGET} ${LIBCURSES_TARGET})
    endif()
    set(LIBEDIT_LIBS ${3RDPARTY_LIBS_DIR}/lib/libedit.a)
    set(LIBEDIT_INCLUDES ${3RDPARTY_LIBS_DIR}/include)
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
  set(LZ4_INCLUDES ${3RDPARTY_LIBS_DIR}/Source/lz4/lib)
  set(LZ4_LIBS ${3RDPARTY_LIBS_DIR}/Source/lz4/lib/liblz4.a)
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
# * "EXTERNAL" - (default) uses the prebuilt library from cubrid/win/3rdparty
set(LIBOPENSSL_TARGET libopenssl)
if(WITH_LIBOPENSSL STREQUAL "EXTERNAL")
  if(UNIX)
    #compile OpenSSL library given an internet url to a OpenSSL archive
    #e.g. https://www.openssl.org/source/openssl-1.1.1f.tar.gz
    externalproject_add(${LIBOPENSSL_TARGET}
      URL                  ${WITH_LIBOPENSSL_URL}
      LOG_CONFIGURE        TRUE
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    <SOURCE_DIR>/config --prefix=${3RDPARTY_LIBS_DIR} no-shared # ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        $(MAKE) all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      $(MAKE) install_sw AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
    set(LIBOPENSSL_LIBS ${3RDPARTY_LIBS_DIR}/lib/libssl.a ${3RDPARTY_LIBS_DIR}/lib/libcrypto.a)
    set(LIBOPENSSL_INCLUDES ${3RDPARTY_LIBS_DIR}/include)
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

# WITH_LIBUNIXODBC can have multiple values with different meanings
# on Linux:
# * "EXTERNAL" - (default) builds unixODBC library from URL stored in ${WITH_LIBUNIXODBC_URL} uses the library created by the build
#
set(LIBUNIXODBC_TARGET libodbc)
if(WITH_LIBUNIXODBC STREQUAL "EXTERNAL")
  if(UNIX)
    #compile unixODBC library given an internet url to a unixODBC archive
    #e.g. http://www.unixodbc.org/unixODBC-2.3.9.tar.gz
    externalproject_add(${LIBUNIXODBC_TARGET}
      URL                  ${WITH_LIBUNIXODBC_URL}
      LOG_CONFIGURE        TRUE
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    <SOURCE_DIR>/configure --prefix=${3RDPARTY_LIBS_DIR} # ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        $(MAKE) AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      $(MAKE) install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      )
    set(LIBUNIXODBC_LIBS ${3RDPARTY_LIBS_DIR}/lib/libodbc.so)
    set(LIBUNIXODBC_INCLUDES ${3RDPARTY_LIBS_DIR}/include)
    list(APPEND EP_TARGETS ${LIBUNIXODBC_TARGET})
  endif(UNIX)
else(WITH_LIBUNIXODBC STREQUAL "EXTERNAL")
    message(FATAL_ERROR "Invalid WITH_LIBUNIXODBC option value ${WITH_LIBUNIXODBC}")
endif(WITH_LIBUNIXODBC STREQUAL "EXTERNAL")
list(APPEND EP_INCLUDES ${LIBUNIXODBC_INCLUDES})

# rapidjson
set(RAPIDJSON_TARGET rapidjson)
externalproject_add(${RAPIDJSON_TARGET}
  # tried URL but archive gets downloaded every time. URL_MD5 may help
  URL                  ${WITH_RAPIDJSON_URL}
  LOG_CONFIGURE        TRUE
  LOG_BUILD            TRUE
  LOG_INSTALL          TRUE
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
set(RAPIDJSON_INCLUDES ${3RDPARTY_LIBS_DIR}/Source/rapidjson/include)
set(RAPIDJSON_LIBS "")

# add to external project targets
list(APPEND EP_TARGETS ${RAPIDJSON_TARGET})
list(APPEND EP_INCLUDES ${RAPIDJSON_INCLUDES})

# [FIXME!] Expose variables for other CMakeLists
set (EP_TARGETS ${EP_TARGETS} PARENT_SCOPE)
set (EP_LIBS ${EP_LIBS} PARENT_SCOPE)
set (EP_INCLUDES ${EP_INCLUDES} PARENT_SCOPE)

# Expose targets for sa - FIXME better solutions
include (3rdparty_util)

expose_3rdparty_variable(LIBCURSES)
expose_3rdparty_variable(LIBEXPAT)
expose_3rdparty_variable(LIBJANSSON)
expose_3rdparty_variable(LIBEDIT)
expose_3rdparty_variable(LZ4)
expose_3rdparty_variable(LIBOPENSSL)
expose_3rdparty_variable(LIBUNIXODBC)
expose_3rdparty_variable(RAPIDJSON)
