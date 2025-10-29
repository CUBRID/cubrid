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

project(test_oos)

#
# test_oos
#

set(TEST_OOS_SOURCES
  test_oos.cpp
  )

set_source_files_properties(
  ${TEST_OOS_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_oos
  ${TEST_OOS_SOURCES}
  )

target_link_libraries(test_oos LINK_PRIVATE ${EP_LIBS})
target_compile_options(test_oos PRIVATE ${compile_warning_flags})
target_compile_definitions(test_oos PRIVATE
  ${COMMON_DEFS} SERVER_MODE
  )
target_include_directories(test_oos PRIVATE
  ${TEST_INCLUDES}
  )
add_dependencies(test_oos
  ${CATCH2_TARGET} ${EP_TARGETS}
  )

add_test(NAME test_oos COMMAND test_oos)
