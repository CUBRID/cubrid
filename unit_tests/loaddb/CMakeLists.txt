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

set(TEST_LOADDB_SOURCES
  test_main.cpp
  test_loaddb.cpp
  )

set(TEST_LOADDB_HEADERS
  test_loaddb.hpp
  )

set_source_files_properties(
  ${TEST_LOADDB_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_loaddb
  ${TEST_LOADDB_SOURCES}
  ${TEST_LOADDB_HEADERS}
  )
  
target_compile_definitions(test_loaddb PRIVATE ${COMMON_DEFS})
target_include_directories(test_loaddb PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_loaddb LINK_PRIVATE test_common)

if(UNIX)
  target_link_libraries(test_loaddb LINK_PRIVATE cubrid)
elseif(WIN32)
  target_link_libraries(test_loaddb LINK_PRIVATE cubrid-win-lib)
else()
  message(SEND_ERROR "Loaddb unit testing is for unix/windows")
endif ()
