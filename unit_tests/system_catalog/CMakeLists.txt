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

project(test_system_catalog_project)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.zip
)
FetchContent_MakeAvailable(googletest)

# SA_MODE: the test boots its database in-process and reads the catalog pages directly.
add_executable(test_catalog_record_gaps test_catalog_record_gaps.cpp)
target_link_libraries(test_catalog_record_gaps PRIVATE ${EP_LIBS} cubridsa GTest::gtest)
target_compile_definitions(test_catalog_record_gaps PRIVATE ${COMMON_DEFS} SA_MODE)
target_include_directories(test_catalog_record_gaps PRIVATE ${TEST_INCLUDES})

# The database lives in a registry of its own under the build tree, so the fixture never reads or rewrites the
# databases.txt of the developer, and it is recreated by every run with the disk layout this build writes.
set(CATALOG_TEST_DATABASES ${CMAKE_CURRENT_BINARY_DIR}/catalog_test_databases)

add_test(NAME catalog_setup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=${CATALOG_TEST_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_db.sh"
)
set_tests_properties(catalog_setup_db PROPERTIES FIXTURES_SETUP CATALOG_DB TIMEOUT 60)

add_test(NAME catalog_cleanup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=${CATALOG_TEST_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cleanup_db.sh"
)
set_tests_properties(catalog_cleanup_db PROPERTIES FIXTURES_CLEANUP CATALOG_DB TIMEOUT 60)

add_test(NAME test_catalog_record_gaps COMMAND test_catalog_record_gaps)
set_tests_properties(test_catalog_record_gaps PROPERTIES
  FIXTURES_REQUIRED CATALOG_DB
  ENVIRONMENT "CUBRID_DATABASES=${CATALOG_TEST_DATABASES}"
  TIMEOUT 60
)
