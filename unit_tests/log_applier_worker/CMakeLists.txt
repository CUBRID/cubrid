project (test_log_applier_worker)

set (TEST_LOG_APPLIER_WORKER_SOURCES
  test_main.cpp
  test_log_applier_dispatch_order.cpp
  test_log_applier_coordinator.cpp
  test_log_applier_worker.cpp
  ${TRANSACTION_DIR}/log_applier_coordinator.cpp
  ${TRANSACTION_DIR}/log_applier_queue.cpp
  ${TRANSACTION_DIR}/log_applier_dispatch_order.cpp
  ${TRANSACTION_DIR}/log_applier_worker.cpp
  )

set_source_files_properties(
  ${TEST_LOG_APPLIER_WORKER_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_log_applier_worker
  ${TEST_LOG_APPLIER_WORKER_SOURCES}
  )

target_compile_definitions(test_log_applier_worker PRIVATE
  CS_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_log_applier_worker PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_log_applier_worker PRIVATE
  test_common
  ${CMAKE_THREAD_LIBS_INIT}
  )

add_dependencies(test_log_applier_worker
  ${TESTS_EXTERNAL_TARGETS}
  )
