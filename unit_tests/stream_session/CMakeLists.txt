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

project (test_stream_session)

# the registry needs nothing from the engine but er_set (), which the test
# supplies, so build it into the test rather than linking a library for it
set (TEST_STREAM_SESSION_SOURCES
  ${COMMUNICATION_DIR}/stream_session.cpp
  test_stream_session.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_STREAM_SESSION_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_stream_session
  ${TEST_STREAM_SESSION_SOURCES}
  )

target_compile_definitions(test_stream_session PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories(test_stream_session PRIVATE
  ${TEST_INCLUDES}
  ${BASE_DIR}
  ${COMMUNICATION_DIR}
  ${CONNECTION_DIR}
  ${THREAD_DIR}
  )

add_dependencies(test_stream_session
  ${CATCH2_TARGET}
  ${EP_TARGETS}
  )
