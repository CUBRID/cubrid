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
# server_request_responder
#
set (TEST_SRR_SOURCES
  test_server_request_responder_main.cpp
  )
set (TEST_SRR_HEADERS
  ${SERVER_DIR}/server_request_responder.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_SRR_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_server_request_responder
  ${TEST_SRR_SOURCES}
  ${TEST_SRR_HEADERS}
  )

target_compile_definitions(test_server_request_responder PRIVATE
  ${COMMON_DEFS}
  SERVER_MODE
  )
target_include_directories(test_server_request_responder PRIVATE
  ${TEST_INCLUDES}
  )
target_link_libraries(test_server_request_responder fake_thread_utils)
add_dependencies(test_server_request_responder
  ${CATCH2_TARGET}
  )
