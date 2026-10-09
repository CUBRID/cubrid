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

project (test_writeset)

set (TEST_LOG_WRITESET_SRC
  test_main.cpp
  test_writeset.cpp
  test_writeset_bench.cpp
  test_writeset_stress.cpp
  test_writeset_concurrency.cpp
  test_writeset_config.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_LOG_WRITESET_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_writeset
  ${TEST_LOG_WRITESET_SRC}
  )

target_compile_definitions(test_writeset PRIVATE
  SERVER_MODE
  CATCH_CONFIG_ENABLE_BENCHMARKING
  ${COMMON_DEFS}
  )

# The test translation units include writeset.hpp, which pulls in the oneTBB
# concurrent_hash_map header, so TBB has to be on the include path; the map
# methods the tests call are instantiated here, so TBB has to be linked too.
target_include_directories(test_writeset PRIVATE
  ${TEST_INCLUDES}
  ${TBB_INCLUDES}
  )

target_link_libraries(test_writeset PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_writeset PRIVATE
    cubrid
    ${TBB_LIBS}
    )
else()
  message( SEND_ERROR "writeset unit testing is for unix")
endif ()

add_dependencies(test_writeset
  ${TESTS_EXTERNAL_TARGETS}
  ${TBB_TARGETS}
  )
