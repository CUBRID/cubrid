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

file(GLOB TEST_SQL_SOURCES test_oos_sql_*.cpp)

foreach(src ${TEST_SQL_SOURCES})
  get_filename_component(test_name ${src} NAME_WE)

  add_executable(${test_name} ${src})
  target_link_libraries(${test_name} PRIVATE ${EP_LIBS} cubridsa GTest::gtest)
  target_compile_definitions(${test_name} PRIVATE ${COMMON_DEFS} SA_MODE)
  target_include_directories(${test_name} PRIVATE ${TEST_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

# Register tests
add_test(NAME test_oos_sql_crud COMMAND test_oos_sql_crud)
add_test(NAME test_oos_sql_ddl COMMAND test_oos_sql_ddl)
add_test(NAME test_oos_sql_delete COMMAND test_oos_sql_delete)
add_test(NAME test_oos_sql_boundary COMMAND test_oos_sql_boundary)
add_test(NAME test_oos_sql_update_delete COMMAND test_oos_sql_update_delete)
add_test(NAME test_oos_sql_txn COMMAND test_oos_sql_txn)

set_tests_properties(
  test_oos_sql_crud test_oos_sql_ddl test_oos_sql_delete
  test_oos_sql_boundary test_oos_sql_update_delete test_oos_sql_txn
  PROPERTIES FIXTURES_REQUIRED OOS_DB RUN_SERIAL TRUE
)
