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

# Project to test the completeness gate of the evaluation properties of operators and functions.
#
#

set (TEST_EVAL_PROP_SOURCES
  test_eval_prop_main.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_EVAL_PROP_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_eval_prop
  ${TEST_EVAL_PROP_SOURCES}
  )

target_compile_definitions(test_eval_prop PRIVATE
  ${COMMON_DEFS}
  SA_MODE
  )

target_include_directories(test_eval_prop PRIVATE
  ${TEST_INCLUDES}
  )

if(UNIX)
  target_link_libraries(test_eval_prop PRIVATE
    cubridsa
    )
else()
  message( SEND_ERROR "Evaluation-properties unit testing is unix only")
endif ()

add_dependencies(test_eval_prop
  ${CATCH2_TARGET}
  )

add_test(NAME test_eval_prop COMMAND test_eval_prop)
