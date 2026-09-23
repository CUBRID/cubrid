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
set(CONTRACT_NEGATIVE_TEST_DRIVER "${CMAKE_CURRENT_LIST_DIR}/run_prebuilt_negative_case.cmake")
set(CONTRACT_EXPECTED_FINGERPRINT "58a2f6e3383ab366c60efd66996e11ac421836c227b702ed32bb705e72dd185e")

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
  consumer_cache_success
  consumer_environment_success
  consumer_cache_environment_equal
  producer_success
  producer_missing_input_atomic
  producer_output_unset
  producer_output_relative
  producer_output_filesystem_root
  producer_output_source_root
  producer_output_binary_root
  producer_output_source_ancestor
  producer_output_binary_ancestor
  producer_output_source_descendant
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
  set_tests_properties("thirdparty-contract.${test_case}" PROPERTIES LABELS thirdparty-contract)
endforeach()

set_tests_properties(
  thirdparty-contract.producer_success
  thirdparty-contract.producer_missing_input_atomic
  thirdparty-contract.producer_output_unset
  thirdparty-contract.producer_output_relative
  thirdparty-contract.producer_output_filesystem_root
  thirdparty-contract.producer_output_source_root
  thirdparty-contract.producer_output_binary_root
  thirdparty-contract.producer_output_source_ancestor
  thirdparty-contract.producer_output_binary_ancestor
  thirdparty-contract.producer_output_source_descendant
  PROPERTIES
  LABELS "thirdparty-contract;thirdparty-contract-producer")

set_tests_properties(
  thirdparty-contract.consumer_cache_success
  thirdparty-contract.consumer_environment_success
  thirdparty-contract.consumer_cache_environment_equal
  PROPERTIES
  LABELS "thirdparty-contract;thirdparty-contract-consumer")

set(CONTRACT_NEGATIVE_CASES
  unknown_mode
  root_only_cache
  root_only_environment
  mode_cache_environment_conflict
  root_cache_environment_conflict
  root_cache_environment_conflict_external
  root_missing
  root_relative
  root_path_missing
  root_not_directory
  source_manifest_missing
  source_manifest_empty
  source_manifest_not_regular
  source_manifest_malformed_json
  source_manifest_unsupported_schema
  source_manifest_unsupported_recipe
  source_manifest_bom
  source_manifest_unsupported_linkage
  prefix_manifest_missing
  prefix_manifest_empty
  prefix_manifest_not_regular
  prefix_manifest_byte_mutation
  prefix_manifest_different_valid
  prefix_manifest_malformed_json
  provenance_missing
  provenance_empty
  provenance_not_regular
  provenance_malformed_json
  provenance_schema
  provenance_producer_revision
  provenance_fingerprint
  provenance_declared_platform
  provenance_observed_platform
  header_missing
  header_empty
  header_not_regular
  header_path_escape
  static_library_missing
  static_library_empty
  static_library_not_regular
  static_library_path_escape
  license_missing
  license_empty
  license_not_regular
  license_path_escape
  shared_library_missing
  shared_library_not_symlink
  shared_library_dangling_symlink
  shared_library_absolute_symlink
  shared_library_escaping_symlink
  shared_library_indirect_escape
  shared_library_empty_payload
  shared_library_nonregular_payload
  shared_library_wrong_soname
  )

foreach(test_case IN LISTS CONTRACT_NEGATIVE_CASES)
  add_test(
    NAME thirdparty-contract-negative.${test_case}
    COMMAND
      "${CMAKE_COMMAND}"
      "-DCASE=${test_case}"
      "-DCONTRACT_MODULE=${CONTRACT_MODULE}"
      "-DEXPECTED_FINGERPRINT=${CONTRACT_EXPECTED_FINGERPRINT}"
      "-DMANIFEST=${CONTRACT_MANIFEST}"
      "-DTEST_BINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
      "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
      "-DTEST_SOURCE_ROOT=${CMAKE_CURRENT_LIST_DIR}"
      -P "${CONTRACT_NEGATIVE_TEST_DRIVER}")
  set_tests_properties(
    "thirdparty-contract-negative.${test_case}"
    PROPERTIES LABELS "thirdparty-contract;thirdparty-contract-negative")
endforeach()
