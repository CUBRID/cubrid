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

project(test_vector)

#
# test_vector : Catch2-based unit tests for or_put_vector/or_get_vector/or_packed_vector_length
#

set(TEST_VECTOR_SOURCES
  test_vector_pack_unpack.cpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_VECTOR_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_vector
  ${TEST_VECTOR_SOURCES}
  )

target_compile_definitions(test_vector PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_vector PRIVATE
  ${TEST_INCLUDES}
  ${EP_INCLUDES}
  )

target_link_libraries(test_vector LINK_PRIVATE
  test_common
  )

add_dependencies(test_vector
  ${CATCH2_TARGET} ${EP_TARGETS}
  )

if(UNIX)
  target_link_libraries(test_vector LINK_PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_vector LINK_PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Vector unit test is only for unix/windows")
endif ()
