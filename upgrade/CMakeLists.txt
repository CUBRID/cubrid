#
#  Copyright 2008 Search Solution Corporation
#  Copyright 2016 CUBRID Corporation
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

file(GLOB UPGRADE_SQL_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/v*_to_v*.sql"
  )

# CONFIGURE_DEPENDS rebuilds on add/remove only; track edits explicitly.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${UPGRADE_SQL_FILES})

list(SORT UPGRADE_SQL_FILES COMPARE NATURAL)

set(ENC_FILES)
set(UPGRADE_CHECKSUM_ENTRIES)
set(EXPECTED_FROM 0)

foreach(SQL ${UPGRADE_SQL_FILES})
  get_filename_component(NAME ${SQL} NAME_WE)

  if(NOT NAME MATCHES "^v([0-9]+)_to_v([0-9]+)$")
    message(FATAL_ERROR "Upgrade SQL filename '${NAME}.sql' does not match v<N>_to_v<N+1>.sql pattern")
  endif()

  if(NOT CMAKE_MATCH_1 EQUAL EXPECTED_FROM)
    message(FATAL_ERROR "Upgrade chain hole: expected v${EXPECTED_FROM}_to_v..., got ${NAME}.sql")
  endif()

  math(EXPR _EXPECTED_TO "${EXPECTED_FROM} + 1")
  if(NOT CMAKE_MATCH_2 EQUAL ${_EXPECTED_TO})
    message(FATAL_ERROR "Non-adjacent upgrade: ${NAME}.sql must be v${EXPECTED_FROM}_to_v${_EXPECTED_TO}")
  endif()

  set(ENC "${CMAKE_CURRENT_BINARY_DIR}/${NAME}.sql.enc")

  file(READ "${SQL}" HEX_ENCODED HEX)
  file(WRITE "${ENC}" "${HEX_ENCODED}")
  file(SHA256 "${SQL}" HASH)

  list(APPEND ENC_FILES "${ENC}")
  string(APPEND UPGRADE_CHECKSUM_ENTRIES "  \"${HASH}\",\n")

  set(EXPECTED_FROM ${_EXPECTED_TO})
endforeach()

list(LENGTH UPGRADE_SQL_FILES UPGRADE_SCRIPT_COUNT_VAL)

if(UPGRADE_SCRIPT_COUNT_VAL EQUAL 0)
  # GCC -Werror=array-bounds rejects indexing a zero-length array, and -Werror=nonnull
  # rejects a NULL placeholder reaching strcmp; use empty string as a benign placeholder.
  set(UPGRADE_CHECKSUM_ENTRIES "  \"\",\n")
endif()

configure_file(upgrade_checksums.h.cmake ${CMAKE_BINARY_DIR}/upgrade_checksums.h @ONLY)

if(ENC_FILES)
  install(FILES ${ENC_FILES}
    DESTINATION ${CUBRID_DATADIR}/upgrade
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
    )
endif()

# Debug-only: QA ad-hoc scripts under internal/ for per-PR catalog churn.
if(CMAKE_BUILD_TYPE MATCHES "Debug")
  file(GLOB INTERNAL_SQL_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/internal/*.sql"
    )
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INTERNAL_SQL_FILES})

  if(INTERNAL_SQL_FILES)
    install(FILES ${INTERNAL_SQL_FILES}
      DESTINATION ${CUBRID_DATADIR}/upgrade/internal
      PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
      )
  endif()
endif()
