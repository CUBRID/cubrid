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

# unit tests logic:
# - all: cmake -DUNIT_TESTS=ON <path>
# - some: cmake -DUNIT_TEST_ABC=ON -DUNIT_TEST_XYZ=ON <path>

project(Test)

set(TEST_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
set(TEST_FAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fake)

set (TEST_INCLUDES
  ${EP_INCLUDES}
  ${TEST_COMMON_DIR}
  ${TEST_FAKE_DIR}
  )

option (UNIT_TEST_MEMORY_ALLOC  "Unit testing: memory allocation")
option (UNIT_TEST_STRING_BUFFER "Unit testing: string buffer with format")
option (UNIT_TEST_LOCKFREE "Unit testing: lockfree module")
option (UNIT_TEST_THREAD "Unit testing: thread module")
option (UNIT_TEST_PACKING "Unit testing: packing")
option (UNIT_TEST_RESOURCE_TRACKER "Unit testing: resource tracker")
option (UNIT_TEST_MONITOR "Unit testing: monitor")
option (UNIT_TEST_LOADDB "Unit testing: loaddb module")
option (UNIT_TEST_REQUEST_CS "Unit testing: request_client_server module")
option (UNIT_TEST_LOG "Unit testing: log")
option (UNIT_TEST_LOG_REPL "Unit testing: log replication")
option (UNIT_TEST_SERVER "Unit testing: server")

message("  unit_tests/...")

# Catch2
set(CATCH2_TARGET catch2)
externalproject_add(${CATCH2_TARGET}
  GIT_REPOSITORY       https://github.com/catchorg/Catch2
  GIT_TAG              255aa5f    # https://github.com/catchorg/Catch2/releases/tag/v2.11.3
  # don't install
  INSTALL_COMMAND      ""
  # don't build test, doc and examples
  CMAKE_CACHE_ARGS     -DCATCH_BUILD_TESTING:STRING=off
		       -DCATCH_INSTALL_DOCS:STRING=off
		       -DCATCH_INSTALL_HELPERS:STRING=off
		       -DBUILD_TESTING:STRING=off
)
# add to include directories
set (CATCH2_DIR ${CMAKE_BINARY_DIR}/external/Source/catch2)
include_directories(${CATCH2_DIR}/single_include)
# add to tests external project targets
list(APPEND TESTS_EXTERNAL_TARGETS ${CATCH2_TARGET})

message("    common and fake")
add_subdirectory(common)
add_subdirectory(fake)

if (UNIT_TESTS OR UNIT_TEST_MEMORY_ALLOC)
  message("    memory_alloc")
  add_subdirectory(memory_alloc)
endif()

if (UNIT_TESTS OR UNIT_TEST_STRING_BUFFER)
  message("    string_buffer")
  add_subdirectory(string_buffer)
endif()

if (UNIT_TESTS OR UNIT_TEST_LOCKFREE)
  message("    lockfree")
  add_subdirectory(lockfree)
endif (UNIT_TESTS OR UNIT_TEST_LOCKFREE)

if (UNIT_TESTS OR UNIT_TEST_THREAD)
  message("    thread")
  add_subdirectory(thread)
endif(UNIT_TESTS OR UNIT_TEST_THREAD)

if (UNIT_TESTS OR UNIT_TEST_PACKING)
  message("    packing")
  add_subdirectory(packing)
endif(UNIT_TESTS OR UNIT_TEST_PACKING)

if (UNIT_TESTS OR UNIT_TEST_OBJECT_FACTORY)
  message("    object_factory")
  add_subdirectory(object_factory)
endif(UNIT_TESTS OR UNIT_TEST_OBJECT_FACTORY)

if (UNIT_TESTS OR UNIT_TEST_RESOURCE_TRACKER)
  message("    resource_tracker")
  add_subdirectory(resource_tracker)
endif(UNIT_TESTS OR UNIT_TEST_RESOURCE_TRACKER)

if (UNIT_TESTS OR UNIT_TEST_MONITOR)
  message("    monitor")
  add_subdirectory(monitor)
endif(UNIT_TESTS OR UNIT_TEST_MONITOR)

if (UNIT_TESTS OR UNIT_TEST_REQUEST_CS)
  message("    request_client_server")
  add_subdirectory(request_client_server)
endif(UNIT_TESTS OR UNIT_TEST_REQUEST_CS)

if (UNIT_TESTS OR UNIT_TEST_LOG)
  message("    log transfer")
  add_subdirectory(log)
endif(UNIT_TESTS OR UNIT_TEST_LOG)

if (UNIT_TESTS OR UNIT_TEST_LOG_REPL)
  message("    log replication")
  add_subdirectory(log_repl)
endif(UNIT_TESTS OR UNIT_TEST_LOG_REPL)

if (UNIT_TESTS OR UNIT_TEST_SERVER)
  message("    server")
  add_subdirectory(server)
endif(UNIT_TESTS OR UNIT_TEST_SERVER)
