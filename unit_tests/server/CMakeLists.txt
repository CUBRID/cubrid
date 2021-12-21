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

#
# request_response_handler
#
set (TEST_RRH_SOURCES
  test_request_response_handler_main.cpp
  )
set (TEST_RRH_HEADERS
  ${SERVER_DIR}/request_response_handler.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_RRH_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_request_response_handler
  ${TEST_RRH_SOURCES}
  ${TEST_RRH_HEADERS}
  )

target_compile_definitions(test_request_response_handler PRIVATE
  ${COMMON_DEFS}
  SERVER_MODE
  )
target_include_directories(test_request_response_handler PRIVATE
  ${TEST_INCLUDES}
  )
target_link_libraries(test_request_response_handler fake_thread_utils)
add_dependencies(test_request_response_handler
  ${CATCH2_TARGET}
  )
