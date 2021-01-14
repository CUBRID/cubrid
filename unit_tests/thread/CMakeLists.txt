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

set (TEST_THREAD_SOURCES
  test_main.cpp
  test_manager.cpp
  test_worker_pool.cpp
  )
set (TEST_THREAD_HEADERS
  test_manager.hpp
  test_worker_pool.hpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_THREAD_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_thread
  ${TEST_THREAD_SOURCES}
  ${TEST_THREAD_HEADERS}
  )
  
target_compile_definitions(test_thread PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories(test_thread PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_thread LINK_PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_thread LINK_PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_thread LINK_PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Thread unit testing is for unix/windows")
endif ()
