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

set (TEST_LOG_RECOVERY_SOURCES
  test_no_logging_index_judgment.cpp
  )

add_executable(test_log_recovery
  ${TEST_LOG_RECOVERY_SOURCES}
  )

target_compile_definitions(test_log_recovery PRIVATE
  SA_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_log_recovery PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_log_recovery PRIVATE
  cubridsa
  )

add_dependencies(test_log_recovery
  ${CATCH2_TARGET}
  ${EP_TARGETS}
  )

add_test (NAME test_log_recovery COMMAND test_log_recovery)
