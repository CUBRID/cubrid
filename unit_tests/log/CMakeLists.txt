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

project(test_log)

#
# test_checkpoint_info
#

set(TEST_CHKPT_INFO_SOURCES
  ${TRANSACTION_DIR}/log_checkpoint_info.cpp
  test_main_chkpt_info.cpp
  ${TEST_FAKE_DIR}/fake_thread_entry.cpp
  ${TRANSACTION_DIR}/client_credentials.cpp
  ${TRANSACTION_DIR}/log_lsa_utils.cpp
  ${BASE_DIR}/packer.cpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_CHKPT_INFO_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_checkpoint_info
  ${TEST_CHKPT_INFO_SOURCES}
  )

target_compile_options(test_checkpoint_info PRIVATE ${compile_warning_flags})
target_compile_definitions(test_checkpoint_info PRIVATE
  ${COMMON_DEFS} SERVER_MODE
  )
target_include_directories(test_checkpoint_info PRIVATE
  ${TEST_INCLUDES}
  )
add_dependencies(test_checkpoint_info
  ${CATCH2_TARGET}
  )
if(WIN32)
  target_link_libraries(test_checkpoint_info PRIVATE ws2_32)
endif(WIN32)

#
# test_meta_log
#
set(TEST_LOG_META_SOURCES
  ${BASE_DIR}/packer.cpp
  ${TRANSACTION_DIR}/log_meta.cpp
  test_main_log_meta.cpp
  )
set_source_files_properties(
  ${TEST_LOG_META_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
add_executable(test_meta_log
  ${TEST_LOG_META_SOURCES}
  )
target_compile_options(test_meta_log PRIVATE
  ${compile_warning_flags}
  )
target_compile_definitions(test_meta_log PRIVATE
  ${COMMON_DEFS}
  )
target_include_directories(test_meta_log PRIVATE
  ${TEST_INCLUDES}
  )
if(WIN32)
  target_link_libraries(test_meta_log PRIVATE ws2_32)
endif(WIN32)
add_dependencies(test_meta_log
  ${CATCH2_TARGET}
  )

#
# test_prior_list_serialize
#
set(TEST_PRIOR_LIST_SERIALIZE_SOURCES
  # Fake objects
  ${TEST_FAKE_DIR}/fake_error_manager.cpp
  ${TEST_FAKE_DIR}/fake_system_parameter.cpp

  # What is tested
  ${TRANSACTION_DIR}/log_append.cpp

  # Test
  test_main_prior_list_serialize.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_PRIOR_LIST_SERIALIZE_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_prior_list_serialize
  ${TEST_PRIOR_LIST_SERIALIZE_SOURCES}
  )

target_compile_options(test_prior_list_serialize PRIVATE
  ${compile_warning_flags}
  )
target_compile_definitions(test_prior_list_serialize PRIVATE
  ${COMMON_DEFS} SERVER_MODE
  )
target_include_directories(test_prior_list_serialize PRIVATE
  ${TEST_INCLUDES}
  )
add_dependencies(test_prior_list_serialize
  ${CATCH2_TARGET} ${LZ4_TARGET}
  )

#
# test_prior_sendrecv
#
set(TEST_PRIOR_SENDRECV_SOURCES
  ${TEST_FAKE_DIR}/fake_system_parameter.cpp
  ${TRANSACTION_DIR}/log_prior_send.cpp
  ${TRANSACTION_DIR}/log_prior_recv.cpp
  test_main_prior_sendrecv.cpp
  )

set_source_files_properties(
  ${TEST_PRIOR_SENDRECV_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_prior_sendrecv
  ${TEST_PRIOR_SENDRECV_SOURCES}
  )

target_compile_options(test_prior_sendrecv PRIVATE
  ${compile_warning_flags}
  )
target_compile_definitions(test_prior_sendrecv PRIVATE
  ${COMMON_DEFS} SERVER_MODE
  )
target_include_directories(test_prior_sendrecv PRIVATE
  ${TEST_INCLUDES}
  )
add_dependencies(test_prior_sendrecv
  ${CATCH2_TARGET} ${LZ4_TARGET}
  )

#
# test_log_lsa_utils
#
set(TEST_LOG_LSA_UTILS_SOURCES
  ${TRANSACTION_DIR}/log_lsa_utils.cpp
  ${BASE_DIR}/packer.cpp
  test_main_log_lsa_utils.cpp)

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_LOG_LSA_UTILS_SOURCES}
  PROPERTIES LANGUAGE CXX)

add_executable(test_log_lsa_utils
  ${TEST_LOG_LSA_UTILS_SOURCES}
  )

target_compile_options(test_log_lsa_utils PRIVATE
  ${compile_warning_flags}
  )
target_compile_definitions(test_log_lsa_utils PRIVATE
  ${COMMON_DEFS} SERVER_MODE
  )
target_include_directories(test_log_lsa_utils PRIVATE
  ${TEST_INCLUDES}
  )
if (WIN32)
 target_link_libraries(test_log_lsa_utils PRIVATE ws2_32)
endif ()
add_dependencies(test_log_lsa_utils
  ${CATCH2_TARGET}
  )

#
# test_log_recovery_parallel
#
set(TEST_LOG_RECOVERY_PARALLEL_SOURCES
  ${TEST_FAKE_DIR}/fake_system_parameter.cpp
  ${TRANSACTION_DIR}/log_recovery_redo_parallel.cpp
  ut_redo_job_impl.cpp
  ut_database.cpp
  test_main_log_recovery_parallel.cpp)

set (TEST_LOG_RECOVERY_PARALLEL_HEADERS
  ${TRANSACTION_DIR}/log_recovery_redo.hpp
  ${TRANSACTION_DIR}/log_recovery_redo_parallel.hpp
  ut_redo_job_impl.hpp
  ut_database.hpp)

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_LOG_RECOVERY_PARALLEL_SOURCES}
  PROPERTIES LANGUAGE CXX)

add_executable(test_log_recovery_parallel
  ${TEST_LOG_RECOVERY_PARALLEL_SOURCES}
  ${TEST_LOG_RECOVERY_PARALLEL_HEADERS}
  )
target_compile_options(test_log_recovery_parallel PRIVATE
  ${compile_warning_flags}
  )
target_compile_definitions(test_log_recovery_parallel PRIVATE
  ${COMMON_DEFS}
  SERVER_MODE
  )
target_include_directories(test_log_recovery_parallel PRIVATE
  ${TEST_INCLUDES}
  )
target_link_libraries(test_log_recovery_parallel PRIVATE test_common)
if(UNIX)
  target_link_libraries(test_log_recovery_parallel PRIVATE cubrid )
elseif(WIN32)
  target_link_libraries(test_log_recovery_parallel PRIVATE cubrid-win-lib )
else()
  message( SEND_ERROR "Recovery only works on UNIX-like/WIN32")
endif ()
