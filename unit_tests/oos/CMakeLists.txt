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

# =============================================================================
# SA_MODE tests (linked to cubridsa, standalone in-process server)
# =============================================================================
set(SA_MODE_TESTS
  test_oos
  test_oos_delete
  test_oos_remove_file
  test_oos_bestspace
  test_byte_span_writer
  test_oos_tde_gate
)

foreach(test_name ${SA_MODE_TESTS})
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE ${EP_LIBS} cubridsa GTest::gtest)
  target_compile_definitions(${test_name} PRIVATE ${COMMON_DEFS} SA_MODE)
  target_include_directories(${test_name} PRIVATE ${TEST_INCLUDES})
endforeach()

# =============================================================================
# SERVER_MODE tests (linked to cubrid, in-process server with MVCC/threading)
# =============================================================================
set(SERVER_MODE_TESTS
  test_oos_server
  test_oos_delete_server
  test_oos_remove_file_server
  test_oos_mock_vacuum_server
  test_oos_vacuum_server
  test_oos_real_vacuum_server
)

foreach(test_name ${SERVER_MODE_TESTS})
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE ${EP_LIBS} cubrid GTest::gtest)
  target_compile_definitions(${test_name} PRIVATE ${COMMON_DEFS} SERVER_MODE)
  target_include_directories(${test_name} PRIVATE ${TEST_INCLUDES})
endforeach()

# =============================================================================
# CTest fixture: auto-create unittestdb before OOS tests, delete it after
# =============================================================================
add_test(NAME oos_setup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_unittestdb.sh"
)
set_tests_properties(oos_setup_db PROPERTIES FIXTURES_SETUP OOS_DB)

add_test(NAME oos_cleanup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cleanup_unittestdb.sh"
)
set_tests_properties(oos_cleanup_db PROPERTIES FIXTURES_CLEANUP OOS_DB TIMEOUT 30)

# =============================================================================
# Register SA_MODE tests
# =============================================================================
add_test(NAME test_oos COMMAND test_oos)
add_test(NAME test_oos_delete COMMAND test_oos_delete)
add_test(NAME test_oos_remove_file COMMAND test_oos_remove_file)
add_test(NAME test_oos_bestspace COMMAND test_oos_bestspace)
# pure-logic test for the bounded writer used by oos_read; no DB fixture.
add_test(NAME test_byte_span_writer COMMAND test_byte_span_writer)
# pure-logic guard that the OOS forward-delete rcvindex is TDE-gated (CBRD-26668); no DB fixture.
add_test(NAME test_oos_tde_gate COMMAND test_oos_tde_gate)

set_tests_properties(
  test_oos test_oos_delete test_oos_remove_file test_oos_bestspace
  PROPERTIES FIXTURES_REQUIRED OOS_DB TIMEOUT 30
)

# =============================================================================
# Register SERVER_MODE tests
# =============================================================================
add_test(NAME test_oos_server COMMAND test_oos_server)
add_test(NAME test_oos_delete_server COMMAND test_oos_delete_server)
add_test(NAME test_oos_remove_file_server COMMAND test_oos_remove_file_server)
add_test(NAME test_oos_mock_vacuum_server COMMAND test_oos_mock_vacuum_server)
add_test(NAME test_oos_vacuum_server COMMAND test_oos_vacuum_server)
add_test(NAME test_oos_real_vacuum_server COMMAND test_oos_real_vacuum_server)

set_tests_properties(
  test_oos_server test_oos_delete_server test_oos_remove_file_server
  test_oos_mock_vacuum_server test_oos_vacuum_server
  PROPERTIES FIXTURES_REQUIRED OOS_DB RUN_SERIAL TRUE TIMEOUT 60
)

# Real-vacuum E2E test polls the vacuum master daemon — needs extra headroom.
set_tests_properties(
  test_oos_real_vacuum_server
  PROPERTIES FIXTURES_REQUIRED OOS_DB RUN_SERIAL TRUE TIMEOUT 120
)

# Note: add_test vs. gtest_discover_tests
# - Prefer add_test if you want to share global setup/teardown, which speeds up test
# - Prefer gtest_discover_tests for isolation between tests, at the cost of test speed
# - Since we do not need isolation here, we use add_test above.

add_subdirectory(sql)
