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

project (request_client_server)

#
# tests for request [sync] client server and all helping classes
#
set (TEST_REQUEST_CS_SOURCES
  ${TEST_FAKE_DIR}/fake_system_parameter.cpp
  ${BASE_DIR}/mem_block.cpp
  ${BASE_DIR}/packer.cpp
  ${COMMUNICATION_DIR}/request_client_server.cpp
  comm_channel_mock.cpp
  test_main.cpp
  )
set (TEST_REQUEST_CS_HEADERS
  ${COMMUNICATION_DIR}/request_client_server.hpp
  ${COMMUNICATION_DIR}/response_broker.hpp
  comm_channel_mock.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_REQUEST_CS_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_request_cs
  ${TEST_REQUEST_CS_SOURCES}
  ${TEST_REQUEST_CS_HEADERS}
  )

target_compile_options(test_request_cs PRIVATE ${compile_warning_flags})
target_compile_definitions(test_request_cs PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories(test_request_cs PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_request_cs PRIVATE
  test_common
  )
if(WIN32)
  target_link_libraries(test_request_cs PRIVATE
  ws2_32
  )
endif(WIN32)

add_dependencies(test_request_cs
  ${CATCH2_TARGET}
  )
