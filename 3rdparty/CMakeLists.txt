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

set(WITH_FLEX_URL "https://github.com/CUBRID/3rdparty/raw/develop/flex/flex-2.6.4.tar.gz")                    # flex
set(WITH_BISON_URL "https://github.com/CUBRID/3rdparty/raw/develop/bison/bison-3.4.1.tar.gz")                 # bison
set(WITH_LIBEXPAT_URL "https://github.com/CUBRID/3rdparty/raw/develop/expat/expat-2.6.4.tar.gz")             # expat library sources URL
set(WITH_LIBJANSSON_URL "https://github.com/CUBRID/3rdparty/raw/develop/jansson/jansson-2.10.tar.gz")         # jansson library sources URL
set(WITH_LIBEDIT_URL "https://github.com/CUBRID/libedit/archive/refs/tags/csql_v1.1.tar.gz")                  # editline library sources URL
set(WITH_RAPIDJSON_URL "https://github.com/CUBRID/3rdparty/raw/develop/rapidjson/v1.1.0.tar.gz")              # rapidjson library sources URL
set(WITH_LIBOPENSSL_URL "https://github.com/CUBRID/3rdparty/raw/develop/openssl/openssl-1.1.1w.tar.gz")       # openssl library sources URL
set(WITH_LIBUNIXODBC_URL "https://github.com/CUBRID/3rdparty/raw/develop/unixODBC/unixODBC-2.3.9.tar.gz")     # unixODBC library sources URL
set(WITH_LIBTBB_URL "https://github.com/CUBRID/3rdparty/raw/develop/tbb/v2021.11.0.tar.gz")                   # intel oneTBB library sources URL
set(WITH_RE2_URL "https://github.com/CUBRID/3rdparty/raw/develop/re2/2022-06-01.tar.gz")                      # RE2
set(WITH_LZ4_URL "https://github.com/CUBRID/3rdparty/raw/develop/lz4/v1.9.4.tar.gz")                          # LZ4
set(WITH_HDF_URL "https://github.com/HDFGroup/hdf5/releases/download/hdf5_1.14.6/hdf5-1.14.6.tar.gz")         # HDF5
set(WITH_ZLIB_URL "https://github.com/madler/zlib")                                                           # ZLIB
set(WITH_HIGHFIVE_URL "https://github.com/highfive-devs/highfive")                                            # HighFive
set(WIN_FLEX_BISON_URL "https://github.com/CUBRID/3rdparty/raw/develop/win_flex_bison/win_flex_bison-2.5.22.zip") # Windows
set(WITH_FAISS_URL "https://github.com/facebookresearch/faiss/archive/refs/tags/v1.8.0.tar.gz")               # Faiss
set(WITH_OPENBLAS_URL "https://github.com/xianyi/OpenBLAS/archive/refs/tags/v0.3.24.tar.gz")                  # OpenBLAS
set(WITH_LAPACK_URL "http://www.netlib.org/lapack/lapack-3.11.0.tar.gz")                                      # LAPACK
set(WITH_USEARCH_URL "https://github.com/unum-cloud/usearch")                  # usearch

# URL_HASH is a hash value used to verify the integrity of a file downloaded from a specified URL.
# It is optional but highly recommended to set this value. If not set, the file will be downloaded
# every time Ninja reconfigures the project, which can be inefficient.
#
# Specifying the URL_HASH ensures:
# 1. The file's integrity is verified against its hash.
# 2. Ninja avoids unnecessary downloads if the file already exists and matches the hash.
#
# How to generate the URL_HASH:
# - Use the `sha256sum` command to compute the hash of the downloaded tarball.
#   Example:
#   $ sha256sum openssl-1.1.1f.tar.gz
#   Then set the resulting SHA256 hash in the format: "SHA256=<hash_value>"
#
# Below are the URL_HASH values for different libraries used in this project:
set(WITH_LIBEXPAT_URL_HASH "SHA256=fd03b7172b3bd7427a3e7a812063f74754f24542429b634e0db6511b53fb2278")		# expat library 
set(WITH_LIBJANSSON_URL_HASH "SHA256=78215ad1e277b42681404c1d66870097a50eb084be9d771b1d15576575cf6447")         # jansson library 
set(WITH_LIBEDIT_URL_HASH "SHA256=972c7d46844b7b56adde028bb2269546938272dd8d7a58978b7a18a6f6e44141")            # editline library 
set(WITH_RAPIDJSON_URL_HASH "SHA256=bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e")          # rapidjson library 
set(WITH_LIBOPENSSL_URL_HASH "SHA256=cf3098950cb4d853ad95c0841f1f9c6d3dc102dccfcacd521d93925208b76ac8")         # openssl library 
set(WITH_LIBUNIXODBC_URL_HASH "SHA256=52833eac3d681c8b0c9a5a65f2ebd745b3a964f208fc748f977e44015a31b207")        # unixODBC library 
set(WITH_LIBTBB_URL_HASH "SHA256=782ce0cab62df9ea125cdea253a50534862b563f1d85d4cda7ad4e77550ac363")             # intel oneTBB library 
set(WITH_RE2_URL_HASH "SHA256=f89c61410a072e5cbcf8c27e3a778da7d6fd2f2b5b1445cd4f4508bee946ab0f")		# RE2
set(WITH_LZ4_URL_HASH "SHA256=0b0e3aa07c8c063ddf40b082bdf7e37a1562bda40a0ff5272957f3e987e0e54b")		# LZ4


# options for external libraries (BUNDLED, EXTERAL or SYSTEM)
set(WITH_EXTERNAL_PREFIX "EXTERNAL" CACHE STRING "External library prefix (default: EXTERNAL)")
set(WITH_BUNDLED_PREFIX "BUNDLED" CACHE STRING "Bundled library prefix (default: BUNDLED)")
set(WITH_SYSTEM_PREFIX "SYSTEM" CACHE STRING "System library prefix (default: SYSTEM)")

if(UNIX)
  set(WITH_LIBFLEXBISON "${WITH_SYSTEM_PREFIX}" CACHE STRING "Build with flex and bison library (default: SYSTEM)")
else(WIN32)
  set(WITH_LIBFLEXBISON "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with flex and bison library (default: EXTERNAL)")
endif()

set(WITH_LIBEXPAT     "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with expat library (default: EXTERNAL)")
set(WITH_LIBJANSSON   "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with jansson library (default: EXTERNAL)")
set(WITH_LIBEDIT      "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with editline library (default: EXTERNAL)")
set(WITH_LIBOPENSSL   "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with openssl library (default: EXTERNAL)")
set(WITH_LIBUNIXODBC  "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with unixODBC library (default: EXTERNAL)")
set(WITH_RE2          "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with re2 library (default: EXTERNAL)")
set(WITH_LZ4          "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with lz4 library (default: EXTERNAL)")
set(WITH_LIBTBB       "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with TBB library (default: EXTERNAL)")
set(WITH_HDF          "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with HDF5 library (default: EXTERNAL)")
set(WITH_ZLIB         "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with ZLIB library (default: EXTERNAL)")
set(WITH_HIGHFIVE     "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with HIGHFIVE library (default: EXTERNAL)")
set(WITH_FAISS        "${WITH_EXTERNAL_PREFIX}" CACHE STRING "Build with Faiss library (default: EXTERNAL)")

message(STATUS "Build with flex and bison library: ${WITH_LIBFLEXBISON}")
message(STATUS "Build with expat library: ${WITH_LIBEXPAT}")
message(STATUS "Build with jansson library: ${WITH_LIBJANSSON}")
message(STATUS "Build with editline library: ${WITH_LIBEDIT}")
message(STATUS "Build with openssl library: ${WITH_LIBOPENSSL}")
message(STATUS "Build with unixODBC library: ${WITH_LIBUNIXODBC}")
message(STATUS "Build with RE2 library: ${WITH_RE2}")
message(STATUS "Build with LZ4 library: ${WITH_LZ4}")
message(STATUS "Build with TBB library: ${WITH_LIBTBB}")
message(STATUS "Build with HDF5 library: ${WITH_HDF}")
message(STATUS "Build with ZLIB library: ${WITH_ZLIB}")
message(STATUS "Build with HighFive library: ${WITH_HIGHFIVE}")
message(STATUS "Build with Faiss library: ${WITH_FAISS}")

macro(ADD_BY_PRODUCTS_VARIABLE prefix_dependee)
  if (CMAKE_GENERATOR MATCHES "Ninja")
    set(${prefix_dependee}_BYPRODUCTS BUILD_BYPRODUCTS "${ARGN}")
  endif()
endmacro()

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
endif(WIN32)

# WITH_LIBFLEXBISON can have multiple values with different meanings
# on Linux:
# * "SYSTEM"   - use flex and bison library located in the system.
# on Windows:
# * "EXTERNAL" - (default) builds flex and bison library from URL
# * "SYSTEM"   - use flex and bison library installed by chocolately
if(WITH_LIBFLEXBISON STREQUAL "EXTERNAL")
  if(WIN32)
    # bison and flex for Windows
    set(WIN_FLEX_BISON_FILENAME "${WINDOWS_EXTERNAL_DIR}/Download/win_flex_bison.zip")
    file(DOWNLOAD ${WIN_FLEX_BISON_URL} ${WIN_FLEX_BISON_FILENAME} INACTIVITY_TIMEOUT 600)

    set(WIN_FLEX_BISON_INSTALL_DIR "${WINDOWS_EXTERNAL_DIR}/Install/win_flex_bison")
    file(MAKE_DIRECTORY ${WIN_FLEX_BISON_INSTALL_DIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar -xf ${WIN_FLEX_BISON_FILENAME} WORKING_DIRECTORY ${WIN_FLEX_BISON_INSTALL_DIR})
    set (CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${WIN_FLEX_BISON_INSTALL_DIR} PARENT_SCOPE)

    execute_process(
      COMMAND ${CMAKE_COMMAND} -E echo "  copying ${WIN_FLEX_BISON_INSTALL_DIR}/FlexLexer.h to ${WINDOWS_EXTERNAL_INCLUDE_DIR}/FlexLexer.h"
      COMMAND ${CMAKE_COMMAND} -E copy ${WIN_FLEX_BISON_INSTALL_DIR}/FlexLexer.h ${WINDOWS_EXTERNAL_INCLUDE_DIR}/FlexLexer.h
    )
	
    set(BISON_ROOT_DIR "${WIN_FLEX_BISON_INSTALL_DIR}")
    set(FLEX_ROOT_DIR "${WIN_FLEX_BISON_INSTALL_DIR}")
    set(BISON_EXECUTABLE "${WIN_FLEX_BISON_INSTALL_DIR}/win_bison.exe")
    set(FLEX_EXECUTABLE "${WIN_FLEX_BISON_INSTALL_DIR}/win_flex.exe")

    set(BISON_ROOT_DIR "${BISON_ROOT_DIR}" PARENT_SCOPE)
    set(FLEX_ROOT_DIR "${FLEX_ROOT_DIR}" PARENT_SCOPE)
    set(BISON_EXECUTABLE "${BISON_EXECUTABLE}" PARENT_SCOPE)
    set(FLEX_EXECUTABLE "${FLEX_EXECUTABLE}" PARENT_SCOPE)
  else(UNIX)
    message(FATAL_ERROR "Building flex and bison library from URL is not supported")
  endif(WIN32)
elseif(WITH_LIBFLEX STREQUAL "SYSTEM")
	include(FindFLEX)
	include(FindBISON)
endif()

find_package(FLEX 2.5.34 REQUIRED)
find_package(BISON 3.0.0 REQUIRED)

if(NOT BISON_FOUND OR NOT FLEX_FOUND)
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
endif(NOT BISON_FOUND OR NOT FLEX_FOUND)

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
    set(LIBEXPAT_LIBS ${3RDPARTY_LIBS_DIR}/lib/libexpat.a)
    set(LIBEXPAT_INCLUDES ${3RDPARTY_LIBS_DIR}/include/expat)
    ADD_BY_PRODUCTS_VARIABLE ("LIBEXPAT" ${LIBEXPAT_LIBS})
    externalproject_add(${LIBEXPAT_TARGET}
      URL                  ${WITH_LIBEXPAT_URL}
      URL_HASH             ${WITH_LIBEXPAT_URL_HASH}
      LOG_CONFIGURE        TRUE
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    ${DEFAULT_CONFIGURE_OPTS} --without-xmlwf  --without-docbook
      BUILD_COMMAND        make AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      "${LIBEXPAT_BYPRODUCTS}"
      )
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
    set(LIBJANSSON_LIBS ${3RDPARTY_LIBS_DIR}/lib/libjansson.a)
    set(LIBJANSSON_INCLUDES ${3RDPARTY_LIBS_DIR}/include/jansson)
    ADD_BY_PRODUCTS_VARIABLE ("LIBJANSSON" ${LIBJANSSON_LIBS})
    externalproject_add(${LIBJANSSON_TARGET}
      URL                  ${WITH_LIBJANSSON_URL}
      URL_HASH             ${WITH_LIBJANSSON_URL_HASH}
      LOG_CONFIGURE        TRUE
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        make all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      "${LIBJANSSON_BYPRODUCTS}"
      )
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
  set(LIBEDIT_TARGET libedit)
  if(WITH_LIBEDIT STREQUAL "EXTERNAL")
    set(LIBEDIT_LIBS ${3RDPARTY_LIBS_DIR}/lib/libedit.a)
    set(LIBEDIT_INCLUDES ${3RDPARTY_LIBS_DIR}/include)
    ADD_BY_PRODUCTS_VARIABLE ("LIBEDIT" ${LIBEDIT_LIBS})
    externalproject_add(${LIBEDIT_TARGET}
      URL                  ${WITH_LIBEDIT_URL}
      URL_HASH             ${WITH_LIBEDIT_URL_HASH}
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    "LDFLAGS=-L${3RDPARTY_LIBS_DIR}" ${DEFAULT_CONFIGURE_OPTS} LIBS=-ldl
      BUILD_COMMAND        make all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      "${LIBEDIT_BYPRODUCTS}"
      )
    if(TARGET ${LIBNCURSES_TARGET})
      add_dependencies(${LIBEDIT_TARGET} ${LIBNCURSES_TARGET})
    endif()
    list(APPEND EP_TARGETS ${LIBEDIT_TARGET})
  elseif(WITH_LIBEDIT STREQUAL "SYSTEM")
    find_library(LIBEDIT_LIBS NAMES libedit.a PATHS ${WITH_EXTERNAL_LIBS_PATH} ${WITH_LIBEDIT_ROOT_PATH}/lib REQUIRED)
    find_path(LIBEDIT_INCLUDES NAMES editline/readline.h histedit.h PATHS ${WITH_EXTERNAL_HEADERS_PATH} ${WITH_LIBEDIT_ROOT_PATH}/include REQUIRED)

    message(STATUS "Found editline library: ${LIBEDIT_LIBS}")
    message(STATUS "Found editline headers: ${LIBEDIT_INCLUDES}")
  endif()
  list(APPEND EP_LIBS ${LIBEDIT_LIBS})
  list(APPEND EP_INCLUDES ${LIBEDIT_INCLUDES})
endif(UNIX)

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
  set(LZ4_INCLUDES ${3RDPARTY_LIBS_DIR}/Source/lz4/lib)
  set(LZ4_LIBS ${3RDPARTY_LIBS_DIR}/Source/lz4/lib/liblz4.a)
  ADD_BY_PRODUCTS_VARIABLE ("LZ4" ${LZ4_LIBS})
  externalproject_add(${LZ4_TARGET}
    URL                  ${WITH_LZ4_URL}
    URL_HASH             ${WITH_LZ4_URL_HASH}
    TIMEOUT              600
    LOG_BUILD            TRUE
    DOWNLOAD_NO_PROGRESS 1
    CONFIGURE_COMMAND     ""                  # no configure
    BUILD_IN_SOURCE       true                # lz4 Makefile is designed to run locally
    BUILD_COMMAND         make CFLAGS=-fPIC   # to allow static linking in shared library
    INSTALL_COMMAND       ""                  # suppress install
    "${LZ4_BYPRODUCTS}"
  )
endif()
list(APPEND EP_TARGETS ${LZ4_TARGET})
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
    set(LIBOPENSSL_LIBS ${3RDPARTY_LIBS_DIR}/lib/libssl.a ${3RDPARTY_LIBS_DIR}/lib/libcrypto.a)
    set(LIBOPENSSL_INCLUDES ${3RDPARTY_LIBS_DIR}/include)
    ADD_BY_PRODUCTS_VARIABLE ("LIBOPENSSL" ${LIBOPENSSL_LIBS})
    externalproject_add(${LIBOPENSSL_TARGET}
      URL                  ${WITH_LIBOPENSSL_URL}
      URL_HASH             ${WITH_LIBOPENSSL_URL_HASH}
      LOG_CONFIGURE        TRUE
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    <SOURCE_DIR>/config --prefix=${3RDPARTY_LIBS_DIR} no-shared # ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        make all AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install_sw AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      "${LIBOPENSSL_BYPRODUCTS}"
      )
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
    set(LIBUNIXODBC_LIBS ${3RDPARTY_LIBS_DIR}/lib/libodbc.so)
    set(LIBUNIXODBC_INCLUDES ${3RDPARTY_LIBS_DIR}/include)
    ADD_BY_PRODUCTS_VARIABLE ("LIBUNIXODBC" ${LIBUNIXODBC_LIBS})
    externalproject_add(${LIBUNIXODBC_TARGET}
      URL                  ${WITH_LIBUNIXODBC_URL}
      URL_HASH             ${WITH_LIBUNIXODBC_URL_HASH}
      LOG_CONFIGURE        TRUE
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    <SOURCE_DIR>/configure --prefix=${3RDPARTY_LIBS_DIR} # ${DEFAULT_CONFIGURE_OPTS}
      BUILD_COMMAND        make AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      INSTALL_COMMAND      make install AUTOCONF=: AUTOHEADER=: AUTOMAKE=: ACLOCAL=:
      "${LIBUNIXODBC_BYPRODUCTS}"
      )
    list(APPEND EP_TARGETS ${LIBUNIXODBC_TARGET})
  endif(UNIX)
else(WITH_LIBUNIXODBC STREQUAL "EXTERNAL")
    message(FATAL_ERROR "Invalid WITH_LIBUNIXODBC option value ${WITH_LIBUNIXODBC}")
endif(WITH_LIBUNIXODBC STREQUAL "EXTERNAL")
list(APPEND EP_INCLUDES ${LIBUNIXODBC_INCLUDES})

# rapidjson
set(RAPIDJSON_TARGET rapidjson)
# add to include directories
set(RAPIDJSON_INCLUDES ${3RDPARTY_LIBS_DIR}/Source/rapidjson/include)
set(RAPIDJSON_LIBS "")
ADD_BY_PRODUCTS_VARIABLE ("RAPIDJSON" ${RAPIDJSON_INCLUDES})
externalproject_add(${RAPIDJSON_TARGET}
  # tried URL but archive gets downloaded every time. URL_MD5 may help
  URL                  ${WITH_RAPIDJSON_URL}
  URL_HASH             ${WITH_RAPIDJSON_URL_HASH}
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
  "${RAPIDJSON_BYPRODUCTS}"
)

# add to external project targets
list(APPEND EP_TARGETS ${RAPIDJSON_TARGET})
list(APPEND EP_INCLUDES ${RAPIDJSON_INCLUDES})

# RE2
set(RE2_TARGET re2)
if(WITH_RE2 STREQUAL "EXTERNAL")
  if(UNIX)
    set(RE2_INCLUDES ${3RDPARTY_LIBS_DIR}/Source/re2)
    set(RE2_LIBS ${3RDPARTY_LIBS_DIR}/Source/re2/obj/libre2.a)
    ADD_BY_PRODUCTS_VARIABLE ("RE2" ${RE2_LIBS})
    externalproject_add(${RE2_TARGET}
      URL                  ${WITH_RE2_URL}
      URL_HASH             ${WITH_RE2_URL_HASH}
      TIMEOUT              600
      LOG_BUILD            TRUE
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND ""        # no configure
      BUILD_IN_SOURCE true        # re2 Makefile is designed to run locally
      BUILD_COMMAND make CFLAGS=-fPIC CXXFLAGS=-fPIC # to allow static linking in shared library
      INSTALL_COMMAND ""
      "${RE2_BYPRODUCTS}"
    )
  else(UNIX)
    set(RE2_INCLUDES ${WINDOWS_EXTERNAL_DIR}/RE2/include)
    set(RE2_LIBS ${WINDOWS_EXTERNAL_DIR}/RE2/lib/re2.lib)
    set(RE2_DLL ${WINDOWS_EXTERNAL_DIR}/RE2/bin/re2.dll)
    add_custom_target(${RE2_TARGET}
      COMMAND ${CMAKE_COMMAND} -E echo "  copying ${RE2_TARGET} to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/re2.dll"
      COMMAND ${CMAKE_COMMAND} -E copy ${RE2_DLL} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/re2.dll
    )
  endif(UNIX)
else(WITH_RE2 STREQUAL "EXTERNAL")
  message(FATAL_ERROR "Invalid WITH_RE2 option value ${WITH_RE2}")
endif()
# add to external project targets
list(APPEND EP_TARGETS ${RE2_TARGET})
list(APPEND EP_LIBS ${RE2_LIBS})
list(APPEND EP_INCLUDES ${RE2_INCLUDES})

# libTBB
set(LIBTBB_TARGET libtbb)
if(WITH_LIBTBB STREQUAL "EXTERNAL")
  if(UNIX)
    #compile TBB library given an internet url to a TBB archive
    #e.g. https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.11.0.tar.gz
    set(LIBTBB_LIBS ${3RDPARTY_LIBS_DIR}/lib/libtbb.a)
    set(LIBTBB_INCLUDES ${3RDPARTY_LIBS_DIR}/Source/libtbb/include/tbb ${3RDPARTY_LIBS_DIR}/Source/libtbb/include/oneapi ${3RDPARTY_LIBS_DIR}/Source/libtbb/include/oneapi/tbb ${3RDPARTY_LIBS_DIR}/Source/libtbb/include/oneapi/tbb/detail)
    set(LIBTBB_CONFIGURE -DCMAKE_INSTALL_PREFIX=${3RDPARTY_LIBS_DIR} -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_CXX_FLAGS=-DTBB_ALLOCATOR_TRAITS_BROKEN -D__TBB_DYNAMIC_LOAD_ENABLED=0 -DTBBMALLOC_BUILD=OFF -DTBBMALLOC_PROXY_BUILD=OFF -DTBB_TEST=OFF -DTBB_ENABLE_IPO=OFF -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release)
    ADD_BY_PRODUCTS_VARIABLE ("LIBTBB" ${LIBTBB_LIBS})
    externalproject_add(${LIBTBB_TARGET}
      URL                  ${WITH_LIBTBB_URL}
      URL_HASH             ${WITH_LIBTBB_URL_HASH}
      LOG_CONFIGURE        TRUE
      LOG_BUILD            TRUE
      LOG_INSTALL          TRUE
      TIMEOUT              600
      DOWNLOAD_NO_PROGRESS 1
      CONFIGURE_COMMAND    ${CMAKE_COMMAND} ${LIBTBB_CONFIGURE}
      BUILD_IN_SOURCE      TRUE
      BUILD_COMMAND        make -j
      INSTALL_COMMAND      make -j install
      "${LIBTBB_BYPRODUCTS}"
    )

    list(APPEND TBB_TARGETS ${LIBTBB_TARGET})
    list(APPEND TBB_INCLUDES ${LIBTBB_INCLUDES})
    list(APPEND TBB_LIBS ${LIBTBB_LIBS})
  endif(UNIX)
endif()

# libhdf5
set(HDF_INCLUDES "")
set(HDF_LIBS "")

set(HDF_TARGET hdf)
if(WITH_HDF STREQUAL "EXTERNAL")
  if(WIN32)
    message(FATAL_ERROR "Invalid WITH_HDF option value ${WITH_HDF}")
  else()
    set(HDF_INCLUDES ${3RDPARTY_LIBS_DIR}/include)
    set(HDF_LIBS ${3RDPARTY_LIBS_DIR}/lib/libhdf5.a)

    ADD_BY_PRODUCTS_VARIABLE ("HDF" ${HDF_LIBS})
    externalproject_add(${HDF_TARGET}
      URL                   ${WITH_HDF_URL}
      TIMEOUT               600
      LOG_BUILD             TRUE
      DOWNLOAD_NO_PROGRESS  1
      CONFIGURE_COMMAND     ${DEFAULT_CONFIGURE_OPTS}
      BUILD_IN_SOURCE       true
      BUILD_COMMAND         make CFLAGS=-fPIC CXXFLAGS=-fPIC # to allow static linking in shared library
      "${HDF_BYPRODUCTS}"
    )

    list(APPEND EP_TARGETS ${HDF_TARGET})
    list(APPEND EP_LIBS ${HDF_LIBS})
    list(APPEND EP_INCLUDES ${HDF_INCLUDES})
  endif()
endif()

# ZLIB (libz)
set(ZLIB_TARGET zlib)
if(WITH_ZLIB STREQUAL "EXTERNAL")
  if(WIN32)
    message(FATAL_ERROR "Invalid WITH_ZLIB option value ${WITH_ZLIB}")
  else()
    set(ZLIB_INCLUDES ${3RDPARTY_LIBS_DIR}/include)
    set(ZLIB_LIBS     ${3RDPARTY_LIBS_DIR}/lib/libz.a)

    ADD_BY_PRODUCTS_VARIABLE ("ZLIB" ${ZLIB_LIBS})
    ExternalProject_Add(
          ${ZLIB_TARGET}
          GIT_REPOSITORY ${WITH_ZLIB_URL}
          GIT_TAG v1.3.1
          CONFIGURE_COMMAND ${CMAKE_COMMAND} -B <BINARY_DIR> -S <SOURCE_DIR>
          -DCMAKE_INSTALL_PREFIX=${3RDPARTY_LIBS_DIR}
          -DCMAKE_POSITION_INDEPENDENT_CODE=ON
          BUILD_COMMAND make -C <BINARY_DIR>
          "${ZLIB_BYPRODUCTS}"
    )

    list(APPEND EP_TARGETS ${ZLIB_TARGET})
    list(APPEND EP_LIBS ${ZLIB_LIBS})
    list(APPEND EP_INCLUDES ${ZLIB_INCLUDES})
  endif()
endif()

# highfive (header-only)
set(HIGHFIVE_TARGET highfive)
if(WITH_HIGHFIVE STREQUAL "EXTERNAL")
  if(WIN32)
    message(FATAL_ERROR "Invalid WITH_HIGHFIVE option value ${WITH_HIGHFIVE}")
  else()
    set(HIGHFIVE_INCLUDES ${3RDPARTY_LIBS_DIR}/include)

    # dependencies (HDF5)
    set(HDF5_LIBRARIES ${HDF_LIBS})
    set(HDF5_INCLUDE_DIRS ${HDF_INCLUDES})

    ADD_BY_PRODUCTS_VARIABLE ("HIGHFIVE" ${HIGHFIVE_INCLUDES}/highfive)
    ExternalProject_Add(
          ${HIGHFIVE_TARGET}
          GIT_REPOSITORY ${WITH_HIGHFIVE_URL}
          GIT_TAG v2.10.1
          CONFIGURE_COMMAND ${CMAKE_COMMAND} -B <BINARY_DIR> -S <SOURCE_DIR>
          -DCMAKE_INSTALL_PREFIX=${3RDPARTY_LIBS_DIR}
          -DHIGHFIVE_EXAMPLES=Off
          -DHIGHFIVE_USE_BOOST=Off
          -DHIGHFIVE_USE_EIGEN=Off
          -DHIGHFIVE_USE_XTENSOR=Off
          -DHIGHFIVE_USE_OPENCV=Off
          -DHIGHFIVE_UNIT_TESTS=Off
          -DDHDF5_BUILD_CPP_LIB=ON
          -DHDF5_LIBRARIES=${HDF5_LIBRARIES}
          -DHDF5_INCLUDE_DIRS=${HDF5_INCLUDE_DIRS}
          -DHDF5_ROOT=${3RDPARTY_LIBS_DIR}
          BUILD_COMMAND make -C <BINARY_DIR>
          "${HIGHFIVE_BYPRODUCTS}"
    )

    list(APPEND EP_TARGETS ${HIGHFIVE_TARGET})
    # list(APPEND EP_LIBS ${HIGHFIVE_LIBS})
    list(APPEND EP_INCLUDES ${HIGHFIVE_INCLUDES})
  endif()
  add_dependencies(${HIGHFIVE_TARGET} ${ZLIB_TARGET})
  add_dependencies(${HIGHFIVE_TARGET} ${HDF_TARGET})
endif()

find_package(AVX)

# libFaiss
set(LIBFAISS_TARGET libfaiss)
set(LIBFAISS_INCLUDES ${3RDPARTY_LIBS_DIR}/faiss/include)

# check option and architecture
if(FAISS_SIMD_MODE STREQUAL "AVX512" AND !CXX_AVX512_FOUND)
  message(FATAL_ERROR "avx512 is not supported on this architecture")
endif()
if(FAISS_SIMD_MODE STREQUAL "AVX2" AND !CXX_AVX2_FOUND)
  message(FATAL_ERROR "avx2 is not supported on this architecture")
endif()

# set libfaiss library path
if(FAISS_SIMD_MODE STREQUAL "AVX512")
  set(LIBFAISS_LIBS_NAME "faiss_avx512")
  set(LIBFAISS_LIBS ${3RDPARTY_LIBS_DIR}/Build/libfaiss/faiss/libfaiss_avx512.a)
  set(FAISS_OPT_LEVEL "avx512")
elseif(FAISS_SIMD_MODE STREQUAL "AVX2")
  set(LIBFAISS_LIBS_NAME "faiss_avx2")
  set(LIBFAISS_LIBS ${3RDPARTY_LIBS_DIR}/Build/libfaiss/faiss/libfaiss_avx2.a)
  set(FAISS_OPT_LEVEL "avx2")
else()
  set(LIBFAISS_LIBS_NAME "faiss")
  set(LIBFAISS_LIBS ${3RDPARTY_LIBS_DIR}/Build/libfaiss/faiss/libfaiss.a)
  set(FAISS_OPT_LEVEL "")
endif()

ADD_BY_PRODUCTS_VARIABLE ("LIBFAISS" ${LIBFAISS_LIBS})
if(WITH_FAISS STREQUAL "EXTERNAL")
  externalproject_add(
        ${LIBFAISS_TARGET}
        URL ${WITH_FAISS_URL}
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        TIMEOUT 600
        DOWNLOAD_NO_PROGRESS 1
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -B <BINARY_DIR> -S <SOURCE_DIR>
        "-DCMAKE_CXX_FLAGS=-llapack -lopenblas"
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${3RDPARTY_LIBS_DIR}/faiss
        -DFAISS_OPT_LEVEL=${FAISS_OPT_LEVEL}
        -DBUILD_SHARED_LIBS=OFF
        -DFAISS_ENABLE_GPU=OFF
        -DFAISS_ENABLE_PYTHON=OFF
        -DBUILD_TESTING=OFF
        BUILD_COMMAND make -C <BINARY_DIR> -j ${LIBFAISS_LIBS_NAME}
        "${LIBFAISS_BYPRODUCTS}"
  )
endif()

list(APPEND EP_TARGETS ${LIBFAISS_TARGET})
list(APPEND EP_INCLUDES ${LIBFAISS_INCLUDES})
list(APPEND EP_LIBS ${LIBFAISS_LIBS})

include(FetchContent)

# usearch
FetchContent_Declare(
  usearch
  GIT_REPOSITORY https://github.com/unum-cloud/usearch.git
  GIT_TAG        v2.17.8
)

FetchContent_MakeAvailable(usearch)
set(USEARCH_TARGET usearch)
list(APPEND USEARCH_INCLUDES ${usearch_SOURCE_DIR}/include ${usearch_SOURCE_DIR}/fp16/include)
message(STATUS "usearch_SOURCE_DIR: ${usearch_SOURCE_DIR}")
list(APPEND USEARCH_TARGETS usearch)

# [FIXME!] Expose variables for other CMakeLists
set (EP_TARGETS ${EP_TARGETS} PARENT_SCOPE)
set (EP_LIBS ${EP_LIBS} PARENT_SCOPE)
set (EP_INCLUDES ${EP_INCLUDES} PARENT_SCOPE)

# TODO: EP_TARGETS, EP_LIBS, and EP_INCLUDES are currently linked across all targets.
# However, TBB is only required on the server side and should only be linked to libcubrid.so.
# To achieve this, TBB has been separated into a distinct list to ensure exclusive linking with libcubrid.so.
# Going forward, directories and libraries strictly used on the server side should be categorized
# under the EP_SERVER_XXX section.
set (TBB_TARGETS ${TBB_TARGETS} PARENT_SCOPE)
set (TBB_INCLUDES ${TBB_INCLUDES} PARENT_SCOPE)
set (TBB_LIBS ${TBB_LIBS} PARENT_SCOPE)

set (USEARCH_TARGETS ${USEARCH_TARGETS} PARENT_SCOPE)
set (USEARCH_INCLUDES ${USEARCH_INCLUDES} PARENT_SCOPE)

# Expose targets for sa - FIXME better solutions
include (3rdparty_util)

if(UNIX)
expose_3rdparty_variable(LIBEDIT)
expose_3rdparty_variable(LIBNCURSES)
expose_3rdparty_variable(HDF)
expose_3rdparty_variable(ZLIB)
expose_3rdparty_variable(HIGHFIVE)
endif(UNIX)

expose_3rdparty_variable(LIBEXPAT)
expose_3rdparty_variable(LIBJANSSON)
expose_3rdparty_variable(LZ4)
expose_3rdparty_variable(LIBOPENSSL)
expose_3rdparty_variable(LIBUNIXODBC)
expose_3rdparty_variable(RAPIDJSON)
expose_3rdparty_variable(RE2)
expose_3rdparty_variable(LIBTBB)
expose_3rdparty_variable(LIBFAISS)
expose_3rdparty_variable(USEARCH)