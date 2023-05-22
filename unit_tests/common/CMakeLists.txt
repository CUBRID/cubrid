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

# Unit test common lib.
#

set (TEST_COMMON_SOURCES
  test_output.cpp
  test_perf_compare.cpp
  test_string_collection.cpp
  )
set (TEST_COMMON_HEADERS
  test_debug.hpp
  test_output.hpp
  test_perf_compare.hpp
  test_string_collection.hpp
  test_timers.hpp
  )
SET_SOURCE_FILES_PROPERTIES(
    ${TEST_COMMON_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
  
list (APPEND ${TEST_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR})

add_library(test_common STATIC
  ${TEST_COMMON_SOURCES}
  ${TEST_COMMON_HEADERS}
  )
set_target_properties(test_common PROPERTIES LINKER_LANGUAGE CXX)

target_compile_definitions(test_common PRIVATE
  ${COMMON_DEFS}
  )
