#
#  Copyright 2026 CUBRID Corporation
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
#

cmake_minimum_required(VERSION 3.21)

set(version_file "${BUILD_DIR}/build-version.txt")
execute_process(
  COMMAND sh "${SOURCE_DIR}/cmake/git-build-version.sh" "${GIT}" "${SOURCE_DIR}" "${version_file}"
  OUTPUT_VARIABLE git_version OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
if(DEFINED ENV{CUBRID_CPACK_GIT_VERSION} AND NOT "$ENV{CUBRID_CPACK_GIT_VERSION}" STREQUAL git_version)
  message(FATAL_ERROR "Git version changed during packaging. Build again before invoking CPack.")
endif()

string(REPLACE " " ";" version_fields "${git_version}")
list(GET version_fields 1 CUBRID_EXTRA_VERSION)
list(GET version_fields 2 short_revision)
set(BUILD_NUMBER "${BASE_VERSION}.${CUBRID_EXTRA_VERSION}")
set(CUBRID_VERSION "${BUILD_NUMBER}-${short_revision}")
configure_file("${SOURCE_DIR}/cmake/build_version.h.cmake" "${BUILD_DIR}/build_version.h" @ONLY)
file(CONFIGURE OUTPUT "${version_file}" CONTENT "@git_version@\n@CUBRID_VERSION@\n" @ONLY)
