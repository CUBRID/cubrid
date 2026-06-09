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

project(test_bestspace)

set(TEST_BESTSPACE_SOURCES
  test_bestspace.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_BESTSPACE_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_bestspace
  ${TEST_BESTSPACE_SOURCES}
  )

target_compile_definitions(test_bestspace PRIVATE
  SERVER_MODE
  UNIT_TEST_BESTSPACE
  ${COMMON_DEFS}
  )

target_include_directories(test_bestspace PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_bestspace PRIVATE
  test_common
  )

if(UNIX)
  target_link_libraries(test_bestspace PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_bestspace PRIVATE
    cubrid-win-lib
    )
else()
  message(SEND_ERROR "Bestspace unit test is only for unix/windows")
endif()

add_dependencies(test_bestspace
  ${CATCH2_TARGET}
  ${EP_TARGETS}
  )
