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

# Project to test memory allocation.
#
#

set (TEST_MEMORY_ALLOC_SOURCES
  test_db_private_alloc.cpp
  test_extensible_array.cpp
  test_main.cpp
  test_memory_alloc_helper.cpp
  test_private_unique_ptr.cpp
  )
set (TEST_MEMORY_ALLOC_HEADERS
  test_db_private_alloc.hpp
  test_extensible_array.hpp
  test_memory_alloc_helper.hpp
  test_private_unique_ptr.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
    ${TEST_MEMORY_ALLOC_SOURCES}
    PROPERTIES LANGUAGE CXX
  )

add_executable(test_memory_alloc
  ${TEST_MEMORY_ALLOC_SOURCES}
  ${TEST_MEMORY_ALLOC_HEADERS}
  )

target_compile_definitions(test_memory_alloc PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

message (STATUS "test includes:  ${TEST_INCLUDES}")
target_include_directories(test_memory_alloc PRIVATE
  ${TEST_INCLUDES}
  ${EP_INCLUDES}
  )

target_link_libraries(test_memory_alloc LINK_PRIVATE
  test_common
  )

if(UNIX)
  target_link_libraries(test_memory_alloc LINK_PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_memory_alloc LINK_PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Memory alloc unit test is only for unix/windows")
endif ()
