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

project (test_scope_exit)

# scope_exit.hpp is header-only and uses only the standard library, so the test does not link an engine library

set (TEST_SCOPE_EXIT_SRC
  test_main.cpp
  test_scope_exit.cpp
  )

add_executable(test_scope_exit
  ${TEST_SCOPE_EXIT_SRC}
  )

add_dependencies(test_scope_exit
  ${CATCH2_TARGET}
  )

target_include_directories(test_scope_exit PRIVATE
  ${TEST_INCLUDES}
  ${CMAKE_SOURCE_DIR}/src/base
  )

add_test(NAME test_scope_exit COMMAND test_scope_exit)
