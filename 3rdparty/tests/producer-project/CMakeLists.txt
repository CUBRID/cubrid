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

project(cubrid_thirdparty_producer_case C)

foreach(required_variable IN ITEMS CONTRACT_MODULE FIXTURE_ROOT MANIFEST)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "producer test requires ${required_variable}")
  endif()
endforeach()

include("${CONTRACT_MODULE}")
cubrid_thirdparty_load_contract("${MANIFEST}")

set(dependency_targets "")
foreach(dependency IN LISTS CUBRID_3RDPARTY_DEPENDENCIES)
  add_custom_target("fixture-${dependency}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/${dependency}.built")
  list(APPEND dependency_targets "fixture-${dependency}")
endforeach()

set(CUBRID_3RDPARTY_EXPAT_INPUT_HEADERS
  "${FIXTURE_ROOT}/include/fixture/shared.h;${FIXTURE_ROOT}/include/fixture/shared.h;${FIXTURE_ROOT}/include/fixture/shared.h")
set(CUBRID_3RDPARTY_LIBEDIT_INPUT_HEADERS
  "${FIXTURE_ROOT}/include/fixture;${FIXTURE_ROOT}/include/fixture/shared.h")
set(CUBRID_3RDPARTY_LZ4_INPUT_HEADERS
  "${FIXTURE_ROOT}/include/fixture/shared.h;${FIXTURE_ROOT}/include/fixture/shared.h;${FIXTURE_ROOT}/include/fixture/shared.h")
set(CUBRID_3RDPARTY_OPENSSL_INPUT_HEADERS "${FIXTURE_ROOT}/include/fixture")
set(CUBRID_3RDPARTY_UNIXODBC_INPUT_HEADERS
  "${FIXTURE_ROOT}/include/fixture/shared.h;${FIXTURE_ROOT}/include/fixture/shared.h;${FIXTURE_ROOT}/include/fixture/shared.h")
set(CUBRID_3RDPARTY_RAPIDJSON_INPUT_HEADERS "${FIXTURE_ROOT}/include/fixture")
set(CUBRID_3RDPARTY_RE2_INPUT_HEADERS "${FIXTURE_ROOT}/include/fixture")
set(CUBRID_3RDPARTY_ONETBB_INPUT_HEADERS
  "${FIXTURE_ROOT}/include/fixture;${FIXTURE_ROOT}/include/fixture")

foreach(dependency IN ITEMS expat libedit lz4 openssl re2 onetbb)
  string(TOUPPER "${dependency}" dependency_variable_name)
  set("CUBRID_3RDPARTY_${dependency_variable_name}_INPUT_LIBRARIES" "${FIXTURE_ROOT}/lib/libfixture.a")
endforeach()
set(CUBRID_3RDPARTY_OPENSSL_INPUT_LIBRARIES
  "${FIXTURE_ROOT}/lib/libfixture.a;${FIXTURE_ROOT}/lib/libfixture.a")
set(CUBRID_3RDPARTY_UNIXODBC_INPUT_LIBRARIES "${FIXTURE_ROOT}/lib/libodbc.so")
if(PRODUCER_MISSING_INPUT)
  set(CUBRID_3RDPARTY_EXPAT_INPUT_HEADERS
    "${FIXTURE_ROOT}/missing-expat.h;${FIXTURE_ROOT}/include/fixture/shared.h;${FIXTURE_ROOT}/include/fixture/shared.h")
endif()

if(NOT DEFINED PRODUCER_SOURCE_ROOT)
  set(PRODUCER_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
endif()

cubrid_thirdparty_add_prefix_target(
  SOURCE_ROOT "${PRODUCER_SOURCE_ROOT}"
  BINARY_ROOT "${CMAKE_BINARY_DIR}"
  DEPENDENCY_TARGETS ${dependency_targets})
