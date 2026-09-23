#
# Copyright 2026 CUBRID Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.21)

project(cubrid_thirdparty_consumer_case C)

foreach(required_variable IN ITEMS CUBRID_SOURCE_ROOT RESULT SENTINEL)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "consumer test requires ${required_variable}")
  endif()
endforeach()

list(APPEND CMAKE_MODULE_PATH
  "${CMAKE_CURRENT_LIST_DIR}/../network-sentinel"
  "${CUBRID_SOURCE_ROOT}/cmake")
add_subdirectory("${CUBRID_SOURCE_ROOT}/3rdparty" thirdparty)

foreach(external_target IN ITEMS
    libexpat libedit lz4 libopenssl libodbc rapidjson re2 libtbb)
  if(TARGET "${external_target}")
    file(WRITE "${SENTINEL}" "CI_PREBUILT declared ExternalProject target ${external_target}\n")
  endif()
endforeach()

add_executable(thirdparty_consumer main.c)
target_include_directories(thirdparty_consumer PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}"
  ${EP_INCLUDES}
  ${TBB_INCLUDES})
target_link_libraries(thirdparty_consumer PRIVATE ${EP_LIBS} ${LIBUNIXODBC_LIBS} ${TBB_LIBS})
set_target_properties(thirdparty_consumer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

file(WRITE "${RESULT}"
  "root=${CUBRID_3RDPARTY_ROOT}\n"
  "ep_targets=${EP_TARGETS}\n"
  "ep_includes=${EP_INCLUDES}\n"
  "ep_libs=${EP_LIBS}\n"
  "tbb_targets=${TBB_TARGETS}\n"
  "tbb_includes=${TBB_INCLUDES}\n"
  "tbb_libs=${TBB_LIBS}\n"
  "libexpat_target=${LIBEXPAT_TARGET}\n"
  "libexpat_includes=${LIBEXPAT_INCLUDES}\n"
  "libexpat_libs=${LIBEXPAT_LIBS}\n"
  "libedit_target=${LIBEDIT_TARGET}\n"
  "libedit_includes=${LIBEDIT_INCLUDES}\n"
  "libedit_libs=${LIBEDIT_LIBS}\n"
  "lz4_target=${LZ4_TARGET}\n"
  "lz4_includes=${LZ4_INCLUDES}\n"
  "lz4_libs=${LZ4_LIBS}\n"
  "libopenssl_target=${LIBOPENSSL_TARGET}\n"
  "libopenssl_includes=${LIBOPENSSL_INCLUDES}\n"
  "libopenssl_libs=${LIBOPENSSL_LIBS}\n"
  "libunixodbc_target=${LIBUNIXODBC_TARGET}\n"
  "libunixodbc_includes=${LIBUNIXODBC_INCLUDES}\n"
  "libunixodbc_libs=${LIBUNIXODBC_LIBS}\n"
  "rapidjson_target=${RAPIDJSON_TARGET}\n"
  "rapidjson_includes=${RAPIDJSON_INCLUDES}\n"
  "rapidjson_libs=${RAPIDJSON_LIBS}\n"
  "re2_target=${RE2_TARGET}\n"
  "re2_includes=${RE2_INCLUDES}\n"
  "re2_libs=${RE2_LIBS}\n"
  "libtbb_target=${LIBTBB_TARGET}\n"
  "libtbb_includes=${LIBTBB_INCLUDES}\n"
  "libtbb_libs=${LIBTBB_LIBS}\n")
