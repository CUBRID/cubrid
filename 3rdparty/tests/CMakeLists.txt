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

project(cubrid_thirdparty_contract_tests NONE)

enable_testing()

set(CONTRACT_MODULE "${CMAKE_CURRENT_LIST_DIR}/../cmake/CubridThirdPartyContract.cmake")
set(CONTRACT_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/../manifest.json")
set(CONTRACT_TEST_DRIVER "${CMAKE_CURRENT_LIST_DIR}/run_contract_case.cmake")
set(CONTRACT_EXPECTED_FINGERPRINT "4c19d1957a6a4df946ab6020a6e18a8bc4688a195221ca5e6c6abac27520c126")

set(CONTRACT_CASES
  valid_manifest
  bom_manifest
  crlf_manifest
  missing_final_lf
  extra_final_lf
  invalid_utf8
  noncanonical_manifest
  unsorted_nested_keys
  unknown_key
  missing_field
  duplicate_dependency
  dependency_order
  unsupported_recipe
  unsupported_linkage
  unsorted_output
  malformed_sha256
  noninteger_recipe_revision
  malformed_json
  unset_mode_uses_external
  external_mode_uses_external
  )

foreach(test_case IN LISTS CONTRACT_CASES)
  add_test(
    NAME thirdparty-contract.${test_case}
    COMMAND
      "${CMAKE_COMMAND}"
      "-DCASE=${test_case}"
      "-DCONTRACT_MODULE=${CONTRACT_MODULE}"
      "-DEXPECTED_FINGERPRINT=${CONTRACT_EXPECTED_FINGERPRINT}"
      "-DMANIFEST=${CONTRACT_MANIFEST}"
      "-DTEST_BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
      "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
      "-DTEST_SOURCE_ROOT=${CMAKE_CURRENT_LIST_DIR}"
      -P "${CONTRACT_TEST_DRIVER}"
    )
endforeach()
