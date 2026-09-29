#
#  Copyright 2024 CUBRID Corporation
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

project (test_master_util)

set (TEST_MASTER_UTIL_SRC
  ${CMAKE_SOURCE_DIR}/src/executables/master_util.c
  test_master_util.cpp
  )

SET_SOURCE_FILES_PROPERTIES (
  ${TEST_MASTER_UTIL_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable (test_master_util
  ${TEST_MASTER_UTIL_SRC}
  )

target_compile_definitions (test_master_util PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories (test_master_util PRIVATE
  ${TEST_INCLUDES}
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src/base
  ${CMAKE_SOURCE_DIR}/src/connection
  ${CMAKE_SOURCE_DIR}/src/executables
  ${CMAKE_SOURCE_DIR}/src/thread
  ${CMAKE_SOURCE_DIR}/src/communication
  ${CMAKE_SOURCE_DIR}/src/compat
  ${CMAKE_SOURCE_DIR}/src/heaplayers
  )

if (UNIX)
  target_link_libraries (test_master_util PRIVATE
    cubrid
    )
elseif (WIN32)
  target_link_libraries (test_master_util PRIVATE
    cubrid-win-lib
    )
else ()
  message (SEND_ERROR "master_util unit testing is for unix/windows")
endif ()
