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

project (test_sidecar)

set (TEST_SIDECAR_SRC
  test_main.cpp
  test_sidecar_format.cpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_SIDECAR_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_sidecar
  ${TEST_SIDECAR_SRC}
  )

add_dependencies(test_sidecar
  ${CATCH2_TARGET}
  )

target_compile_definitions(test_sidecar PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_sidecar PRIVATE
  ${TEST_INCLUDES}
  ${CMAKE_SOURCE_DIR}/src/base
  ${CMAKE_SOURCE_DIR}/src/compat
  ${CMAKE_SOURCE_DIR}/src/storage
  ${CMAKE_SOURCE_DIR}/src/thread
  )

if(UNIX)
  target_link_libraries(test_sidecar PRIVATE
    cubrid
    )
else()
  message( SEND_ERROR "sidecar unit testing is for unix" )
endif ()

add_test(NAME test_sidecar COMMAND test_sidecar)
