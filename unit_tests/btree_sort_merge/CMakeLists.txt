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
#

project (test_btree_sort_merge)

set (TEST_BTREE_SORT_MERGE_SRC
  test_main.cpp
  test_btree_sort_merge.cpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_BTREE_SORT_MERGE_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_btree_sort_merge
  ${TEST_BTREE_SORT_MERGE_SRC}
  )

add_dependencies(test_btree_sort_merge
  ${CATCH2_TARGET}
  )

target_compile_definitions(test_btree_sort_merge PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_btree_sort_merge PRIVATE
  ${TEST_INCLUDES}
  ${CMAKE_SOURCE_DIR}/src/base
  ${CMAKE_SOURCE_DIR}/src/compat
  ${CMAKE_SOURCE_DIR}/src/storage
  ${CMAKE_SOURCE_DIR}/src/thread
  )

if(UNIX)
  target_link_libraries(test_btree_sort_merge PRIVATE
    cubrid
    )
else()
  message( SEND_ERROR "btree_sort_merge unit testing is for unix" )
endif ()

add_test(NAME test_btree_sort_merge COMMAND test_btree_sort_merge)
