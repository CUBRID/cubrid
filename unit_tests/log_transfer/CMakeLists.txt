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

project(test_log_transfer)

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
target_compile_options(test_meta_log PRIVATE ${compile_warning_flags})
target_compile_definitions(test_meta_log PRIVATE ${COMMON_DEFS})
target_include_directories(test_meta_log PRIVATE ${TEST_INCLUDES})
if(WIN32)
  target_link_libraries(test_meta_log PRIVATE ws2_32)
endif(WIN32)
add_dependencies(test_meta_log ${CATCH2_TARGET})

#
# test_prior_list_serialize
# 
set(TEST_PRIOR_LIST_SERIALIZE_SOURCES
  # Fake objects
  ${FAKE_DIR}/fake_error_manager.cpp
  ${FAKE_DIR}/fake_system_parameter.cpp

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

target_compile_options(test_prior_list_serialize PRIVATE ${compile_warning_flags})
target_compile_definitions(test_prior_list_serialize PRIVATE
  ${COMMON_DEFS} SERVER_MODE
  )
target_include_directories(test_prior_list_serialize PRIVATE
  ${TEST_INCLUDES}
  )
add_dependencies(test_prior_list_serialize
  ${CATCH2_TARGET}
  )
  
#
# test_prior_sendrecv
#
set(TEST_PRIOR_SENDRECV_SOURCES
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
  ${CATCH2_TARGET}
  )

#
# test_main_async_log_page_fetcher
# 

set(TEST_ASYNC_LOG_PAGE_FETCHER_SOURCES
  ${TRANSACTION_DIR}/log_page_async_fetcher.cpp
  test_main_async_log_page_fetcher.cpp)

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_ASYNC_LOG_PAGE_FETCHER_SOURCES}
  PROPERTIES LANGUAGE CXX)

add_executable(test_async_log_page_fetcher
  ${TEST_ASYNC_LOG_PAGE_FETCHER_SOURCES}
)

target_compile_options(test_async_log_page_fetcher PRIVATE
  ${compile_warning_flags}
)

target_compile_definitions(test_async_log_page_fetcher PRIVATE
  ${COMMON_DEFS} SERVER_MODE
)

target_include_directories(test_async_log_page_fetcher PRIVATE
  ${TEST_INCLUDES}
)

target_link_libraries(test_async_log_page_fetcher fake_thread_utils)

add_dependencies(test_async_log_page_fetcher
  ${CATCH2_TARGET}
)
