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

project (test_radix_sort)

set (TEST_RADIX_SORT_SRC
  test_main.cpp
  test_radix_sort.cpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_RADIX_SORT_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_radix_sort
  ${TEST_RADIX_SORT_SRC}
  )

add_dependencies(test_radix_sort
  ${CATCH2_TARGET}
  )

target_compile_definitions(test_radix_sort PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_radix_sort PRIVATE
  ${TEST_INCLUDES}
  ${CMAKE_SOURCE_DIR}/src/base
  ${CMAKE_SOURCE_DIR}/src/compat
  ${CMAKE_SOURCE_DIR}/src/storage
  ${CMAKE_SOURCE_DIR}/src/thread
  )

if(UNIX)
  target_link_libraries(test_radix_sort PRIVATE
    cubrid
    )
else()
  message( SEND_ERROR "radix_sort unit testing is for unix" )
endif ()

add_test(NAME test_radix_sort COMMAND test_radix_sort)
