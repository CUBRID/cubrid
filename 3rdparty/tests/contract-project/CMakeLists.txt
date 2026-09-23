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

project(cubrid_thirdparty_contract_case NONE)

if(NOT DEFINED CONTRACT_MODULE OR NOT DEFINED MANIFEST)
  message(FATAL_ERROR "contract test requires CONTRACT_MODULE and MANIFEST")
endif()

include("${CONTRACT_MODULE}")

cubrid_thirdparty_initialize_request("${MANIFEST}")
cubrid_thirdparty_select_mode(thirdparty_route)
cubrid_thirdparty_load_contract("${MANIFEST}")

if(thirdparty_route STREQUAL "EXTERNAL")
  file(WRITE "${MARKER}" "external route selected\n")
elseif(thirdparty_route STREQUAL "CI_PREBUILT")
  cubrid_thirdparty_use_prebuilt_prefix()
endif()

file(WRITE "${RESULT}"
  "schema=${CUBRID_3RDPARTY_SCHEMA}\n"
  "fingerprint=${CUBRID_3RDPARTY_SPEC_FINGERPRINT}\n"
  "dependencies=${CUBRID_3RDPARTY_DEPENDENCIES}\n"
  "expat_headers=${CUBRID_3RDPARTY_EXPAT_OUTPUT_HEADERS}\n"
  "openssl_libraries=${CUBRID_3RDPARTY_OPENSSL_OUTPUT_LIBRARIES}\n"
  "route=${thirdparty_route}\n"
  "unixodbc_soname=${CUBRID_3RDPARTY_UNIXODBC_OUTPUT_SONAMES}\n"
  )
