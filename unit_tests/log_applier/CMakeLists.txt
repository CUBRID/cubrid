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

project (test_log_applier)

# The coordinator's bookkeeping modules (in-flight list, gate, pending list, task and result queues) keep their
# state in file statics and are not exported from cubridcs, so their sources are compiled into the test; cubridcs
# supplies the error manager and the other client runtime they call. The worker module is compiled in too: its
# sub-client calls (db_restart_sub, db_shutdown_sub) need a server, and test_log_applier_worker.cpp defines them
# in place of cubridcs's. The coordinator itself needs a server connection and is not unit tested here.
set (TEST_LOG_APPLIER_SRC
  test_main.cpp
  test_log_applier_inflight.cpp
  test_log_applier_gate.cpp
  test_log_applier_pending.cpp
  test_log_applier_queues.cpp
  test_log_applier_worker.cpp
  ${APPLIER_DIR}/log_applier_inflight.cpp
  ${APPLIER_DIR}/log_applier_gate.cpp
  ${APPLIER_DIR}/log_applier_pending.cpp
  ${APPLIER_DIR}/log_applier_task_queue.cpp
  ${APPLIER_DIR}/log_applier_result_queue.cpp
  ${APPLIER_DIR}/log_applier_worker.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_LOG_APPLIER_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_log_applier
  ${TEST_LOG_APPLIER_SRC}
  )

target_compile_definitions(test_log_applier PRIVATE
  CS_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_log_applier PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_log_applier PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_log_applier PRIVATE
    cubridcs
    ${CMAKE_THREAD_LIBS_INIT}
    )
else()
  message( SEND_ERROR "log applier unit testing is for unix")
endif ()

add_dependencies(test_log_applier
  ${TESTS_EXTERNAL_TARGETS}
  )
