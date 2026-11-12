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

project(test_oos_project)

function(oos_check_test_timeouts)
  get_property(oos_tests DIRECTORY PROPERTY TESTS)
  foreach(oos_test IN LISTS oos_tests)
    get_test_property(${oos_test} TIMEOUT oos_timeout)
    if(NOT oos_timeout MATCHES "^[0-9]+([.][0-9]+)?$" OR oos_timeout LESS_EQUAL 0 OR oos_timeout GREATER 60)
      message(FATAL_ERROR
        "Test ${oos_test} in ${CMAKE_CURRENT_SOURCE_DIR} must have a positive TIMEOUT no greater than 60")
    endif()
  endforeach()
endfunction()
cmake_language(DEFER CALL oos_check_test_timeouts)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.zip
)
FetchContent_MakeAvailable(googletest)

# =============================================================================
# SA_MODE tests (linked to cubridsa, standalone in-process server)
# =============================================================================
set(SA_MODE_TESTS
  test_oos
  test_oos_delete
  test_oos_identity_stamp
  test_oos_eager_diagnostics
  test_oos_no_logging
  test_oos_remove_file
  test_oos_bestspace
  test_oos_growth_sweep
  test_oos_record_flags
  test_oos_reference_errors
  test_byte_span_writer
  test_oos_tde_gate
  test_oos_workspace
)

foreach(test_name ${SA_MODE_TESTS})
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE ${EP_LIBS} cubridsa GTest::gtest)
  target_compile_definitions(${test_name} PRIVATE ${COMMON_DEFS} SA_MODE)
  target_include_directories(${test_name} PRIVATE ${TEST_INCLUDES})
endforeach()

# Native SQL-log cache/decoder lifetime tests require neither a database nor WAL replay.
add_executable(test_oos_sql_log_cache test_oos_sql_log_cache.cpp)
target_link_libraries(test_oos_sql_log_cache PRIVATE ${EP_LIBS} cubridcs GTest::gtest)
target_compile_definitions(test_oos_sql_log_cache PRIVATE ${COMMON_DEFS} CS_MODE)
target_include_directories(test_oos_sql_log_cache PRIVATE ${TEST_INCLUDES})
add_test(NAME test_oos_sql_log_cache COMMAND test_oos_sql_log_cache)
set_tests_properties(test_oos_sql_log_cache PROPERTIES TIMEOUT 30)

# =============================================================================
# SERVER_MODE tests (linked to cubrid, in-process server with MVCC/threading)
# =============================================================================
set(SERVER_MODE_TESTS
  test_oos_server
  test_oos_delete_server
  test_oos_remove_file_server
  test_oos_mock_vacuum_server
  test_oos_vacuum_server
  test_oos_real_vacuum_server
)

foreach(test_name ${SERVER_MODE_TESTS})
  add_executable(${test_name} ${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE ${EP_LIBS} cubrid GTest::gtest)
  target_compile_definitions(${test_name} PRIVATE ${COMMON_DEFS} SERVER_MODE)
  target_include_directories(${test_name} PRIVATE ${TEST_INCLUDES})
endforeach()

# =============================================================================
# CTest fixture: auto-create unittestdb before OOS tests, delete it after
#
# Every fixture setup and cleanup below reads the shared databases.txt to decide ownership while
# createdb/deletedb rewrite it in place, so they take one registry lock instead of running in parallel.
# =============================================================================
add_test(NAME oos_setup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_unittestdb.sh"
)
set_tests_properties(oos_setup_db
  PROPERTIES FIXTURES_SETUP OOS_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

add_test(NAME oos_cleanup_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cleanup_unittestdb.sh"
)
set_tests_properties(oos_cleanup_db
  PROPERTIES FIXTURES_CLEANUP OOS_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 30
)

# Script lifecycle regression uses a private sandbox utility, without a database fixture.
add_test(NAME test_oos_fixture_lifecycle
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_unittestdb_lifecycle.sh"
)
set_tests_properties(test_oos_fixture_lifecycle PROPERTIES TIMEOUT 30)

# =============================================================================
# CTest fixtures: databases owned by a single binary
#
# A binary that turns logging off for its whole process, or that kills a writer and recovers, must
# not run against the shared unittestdb fixture: what it leaves behind would reach every other OOS
# binary. Each such binary gets its own database, recreated from scratch by its setup so its disk
# layout is always the one this build writes (CBRD-26950).
# =============================================================================
add_test(NAME oos_setup_no_logging_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_isolated_db.sh" oosnologdb
)
set_tests_properties(oos_setup_no_logging_db
  PROPERTIES FIXTURES_SETUP OOS_NO_LOGGING_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

add_test(NAME oos_cleanup_no_logging_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cleanup_isolated_db.sh" oosnologdb
)
set_tests_properties(oos_cleanup_no_logging_db
  PROPERTIES FIXTURES_CLEANUP OOS_NO_LOGGING_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

add_test(NAME oos_setup_recovery_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_isolated_db.sh" oosrecoverydb
)
set_tests_properties(oos_setup_recovery_db
  PROPERTIES FIXTURES_SETUP OOS_RECOVERY_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

add_test(NAME oos_cleanup_recovery_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cleanup_isolated_db.sh" oosrecoverydb
)
set_tests_properties(oos_cleanup_recovery_db
  PROPERTIES FIXTURES_CLEANUP OOS_RECOVERY_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

add_test(NAME oos_setup_serial_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_isolated_db.sh" oosserialdb --db-page-size=4K --log-page-size=4K
)
set_tests_properties(oos_setup_serial_db
  PROPERTIES FIXTURES_SETUP OOS_SERIAL_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

add_test(NAME oos_cleanup_serial_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cleanup_isolated_db.sh" oosserialdb
)
set_tests_properties(oos_cleanup_serial_db
  PROPERTIES FIXTURES_CLEANUP OOS_SERIAL_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

# The storage format guard test patches the volume header of its own database and leaves it refused, so that
# this cleanup proves deletedb still removes a database that boot refuses.
add_test(NAME oos_setup_format_guard_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_isolated_db.sh" oosformatdb
)
set_tests_properties(oos_setup_format_guard_db
  PROPERTIES FIXTURES_SETUP OOS_FORMAT_GUARD_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

add_test(NAME oos_cleanup_format_guard_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cleanup_isolated_db.sh" oosformatdb
)
set_tests_properties(oos_cleanup_format_guard_db
  PROPERTIES FIXTURES_CLEANUP OOS_FORMAT_GUARD_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

# Boot refuses a database whose volume header lacks this build's storage format stamp (pre-OOS 11.5 builds).
add_test(NAME test_oos_storage_format_guard
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_storage_format_guard.sh" oosformatdb
)
set_tests_properties(test_oos_storage_format_guard
  PROPERTIES FIXTURES_REQUIRED OOS_FORMAT_GUARD_DB TIMEOUT 60
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME oos_setup_concurrent_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/setup_isolated_db.sh" oosconcurdb
)
set_tests_properties(oos_setup_concurrent_db
  PROPERTIES FIXTURES_SETUP OOS_CONCURRENT_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

add_test(NAME oos_cleanup_concurrent_db
  COMMAND ${CMAKE_COMMAND} -E env
    "CUBRID=$ENV{CUBRID}"
    "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
    "PATH=$ENV{PATH}"
    bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/cleanup_isolated_db.sh" oosconcurdb
)
set_tests_properties(oos_cleanup_concurrent_db
  PROPERTIES FIXTURES_CLEANUP OOS_CONCURRENT_DB RESOURCE_LOCK OOS_DATABASES_REGISTRY TIMEOUT 60
)

foreach(round RANGE 1 3)
  add_test(NAME test_oos_concurrent_insert_${round}
    COMMAND ${CMAKE_COMMAND} -E env
      "CUBRID=$ENV{CUBRID}"
      "CUBRID_DATABASES=$ENV{CUBRID_DATABASES}"
      "PATH=$ENV{PATH}"
      "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_oos_concurrent_insert.py"
      oosconcurdb "${CMAKE_CURRENT_BINARY_DIR}/concurrent-insert-${round}" --sessions 8 --rows 1500 --rounds 1
  )
  set_tests_properties(test_oos_concurrent_insert_${round}
    PROPERTIES FIXTURES_REQUIRED OOS_CONCURRENT_DB RESOURCE_LOCK OOS_CONCURRENT_DB TIMEOUT 60
  )
endforeach()

add_test(NAME test_oos_concurrent_insert_driver
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_oos_concurrent_insert_driver.py"
)
set_tests_properties(test_oos_concurrent_insert_driver PROPERTIES TIMEOUT 30)

# =============================================================================
# Register SA_MODE tests
# =============================================================================
add_test(NAME test_oos COMMAND test_oos)
add_test(NAME test_oos_delete COMMAND test_oos_delete)
# identity stamp contract: insert issues it, reads verify it, deletes require it (CBRD-26950).
add_test(NAME test_oos_identity_stamp COMMAND test_oos_identity_stamp)
# eager (non-MVCC) DML completes and diagnoses skipped OOS reclamation (CBRD-26950); the SQL-level
# test_oos_sql_eager_cleanup covers that unskipped eager reclamation actually reclaims.
add_test(NAME test_oos_eager_diagnostics COMMAND test_oos_eager_diagnostics)
# the identity stamp's logging precondition (CBRD-26950): distinct stamps across slot reuse need
# logged operations. Owns oosnologdb because log_set_no_logging() is process-global and one-way and
# what it writes is not recoverable.
add_test(NAME test_oos_no_logging COMMAND test_oos_no_logging)
set_tests_properties(test_oos_no_logging
  PROPERTIES FIXTURES_REQUIRED OOS_NO_LOGGING_DB RUN_SERIAL TRUE TIMEOUT 60
)
add_test(NAME test_oos_remove_file COMMAND test_oos_remove_file)
add_test(NAME test_oos_bestspace COMMAND test_oos_bestspace)
# growth-gate incremental sweep: delete bursts must not grow the OOS file on re-insert (CBRD-26786).
add_test(NAME test_oos_growth_sweep COMMAND test_oos_growth_sweep)
# pure-logic test for separating record metadata from MVCC lifecycle flags.
add_test(NAME test_oos_record_flags COMMAND test_oos_record_flags)
# Allocation failures are simulated within this binary; it needs no database fixture.
add_test(NAME test_oos_reference_errors COMMAND test_oos_reference_errors)
set_tests_properties(test_oos_reference_errors PROPERTIES TIMEOUT 30)
# pure-logic test for the bounded writer used by oos_read; no DB fixture.
add_test(NAME test_byte_span_writer COMMAND test_byte_span_writer)
# pure-logic guard that the OOS forward-delete rcvindex is TDE-gated (CBRD-26668); no DB fixture.
add_test(NAME test_oos_tde_gate COMMAND test_oos_tde_gate)
set_tests_properties(test_oos_record_flags test_byte_span_writer test_oos_tde_gate PROPERTIES TIMEOUT 30)

set_tests_properties(
  test_oos_delete test_oos_remove_file test_oos_bestspace test_oos_growth_sweep
  PROPERTIES FIXTURES_REQUIRED OOS_DB RESOURCE_LOCK OOS_SHARED_UNITTESTDB TIMEOUT 30
)
set_tests_properties(test_oos test_oos_identity_stamp test_oos_eager_diagnostics
  PROPERTIES FIXTURES_REQUIRED OOS_DB RESOURCE_LOCK OOS_SHARED_UNITTESTDB TIMEOUT 60
)

# =============================================================================
# Register SERVER_MODE tests
# =============================================================================
add_test(NAME test_oos_server COMMAND test_oos_server)
add_test(NAME test_oos_delete_server COMMAND test_oos_delete_server)
add_test(NAME test_oos_remove_file_server COMMAND test_oos_remove_file_server)
add_test(NAME test_oos_mock_vacuum_server COMMAND test_oos_mock_vacuum_server)
add_test(NAME test_oos_vacuum_server COMMAND test_oos_vacuum_server)
string(CONCAT oos_real_vacuum_rows_filter
  "OosRealVacuum.SingleRowDeleteDrainsCompletely:"
  "OosRealVacuum.MultiChunkChainsDrainCompletely:"
  "OosRealVacuum.UpdateStaleVersionDrainsNewSurvives:"
  "OosRealVacuum.MultiVersionForwardChainDrains:"
  "OosRealVacuum.ChunkBoundaryExactSizes:"
  "OosRealVacuum.ReVacuumAfterDrainIsIdempotent"
)
add_test(NAME test_oos_real_vacuum_rows COMMAND test_oos_real_vacuum_server
  "--gtest_filter=${oos_real_vacuum_rows_filter}")
string(CONCAT oos_real_vacuum_snapshots_filter
  "OosRealVacuum.SnapshotBlocksReclaimThenDrains:"
  "OosRealVacuum.TwoReadersOldestSnapshotGatesReclaim:"
  "OosRealVacuum.UpdateSnapshotBlocksOldVersionReclaim:"
  "OosRealVacuum.LiveRowNeverReclaimed:"
  "OosRealVacuum.VacuumWaitsForHeapHeaderWithoutHoldingLastPage"
)
add_test(NAME test_oos_real_vacuum_snapshots COMMAND test_oos_real_vacuum_server
  "--gtest_filter=${oos_real_vacuum_snapshots_filter}")

set_tests_properties(
  test_oos_server test_oos_delete_server test_oos_remove_file_server
  test_oos_mock_vacuum_server test_oos_vacuum_server
  PROPERTIES FIXTURES_REQUIRED OOS_DB RUN_SERIAL TRUE TIMEOUT 60
)

set_tests_properties(
  test_oos_real_vacuum_rows test_oos_real_vacuum_snapshots
  PROPERTIES FIXTURES_REQUIRED OOS_DB RUN_SERIAL TRUE TIMEOUT 60
)

# The server reads CUBRID_<PARAMETER> at every parameter load, so these runs have a short page latch timeout and a
# vacuum wait for the heap header can fail. Debug builds stop at that timeout, so fault injection 700000
# (FI_TEST_VACUUM_OOS_HEADER_WAIT_TIMEOUT) makes the wait fail at once instead. The list is set before the vacuum
# workers exist and never changes. UnrelatedHeapVacuumedWhileHeaderLookupKeepsFailing needs one vacuum worker
# (CUBRID_VACUUM_WORKER_COUNT=1). With ten, a second worker vacuums the unrelated heap's block while the first one
# retries the held header, so the case passes even if vacuum retries in place.
set(OOS_HEADER_HELD_ENV
  "CUBRID_PAGE_LATCH_TIMEOUT_IN_MSECS=2000;CUBRID_FAULT_INJECTION_IDS=700000;CUBRID_VACUUM_WORKER_COUNT=1")
string(CONCAT oos_real_vacuum_latch_timeout_filter
  "OosRealVacuumHeaderHeld.HomeRowOosReclaimedAfterHeaderLookupFailure:"
  "OosRealVacuumHeaderHeld.RelocatedRowOosReclaimedAfterHeaderLookupFailure:"
  "OosRealVacuumHeaderHeld.UnrelatedHeapVacuumedWhileHeaderLookupKeepsFailing:"
  "OosRealVacuumHeaderHeld.UnrelatedRowInSameBlockVacuumedWhileHeaderLookupKeepsFailing"
)
add_test(NAME test_oos_real_vacuum_latch_timeout
  COMMAND test_oos_real_vacuum_server "--gtest_filter=${oos_real_vacuum_latch_timeout_filter}"
)
string(CONCAT oos_real_vacuum_latch_timeout_retry_filter
  "OosRealVacuumHeaderHeld.FailingHeapCostsOneLookupPerRun:"
  "OosRealVacuumHeaderHeld.ImmediateFailureRerunsAtMostOncePerSecond"
)
add_test(NAME test_oos_real_vacuum_latch_timeout_retry
  COMMAND test_oos_real_vacuum_server "--gtest_filter=${oos_real_vacuum_latch_timeout_retry_filter}"
)
# Stopping the vacuum workers ends vacuum for the rest of the process, so this case runs alone.
add_test(NAME test_oos_real_vacuum_latch_timeout_shutdown
  COMMAND test_oos_real_vacuum_server --gtest_filter=OosRealVacuumHeaderHeld.Shutdown*
)
set_tests_properties(
  test_oos_real_vacuum_latch_timeout test_oos_real_vacuum_latch_timeout_retry
  test_oos_real_vacuum_latch_timeout_shutdown
  PROPERTIES FIXTURES_REQUIRED OOS_DB RUN_SERIAL TRUE TIMEOUT 60
    ENVIRONMENT "${OOS_HEADER_HELD_ENV}"
)

# Note: add_test vs. gtest_discover_tests
# - Prefer add_test if you want to share global setup/teardown, which speeds up test
# - Prefer gtest_discover_tests for isolation between tests, at the cost of test speed
# - Since we do not need isolation here, we use add_test above.

add_subdirectory(sql)

# Real standalone loader/workspace routes require installed utilities, like the DB fixtures.
# Each GoogleTest case owns a private registry and database; never use shared unittestdb here.
string(CONCAT oos_workspace_loader_filter
  "OosWorkspaceTest.StandaloneLoaderStoresOos:"
  "OosWorkspaceTest.LoaderRejectsOosBigoneButKeepsOrdinaryBigone:"
  "OosWorkspaceTest.LoaderPreservesForwardAndBackwardReferences"
)
add_test(NAME test_oos_workspace_loader COMMAND test_oos_workspace
  "--gtest_filter=${oos_workspace_loader_filter}")
string(CONCAT oos_workspace_insert_filter
  "OosWorkspaceTest.WorkspaceInsertUsesReservedOid:"
  "OosWorkspaceTest.InsertRollbackReclaimsFlushedChain"
)
add_test(NAME test_oos_workspace_insert COMMAND test_oos_workspace
  "--gtest_filter=${oos_workspace_insert_filter}")
string(CONCAT oos_workspace_storage_policy_filter
  "OosWorkspaceTest.PartitionsOwnSeparateOosFiles:"
  "OosWorkspaceTest.LoaderHonorsStoragePolicy"
)
add_test(NAME test_oos_workspace_storage_policy COMMAND test_oos_workspace
  "--gtest_filter=${oos_workspace_storage_policy_filter}")
string(CONCAT oos_workspace_update_filter
  "OosWorkspaceTest.WorkspaceUpdateRollbackCommitAndDelete:"
  "OosWorkspaceTest.SuccessfulNoLoggingLoadStoresOos"
)
add_test(NAME test_oos_workspace_update COMMAND test_oos_workspace
  "--gtest_filter=${oos_workspace_update_filter}")
string(CONCAT oos_workspace_load_failure_filter
  "OosWorkspaceTest.FailedUniqueLoadRollsBackWithoutOrphans:"
  "OosWorkspaceTest.FilteredDuplicateLeavesNoOrphanAndContinues"
)
add_test(NAME test_oos_workspace_load_failure COMMAND test_oos_workspace
  "--gtest_filter=${oos_workspace_load_failure_filter}")
string(CONCAT oos_workspace_lob_filter
  "OosWorkspaceTest.WorkspaceUpdatePreservesExternalLobFiles:"
  "OosWorkspaceTest.PartitionMovementTransfersOwnership"
)
add_test(NAME test_oos_workspace_lob COMMAND test_oos_workspace
  "--gtest_filter=${oos_workspace_lob_filter}")
set_tests_properties(
  test_oos_workspace_loader test_oos_workspace_insert test_oos_workspace_update
  test_oos_workspace_storage_policy test_oos_workspace_load_failure test_oos_workspace_lob
  PROPERTIES TIMEOUT 60
)

get_property(oos_tests DIRECTORY PROPERTY TESTS)
foreach(oos_test IN LISTS oos_tests)
  foreach(oos_fixture_property FIXTURES_REQUIRED FIXTURES_SETUP FIXTURES_CLEANUP)
    get_test_property(${oos_test} ${oos_fixture_property} oos_fixture)
    if(oos_fixture)
      set_tests_properties(${oos_test} PROPERTIES RUN_SERIAL TRUE)
    endif()
  endforeach()
endforeach()
set_property(TEST ${oos_tests} APPEND PROPERTY ENVIRONMENT
  "CUBRID_CALL_STACK_DUMP_ON_ERROR=no" "CUBRID_CALL_STACK_DUMP_ACTIVATION_LIST=null")
