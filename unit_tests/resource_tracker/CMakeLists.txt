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

set (TEST_RESTRACK_SOURCES
  test_restrack_main.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_resource_tracker
  ${TEST_RESTRACK_SOURCES}
  ${TEST_RESTRACK_HEADERS}
  )

target_compile_definitions(test_resource_tracker PRIVATE
  ${COMMON_DEFS}
  SERVER_MODE # to suppress module checks in headers
  )
if(UNIX)
  target_link_libraries(test_resource_tracker PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_resource_tracker PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "resource tracker unit testing is for unix/windows")
endif ()
