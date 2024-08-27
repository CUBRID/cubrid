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
#

project(test_memory_monitor)

#
# test_memory_monitor
#

set(TEST_MEMORY_MONITOR_SOURCES
  ${BASE_DIR}/memory_monitor_sr.cpp
  test_memory_monitor_main.cpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_MEMORY_MONITOR_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_memory_monitor
  ${TEST_MEMORY_MONITOR_SOURCES}
  )

target_link_libraries(test_memory_monitor LINK_PRIVATE ${EP_LIBS})
target_compile_options(test_memory_monitor PRIVATE ${compile_warning_flags})
target_compile_definitions(test_memory_monitor PRIVATE
  ${COMMON_DEFS} SERVER_MODE
  )
target_include_directories(test_memory_monitor PRIVATE
  ${TEST_INCLUDES}
  )
add_dependencies(test_memory_monitor
  ${CATCH2_TARGET} ${EP_TARGETS}
  )
if(WIN32)
  target_link_libraries(test_memory_monitor PRIVATE ws2_32)
endif(WIN32)
