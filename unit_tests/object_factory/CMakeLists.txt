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

project (test_object_factory)

set (TEST_OBJECT_FACTORY_SRC
  test_main.cpp
  test_object_factory.cpp
  )
set (TEST_OBJECT_FACTORY_HPP
  test_object_factory.hpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_OBJECT_FACTORY_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_object_factory
  ${TEST_OBJECT_FACTORY_SRC}
  ${TEST_OBJECT_FACTORY_HPP}
  )

target_compile_definitions(test_object_factory PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_object_factory PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_object_factory PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_object_factory PRIVATE
    cubrid
    )
elseif(WIN32)
	target_link_libraries(test_object_factory PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Object factory unit testing is for unix/windows")
endif ()
