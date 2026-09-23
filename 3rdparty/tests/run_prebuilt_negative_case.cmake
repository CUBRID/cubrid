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

foreach(required_variable IN ITEMS
    CASE CONTRACT_MODULE EXPECTED_FINGERPRINT MANIFEST TEST_BINARY_ROOT TEST_GENERATOR TEST_SOURCE_ROOT)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "negative contract test requires ${required_variable}")
  endif()
endforeach()

set(case_root "${TEST_BINARY_ROOT}/negative-${CASE}")
set(fixture_root "${case_root}/fixture")
set(fixture_build "${case_root}/fixture-build")
set(producer_build "${case_root}/producer-build")
set(prefix_root "${case_root}/prefix")
set(relocated_root "${case_root}/relocated-prefix")
set(consumer_build "${case_root}/consumer-build")
set(sentinel "${case_root}/external-project-network.sentinel")
set(result "${case_root}/result.txt")
set(marker "${case_root}/external-project.marker")
set(source_manifest "${MANIFEST}")

file(REMOVE_RECURSE "${case_root}")
file(MAKE_DIRECTORY "${case_root}")

function(_run_checked description)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE command_status
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr)
  if(NOT command_status EQUAL 0)
    message(FATAL_ERROR
      "${description} failed (${command_status}):\n${command_stdout}${command_stderr}")
  endif()
endfunction()

_run_checked("fixture configure"
  "${CMAKE_COMMAND}"
  -S "${TEST_SOURCE_ROOT}/fixture"
  -B "${fixture_build}"
  -G "${TEST_GENERATOR}"
  -DCMAKE_BUILD_TYPE=Release
  "-DCMAKE_INSTALL_PREFIX=${fixture_root}")
_run_checked("fixture build"
  "${CMAKE_COMMAND}" --build "${fixture_build}" --target install)
_run_checked("prefix producer configure"
  "${CMAKE_COMMAND}"
  -S "${TEST_SOURCE_ROOT}/producer-project"
  -B "${producer_build}"
  -G "${TEST_GENERATOR}"
  "-DCONTRACT_MODULE=${CONTRACT_MODULE}"
  "-DFIXTURE_ROOT=${fixture_root}"
  "-DMANIFEST=${MANIFEST}"
  "-DCUBRID_3RDPARTY_PREFIX_OUTPUT=${prefix_root}")
_run_checked("prefix producer build"
  "${CMAKE_COMMAND}" --build "${producer_build}" --target cubrid_thirdparty_prefix)
file(RENAME "${prefix_root}" "${relocated_root}")
set(prefix_root "${relocated_root}")

set(mode "CI_PREBUILT")
set(root "${prefix_root}")
set(cache_mode "${mode}")
set(cache_root "${root}")
set(environment_mode "")
set(environment_root "")
set(expected_check "")
set(use_contract_project FALSE)

set(prefix_manifest "${prefix_root}/share/cubrid-thirdparty/manifest.json")
set(provenance "${prefix_root}/share/cubrid-thirdparty/provenance.json")
set(header "${prefix_root}/include/expat.h")
set(static_library "${prefix_root}/lib/libexpat.a")
set(license "${prefix_root}/licenses/libexpat_license.txt")
set(shared_link "${prefix_root}/lib/libodbc.so")
set(shared_soname_link "${prefix_root}/lib/libodbc.so.2")
set(shared_payload "${prefix_root}/lib/libodbc.so.2.0.0")
file(READ "${provenance}" original_provenance_json)
string(JSON original_producer_revision GET "${original_provenance_json}" producer commit)

if(CASE STREQUAL "unknown_mode")
  set(cache_mode "FROZEN")
  set(expected_check "mode_supported")
elseif(CASE STREQUAL "root_only_cache")
  set(cache_mode "")
  set(expected_check "mode_required_with_root")
elseif(CASE STREQUAL "root_only_environment")
  set(cache_mode "")
  set(cache_root "")
  set(environment_root "${root}")
  set(expected_check "mode_required_with_root")
elseif(CASE STREQUAL "mode_cache_environment_conflict")
  set(environment_mode "EXTERNAL")
  set(expected_check "mode_cache_environment_match")
elseif(CASE STREQUAL "root_cache_environment_conflict")
  set(environment_mode "CI_PREBUILT")
  set(environment_root "${case_root}/different-prefix")
  set(expected_check "root_cache_environment_match")
elseif(CASE STREQUAL "root_cache_environment_conflict_external")
  set(cache_mode "EXTERNAL")
  set(environment_mode "EXTERNAL")
  set(environment_root "${case_root}/different-prefix")
  set(expected_check "root_cache_environment_match")
elseif(CASE STREQUAL "root_missing")
  set(cache_root "")
  set(expected_check "root_required")
elseif(CASE STREQUAL "root_relative")
  set(cache_root "relative-prefix")
  set(expected_check "root_absolute")
elseif(CASE STREQUAL "root_path_missing")
  set(cache_root "${case_root}/missing-prefix")
  set(expected_check "root_directory")
elseif(CASE STREQUAL "root_not_directory")
  set(cache_root "${case_root}/prefix-file")
  file(WRITE "${cache_root}" "not a directory\n")
  set(expected_check "root_directory")
elseif(CASE MATCHES "^source_manifest_")
  set(use_contract_project TRUE)
  set(source_manifest "${case_root}/source-manifest.json")
  if(CASE STREQUAL "source_manifest_empty")
    file(WRITE "${source_manifest}" "")
    set(expected_check "source_manifest_nonempty")
  elseif(CASE STREQUAL "source_manifest_not_regular")
    file(MAKE_DIRECTORY "${source_manifest}")
    set(expected_check "source_manifest_regular")
  elseif(CASE STREQUAL "source_manifest_malformed_json")
    file(WRITE "${source_manifest}" "{malformed json}\n")
    set(expected_check "source_manifest_json")
  elseif(CASE STREQUAL "source_manifest_unsupported_schema")
    file(READ "${MANIFEST}" source_manifest_contents)
    string(REPLACE
      "\"schema\": \"cubrid-thirdparty-manifest-v1\""
      "\"schema\": \"unsupported-manifest\""
      source_manifest_contents "${source_manifest_contents}")
    file(WRITE "${source_manifest}" "${source_manifest_contents}")
    set(expected_check "source_manifest_schema")
  elseif(CASE STREQUAL "source_manifest_unsupported_recipe")
    file(READ "${MANIFEST}" source_manifest_contents)
    string(REGEX REPLACE
      "\"recipe\": \"autoconf\""
      "\"recipe\": \"unsupported\""
      source_manifest_contents "${source_manifest_contents}")
    file(WRITE "${source_manifest}" "${source_manifest_contents}")
    set(expected_check "source_manifest_contract:unsupported recipe: unsupported")
  elseif(CASE STREQUAL "source_manifest_bom")
    file(READ "${MANIFEST}" source_manifest_contents)
    string(ASCII 239 187 191 utf8_bom)
    file(WRITE "${source_manifest}" "${utf8_bom}${source_manifest_contents}")
    set(expected_check "source_manifest_contract:manifest must be UTF-8 without BOM")
  elseif(CASE STREQUAL "source_manifest_unsupported_linkage")
    file(READ "${MANIFEST}" source_manifest_contents)
    string(REGEX REPLACE
      "\"linkage\": \"static\""
      "\"linkage\": \"dynamic\""
      source_manifest_contents "${source_manifest_contents}")
    file(WRITE "${source_manifest}" "${source_manifest_contents}")
    set(expected_check "source_manifest_contract:unsupported linkage: dynamic")
  else()
    set(expected_check "source_manifest_regular")
  endif()
elseif(CASE STREQUAL "prefix_manifest_missing")
  file(REMOVE "${prefix_manifest}")
  set(expected_check "prefix_manifest_regular")
elseif(CASE STREQUAL "prefix_manifest_empty")
  file(WRITE "${prefix_manifest}" "")
  set(expected_check "prefix_manifest_nonempty")
elseif(CASE STREQUAL "prefix_manifest_not_regular")
  file(REMOVE "${prefix_manifest}")
  file(MAKE_DIRECTORY "${prefix_manifest}")
  set(expected_check "prefix_manifest_regular")
elseif(CASE STREQUAL "prefix_manifest_byte_mutation")
  file(APPEND "${prefix_manifest}" " ")
  set(expected_check "manifest_fingerprint")
elseif(CASE STREQUAL "prefix_manifest_different_valid")
  file(READ "${prefix_manifest}" prefix_contents)
  string(REPLACE "expat-2.8.2.tar.gz" "expat-2.8.3.tar.gz" prefix_contents "${prefix_contents}")
  file(WRITE "${prefix_manifest}" "${prefix_contents}")
  set(expected_check "manifest_fingerprint")
elseif(CASE STREQUAL "prefix_manifest_malformed_json")
  file(WRITE "${prefix_manifest}" "{malformed json}\n")
  set(expected_check "manifest_fingerprint")
elseif(CASE STREQUAL "provenance_missing")
  file(REMOVE "${provenance}")
  set(expected_check "provenance_regular")
elseif(CASE STREQUAL "provenance_empty")
  file(WRITE "${provenance}" "")
  set(expected_check "provenance_nonempty")
elseif(CASE STREQUAL "provenance_not_regular")
  file(REMOVE "${provenance}")
  file(MAKE_DIRECTORY "${provenance}")
  set(expected_check "provenance_regular")
elseif(CASE STREQUAL "provenance_malformed_json")
  file(WRITE "${provenance}" "{malformed json}\n")
  set(expected_check "provenance_json")
elseif(CASE MATCHES "^provenance_")
  file(READ "${provenance}" provenance_json)
  if(CASE STREQUAL "provenance_schema")
    string(JSON provenance_json SET "${provenance_json}" schema "\"unsupported-provenance\"")
    set(expected_check "provenance_schema")
  elseif(CASE STREQUAL "provenance_producer_revision")
    string(JSON provenance_json SET "${provenance_json}" producer commit "\"55208c13\"")
    set(expected_check "producer_revision")
  elseif(CASE STREQUAL "provenance_fingerprint")
    string(JSON provenance_json SET "${provenance_json}" spec_fingerprint
      "\"0000000000000000000000000000000000000000000000000000000000000000\"")
    set(expected_check "provenance_fingerprint")
  elseif(CASE STREQUAL "provenance_declared_platform")
    string(JSON provenance_json SET "${provenance_json}" platform declared variant "\"other-variant\"")
    set(expected_check "provenance_declared_platform")
  elseif(CASE STREQUAL "provenance_observed_platform")
    string(JSON provenance_json SET "${provenance_json}" platform observed architecture "\"aarch64\"")
    set(expected_check "provenance_observed_platform")
  endif()
  file(WRITE "${provenance}" "${provenance_json}\n")
elseif(CASE STREQUAL "header_missing")
  file(REMOVE "${header}")
  set(expected_check "header_regular:include/expat.h")
elseif(CASE STREQUAL "header_empty")
  file(WRITE "${header}" "")
  set(expected_check "header_nonempty:include/expat.h")
elseif(CASE STREQUAL "header_not_regular")
  file(REMOVE "${header}")
  file(CREATE_LINK "expat_external.h" "${header}" SYMBOLIC)
  set(expected_check "header_regular:include/expat.h")
elseif(CASE STREQUAL "header_path_escape")
  file(WRITE "${case_root}/outside-header.h" "outside\n")
  file(REMOVE "${header}")
  file(CREATE_LINK "${case_root}/outside-header.h" "${header}" SYMBOLIC)
  set(expected_check "artifact_inside_root:include/expat.h")
elseif(CASE STREQUAL "static_library_missing")
  file(REMOVE "${static_library}")
  set(expected_check "static_library_regular:lib/libexpat.a")
elseif(CASE STREQUAL "static_library_empty")
  file(WRITE "${static_library}" "")
  set(expected_check "static_library_nonempty:lib/libexpat.a")
elseif(CASE STREQUAL "static_library_not_regular")
  file(REMOVE "${static_library}")
  file(MAKE_DIRECTORY "${static_library}")
  set(expected_check "static_library_regular:lib/libexpat.a")
elseif(CASE STREQUAL "static_library_path_escape")
  file(WRITE "${case_root}/outside-static.a" "outside\n")
  file(REMOVE "${static_library}")
  file(CREATE_LINK "${case_root}/outside-static.a" "${static_library}" SYMBOLIC)
  set(expected_check "artifact_inside_root:lib/libexpat.a")
elseif(CASE STREQUAL "license_missing")
  file(REMOVE "${license}")
  set(expected_check "license_regular:licenses/libexpat_license.txt")
elseif(CASE STREQUAL "license_empty")
  file(WRITE "${license}" "")
  set(expected_check "license_nonempty:licenses/libexpat_license.txt")
elseif(CASE STREQUAL "license_not_regular")
  file(REMOVE "${license}")
  file(MAKE_DIRECTORY "${license}")
  set(expected_check "license_regular:licenses/libexpat_license.txt")
elseif(CASE STREQUAL "license_path_escape")
  file(WRITE "${case_root}/outside-license.txt" "outside\n")
  file(REMOVE "${license}")
  file(CREATE_LINK "${case_root}/outside-license.txt" "${license}" SYMBOLIC)
  set(expected_check "artifact_inside_root:licenses/libexpat_license.txt")
elseif(CASE STREQUAL "shared_library_missing")
  file(REMOVE "${shared_link}")
  set(expected_check "shared_library_symlink:lib/libodbc.so")
elseif(CASE STREQUAL "shared_library_not_symlink")
  file(REMOVE "${shared_link}")
  configure_file("${shared_payload}" "${shared_link}" COPYONLY)
  set(expected_check "shared_library_symlink:lib/libodbc.so")
elseif(CASE STREQUAL "shared_library_dangling_symlink")
  file(REMOVE "${shared_link}")
  file(CREATE_LINK "missing.so" "${shared_link}" SYMBOLIC)
  set(expected_check "shared_library_dangling:lib/libodbc.so")
elseif(CASE STREQUAL "shared_library_absolute_symlink")
  file(REMOVE "${shared_link}")
  file(CREATE_LINK "${shared_payload}" "${shared_link}" SYMBOLIC)
  set(expected_check "shared_library_relative_symlink:lib/libodbc.so")
elseif(CASE STREQUAL "shared_library_escaping_symlink")
  file(WRITE "${case_root}/outside-shared.so" "outside\n")
  file(REMOVE "${shared_link}")
  file(CREATE_LINK "../../outside-shared.so" "${shared_link}" SYMBOLIC)
  set(expected_check "shared_library_inside_root:lib/libodbc.so")
elseif(CASE STREQUAL "shared_library_indirect_escape")
  file(WRITE "${case_root}/outside-shared.so" "outside\n")
  file(CREATE_LINK "${case_root}" "${prefix_root}/lib/escape" SYMBOLIC)
  file(REMOVE "${shared_link}")
  file(CREATE_LINK "escape/outside-shared.so" "${shared_link}" SYMBOLIC)
  set(expected_check "shared_library_inside_root:lib/libodbc.so")
elseif(CASE STREQUAL "shared_library_empty_payload")
  file(WRITE "${shared_payload}" "")
  set(expected_check "shared_library_nonempty:lib/libodbc.so")
elseif(CASE STREQUAL "shared_library_nonregular_payload")
  file(REMOVE "${shared_payload}")
  file(MAKE_DIRECTORY "${shared_payload}")
  set(expected_check "shared_library_regular:lib/libodbc.so")
elseif(CASE STREQUAL "shared_library_wrong_soname")
  configure_file("${fixture_root}/lib/libodbc_wrong.so" "${shared_payload}" COPYONLY)
  set(expected_check "shared_library_soname:lib/libodbc.so")
else()
  message(FATAL_ERROR "unknown negative case: ${CASE}")
endif()

if(NOT cache_mode STREQUAL "")
  set(expected_mode_value "${cache_mode}")
elseif(NOT environment_mode STREQUAL "")
  set(expected_mode_value "${environment_mode}")
else()
  set(expected_mode_value "unset")
endif()
if(NOT cache_root STREQUAL "")
  set(expected_root_value "${cache_root}")
elseif(NOT environment_root STREQUAL "")
  set(expected_root_value "${environment_root}")
else()
  set(expected_root_value "unavailable")
endif()
if(expected_root_value STREQUAL "unavailable")
  set(expected_prefix_manifest_value "unavailable")
  set(expected_actual_fingerprint "missing")
else()
  set(expected_prefix_manifest_value
    "${expected_root_value}/share/cubrid-thirdparty/manifest.json")
  set(expected_actual_fingerprint "unreadable")
endif()
set(expected_fingerprint "${EXPECTED_FINGERPRINT}")
set(expected_producer_revision "${original_producer_revision}")
if(EXISTS "${source_manifest}")
  file(REAL_PATH "${source_manifest}" expected_source_manifest_value)
else()
  set(expected_source_manifest_value "${source_manifest}")
endif()

if(CASE MATCHES "^(unknown_mode|root_only_|mode_cache_environment_conflict|root_cache_environment_conflict)")
  set(expected_fingerprint "unavailable")
  set(expected_producer_revision "unknown")
elseif(CASE STREQUAL "root_missing")
  set(expected_actual_fingerprint "missing")
  set(expected_producer_revision "unknown")
elseif(CASE STREQUAL "root_relative")
  set(expected_producer_revision "unknown")
elseif(CASE STREQUAL "root_path_missing")
  set(expected_actual_fingerprint "missing")
  set(expected_producer_revision "unknown")
elseif(CASE STREQUAL "root_not_directory")
  set(expected_producer_revision "unknown")
elseif(CASE MATCHES "^source_manifest_")
  set(expected_fingerprint "unavailable")
  set(expected_producer_revision "unknown")
elseif(CASE MATCHES "^prefix_manifest_")
  set(expected_producer_revision "unknown")
  if(CASE STREQUAL "prefix_manifest_missing")
    set(expected_actual_fingerprint "missing")
  elseif(CASE STREQUAL "prefix_manifest_not_regular")
    set(expected_actual_fingerprint "unreadable")
  else()
    file(SHA256 "${prefix_manifest}" expected_actual_fingerprint)
    string(TOLOWER "${expected_actual_fingerprint}" expected_actual_fingerprint)
  endif()
elseif(CASE MATCHES "^provenance_(missing|empty|not_regular|malformed_json|schema)$")
  set(expected_actual_fingerprint "${EXPECTED_FINGERPRINT}")
  set(expected_producer_revision "unknown")
else()
  set(expected_actual_fingerprint "${EXPECTED_FINGERPRINT}")
  file(READ "${provenance}" expected_provenance_json)
  string(JSON expected_producer_revision GET "${expected_provenance_json}" producer commit)
endif()

set(environment_arguments
  --unset=CUBRID_3RDPARTY_MODE
  --unset=CUBRID_3RDPARTY_ROOT)
if(NOT environment_mode STREQUAL "")
  list(APPEND environment_arguments "CUBRID_3RDPARTY_MODE=${environment_mode}")
endif()
if(NOT environment_root STREQUAL "")
  list(APPEND environment_arguments "CUBRID_3RDPARTY_ROOT=${environment_root}")
endif()
set(cache_arguments "")
if(NOT cache_mode STREQUAL "")
  list(APPEND cache_arguments "-DCUBRID_3RDPARTY_MODE=${cache_mode}")
endif()
if(NOT cache_root STREQUAL "")
  list(APPEND cache_arguments "-DCUBRID_3RDPARTY_ROOT=${cache_root}")
endif()

if(use_contract_project)
  set(project_source "${TEST_SOURCE_ROOT}/contract-project")
  list(APPEND cache_arguments
    "-DCONTRACT_MODULE=${CONTRACT_MODULE}"
    "-DMANIFEST=${source_manifest}"
    "-DMARKER=${marker}"
    "-DSENTINEL=${sentinel}"
    "-DRESULT=${result}")
else()
  set(project_source "${TEST_SOURCE_ROOT}/consumer-project")
  list(APPEND cache_arguments
    "-DCUBRID_SOURCE_ROOT=${TEST_SOURCE_ROOT}/../.."
    "-DRESULT=${result}"
    "-DSENTINEL=${sentinel}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    ${environment_arguments}
    "${CMAKE_COMMAND}"
    -S "${project_source}"
    -B "${consumer_build}"
    -G "${TEST_GENERATOR}"
    ${cache_arguments}
  RESULT_VARIABLE configure_status
  OUTPUT_VARIABLE configure_stdout
  ERROR_VARIABLE configure_stderr)
set(configure_output "${configure_stdout}${configure_stderr}")

if(configure_status EQUAL 0)
  message(FATAL_ERROR "negative case ${CASE} unexpectedly configured successfully")
endif()
if(EXISTS "${sentinel}")
  file(READ "${sentinel}" sentinel_contents)
  message(FATAL_ERROR
    "negative case ${CASE} touched ExternalProject/network sentinel: ${sentinel_contents}")
endif()
if(EXISTS "${marker}")
  message(FATAL_ERROR "negative case ${CASE} fell back to the ExternalProject route")
endif()

foreach(expected_fragment IN ITEMS
    "mode=${expected_mode_value}"
    "root=${expected_root_value}"
    "source_manifest=${expected_source_manifest_value}"
    "prefix_manifest=${expected_prefix_manifest_value}"
    "expected_fingerprint=${expected_fingerprint}"
    "actual_fingerprint=${expected_actual_fingerprint}"
    "producer_revision=${expected_producer_revision}"
    "failed_check=${expected_check}")
  string(FIND "${configure_output}" "${expected_fragment}" fragment_position)
  if(fragment_position EQUAL -1)
    message(FATAL_ERROR
      "negative case ${CASE} output is missing '${expected_fragment}':\n${configure_output}")
  endif()
endforeach()
