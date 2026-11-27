#
#
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
#

project (test_bestspace_map)

# bestspace_map.cpp does not depend on the server: it is compiled directly, without an engine library.
# bit.c supplies bit64_count_trailing_zeros. SERVER_MODE stays undefined (memory_cwrapper.h would need the
# memory monitor otherwise).
set (TEST_BESTSPACE_MAP_SOURCES
  ${STORAGE_DIR}/bestspace_map.cpp
  ${BASE_DIR}/bit.c
  test_bestspace_map.cpp
  test_bestspace_sector_table.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_BESTSPACE_MAP_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_bestspace_map
  ${TEST_BESTSPACE_MAP_SOURCES}
  )

target_compile_definitions(test_bestspace_map PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories(test_bestspace_map PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_bestspace_map LINK_PRIVATE
  ${CMAKE_THREAD_LIBS_INIT}
  )

add_dependencies(test_bestspace_map
  ${CATCH2_TARGET}
  ${EP_TARGETS}
  )
