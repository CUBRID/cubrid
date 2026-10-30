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

set (TEST_LOG_PRIOR_INFLIGHT_SOURCES
  test_main.cpp
  test_inflight_ring.cpp
)
set (TEST_LOG_PRIOR_INFLIGHT_HEADERS
  test_inflight_ring.hpp
)
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_LOG_PRIOR_INFLIGHT_SOURCES}
  PROPERTIES LANGUAGE CXX
)

add_executable(test_log_prior_inflight
  ${TEST_LOG_PRIOR_INFLIGHT_SOURCES}
  ${TEST_LOG_PRIOR_INFLIGHT_HEADERS}
  )

# log_prior_inflight_ring.hpp is a template over log_lsa.hpp and a forward-declared node, so this target
# needs no engine library and no Catch2 - only the shared test output helpers.
target_compile_definitions(test_log_prior_inflight PRIVATE
  SA_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_log_prior_inflight PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_log_prior_inflight LINK_PRIVATE
  test_common
  )
