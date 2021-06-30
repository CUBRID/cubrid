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

project (test_packing)

set (TEST_PACKING_SRC
  test_main.cpp
  test_packing.cpp
  )
set (TEST_PACKING_H
  test_packing.hpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_PACKING_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_packing
  ${TEST_PACKING_SRC}
  ${TEST_PACKING_H}
  )

target_compile_definitions(test_packing PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_packing PRIVATE
  ${TEST_INCLUDES}
  )

if(UNIX)
  target_link_libraries(test_packing PRIVATE
    test_common
    cubrid
    )
elseif(WIN32)
	target_link_libraries(test_packing PRIVATE 
    test_common
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Packing unit testing is for unix/windows")
endif ()
