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

project(test_log_repl)

#
# test_log_repl_atomic_helper
#
set(TEST_LOG_REPL_ATOMIC_HELPER_SOURCES
  ${TEST_FAKE_DIR}/fake_thread_entry.cpp
  ${TRANSACTION_DIR}/atomic_replication_helper.cpp
  test_main_log_repl_atomic_helper.cpp
  )

set (TEST_LOG_REPL_ATOMIC_HELPER_HEADERS
  ${TRANSACTION_DIR}/atomic_replication_helper.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_LOG_REPL_ATOMIC_HELPER_SOURCES}
  PROPERTIES LANGUAGE CXX)

add_executable(test_log_repl_atomic_helper
  ${TEST_LOG_REPL_ATOMIC_HELPER_SOURCES}
  ${TEST_LOG_REPL_ATOMIC_HELPER_HEADERS}
  )
target_compile_options(test_log_repl_atomic_helper PRIVATE
  ${compile_warning_flags}
  )
target_compile_definitions(test_log_repl_atomic_helper PRIVATE
  ${COMMON_DEFS}
  SERVER_MODE
  )
target_include_directories(test_log_repl_atomic_helper PRIVATE
  ${TEST_INCLUDES}
  )
#target_link_libraries(test_log_repl_atomic_helper PRIVATE test_common)
#if(UNIX)
#  target_link_libraries(test_log_repl_atomic_helper PRIVATE cubrid)
#elseif(WIN32)
#  target_link_libraries(test_log_repl_atomic_helper PRIVATE cubrid-win-lib)
#else()
#  message(SEND_ERROR "Atomic replication only works on UNIX-like/WIN32")
#endif ()
