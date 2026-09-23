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

find_program(readelf_executable NAMES readelf REQUIRED)

function(_require_soname library_path expected_soname description)
  execute_process(
    COMMAND "${readelf_executable}" -d "${library_path}"
    RESULT_VARIABLE readelf_status
    OUTPUT_VARIABLE dynamic_section
    ERROR_VARIABLE readelf_stderr)
  if(NOT readelf_status EQUAL 0 OR NOT dynamic_section MATCHES "SONAME.*\\[${expected_soname}\\]")
    message(FATAL_ERROR
      "${description} does not declare SONAME ${expected_soname}:\n"
      "${dynamic_section}${readelf_stderr}")
  endif()
endfunction()

function(_require_relative_symlink symlink_path expected_target description)
  if(NOT IS_SYMLINK "${symlink_path}")
    message(FATAL_ERROR "${description} is not a symlink: ${symlink_path}")
  endif()
  file(READ_SYMLINK "${symlink_path}" actual_target)
  if(NOT actual_target STREQUAL expected_target)
    message(FATAL_ERROR
      "${description} targets '${actual_target}', expected '${expected_target}'")
  endif()
endfunction()

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
    "  \"recipe_revision\": 2,\n  \"schema\": \"cubrid-thirdparty-manifest-v1\"\n"
    "  \"recipe_revision\": 2\n" manifest_contents "${manifest_contents}")
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
  string(REPLACE "\"recipe_revision\": 2" "\"recipe_revision\": 2.5" manifest_contents "${manifest_contents}")
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
    "lib/libodbc.so.2.0.0"
    "licenses/fixture.txt")
  if(NOT EXISTS "${fixture_root}/${fixture_file}")
    message(FATAL_ERROR "fixture is missing ${fixture_file}")
  endif()
endforeach()
_require_soname("${fixture_root}/lib/libodbc.so.2.0.0" "libodbc.so.2" "fixture payload")
_require_relative_symlink(
  "${fixture_root}/lib/libodbc.so" "libodbc.so.2" "fixture linker-name symlink")
_require_relative_symlink(
  "${fixture_root}/lib/libodbc.so.2" "libodbc.so.2.0.0" "fixture SONAME symlink")

if(CASE MATCHES "^producer_")
  set(producer_build "${case_root}/producer-build")
  set(producer_output "${case_root}/producer-prefix")
  set(expect_producer_configure_success TRUE)
  set(expect_producer_build_success TRUE)
  set(expected_producer_diagnostic "")
  set(producer_arguments
    "-DCONTRACT_MODULE=${CONTRACT_MODULE}"
    "-DFIXTURE_ROOT=${fixture_root}"
    "-DMANIFEST=${MANIFEST}")
  if(CASE STREQUAL "producer_output_source_descendant")
    set(clean_repository "${case_root}/clean-repository")
    file(MAKE_DIRECTORY "${clean_repository}/3rdparty")
    configure_file("${MANIFEST}" "${clean_repository}/3rdparty/manifest.json" COPYONLY)
    get_filename_component(manifest_directory "${MANIFEST}" DIRECTORY)
    file(COPY "${manifest_directory}/license" DESTINATION "${clean_repository}/3rdparty")
    foreach(git_command IN ITEMS init add commit)
      if(git_command STREQUAL "init")
        set(git_arguments init -q)
      elseif(git_command STREQUAL "add")
        set(git_arguments add 3rdparty)
      else()
        set(git_arguments -c user.name=Fixture -c user.email=fixture@example.invalid
          commit -q -m "fixture repository")
      endif()
      execute_process(
        COMMAND git -C "${clean_repository}" ${git_arguments}
        RESULT_VARIABLE git_status
        OUTPUT_VARIABLE git_stdout
        ERROR_VARIABLE git_stderr)
      if(NOT git_status EQUAL 0)
        message(FATAL_ERROR "fixture git ${git_command} failed:\n${git_stdout}${git_stderr}")
      endif()
    endforeach()
    set(producer_output "${clean_repository}/generated/prefix")
    set(producer_arguments
      "-DCONTRACT_MODULE=${CONTRACT_MODULE}"
      "-DFIXTURE_ROOT=${fixture_root}"
      "-DMANIFEST=${clean_repository}/3rdparty/manifest.json"
      "-DPRODUCER_SOURCE_ROOT=${clean_repository}"
      "-DCUBRID_3RDPARTY_PREFIX_OUTPUT=${producer_output}")
  endif()
  if(CASE STREQUAL "producer_missing_input_atomic")
    file(MAKE_DIRECTORY "${producer_output}")
    file(WRITE "${producer_output}/existing-prefix.marker" "preserve me\n")
    list(APPEND producer_arguments
      "-DCUBRID_3RDPARTY_PREFIX_OUTPUT=${producer_output}"
      -DPRODUCER_MISSING_INPUT=ON)
    set(expect_producer_build_success FALSE)
    set(expected_producer_diagnostic "input is missing")
  elseif(CASE STREQUAL "producer_output_unset")
    set(expect_producer_configure_success FALSE)
    set(expected_producer_diagnostic "CUBRID_3RDPARTY_PREFIX_OUTPUT is required")
  elseif(CASE STREQUAL "producer_output_relative")
    list(APPEND producer_arguments -DCUBRID_3RDPARTY_PREFIX_OUTPUT=relative-prefix)
    set(expect_producer_configure_success FALSE)
    set(expected_producer_diagnostic "CUBRID_3RDPARTY_PREFIX_OUTPUT must be absolute")
  elseif(CASE STREQUAL "producer_output_filesystem_root")
    list(APPEND producer_arguments -DCUBRID_3RDPARTY_PREFIX_OUTPUT=/)
    set(expect_producer_configure_success FALSE)
    set(expected_producer_diagnostic "filesystem root")
  elseif(CASE STREQUAL "producer_output_source_root")
    list(APPEND producer_arguments "-DCUBRID_3RDPARTY_PREFIX_OUTPUT=${TEST_SOURCE_ROOT}/producer-project")
    set(expect_producer_configure_success FALSE)
    set(expected_producer_diagnostic "project root or its ancestor")
  elseif(CASE STREQUAL "producer_output_binary_root")
    list(APPEND producer_arguments "-DCUBRID_3RDPARTY_PREFIX_OUTPUT=${producer_build}")
    set(expect_producer_configure_success FALSE)
    set(expected_producer_diagnostic "project root or its ancestor")
  elseif(CASE STREQUAL "producer_output_source_ancestor")
    list(APPEND producer_arguments "-DCUBRID_3RDPARTY_PREFIX_OUTPUT=${TEST_SOURCE_ROOT}")
    set(expect_producer_configure_success FALSE)
    set(expected_producer_diagnostic "project root or its ancestor")
  elseif(CASE STREQUAL "producer_output_binary_ancestor")
    list(APPEND producer_arguments "-DCUBRID_3RDPARTY_PREFIX_OUTPUT=${case_root}")
    set(expect_producer_configure_success FALSE)
    set(expected_producer_diagnostic "project root or its ancestor")
  else()
    list(APPEND producer_arguments "-DCUBRID_3RDPARTY_PREFIX_OUTPUT=${producer_output}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -S "${TEST_SOURCE_ROOT}/producer-project"
      -B "${producer_build}"
      -G "${TEST_GENERATOR}"
      ${producer_arguments}
    RESULT_VARIABLE producer_status
    OUTPUT_VARIABLE producer_stdout
    ERROR_VARIABLE producer_stderr
    )
  set(producer_output_text "${producer_stdout}${producer_stderr}")
  if(expect_producer_configure_success AND NOT producer_status EQUAL 0)
    message(FATAL_ERROR "producer configure failed (${producer_status}):\n${producer_output_text}")
  elseif(NOT expect_producer_configure_success)
    if(producer_status EQUAL 0)
      message(FATAL_ERROR "producer configure unexpectedly succeeded for ${CASE}")
    endif()
    string(FIND "${producer_output_text}" "${expected_producer_diagnostic}" diagnostic_position)
    if(diagnostic_position EQUAL -1)
      message(FATAL_ERROR
        "producer configure failure did not contain '${expected_producer_diagnostic}':\n${producer_output_text}")
    endif()
    return()
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${producer_build}" --target cubrid_thirdparty_prefix
    RESULT_VARIABLE producer_status
    OUTPUT_VARIABLE producer_stdout
    ERROR_VARIABLE producer_stderr
    )
  set(producer_output_text "${producer_stdout}${producer_stderr}")
  if(expect_producer_build_success AND NOT producer_status EQUAL 0)
    message(FATAL_ERROR "producer build failed (${producer_status}):\n${producer_output_text}")
  elseif(NOT expect_producer_build_success)
    if(producer_status EQUAL 0)
      message(FATAL_ERROR "producer build unexpectedly succeeded for ${CASE}")
    endif()
    string(FIND "${producer_output_text}" "${expected_producer_diagnostic}" diagnostic_position)
    if(diagnostic_position EQUAL -1)
      message(FATAL_ERROR
        "producer build failure did not contain '${expected_producer_diagnostic}':\n${producer_output_text}")
    endif()
    if(NOT EXISTS "${producer_output}/existing-prefix.marker")
      message(FATAL_ERROR "failed producer replaced or removed the previous prefix")
    endif()
    return()
  endif()

  foreach(dependency IN ITEMS expat libedit lz4 openssl unixodbc rapidjson re2 onetbb)
    if(NOT EXISTS "${producer_build}/${dependency}.built")
      message(FATAL_ERROR "producer target did not wait for ${dependency}")
    endif()
  endforeach()
  file(GLOB prefix_top_level RELATIVE "${producer_output}" "${producer_output}/*")
  list(SORT prefix_top_level)
  if(NOT "${prefix_top_level}" STREQUAL "include;lib;licenses;share")
    message(FATAL_ERROR "producer top-level layout is invalid: ${prefix_top_level}")
  endif()
  foreach(required_output IN ITEMS
      include/expat.h include/expat_config.h include/expat_external.h
      include/editline include/histedit.h
      include/lz4.h include/lz4frame.h include/lz4hc.h
      include/openssl include/sql.h include/sqlext.h include/sqltypes.h
      include/rapidjson include/re2 include/oneapi include/tbb
      lib/libexpat.a lib/libedit.a lib/liblz4.a lib/libssl.a lib/libcrypto.a
      lib/libodbc.so lib/libre2.a lib/libtbb.a
      share/cubrid-thirdparty/manifest.json share/cubrid-thirdparty/provenance.json)
    if(NOT EXISTS "${producer_output}/${required_output}" AND NOT IS_SYMLINK "${producer_output}/${required_output}")
      message(FATAL_ERROR "producer prefix is missing ${required_output}")
    endif()
  endforeach()
  foreach(excluded_library IN ITEMS libodbcinst.so libodbccr.so)
    if(EXISTS "${producer_output}/lib/${excluded_library}" OR IS_SYMLINK "${producer_output}/lib/${excluded_library}")
      message(FATAL_ERROR "producer prefix contains unused ${excluded_library}")
    endif()
  endforeach()
  file(SHA256 "${MANIFEST}" source_manifest_sha256)
  file(SHA256 "${producer_output}/share/cubrid-thirdparty/manifest.json" prefix_manifest_sha256)
  if(NOT source_manifest_sha256 STREQUAL prefix_manifest_sha256)
    message(FATAL_ERROR "producer did not preserve exact manifest bytes")
  endif()
  file(READ "${producer_output}/share/cubrid-thirdparty/provenance.json" provenance_json)
  foreach(provenance_path IN ITEMS schema spec_fingerprint "producer commit" "producer dirty"
      "build timestamp" "build compiler id" "build compiler path" "build compiler version"
      "build compiler target" "platform declared architecture" "platform declared os"
      "platform declared variant" "platform observed architecture" "platform observed os"
      "artifacts regular_files" "artifacts symlinks")
    string(REPLACE " " ";" provenance_path_list "${provenance_path}")
    string(JSON ignored ERROR_VARIABLE provenance_error GET "${provenance_json}" ${provenance_path_list})
    if(provenance_error)
      message(FATAL_ERROR "provenance is missing ${provenance_path}: ${provenance_error}")
    endif()
  endforeach()
  if(CASE STREQUAL "producer_output_source_descendant")
    string(JSON producer_dirty GET "${provenance_json}" producer dirty)
    if(producer_dirty)
      message(FATAL_ERROR "producer-owned source-descendant output made clean provenance dirty")
    endif()
    execute_process(
      COMMAND "${CMAKE_COMMAND}" --build "${producer_build}" --target cubrid_thirdparty_prefix
      RESULT_VARIABLE producer_status
      OUTPUT_VARIABLE producer_stdout
      ERROR_VARIABLE producer_stderr)
    if(NOT producer_status EQUAL 0)
      message(FATAL_ERROR
        "repeated source-descendant producer build failed (${producer_status}):\n"
        "${producer_stdout}${producer_stderr}")
    endif()
    file(READ "${producer_output}/share/cubrid-thirdparty/provenance.json" repeated_provenance_json)
    string(JSON repeated_producer_dirty GET "${repeated_provenance_json}" producer dirty)
    if(repeated_producer_dirty)
      message(FATAL_ERROR "previous producer-owned source-descendant output made clean provenance dirty")
    endif()
  endif()
  foreach(optional_path IN ITEMS
      "build;base_image;reference" "build;base_image;digest"
      "build;cubridci_image;reference" "build;cubridci_image;digest")
    string(JSON optional_type TYPE "${provenance_json}" ${optional_path})
    if(NOT optional_type STREQUAL "NULL")
      message(FATAL_ERROR "unavailable provenance value ${optional_path} is not null")
    endif()
  endforeach()
  _require_relative_symlink(
    "${producer_output}/lib/libodbc.so" "libodbc.so.2" "producer linker-name symlink")
  _require_relative_symlink(
    "${producer_output}/lib/libodbc.so.2" "libodbc.so.2.0.0" "producer SONAME symlink")

  set(relocated_output "${case_root}/relocated-prefix")
  file(RENAME "${producer_output}" "${relocated_output}")
  _require_soname(
    "${relocated_output}/lib/libodbc.so.2" "libodbc.so.2" "relocated shared library")
  foreach(forbidden_path IN ITEMS
      "${TEST_SOURCE_ROOT}" "${producer_build}" "${producer_output}.stage" "${producer_output}")
    execute_process(
      COMMAND grep -r -a -F -l -- "${forbidden_path}" "${relocated_output}"
      RESULT_VARIABLE grep_status
      OUTPUT_VARIABLE matching_files
      ERROR_VARIABLE grep_error)
    if(grep_status EQUAL 0)
      message(FATAL_ERROR "relocated prefix contains producer path ${forbidden_path} in ${matching_files}")
    elseif(NOT grep_status EQUAL 1)
      message(FATAL_ERROR "cannot scan relocated prefix: ${grep_error}")
    endif()
  endforeach()
  return()
endif()

if(CASE MATCHES "^consumer_")
  set(producer_build "${case_root}/producer-build")
  set(producer_output "${case_root}/producer-prefix")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -S "${TEST_SOURCE_ROOT}/producer-project"
      -B "${producer_build}"
      -G "${TEST_GENERATOR}"
      "-DCONTRACT_MODULE=${CONTRACT_MODULE}"
      "-DFIXTURE_ROOT=${fixture_root}"
      "-DMANIFEST=${MANIFEST}"
      "-DCUBRID_3RDPARTY_PREFIX_OUTPUT=${producer_output}"
    RESULT_VARIABLE producer_status
    OUTPUT_VARIABLE producer_stdout
    ERROR_VARIABLE producer_stderr)
  if(NOT producer_status EQUAL 0)
    message(FATAL_ERROR
      "consumer fixture producer configure failed (${producer_status}):\n"
      "${producer_stdout}${producer_stderr}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${producer_build}" --target cubrid_thirdparty_prefix
    RESULT_VARIABLE producer_status
    OUTPUT_VARIABLE producer_stdout
    ERROR_VARIABLE producer_stderr)
  if(NOT producer_status EQUAL 0)
    message(FATAL_ERROR
      "consumer fixture producer build failed (${producer_status}):\n"
      "${producer_stdout}${producer_stderr}")
  endif()

  set(relocated_output "${case_root}/relocated-prefix")
  file(RENAME "${producer_output}" "${relocated_output}")
  set(consumer_environment_arguments
    --unset=CUBRID_3RDPARTY_MODE
    --unset=CUBRID_3RDPARTY_ROOT)
  set(consumer_cache_arguments "")
  if(CASE STREQUAL "consumer_environment_success")
    list(APPEND consumer_environment_arguments
      CUBRID_3RDPARTY_MODE=CI_PREBUILT
      "CUBRID_3RDPARTY_ROOT=${relocated_output}")
  elseif(CASE STREQUAL "consumer_cache_environment_equal")
    list(APPEND consumer_environment_arguments
      CUBRID_3RDPARTY_MODE=CI_PREBUILT
      "CUBRID_3RDPARTY_ROOT=${relocated_output}")
    list(APPEND consumer_cache_arguments
      -DCUBRID_3RDPARTY_MODE=CI_PREBUILT
      "-DCUBRID_3RDPARTY_ROOT=${relocated_output}")
  else()
    list(APPEND consumer_cache_arguments
      -DCUBRID_3RDPARTY_MODE=CI_PREBUILT
      "-DCUBRID_3RDPARTY_ROOT=${relocated_output}")
  endif()
  set(configure_command
    "${CMAKE_COMMAND}" -E env
    ${consumer_environment_arguments}
    "${CMAKE_COMMAND}"
    -S "${TEST_SOURCE_ROOT}/consumer-project"
    -B "${case_build}"
    -G "${TEST_GENERATOR}"
    "-DCUBRID_SOURCE_ROOT=${TEST_SOURCE_ROOT}/../.."
    "-DRESULT=${result}"
    "-DSENTINEL=${sentinel}"
    ${consumer_cache_arguments})
  execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_status
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr)
  if(NOT configure_status EQUAL 0)
    message(FATAL_ERROR
      "consumer configure failed (${configure_status}):\n${configure_stdout}${configure_stderr}")
  endif()
  if(EXISTS "${sentinel}")
    file(READ "${sentinel}" sentinel_contents)
    message(FATAL_ERROR "ExternalProject sentinel was touched: ${sentinel_contents}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${case_build}" --target thirdparty_consumer
    RESULT_VARIABLE consumer_status
    OUTPUT_VARIABLE consumer_stdout
    ERROR_VARIABLE consumer_stderr)
  if(NOT consumer_status EQUAL 0)
    message(FATAL_ERROR
      "consumer build failed (${consumer_status}):\n${consumer_stdout}${consumer_stderr}")
  endif()
  execute_process(
    COMMAND "${case_build}/bin/thirdparty_consumer"
    RESULT_VARIABLE consumer_status
    OUTPUT_VARIABLE consumer_stdout
    ERROR_VARIABLE consumer_stderr)
  if(NOT consumer_status EQUAL 0)
    message(FATAL_ERROR
      "consumer executable failed (${consumer_status}):\n${consumer_stdout}${consumer_stderr}")
  endif()
  file(READ "${result}" result_contents)
  string(CONCAT expected_ep_includes
    "${relocated_output}/include;${relocated_output}/include;${relocated_output}/include;"
    "${relocated_output}/include;${relocated_output}/include;${relocated_output}/include;"
    "${relocated_output}/include")
  string(CONCAT expected_ep_libs
    "${relocated_output}/lib/libexpat.a;${relocated_output}/lib/libedit.a;"
    "${relocated_output}/lib/liblz4.a;${relocated_output}/lib/libssl.a;"
    "${relocated_output}/lib/libcrypto.a;${relocated_output}/lib/libre2.a")
  string(CONCAT expected_tbb_includes
    "${relocated_output}/include/tbb;${relocated_output}/include/oneapi;"
    "${relocated_output}/include/oneapi/tbb;${relocated_output}/include/oneapi/tbb/detail")
  foreach(expected_line IN ITEMS
      "root=${relocated_output}"
      "ep_targets="
      "ep_includes=${expected_ep_includes}"
      "ep_libs=${expected_ep_libs}"
      "tbb_targets="
      "tbb_includes=${expected_tbb_includes}"
      "libexpat_target="
      "libedit_target="
      "lz4_target="
      "libopenssl_target="
      "libunixodbc_target="
      "rapidjson_target="
      "re2_target="
      "libtbb_target=")
    string(FIND "${result_contents}" "${expected_line}\n" expected_line_position)
    if(expected_line_position EQUAL -1)
      message(FATAL_ERROR "consumer interface is missing '${expected_line}':\n${result_contents}")
    endif()
  endforeach()
  foreach(interface_line IN ITEMS
      ep_includes ep_libs tbb_includes tbb_libs
      libexpat_includes libexpat_libs libedit_includes libedit_libs
      lz4_includes lz4_libs libopenssl_includes libopenssl_libs
      libunixodbc_includes libunixodbc_libs rapidjson_includes
      re2_includes re2_libs libtbb_includes libtbb_libs)
    if(NOT result_contents MATCHES "${interface_line}=${relocated_output}/")
      message(FATAL_ERROR
        "consumer interface ${interface_line} does not point into relocated prefix:\n${result_contents}")
    endif()
  endforeach()
  return()
endif()

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
