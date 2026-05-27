#
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

set (TEST_PX_HEAP_SCAN_SOURCES
  test_px_heap_scan_checker.cpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_PX_HEAP_SCAN_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_px_heap_scan
  ${TEST_PX_HEAP_SCAN_SOURCES}
  )

target_compile_definitions(test_px_heap_scan PRIVATE
  ${COMMON_DEFS}
  SA_MODE
  )

target_include_directories(test_px_heap_scan PRIVATE
  ${TEST_INCLUDES}
  ${EP_INCLUDES}
  )

target_link_libraries(test_px_heap_scan LINK_PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_px_heap_scan LINK_PRIVATE
    cubridsa
    )
elseif(WIN32)
  target_link_libraries(test_px_heap_scan LINK_PRIVATE
    cubridsa
    )
else()
  message(SEND_ERROR "px_heap_scan unit testing is for unix/windows")
endif()

add_dependencies(test_px_heap_scan ${TESTS_EXTERNAL_TARGETS})
add_test(NAME test_px_heap_scan COMMAND test_px_heap_scan)
