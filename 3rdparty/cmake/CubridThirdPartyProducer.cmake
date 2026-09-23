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

cmake_policy(VERSION 3.21)

if(NOT DEFINED PRODUCER_CONFIG OR NOT EXISTS "${PRODUCER_CONFIG}")
  message(FATAL_ERROR "CUBRID third-party producer: PRODUCER_CONFIG is required")
endif()
include("${PRODUCER_CONFIG}")
include("${PRODUCER_CONTRACT_MODULE}")
cubrid_thirdparty_load_contract("${PRODUCER_MANIFEST}")

if(NOT PRODUCER_OBSERVED_OS STREQUAL CUBRID_3RDPARTY_PLATFORM_OS
    OR NOT PRODUCER_OBSERVED_ARCHITECTURE STREQUAL CUBRID_3RDPARTY_PLATFORM_ARCHITECTURE)
  message(FATAL_ERROR
    "CUBRID third-party producer: observed platform ${PRODUCER_OBSERVED_OS}/${PRODUCER_OBSERVED_ARCHITECTURE} "
    "does not match manifest ${CUBRID_3RDPARTY_PLATFORM_OS}/${CUBRID_3RDPARTY_PLATFORM_ARCHITECTURE}")
endif()

function(_producer_fail message_text)
  message(FATAL_ERROR "CUBRID third-party producer: ${message_text}")
endfunction()

function(_producer_require_inside path root description)
  file(REAL_PATH "${path}" resolved_path)
  file(REAL_PATH "${root}" resolved_root)
  file(RELATIVE_PATH relative_path "${resolved_root}" "${resolved_path}")
  if(IS_ABSOLUTE "${relative_path}" OR relative_path MATCHES "^\\.\\.(/|$)")
    _producer_fail("${description} resolves outside the prefix: ${path}")
  endif()
endfunction()

function(_producer_copy_path source_path destination_path)
  if(NOT EXISTS "${source_path}" AND NOT IS_SYMLINK "${source_path}")
    _producer_fail("input is missing: ${source_path}")
  endif()
  get_filename_component(destination_parent "${destination_path}" DIRECTORY)
  file(MAKE_DIRECTORY "${destination_parent}")
  if(IS_DIRECTORY "${source_path}")
    file(MAKE_DIRECTORY "${destination_path}")
    file(COPY "${source_path}/" DESTINATION "${destination_path}")
  elseif(IS_SYMLINK "${source_path}")
    _producer_fail("non-shared input must not be a symlink: ${source_path}")
  else()
    configure_file("${source_path}" "${destination_path}" COPYONLY)
  endif()
endfunction()

function(_producer_resolve_shared_payload link_path allowed_root description output_variable)
  set(current_link "${link_path}")
  set(chain_depth 0)
  while(IS_SYMLINK "${current_link}")
    math(EXPR chain_depth "${chain_depth} + 1")
    if(chain_depth GREATER 32)
      _producer_fail("${description} symlink chain is cyclic or too deep: ${link_path}")
    endif()
    file(READ_SYMLINK "${current_link}" link_target)
    if(IS_ABSOLUTE "${link_target}")
      _producer_fail("${description} symlink target must be relative: ${current_link} -> ${link_target}")
    endif()
    get_filename_component(current_link "${current_link}/../${link_target}" ABSOLUTE)
    _producer_require_inside("${current_link}" "${allowed_root}" "${description} symlink")
  endwhile()
  if(NOT EXISTS "${current_link}" OR IS_DIRECTORY "${current_link}")
    _producer_fail("${description} symlink chain is dangling: ${link_path}")
  endif()
  file(SIZE "${current_link}" payload_size)
  if(payload_size EQUAL 0)
    _producer_fail("${description} payload is empty: ${current_link}")
  endif()
  set(${output_variable} "${current_link}" PARENT_SCOPE)
endfunction()

function(_producer_require_soname payload_path expected_soname description)
  execute_process(
    COMMAND "${PRODUCER_READELF_EXECUTABLE}" -d "${payload_path}"
    RESULT_VARIABLE readelf_status
    OUTPUT_VARIABLE dynamic_section
    ERROR_VARIABLE readelf_error)
  if(NOT readelf_status EQUAL 0 OR NOT dynamic_section MATCHES "SONAME.*\\[${expected_soname}\\]")
    _producer_fail(
      "${description} SONAME is not ${expected_soname}: ${payload_path}\n"
      "${dynamic_section}${readelf_error}")
  endif()
endfunction()

function(_producer_copy_shared_chain source_link destination_link expected_soname)
  if(NOT IS_SYMLINK "${source_link}")
    _producer_fail("shared-library link is not a symlink: ${source_link}")
  endif()
  get_filename_component(source_directory "${source_link}" DIRECTORY)
  _producer_resolve_shared_payload(
    "${source_link}" "${source_directory}" "shared-library input" source_payload)
  get_filename_component(destination_directory "${destination_link}" DIRECTORY)
  file(MAKE_DIRECTORY "${destination_directory}")
  get_filename_component(payload_name "${source_payload}" NAME)
  set(destination_soname "${destination_directory}/${expected_soname}")
  set(destination_payload "${destination_directory}/${payload_name}")
  if(destination_link STREQUAL destination_soname OR destination_soname STREQUAL destination_payload)
    _producer_fail("shared-library chain requires distinct link, SONAME, and payload names")
  endif()
  file(CREATE_LINK "${expected_soname}" "${destination_link}" SYMBOLIC)
  file(CREATE_LINK "${payload_name}" "${destination_soname}" SYMBOLIC)
  configure_file("${source_payload}" "${destination_payload}" COPYONLY)
  _producer_require_soname("${destination_payload}" "${expected_soname}" "shared-library payload")
endfunction()

function(_producer_json_nullable value output_variable)
  if(value STREQUAL "")
    set(rendered "null")
  else()
    _cubrid_thirdparty_quote_json("${value}" rendered)
  endif()
  set(${output_variable} "${rendered}" PARENT_SCOPE)
endfunction()

function(_producer_validate_tree prefix_root)
  file(GLOB top_level_entries RELATIVE "${prefix_root}" "${prefix_root}/*")
  list(SORT top_level_entries)
  if(NOT "${top_level_entries}" STREQUAL "include;lib;licenses;share")
    _producer_fail("prefix top-level layout is invalid: ${top_level_entries}")
  endif()

  set(prefix_manifest "${prefix_root}/share/cubrid-thirdparty/manifest.json")
  if(NOT EXISTS "${prefix_manifest}" OR IS_DIRECTORY "${prefix_manifest}")
    _producer_fail("prefix manifest is missing: ${prefix_manifest}")
  endif()
  file(SHA256 "${prefix_manifest}" prefix_fingerprint)
  if(NOT prefix_fingerprint STREQUAL CUBRID_3RDPARTY_SPEC_FINGERPRINT)
    _producer_fail(
      "prefix manifest fingerprint mismatch: expected ${CUBRID_3RDPARTY_SPEC_FINGERPRINT}, got ${prefix_fingerprint}")
  endif()

  foreach(dependency IN LISTS CUBRID_3RDPARTY_DEPENDENCIES)
    string(TOUPPER "${dependency}" dependency_variable_name)
    set(header_variable "CUBRID_3RDPARTY_${dependency_variable_name}_OUTPUT_HEADERS")
    foreach(header_path IN LISTS ${header_variable})
      set(full_header_path "${prefix_root}/${header_path}")
      if(NOT EXISTS "${full_header_path}" AND NOT IS_SYMLINK "${full_header_path}")
        _producer_fail("manifest header is missing: ${header_path}")
      endif()
      _producer_require_inside("${full_header_path}" "${prefix_root}" "manifest header ${header_path}")
      if(IS_DIRECTORY "${full_header_path}")
        file(GLOB_RECURSE header_files LIST_DIRECTORIES FALSE "${full_header_path}/*")
        if("${header_files}" STREQUAL "")
          _producer_fail("manifest header directory is empty: ${header_path}")
        endif()
      else()
        file(SIZE "${full_header_path}" header_size)
        if(header_size EQUAL 0)
          _producer_fail("manifest header is empty: ${header_path}")
        endif()
      endif()
    endforeach()

    set(library_variable "CUBRID_3RDPARTY_${dependency_variable_name}_OUTPUT_LIBRARIES")
    set(linkage_variable "CUBRID_3RDPARTY_${dependency_variable_name}_OUTPUT_LINKAGES")
    set(soname_variable "CUBRID_3RDPARTY_${dependency_variable_name}_OUTPUT_SONAMES")
    list(LENGTH ${library_variable} library_count)
    if(library_count GREATER 0)
      math(EXPR last_library "${library_count} - 1")
      foreach(library_index RANGE 0 ${last_library})
        list(GET ${library_variable} ${library_index} library_path)
        list(GET ${linkage_variable} ${library_index} linkage)
        list(GET ${soname_variable} ${library_index} soname)
        set(full_library_path "${prefix_root}/${library_path}")
        if(linkage STREQUAL "static")
          if(NOT EXISTS "${full_library_path}" OR IS_DIRECTORY "${full_library_path}"
              OR IS_SYMLINK "${full_library_path}")
            _producer_fail("static library is not a regular file: ${library_path}")
          endif()
          file(SIZE "${full_library_path}" library_size)
          if(library_size EQUAL 0)
            _producer_fail("static library is empty: ${library_path}")
          endif()
        else()
          if(NOT IS_SYMLINK "${full_library_path}")
            _producer_fail("shared-library link is not a symlink: ${library_path}")
          endif()
          _producer_resolve_shared_payload(
            "${full_library_path}" "${prefix_root}/lib" "shared-library ${library_path}" library_payload)
          _producer_require_soname("${library_payload}" "${soname}" "shared-library ${library_path}")
        endif()
        _producer_require_inside("${full_library_path}" "${prefix_root}" "manifest library ${library_path}")
      endforeach()
    endif()

    set(license_destination_variable "CUBRID_3RDPARTY_${dependency_variable_name}_LICENSE_DESTINATIONS")
    set(license_sha256_variable "CUBRID_3RDPARTY_${dependency_variable_name}_LICENSE_SHA256S")
    list(LENGTH ${license_destination_variable} license_count)
    math(EXPR last_license "${license_count} - 1")
    foreach(license_index RANGE 0 ${last_license})
      list(GET ${license_destination_variable} ${license_index} license_destination)
      list(GET ${license_sha256_variable} ${license_index} expected_license_sha256)
      set(full_license_path "${prefix_root}/${license_destination}")
      if(NOT EXISTS "${full_license_path}" OR IS_DIRECTORY "${full_license_path}" OR IS_SYMLINK "${full_license_path}")
        _producer_fail("license is not a regular file: ${license_destination}")
      endif()
      _producer_require_inside("${full_license_path}" "${prefix_root}" "license ${license_destination}")
      file(SHA256 "${full_license_path}" actual_license_sha256)
      if(NOT actual_license_sha256 STREQUAL expected_license_sha256)
        _producer_fail("license digest mismatch: ${license_destination}")
      endif()
    endforeach()
  endforeach()
endfunction()

set(stage_root "${PRODUCER_OUTPUT}.stage")
set(backup_root "${PRODUCER_OUTPUT}.previous")

execute_process(
  COMMAND git -C "${PRODUCER_REPOSITORY_ROOT}" rev-parse HEAD
  RESULT_VARIABLE git_status
  OUTPUT_VARIABLE producer_revision
  ERROR_VARIABLE git_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
string(LENGTH "${producer_revision}" producer_revision_length)
if(NOT git_status EQUAL 0 OR NOT producer_revision_length EQUAL 40
    OR NOT producer_revision MATCHES "^[0-9a-f]+$")
  _producer_fail("cannot determine full producer commit: ${git_error}")
endif()
set(git_status_pathspecs .)
foreach(producer_owned_path IN ITEMS "${PRODUCER_OUTPUT}" "${stage_root}" "${backup_root}")
  file(RELATIVE_PATH producer_owned_relative "${PRODUCER_REPOSITORY_ROOT}" "${producer_owned_path}")
  if(NOT IS_ABSOLUTE "${producer_owned_relative}"
      AND NOT producer_owned_relative MATCHES "^\\.\\.(/|$)")
    list(APPEND git_status_pathspecs
      ":(exclude,top)${producer_owned_relative}"
      ":(exclude,top)${producer_owned_relative}/**")
  endif()
endforeach()
execute_process(
  COMMAND git -C "${PRODUCER_REPOSITORY_ROOT}" status --porcelain=v1 --untracked-files=all
    -- ${git_status_pathspecs}
  RESULT_VARIABLE git_status
  OUTPUT_VARIABLE dirty_output
  ERROR_VARIABLE git_error)
if(NOT git_status EQUAL 0)
  _producer_fail("cannot determine producer dirty state: ${git_error}")
endif()
if(dirty_output STREQUAL "")
  set(producer_dirty false)
else()
  set(producer_dirty true)
endif()

file(REMOVE_RECURSE "${stage_root}" "${backup_root}")
file(MAKE_DIRECTORY
  "${stage_root}/include"
  "${stage_root}/lib"
  "${stage_root}/licenses"
  "${stage_root}/share/cubrid-thirdparty")

string(JSON artifact_count LENGTH "${PRODUCER_ARTIFACTS_JSON}")
if(artifact_count GREATER 0)
  math(EXPR last_artifact "${artifact_count} - 1")
  foreach(artifact_index RANGE 0 ${last_artifact})
    foreach(artifact_field IN ITEMS kind source destination linkage soname)
      string(JSON ${artifact_field} GET "${PRODUCER_ARTIFACTS_JSON}"
        ${artifact_index} "${artifact_field}")
    endforeach()
    if(linkage STREQUAL "shared")
      _producer_copy_shared_chain("${source}" "${stage_root}/${destination}" "${soname}")
    else()
      _producer_copy_path("${source}" "${stage_root}/${destination}")
    endif()
  endforeach()
endif()

foreach(dependency IN LISTS CUBRID_3RDPARTY_DEPENDENCIES)
  string(TOUPPER "${dependency}" dependency_variable_name)
  set(license_source_variable "CUBRID_3RDPARTY_${dependency_variable_name}_LICENSE_SOURCES")
  set(license_destination_variable "CUBRID_3RDPARTY_${dependency_variable_name}_LICENSE_DESTINATIONS")
  list(LENGTH ${license_source_variable} license_count)
  math(EXPR last_license "${license_count} - 1")
  foreach(license_index RANGE 0 ${last_license})
    list(GET ${license_source_variable} ${license_index} license_source)
    list(GET ${license_destination_variable} ${license_index} license_destination)
    _producer_copy_path(
      "${PRODUCER_REPOSITORY_ROOT}/${license_source}"
      "${stage_root}/${license_destination}")
  endforeach()
endforeach()

configure_file(
  "${PRODUCER_MANIFEST}"
  "${stage_root}/share/cubrid-thirdparty/manifest.json"
  COPYONLY)
_producer_validate_tree("${stage_root}")

file(GLOB_RECURSE inventory_paths LIST_DIRECTORIES FALSE RELATIVE "${stage_root}" "${stage_root}/*")
list(SORT inventory_paths)
set(regular_inventory "")
set(symlink_inventory "")
foreach(inventory_path IN LISTS inventory_paths)
  set(full_inventory_path "${stage_root}/${inventory_path}")
  _cubrid_thirdparty_quote_json("${inventory_path}" quoted_inventory_path)
  if(IS_SYMLINK "${full_inventory_path}")
    file(READ_SYMLINK "${full_inventory_path}" symlink_target)
    _cubrid_thirdparty_quote_json("${symlink_target}" quoted_symlink_target)
    list(APPEND symlink_inventory
      "    {\"path\": ${quoted_inventory_path}, \"target\": ${quoted_symlink_target}}")
  else()
    file(SHA256 "${full_inventory_path}" artifact_sha256)
    list(APPEND regular_inventory
      "    {\"path\": ${quoted_inventory_path}, \"sha256\": \"${artifact_sha256}\"}")
  endif()
endforeach()
string(JOIN ",\n" regular_inventory_json ${regular_inventory})
string(JOIN ",\n" symlink_inventory_json ${symlink_inventory})

string(TIMESTAMP production_timestamp "%Y-%m-%dT%H:%M:%SZ" UTC)
foreach(provenance_value IN ITEMS
    PRODUCER_BASE_IMAGE_REFERENCE PRODUCER_BASE_IMAGE_DIGEST
    PRODUCER_CUBRIDCI_IMAGE_REFERENCE PRODUCER_CUBRIDCI_IMAGE_DIGEST)
  _producer_json_nullable("${${provenance_value}}" "${provenance_value}_JSON")
endforeach()
foreach(provenance_value IN ITEMS
    PRODUCER_CMAKE_VERSION PRODUCER_GENERATOR PRODUCER_COMPILER_ID PRODUCER_COMPILER_PATH
    PRODUCER_COMPILER_VERSION PRODUCER_COMPILER_TARGET PRODUCER_OBSERVED_OS
    PRODUCER_OBSERVED_ARCHITECTURE production_timestamp)
  _cubrid_thirdparty_quote_json("${${provenance_value}}" "${provenance_value}_JSON")
endforeach()

set(provenance_json "{\n")
string(APPEND provenance_json "  \"artifacts\": {\n")
string(APPEND provenance_json "    \"regular_files\": [\n${regular_inventory_json}\n    ],\n")
string(APPEND provenance_json "    \"symlinks\": [\n${symlink_inventory_json}\n    ]\n  },\n")
string(APPEND provenance_json "  \"build\": {\n")
string(APPEND provenance_json
  "    \"base_image\": {\"digest\": ${PRODUCER_BASE_IMAGE_DIGEST_JSON}, "
  "\"reference\": ${PRODUCER_BASE_IMAGE_REFERENCE_JSON}},\n")
string(APPEND provenance_json
  "    \"cmake\": {\"generator\": ${PRODUCER_GENERATOR_JSON}, "
  "\"version\": ${PRODUCER_CMAKE_VERSION_JSON}},\n")
string(APPEND provenance_json
  "    \"compiler\": {\"id\": ${PRODUCER_COMPILER_ID_JSON}, "
  "\"path\": ${PRODUCER_COMPILER_PATH_JSON}, "
  "\"target\": ${PRODUCER_COMPILER_TARGET_JSON}, "
  "\"version\": ${PRODUCER_COMPILER_VERSION_JSON}},\n")
string(APPEND provenance_json
  "    \"cubridci_image\": {\"digest\": ${PRODUCER_CUBRIDCI_IMAGE_DIGEST_JSON}, "
  "\"reference\": ${PRODUCER_CUBRIDCI_IMAGE_REFERENCE_JSON}},\n")
string(APPEND provenance_json "    \"timestamp\": ${production_timestamp_JSON}\n  },\n")
string(APPEND provenance_json "  \"platform\": {\n")
string(APPEND provenance_json
  "    \"declared\": {\"architecture\": \"${CUBRID_3RDPARTY_PLATFORM_ARCHITECTURE}\", "
  "\"os\": \"${CUBRID_3RDPARTY_PLATFORM_OS}\", "
  "\"variant\": \"${CUBRID_3RDPARTY_PLATFORM_VARIANT}\"},\n")
string(APPEND provenance_json
  "    \"observed\": {\"architecture\": ${PRODUCER_OBSERVED_ARCHITECTURE_JSON}, "
  "\"os\": ${PRODUCER_OBSERVED_OS_JSON}}\n  },\n")
string(APPEND provenance_json
  "  \"producer\": {\"commit\": \"${producer_revision}\", \"dirty\": ${producer_dirty}},\n")
string(APPEND provenance_json "  \"schema\": \"cubrid-thirdparty-provenance-v1\",\n")
string(APPEND provenance_json "  \"spec_fingerprint\": \"${CUBRID_3RDPARTY_SPEC_FINGERPRINT}\"\n}\n")
string(JSON provenance_type ERROR_VARIABLE provenance_error TYPE "${provenance_json}")
if(provenance_error OR NOT provenance_type STREQUAL "OBJECT")
  _producer_fail("generated provenance is not valid JSON: ${provenance_error}")
endif()
file(WRITE "${stage_root}/share/cubrid-thirdparty/provenance.json" "${provenance_json}")

foreach(forbidden_path IN ITEMS
    "${PRODUCER_SOURCE_ROOT}" "${PRODUCER_BINARY_ROOT}" "${stage_root}" "${PRODUCER_OUTPUT}")
  execute_process(
    COMMAND grep -r -a -F -l -- "${forbidden_path}" "${stage_root}"
    RESULT_VARIABLE grep_status
    OUTPUT_VARIABLE matching_files
    ERROR_VARIABLE grep_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(grep_status EQUAL 0)
    _producer_fail("prefix contains producer path '${forbidden_path}' in ${matching_files}")
  elseif(NOT grep_status EQUAL 1)
    _producer_fail("failed to scan prefix for producer paths: ${grep_error}")
  endif()
endforeach()

if(EXISTS "${PRODUCER_OUTPUT}" OR IS_SYMLINK "${PRODUCER_OUTPUT}")
  file(RENAME "${PRODUCER_OUTPUT}" "${backup_root}" RESULT backup_status)
  if(NOT backup_status STREQUAL "0")
    _producer_fail("cannot preserve previous prefix before publication: ${backup_status}")
  endif()
endif()
file(RENAME "${stage_root}" "${PRODUCER_OUTPUT}" RESULT publish_status)
if(NOT publish_status STREQUAL "0")
  if(EXISTS "${backup_root}" OR IS_SYMLINK "${backup_root}")
    file(RENAME "${backup_root}" "${PRODUCER_OUTPUT}")
  endif()
  _producer_fail("cannot atomically publish prefix: ${publish_status}")
endif()
file(REMOVE_RECURSE "${backup_root}")
message(STATUS "Published CUBRID third-party prefix: ${PRODUCER_OUTPUT}")
