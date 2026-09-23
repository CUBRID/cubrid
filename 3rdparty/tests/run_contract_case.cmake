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
    message(FATAL_ERROR "contract test driver requires ${required_variable}")
  endif()
endforeach()

set(case_root "${TEST_BINARY_ROOT}/case-${CASE}")
set(case_manifest "${case_root}/manifest.json")
set(case_build "${case_root}/build")
set(marker "${case_root}/external-project.marker")
set(sentinel "${case_root}/external-project.sentinel")
set(result "${case_root}/result.txt")
set(fixture_root "${case_root}/prefix-fixture")
set(fixture_build "${case_root}/fixture-build")

file(REMOVE_RECURSE "${case_root}")
file(MAKE_DIRECTORY "${case_root}")
file(READ "${MANIFEST}" manifest_contents)

set(expect_success TRUE)
set(expected_diagnostic "")
if(CASE STREQUAL "bom_manifest")
  string(ASCII 239 187 191 utf8_bom)
  string(PREPEND manifest_contents "${utf8_bom}")
  set(expect_success FALSE)
  set(expected_diagnostic "UTF-8 without BOM")
elseif(CASE STREQUAL "crlf_manifest")
  string(REPLACE "\n" "\r\n" manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "must use LF line endings")
elseif(CASE STREQUAL "missing_final_lf")
  string(LENGTH "${manifest_contents}" manifest_length)
  math(EXPR truncated_length "${manifest_length} - 1")
  string(SUBSTRING "${manifest_contents}" 0 ${truncated_length} manifest_contents)
  set(expect_success FALSE)
  set(expected_diagnostic "exactly one final LF")
elseif(CASE STREQUAL "extra_final_lf")
  string(APPEND manifest_contents "\n")
  set(expect_success FALSE)
  set(expected_diagnostic "exactly one final LF")
elseif(CASE STREQUAL "invalid_utf8")
  string(ASCII 255 invalid_utf8_byte)
  string(REPLACE "cubrid-thirdparty-manifest-v1"
    "cubrid-thirdparty-manifest-v1${invalid_utf8_byte}" manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "must contain valid UTF-8")
elseif(CASE STREQUAL "noncanonical_manifest")
  string(REGEX REPLACE "^\\{" " {" manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "third-party manifest is not canonical")
elseif(CASE STREQUAL "unsorted_nested_keys")
  string(CONCAT canonical_source
    "      \"source\": {\n"
    "        \"sha256\": \"ef7d1994f533c9e7343d6c19f31064fc8ebbcbcaa144be3812b4f43052a05f4c\",\n"
    "        \"url\": \"https://github.com/CUBRID/3rdparty/raw/develop/expat/expat-2.8.2.tar.gz\"\n"
    "      }")
  string(CONCAT unsorted_source
    "      \"source\": {\n"
    "        \"url\": \"https://github.com/CUBRID/3rdparty/raw/develop/expat/expat-2.8.2.tar.gz\",\n"
    "        \"sha256\": \"ef7d1994f533c9e7343d6c19f31064fc8ebbcbcaa144be3812b4f43052a05f4c\"\n"
    "      }")
  string(REPLACE "${canonical_source}" "${unsorted_source}" manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "third-party manifest is not canonical")
elseif(CASE STREQUAL "unknown_key")
  string(REPLACE "{\n  \"dependencies\"" "{\n  \"bogus\": \"value\",\n  \"dependencies\""
    manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "unknown or missing keys at manifest root")
elseif(CASE STREQUAL "missing_field")
  string(REPLACE
    "  \"recipe_revision\": 1,\n  \"schema\": \"cubrid-thirdparty-manifest-v1\"\n"
    "  \"recipe_revision\": 1\n" manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "unknown or missing keys at manifest root")
elseif(CASE STREQUAL "duplicate_dependency")
  string(REPLACE "\"name\": \"libedit\"" "\"name\": \"expat\"" manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "duplicate dependency name: expat")
elseif(CASE STREQUAL "dependency_order")
  string(REPLACE "\"name\": \"expat\"" "\"name\": \"temporary\"" manifest_contents "${manifest_contents}")
  string(REPLACE "\"name\": \"libedit\"" "\"name\": \"expat\"" manifest_contents "${manifest_contents}")
  string(REPLACE "\"name\": \"temporary\"" "\"name\": \"libedit\"" manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "dependencies must be in graph order")
elseif(CASE STREQUAL "unsupported_recipe")
  string(REGEX REPLACE "\"recipe\": \"autoconf\"" "\"recipe\": \"unsupported\""
    manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "unsupported recipe: unsupported")
elseif(CASE STREQUAL "unsupported_linkage")
  string(REGEX REPLACE "\"linkage\": \"static\"" "\"linkage\": \"dynamic\"" manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "unsupported linkage: dynamic")
elseif(CASE STREQUAL "unsorted_output")
  string(REPLACE
    "\"include/lz4.h\",\n          \"include/lz4frame.h\""
    "\"include/lz4frame.h\",\n          \"include/lz4.h\""
    manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "outputs.headers must be sorted")
elseif(CASE STREQUAL "malformed_sha256")
  string(REGEX REPLACE "\"sha256\": \"[0-9a-f]+\"" "\"sha256\": \"not-a-sha256\""
    manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "lowercase hexadecimal characters")
elseif(CASE STREQUAL "noninteger_recipe_revision")
  string(REPLACE "\"recipe_revision\": 1" "\"recipe_revision\": 1.5" manifest_contents "${manifest_contents}")
  set(expect_success FALSE)
  set(expected_diagnostic "recipe_revision must be an integer")
elseif(CASE STREQUAL "malformed_json")
  string(LENGTH "${manifest_contents}" manifest_length)
  math(EXPR truncated_length "${manifest_length} - 2")
  string(SUBSTRING "${manifest_contents}" 0 ${truncated_length} manifest_contents)
  set(expect_success FALSE)
  set(expected_diagnostic "is not valid JSON")
endif()

file(WRITE "${case_manifest}" "${manifest_contents}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${TEST_SOURCE_ROOT}/fixture"
    -B "${fixture_build}"
    -G "${TEST_GENERATOR}"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_INSTALL_PREFIX=${fixture_root}"
  RESULT_VARIABLE fixture_status
  OUTPUT_VARIABLE fixture_stdout
  ERROR_VARIABLE fixture_stderr
  )
if(NOT fixture_status EQUAL 0)
  message(FATAL_ERROR "fixture configure failed (${fixture_status}):\n${fixture_stdout}${fixture_stderr}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${fixture_build}" --target install
  RESULT_VARIABLE fixture_status
  OUTPUT_VARIABLE fixture_stdout
  ERROR_VARIABLE fixture_stderr
  )
if(NOT fixture_status EQUAL 0)
  message(FATAL_ERROR "fixture build failed (${fixture_status}):\n${fixture_stdout}${fixture_stderr}")
endif()
foreach(fixture_file IN ITEMS
    "include/fixture/header_only.hpp"
    "include/fixture/shared.h"
    "lib/libfixture.a"
    "lib/libfixture.so.2.0.0"
    "licenses/fixture.txt")
  if(NOT EXISTS "${fixture_root}/${fixture_file}")
    message(FATAL_ERROR "fixture is missing ${fixture_file}")
  endif()
endforeach()
find_program(readelf_executable NAMES readelf)
if(NOT readelf_executable)
  message(FATAL_ERROR "readelf is required to validate the fixture SONAME")
endif()
execute_process(
  COMMAND "${readelf_executable}" -d "${fixture_root}/lib/libfixture.so.2.0.0"
  RESULT_VARIABLE readelf_status
  OUTPUT_VARIABLE dynamic_section
  ERROR_VARIABLE readelf_stderr
  )
if(NOT readelf_status EQUAL 0 OR NOT dynamic_section MATCHES "SONAME.*\\[libfixture.so.2\\]")
  message(FATAL_ERROR "fixture payload does not declare SONAME libfixture.so.2:\n${dynamic_section}${readelf_stderr}")
endif()
foreach(symlink_spec IN ITEMS "libfixture.so=libfixture.so.2" "libfixture.so.2=libfixture.so.2.0.0")
  string(REPLACE "=" ";" symlink_fields "${symlink_spec}")
  list(GET symlink_fields 0 symlink_name)
  list(GET symlink_fields 1 expected_target)
  set(symlink_path "${fixture_root}/lib/${symlink_name}")
  if(NOT IS_SYMLINK "${symlink_path}")
    message(FATAL_ERROR "fixture is missing relative symlink ${symlink_name}")
  endif()
  file(READ_SYMLINK "${symlink_path}" actual_target)
  if(NOT actual_target STREQUAL expected_target)
    message(FATAL_ERROR
      "fixture symlink ${symlink_name} targets '${actual_target}', expected '${expected_target}'")
  endif()
endforeach()

set(configure_command
  "${CMAKE_COMMAND}" -E env
  --unset=CUBRID_3RDPARTY_MODE
  --unset=CUBRID_3RDPARTY_ROOT
  "${CMAKE_COMMAND}"
  -S "${TEST_SOURCE_ROOT}/contract-project"
  -B "${case_build}"
  "-DCONTRACT_MODULE=${CONTRACT_MODULE}"
  "-DMANIFEST=${case_manifest}"
  "-DMARKER=${marker}"
  "-DSENTINEL=${sentinel}"
  "-DRESULT=${result}"
  )
if(CASE STREQUAL "external_mode_uses_external")
  list(APPEND configure_command -DCUBRID_3RDPARTY_MODE=EXTERNAL)
endif()

execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE configure_status
  OUTPUT_VARIABLE configure_stdout
  ERROR_VARIABLE configure_stderr
  )
set(configure_output "${configure_stdout}${configure_stderr}")

if(expect_success)
  if(NOT configure_status EQUAL 0)
    message(FATAL_ERROR "expected configure success, got ${configure_status}:\n${configure_output}")
  endif()
  if(NOT EXISTS "${marker}")
    message(FATAL_ERROR "ExternalProject compatibility marker was not created")
  endif()
  if(EXISTS "${sentinel}")
    message(FATAL_ERROR "CI_PREBUILT sentinel must remain untouched on the ExternalProject route")
  endif()
  file(READ "${result}" result_contents)
  foreach(expected_line IN ITEMS
      "schema=cubrid-thirdparty-manifest-v1"
      "fingerprint=${EXPECTED_FINGERPRINT}"
      "dependencies=expat;libedit;lz4;openssl;unixodbc;rapidjson;re2;onetbb"
      "expat_headers=include/expat.h;include/expat_config.h;include/expat_external.h"
      "openssl_libraries=lib/libssl.a;lib/libcrypto.a"
      "route=EXTERNAL")
    if(NOT result_contents MATCHES "${expected_line}")
      message(FATAL_ERROR "result is missing '${expected_line}':\n${result_contents}")
    endif()
  endforeach()
  if(NOT result_contents MATCHES "unixodbc_soname=libodbc.so.2")
    message(FATAL_ERROR "result is missing unixODBC SONAME:\n${result_contents}")
  endif()
else()
  if(configure_status EQUAL 0)
    message(FATAL_ERROR "expected configure failure for ${CASE}, but it succeeded")
  endif()
  string(FIND "${configure_output}" "${expected_diagnostic}" diagnostic_position)
  if(diagnostic_position EQUAL -1)
    message(FATAL_ERROR
      "configure failure did not contain '${expected_diagnostic}':\n${configure_output}")
  endif()
endif()
