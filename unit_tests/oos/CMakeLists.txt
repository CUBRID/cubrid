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

# # Collect all test source files matching test_*.cpp
file(GLOB TEST_CPP_SOURCES test_*.cpp)

foreach(src ${TEST_CPP_SOURCES})
  # Extract the base name without extension
  get_filename_component(test_name ${src} NAME_WE)

  # Define executable
  add_executable(${test_name} ${src})

  # Link libraries
  target_link_libraries(${test_name} PRIVATE ${EP_LIBS} cubridsa GTest::gtest)

  # Common definitions and includes
  target_compile_definitions(${test_name} PRIVATE ${COMMON_DEFS} SA_MODE)
  target_include_directories(${test_name} PRIVATE ${TEST_INCLUDES})
  

  # Register the test automatically:
  # add_test(NAME ${test_name} COMMAND ${test_name})
  #
  # but while active development,
  # it is convenient to comment this out to avoid running all tests at once.
  # Prefer to register tests one by one below.
endforeach()

# Register individual tests one by one.
add_test(NAME test_oos COMMAND test_oos)
# add_test(NAME test_oos_persist COMMAND test_oos_persist)
# add_test(NAME test_oos_record_descriptor COMMAND test_oos_record_descriptor)

# Note: add_test vs. gtest_discover_tests
# - Prefer add_test if you want to share global setup/teardown, which speeds up test
# - Prefer gtest_discover_tests for isolation between tests, at the cost of test speed
# - Since we do not need isolation here, we use add_test aboe.
