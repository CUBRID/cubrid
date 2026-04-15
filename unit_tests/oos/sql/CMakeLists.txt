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

project(test_oos_sql_project)

# =============================================================================
# SA_MODE tests (linked to cubridsa, in-process server)
# =============================================================================
file(GLOB TEST_SQL_SOURCES test_oos_sql_*.cpp)

foreach(src ${TEST_SQL_SOURCES})
  get_filename_component(test_name ${src} NAME_WE)

  add_executable(${test_name} ${src})
  target_link_libraries(${test_name} PRIVATE ${EP_LIBS} cubridsa GTest::gtest)
  target_compile_definitions(${test_name} PRIVATE ${COMMON_DEFS} SA_MODE)
  target_include_directories(${test_name} PRIVATE ${TEST_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

# Register SA_MODE tests
add_test(NAME test_oos_sql_crud COMMAND test_oos_sql_crud)
add_test(NAME test_oos_sql_ddl COMMAND test_oos_sql_ddl)
add_test(NAME test_oos_sql_delete COMMAND test_oos_sql_delete)
add_test(NAME test_oos_sql_boundary COMMAND test_oos_sql_boundary)
add_test(NAME test_oos_sql_update_delete COMMAND test_oos_sql_update_delete)
add_test(NAME test_oos_sql_txn COMMAND test_oos_sql_txn)
add_test(NAME test_oos_sql_vacuum COMMAND test_oos_sql_vacuum)
add_test(NAME test_oos_sql_reclaim COMMAND test_oos_sql_reclaim)

set_tests_properties(
  test_oos_sql_crud test_oos_sql_ddl test_oos_sql_delete
  test_oos_sql_boundary test_oos_sql_update_delete test_oos_sql_txn
  test_oos_sql_vacuum test_oos_sql_reclaim
  PROPERTIES FIXTURES_REQUIRED OOS_DB RUN_SERIAL TRUE TIMEOUT 30
)

# =============================================================================
# CS_MODE tests (linked to cubridcs, requires running cub_server)
# =============================================================================
add_executable(test_oos_sql_reclaim_cs test_oos_sql_reclaim.cpp)
target_link_libraries(test_oos_sql_reclaim_cs PRIVATE ${EP_LIBS} cubridcs GTest::gtest)
target_compile_definitions(test_oos_sql_reclaim_cs PRIVATE ${COMMON_DEFS} CS_MODE)
target_include_directories(test_oos_sql_reclaim_cs PRIVATE ${TEST_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR})

# CS_MODE fixture: start/stop cub_server around CS tests
add_test(NAME oos_start_server
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash -c "cubrid server start unittestdb 2>/dev/null || true"
)
set_tests_properties(oos_start_server PROPERTIES
  FIXTURES_SETUP OOS_SERVER
  FIXTURES_REQUIRED OOS_DB
  TIMEOUT 30
)

add_test(NAME oos_stop_server
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash -c "cubrid server stop unittestdb 2>/dev/null || true"
)
set_tests_properties(oos_stop_server PROPERTIES
  FIXTURES_CLEANUP OOS_SERVER
  TIMEOUT 30
)

add_test(NAME test_oos_sql_reclaim_cs COMMAND test_oos_sql_reclaim_cs)
set_tests_properties(test_oos_sql_reclaim_cs
  PROPERTIES FIXTURES_REQUIRED "OOS_DB;OOS_SERVER" RUN_SERIAL TRUE TIMEOUT 60
)
