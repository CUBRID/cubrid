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

# =============================================================================
# CTest fixture: auto-create unittestdb before OOS tests, delete it after
# =============================================================================
add_test(NAME oos_setup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash -c "
      if [ ! -f \"$ENV{CUBRID_DATABASES}/databases.txt\" ] || ! grep -q unittestdb \"$ENV{CUBRID_DATABASES}/databases.txt\" 2>/dev/null; then
        mkdir -p \"$ENV{CUBRID_DATABASES}/unittestdb\" && \
        cubrid createdb --db-volume-size=20M --log-volume-size=20M unittestdb en_US.utf8 \
          -F \"$ENV{CUBRID_DATABASES}/unittestdb\"
      fi
    "
)
set_tests_properties(oos_setup_db PROPERTIES FIXTURES_SETUP OOS_DB)

add_test(NAME oos_cleanup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash -c "
      if grep -q unittestdb \"$ENV{CUBRID_DATABASES}/databases.txt\" 2>/dev/null; then
        cubrid deletedb unittestdb
      fi
    "
)
set_tests_properties(oos_cleanup_db PROPERTIES FIXTURES_CLEANUP OOS_DB)

# Register individual tests one by one.
add_test(NAME test_oos COMMAND test_oos)
add_test(NAME test_oos_delete COMMAND test_oos_delete)
add_test(NAME test_oos_remove_file COMMAND test_oos_remove_file)
add_test(NAME test_oos_bestspace COMMAND test_oos_bestspace)
# pure-logic test for the bounded writer used by oos_read; no DB fixture.
add_test(NAME test_byte_span_writer COMMAND test_byte_span_writer)
# add_test(NAME test_oos_persist COMMAND test_oos_persist)
# add_test(NAME test_oos_record_descriptor COMMAND test_oos_record_descriptor)

set_tests_properties(
  test_oos test_oos_delete test_oos_remove_file test_oos_bestspace
  PROPERTIES FIXTURES_REQUIRED OOS_DB
)

# Note: add_test vs. gtest_discover_tests
# - Prefer add_test if you want to share global setup/teardown, which speeds up test
# - Prefer gtest_discover_tests for isolation between tests, at the cost of test speed
# - Since we do not need isolation here, we use add_test above.

add_subdirectory(sql)
