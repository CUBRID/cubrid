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

# Project to test memory allocation.
#
#

set (TEST_MONITOR_SOURCES
  test_monitor_main.cpp
  )
set (TEST_MONITOR_HEADERS
  ${MONITOR_DIR}/monitor_collect.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_MONITOR_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_monitor
  ${TEST_MONITOR_SOURCES}
  ${TEST_MONITOR_HEADERS}
  )

target_compile_definitions(test_monitor PRIVATE
  ${COMMON_DEFS}
  SERVER_MODE
  )

target_include_directories(test_monitor PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_monitor PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_monitor PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_monitor PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Transfer channel unit testing is for unix/windows")
endif ()
