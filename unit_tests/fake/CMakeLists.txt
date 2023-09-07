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

#
# Fake libs for testing
#
project(fake)

#
# fake_thread_utils - use it to add thread_manager, thread_worker_pool and thread_daemon to your tests.
#
set(FAKE_THREAD_UTILS_SRC
  fake_error_manager.cpp
  fake_system_parameter.cpp
  fake_thread_entry.cpp
  fake_thread_utils.cpp
  fake_server_type.cpp
  ${BASE_DIR}/perf.cpp
  ${THREAD_DIR}/thread_manager.cpp
  ${THREAD_DIR}/thread_daemon.cpp
  ${THREAD_DIR}/thread_entry_task.cpp
  ${THREAD_DIR}/thread_looper.cpp
  ${THREAD_DIR}/thread_waiter.cpp
  ${THREAD_DIR}/thread_worker_pool.cpp
)
SET_SOURCE_FILES_PROPERTIES(
  ${FAKE_THREAD_UTILS_SRC}
  PROPERTIES LANGUAGE CXX
)
add_library(fake_thread_utils STATIC
  ${FAKE_THREAD_UTILS_SRC}
)
target_compile_options(fake_thread_utils PRIVATE
  ${compile_warning_flags}
)
target_compile_definitions(fake_thread_utils PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
)
target_include_directories(fake_thread_utils PRIVATE
  ${EP_INCLUDES}
)
add_dependencies(fake_thread_utils
  ${EP_TARGETS}
)
if(WIN32)
  target_link_libraries(fake_thread_utils PUBLIC ws2_32)
endif(WIN32)
