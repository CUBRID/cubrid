#
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

set(TEST_SERVER_COMPILE_SOURCES
  test_main.cpp
  )

set_source_files_properties(
  ${TEST_SERVER_COMPILE_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_server_compile
  ${TEST_SERVER_COMPILE_SOURCES}
  )

# SERVER_MODE must match the cubrid (server) library this test links against;
# proving the parser lives in the SERVER_MODE build is the point of the test.
target_compile_definitions(test_server_compile PRIVATE ${COMMON_DEFS} SERVER_MODE)
target_include_directories(test_server_compile PRIVATE ${TEST_INCLUDES})

if(UNIX)
  target_link_libraries(test_server_compile LINK_PRIVATE cubrid)
else()
  message(SEND_ERROR "server_compile unit testing is unix-only")
endif ()
