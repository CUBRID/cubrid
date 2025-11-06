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

project(test_oos_project)

#
# test_oos
#

set(TEST_OOS_SOURCES
  test_oos.cpp
  test_oos_persist.cpp
  test_oos_record_descriptor.cpp
  )

set_source_files_properties(
  ${TEST_OOS_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_oos
  test_oos.cpp
  )
add_executable(test_oos_persist
  test_oos_persist.cpp
  )
add_executable(test_oos_record_descriptor
  test_oos_record_descriptor.cpp
  )

target_link_libraries(test_oos LINK_PRIVATE ${EP_LIBS})
target_link_libraries(test_oos_persist LINK_PRIVATE ${EP_LIBS})
target_link_libraries(test_oos_record_descriptor LINK_PRIVATE ${EP_LIBS})

target_compile_definitions(test_oos PRIVATE
  ${COMMON_DEFS} SA_MODE
  )
target_compile_definitions(test_oos_persist PRIVATE
  ${COMMON_DEFS} SA_MODE
  )
target_compile_definitions(test_oos_record_descriptor PRIVATE
  ${COMMON_DEFS} SA_MODE
  )

target_link_libraries(test_oos PRIVATE
  cubridsa
  )
target_link_libraries(test_oos_persist PRIVATE
  cubridsa
  )
target_link_libraries(test_oos_record_descriptor PRIVATE
  cubridsa
  )

target_include_directories(test_oos PRIVATE
  ${TEST_INCLUDES}
  )
target_include_directories(test_oos_persist PRIVATE
  ${TEST_INCLUDES}
  )
target_include_directories(test_oos_record_descriptor PRIVATE
  ${TEST_INCLUDES}
  )

# prefer gtest over gtest_main to have custom main function
# target_link_libraries(test_oos PRIVATE GTest::gtest_main)
target_link_libraries(test_oos PRIVATE GTest::gtest)
target_link_libraries(test_oos_persist PRIVATE GTest::gtest)
target_link_libraries(test_oos_record_descriptor PRIVATE GTest::gtest)

# in order to run each testcases in isolated process, uncomment below
# include(GoogleTest)
# gtest_discover_tests(test_oos)
# gtest_discover_tests(test_oos_persist)
# gtest_discover_tests(test_oos_record_descriptor)

# if not using gtest, this adds test_oos to ctest
add_test(NAME test_oos COMMAND test_oos)
add_test(NAME test_oos_persist COMMAND test_oos_persist)
add_test(NAME test_oos_record_descriptor COMMAND test_oos_record_descriptor)
