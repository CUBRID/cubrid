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

project(test_log_transfer)

set(TEST_PRIOR_LIST_SERIALIZE_SOURCES
  ${TRANSACTION_DIR}/log_append.cpp
  test_main_prior_list_serialize.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_PRIOR_LIST_SERIALIZE_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
  
add_executable(test_prior_list_serialize
  ${TEST_PRIOR_LIST_SERIALIZE_SOURCES}
  )

target_compile_options(test_prior_list_serialize PRIVATE ${compile_warning_flags})
target_compile_definitions(test_prior_list_serialize PRIVATE
  ${COMMON_DEFS} SERVER_MODE
  )
target_include_directories(test_prior_list_serialize PRIVATE
  ${TEST_INCLUDES}
  )
add_dependencies(test_prior_list_serialize
  ${CATCH2_TARGET}
  )
  
