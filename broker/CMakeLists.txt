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

#set(BROKER_DEFS _BSD_SOURCE _XOPEN_SOURCE ASYNC_MODE CAS_BROKER)
set(BROKER_DEFS _XOPEN_SOURCE ASYNC_MODE CAS_BROKER)

set(CUB_BROKER_SOURCES
  ${BROKER_DIR}/broker.c
  ${BROKER_DIR}/broker_shm.c
  ${BROKER_DIR}/broker_error.c
  ${BROKER_DIR}/broker_util.c
  ${BROKER_DIR}/broker_process_size.c
  ${BROKER_DIR}/broker_max_heap.c
  ${BROKER_DIR}/broker_access_list.c
  ${BROKER_DIR}/broker_filename.c
  ${BROKER_DIR}/shard_metadata.c
  ${BROKER_DIR}/shard_shm.c
  ${BROKER_DIR}/broker_proxy_conn.c
  )

if(UNIX)
  list(APPEND CUB_BROKER_SOURCES ${BROKER_DIR}/broker_send_fd.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${CUB_BROKER_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
else(UNIX)
  list(APPEND CUB_BROKER_SOURCES ${BROKER_DIR}/broker_list.c)
  list(APPEND CUB_BROKER_SOURCES ${BROKER_DIR}/broker_wsa_init.c)
  list(APPEND CUB_BROKER_SOURCES ${BROKER_DIR}/cas_meta.c)
  list(APPEND CUB_BROKER_SOURCES ${BASE_DIR}/porting.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${CUB_BROKER_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND CUB_BROKER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(UNIX)
# cub_broker use WinMain() on Windows platform
add_executable(cub_broker WIN32 ${CUB_BROKER_SOURCES})
set_target_properties(cub_broker PROPERTIES PUBLIC_HEADER "${COMPAT_DIR}/dbtran_def.h;${BROKER_DIR}/cas_error.h")
target_compile_definitions(cub_broker PRIVATE ${BROKER_DEFS})
if(WIN32)
  target_link_libraries(cub_broker LINK_PRIVATE ws2_32)
endif(WIN32)
target_link_libraries(cub_broker LINK_PRIVATE cubridcs)
if(UNIX)
  target_link_libraries(cub_broker LINK_PRIVATE m)
endif(UNIX)

add_executable(cub_gateway WIN32 ${CUB_BROKER_SOURCES})
set_target_properties(cub_gateway PROPERTIES PUBLIC_HEADER "${COMPAT_DIR}/dbtran_def.h;${BROKER_DIR}/cas_error.h")
target_compile_definitions(cub_gateway PRIVATE ${BROKER_DEFS} FOR_ODBC_GATEWAY)
if(WIN32)
  target_link_libraries(cub_gateway LINK_PRIVATE ws2_32)
endif(WIN32)
target_link_libraries(cub_gateway LINK_PRIVATE cubridcs)
if(UNIX)
  target_link_libraries(cub_gateway LINK_PRIVATE m)
endif(UNIX)

set(BROKER_MONITOR_SOURCES
  ${BROKER_DIR}/broker_monitor.c
  ${BROKER_DIR}/broker_config.c
  ${BROKER_DIR}/broker_shm.c
  ${BROKER_DIR}/broker_error.c
  ${BROKER_DIR}/broker_util.c
  ${BROKER_DIR}/broker_process_size.c
  ${BROKER_DIR}/broker_max_heap.c
  ${BROKER_DIR}/broker_process_info.c
  ${BROKER_DIR}/broker_filename.c
  ${BROKER_DIR}/shard_metadata.c
  ${BROKER_DIR}/shard_shm.c
  ${BROKER_DIR}/cas_util.c
  ${BASE_DIR}/util_func.c
  )
SET_SOURCE_FILES_PROPERTIES(
  ${BROKER_MONITOR_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
if(WIN32)
  list(APPEND BROKER_MONITOR_SOURCES ${BASE_DIR}/porting.c)
  list(APPEND BROKER_MONITOR_SOURCES ${BASE_DIR}/rand.c)
  list(APPEND BROKER_MONITOR_SOURCES ${BROKER_DIR}/broker_list.c)
  list(APPEND BROKER_MONITOR_SOURCES ${BROKER_DIR}/broker_proxy_conn.c)
  
  SET_SOURCE_FILES_PROPERTIES(
    ${BASE_DIR}/porting.c
    ${BASE_DIR}/rand.c
    ${BROKER_DIR}/broker_list.c
    ${BROKER_DIR}/broker_proxy_conn.c
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND BROKER_MONITOR_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(broker_monitor ${BROKER_MONITOR_SOURCES})
target_compile_definitions(broker_monitor PRIVATE ${BROKER_DEFS})
target_link_libraries(broker_monitor LINK_PRIVATE cubridcs)
if(WIN32)
  target_link_libraries(broker_monitor LINK_PRIVATE ws2_32)
endif(WIN32)

add_executable(gateway_monitor ${BROKER_MONITOR_SOURCES})
target_compile_definitions(gateway_monitor PRIVATE ${BROKER_DEFS} FOR_ODBC_GATEWAY)
target_link_libraries(gateway_monitor LINK_PRIVATE cubridcs)
if(WIN32)
  target_link_libraries(gateway_monitor LINK_PRIVATE ws2_32)
endif(WIN32)


set(BROKER_CHANGER_SOURCES
  ${BROKER_DIR}/broker_changer.c
  ${BROKER_DIR}/broker_admin_pub.c
  ${BROKER_DIR}/broker_shm.c
  ${BROKER_DIR}/broker_error.c
  ${BROKER_DIR}/broker_process_size.c
  ${BROKER_DIR}/broker_config.c
  ${BROKER_DIR}/broker_util.c
  ${BROKER_DIR}/broker_max_heap.c
  ${BROKER_DIR}/broker_filename.c
  ${BROKER_DIR}/broker_acl.c
  ${BROKER_DIR}/shard_metadata.c
  ${BROKER_DIR}/shard_shm.c
  ${BROKER_DIR}/shard_key_func.c
  )
if(WIN32)
  list(APPEND BROKER_CHANGER_SOURCES ${BASE_DIR}/porting.c)
  list(APPEND BROKER_CHANGER_SOURCES ${BROKER_DIR}/broker_list.c)
  list(APPEND BROKER_CHANGER_SOURCES ${BROKER_DIR}/broker_wsa_init.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${BROKER_CHANGER_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND BROKER_CHANGER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
else(WIN32)
  SET_SOURCE_FILES_PROPERTIES(
    ${BROKER_CHANGER_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
endif(WIN32)
add_executable(broker_changer ${BROKER_CHANGER_SOURCES})
target_compile_definitions(broker_changer PRIVATE ${BROKER_DEFS})
target_link_libraries(broker_changer LINK_PRIVATE cubridcs)
if(UNIX)
  target_link_libraries(broker_changer LINK_PRIVATE rt)
  target_link_libraries(broker_changer LINK_PRIVATE m)
else(UNIX)
  target_link_libraries(broker_changer LINK_PRIVATE ws2_32)
  if(MSVC_VERSION LESS 1600)
    # for VS 2008 (9.0) or less version
    set_target_properties(broker_changer PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
  else(MSVC_VERSION LESS 1600)
    set_target_properties(broker_changer PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
  endif(MSVC_VERSION LESS 1600)
endif(UNIX)


set(CUBRID_BROKER_SOURCES
  ${BASE_DIR}/porting.c
  ${BROKER_DIR}/broker_config.c
  ${BROKER_DIR}/broker_admin.c
  ${BROKER_DIR}/broker_shm.c
  ${BROKER_DIR}/broker_error.c
  ${BROKER_DIR}/broker_util.c
  ${BROKER_DIR}/broker_admin_pub.c
  ${BROKER_DIR}/broker_process_size.c
  ${BROKER_DIR}/broker_max_heap.c
  ${BROKER_DIR}/broker_filename.c
  ${BROKER_DIR}/broker_acl.c
  ${BROKER_DIR}/shard_metadata.c
  ${BROKER_DIR}/shard_shm.c
  ${BROKER_DIR}/shard_key_func.c
  ${BASE_DIR}/util_func.c
  )
SET_SOURCE_FILES_PROPERTIES(
  ${CUBRID_BROKER_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
if(WIN32)
  list(APPEND CUBRID_BROKER_SOURCES ${BASE_DIR}/rand.c)
  list(APPEND CUBRID_BROKER_SOURCES ${BROKER_DIR}/broker_list.c)
  list(APPEND CUBRID_BROKER_SOURCES ${BROKER_DIR}/broker_wsa_init.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${BASE_DIR}/rand.c
    ${BROKER_DIR}/broker_list.c
    ${BROKER_DIR}/broker_wsa_init.c
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND CUBRID_BROKER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cubrid_broker ${CUBRID_BROKER_SOURCES})
target_compile_definitions(cubrid_broker PRIVATE ${BROKER_DEFS})
target_link_libraries(cubrid_broker LINK_PRIVATE cubridcs)
if(UNIX)
  target_link_libraries(cubrid_broker LINK_PRIVATE m)
endif(UNIX)
if(WIN32)
  target_link_libraries(cubrid_broker LINK_PRIVATE ws2_32)
  if(MSVC_VERSION LESS 1600)
    # for VS 2008 (9.0) or less version
    set_target_properties(cubrid_broker PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
  else(MSVC_VERSION LESS 1600)
    set_target_properties(cubrid_broker PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
  endif(MSVC_VERSION LESS 1600)
endif(WIN32)

add_executable(cubrid_gateway ${CUBRID_BROKER_SOURCES})
target_compile_definitions(cubrid_gateway PRIVATE ${BROKER_DEFS} FOR_ODBC_GATEWAY)
target_link_libraries(cubrid_gateway LINK_PRIVATE cubridcs)
if(UNIX)
  target_link_libraries(cubrid_gateway LINK_PRIVATE m)
endif(UNIX)
if(WIN32)
  target_link_libraries(cubrid_gateway LINK_PRIVATE ws2_32)
  if(MSVC_VERSION LESS 1600)
    # for VS 2008 (9.0) or less version
    set_target_properties(cubrid_gateway PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
  else(MSVC_VERSION LESS 1600)
    set_target_properties(cubrid_gateway PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
  endif(MSVC_VERSION LESS 1600)
endif(WIN32)


set(CUB_CAS_SOURCES
  ${BROKER_DIR}/cas.c
  ${BROKER_DIR}/cas_network.c
  ${BROKER_DIR}/cas_net_buf.c
  ${BROKER_DIR}/cas_function.c
  ${BROKER_DIR}/cas_execute.c
  ${BROKER_DIR}/cas_handle.c
  ${BROKER_DIR}/broker_util.c
  ${BROKER_DIR}/cas_str_like.c
  ${BROKER_DIR}/cas_xa.c
  ${BROKER_DIR}/cas_schema_info.c
  ${BROKER_DIR}/cas_log.c
  ${BROKER_DIR}/broker_shm.c
  ${BROKER_DIR}/cas_sql_log2.c
  ${BROKER_DIR}/broker_filename.c
  ${BROKER_DIR}/cas_error.c
  ${BROKER_DIR}/broker_acl.c
  ${BROKER_DIR}/cas_meta.c
  ${BROKER_DIR}/cas_ssl.c
  ${BROKER_DIR}/cas_util.c
  ${BASE_DIR}/ddl_log.c
  )

if(UNIX)
  list(APPEND CUB_CAS_SOURCES ${BROKER_DIR}/broker_error.c)
  list(APPEND CUB_CAS_SOURCES ${BROKER_DIR}/broker_process_size.c)
  list(APPEND CUB_CAS_SOURCES ${BROKER_DIR}/broker_recv_fd.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${CUB_CAS_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
else(UNIX)
  SET_SOURCE_FILES_PROPERTIES(
    ${CUB_CAS_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND CUB_CAS_SOURCES ${BASE_DIR}/chartype.c)
  list(APPEND CUB_CAS_SOURCES ${BASE_DIR}/porting.c)
  list(APPEND CUB_CAS_SOURCES ${BROKER_DIR}/broker_list.c)
  list(APPEND CUB_CAS_SOURCES ${BROKER_DIR}/broker_wsa_init.c)
  list(APPEND CUB_CAS_SOURCES ${BROKER_DIR}/cas_util.c)
  
  SET_SOURCE_FILES_PROPERTIES(
    ${BASE_DIR}/porting.c
    ${BROKER_DIR}/broker_list.c
    ${BROKER_DIR}/broker_wsa_init.c
    ${BROKER_DIR}/cas_util.c
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND CUB_CAS_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(UNIX)

add_executable(cub_cas WIN32 ${CUB_CAS_SOURCES})
target_compile_definitions(cub_cas PRIVATE ${BROKER_DEFS} CAS_CUBRID)
target_include_directories(cub_cas PRIVATE ${EP_INCLUDES})
target_link_libraries(cub_cas LINK_PRIVATE cubridcs)
if(UNIX)
  target_link_libraries(cub_cas LINK_PRIVATE rt)
else(UNIX)
  target_link_libraries(cub_cas LINK_PRIVATE ws2_32 dbghelp ${LIBOPENSSL_LIBS})
endif(UNIX)


set(CUB_CAS_CGW_SOURCES
  ${BROKER_DIR}/cas.c
  ${BROKER_DIR}/cas_network.c
  ${BROKER_DIR}/cas_net_buf.c
  ${BROKER_DIR}/cas_function.c
  ${BROKER_DIR}/cas_execute.c
  ${BROKER_DIR}/cas_handle.c
  ${BROKER_DIR}/broker_util.c
  ${BROKER_DIR}/cas_str_like.c
  ${BROKER_DIR}/cas_xa.c
  ${BROKER_DIR}/cas_schema_info.c
  ${BROKER_DIR}/cas_log.c
  ${BROKER_DIR}/broker_shm.c
  ${BROKER_DIR}/cas_sql_log2.c
  ${BROKER_DIR}/broker_filename.c
  ${BROKER_DIR}/cas_error.c
  ${BROKER_DIR}/broker_acl.c
  ${BROKER_DIR}/cas_meta.c
  ${BROKER_DIR}/cas_ssl.c
  ${BROKER_DIR}/cas_cgw.c
  ${BROKER_DIR}/cas_util.c
  ${BASE_DIR}/ddl_log.c
  )

if(UNIX)
  list(APPEND CUB_CAS_CGW_SOURCES ${BROKER_DIR}/broker_error.c)
  list(APPEND CUB_CAS_CGW_SOURCES ${BROKER_DIR}/broker_process_size.c)
  list(APPEND CUB_CAS_CGW_SOURCES ${BROKER_DIR}/broker_recv_fd.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${CUB_CAS_CGW_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
else(UNIX)
  SET_SOURCE_FILES_PROPERTIES(
    ${CUB_CAS_CGW_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND CUB_CAS_CGW_SOURCES ${BASE_DIR}/chartype.c)
  list(APPEND CUB_CAS_CGW_SOURCES ${BASE_DIR}/porting.c)
  list(APPEND CUB_CAS_CGW_SOURCES ${BROKER_DIR}/broker_list.c)
  list(APPEND CUB_CAS_CGW_SOURCES ${BROKER_DIR}/broker_wsa_init.c)
  list(APPEND CUB_CAS_CGW_SOURCES ${BROKER_DIR}/cas_util.c)
  
  SET_SOURCE_FILES_PROPERTIES(
    ${BASE_DIR}/porting.c
    ${BROKER_DIR}/broker_list.c
    ${BROKER_DIR}/broker_wsa_init.c
    ${BROKER_DIR}/cas_util.c
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND CUB_CAS_CGW_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(UNIX)

add_executable(cub_cas_cgw WIN32 ${CUB_CAS_CGW_SOURCES})
target_compile_definitions(cub_cas_cgw PRIVATE ${BROKER_DEFS} CAS_FOR_CGW CAS_CUBRID)
target_include_directories(cub_cas_cgw PRIVATE ${EP_INCLUDES})
target_link_libraries(cub_cas_cgw LINK_PRIVATE cubridcs)
if(UNIX)
  target_link_libraries(cub_cas_cgw LINK_PRIVATE rt ${LIBUNIXODBC_LIBS})
else(UNIX)
  target_link_libraries(cub_cas_cgw LINK_PRIVATE ws2_32 dbghelp ${LIBOPENSSL_LIBS} odbc32 odbccp32)
endif(UNIX)

set(CUB_PROXY_SOURCES
  ${BROKER_DIR}/shard_proxy.c
  ${BROKER_DIR}/shard_proxy_io.c
  ${BROKER_DIR}/shard_proxy_function.c
  ${BROKER_DIR}/broker_process_size.c
  ${BROKER_DIR}/broker_shm.c
  ${BROKER_DIR}/broker_filename.c
  ${BROKER_DIR}/broker_error.c
  ${BROKER_DIR}/broker_util.c
  ${BROKER_DIR}/shard_metadata.c
  ${BROKER_DIR}/cas_net_buf.c
  ${BROKER_DIR}/shard_statement.c
  ${BROKER_DIR}/shard_shm.c
  ${BROKER_DIR}/shard_proxy_queue.c
  ${BROKER_DIR}/shard_proxy_handler.c
  ${BROKER_DIR}/shard_parser.c
  ${BROKER_DIR}/shard_key_func.c
  ${BROKER_DIR}/shard_proxy_log.c
  ${BROKER_DIR}/cas_util.c
  ${BROKER_DIR}/broker_acl.c
  ${BROKER_DIR}/cas_meta.c
  )
if(UNIX)
  list(APPEND CUB_PROXY_SOURCES ${BROKER_DIR}/broker_recv_fd.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${CUB_PROXY_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
else(UNIX)
  list(APPEND CUB_PROXY_SOURCES ${BASE_DIR}/porting.c)
  list(APPEND CUB_PROXY_SOURCES ${BROKER_DIR}/broker_filename.c)
  list(APPEND CUB_PROXY_SOURCES ${BROKER_DIR}/broker_list.c)
  list(APPEND CUB_PROXY_SOURCES ${BROKER_DIR}/broker_wsa_init.c)
  list(APPEND CUB_PROXY_SOURCES ${BROKER_DIR}/cas_util.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${CUB_PROXY_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND CUB_PROXY_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(UNIX)
add_executable(cub_proxy ${CUB_PROXY_SOURCES})
set_target_properties(cub_proxy PROPERTIES PUBLIC_HEADER "${BROKER_DIR}/shard_key.h")
target_compile_definitions(cub_proxy PRIVATE ${BROKER_DEFS} CUB_PROXY)
target_include_directories(cub_proxy PRIVATE ${EP_INCLUDES})
target_link_libraries(cub_proxy LINK_PRIVATE cubridcs)
if(WIN32)
  target_link_libraries(cub_proxy LINK_PRIVATE ws2_32)
endif(WIN32)
if(UNIX)
  target_link_libraries(cub_proxy LINK_PRIVATE m)
endif(UNIX)


if(WITH_CCI)
  set(BROKER_LOG_CONVERTER_SOURCES
    ${BROKER_DIR}/broker_log_converter.c
    ${BROKER_DIR}/broker_log_util.c
    ${BROKER_DIR}/log_top_string.c
    )
  SET_SOURCE_FILES_PROPERTIES(
    ${BROKER_LOG_CONVERTER_SOURCES}
    PROPERTIES LANGUAGE CXX
    )
  if(WIN32)
    list(APPEND BROKER_LOG_CONVERTER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
  endif(WIN32)
  add_executable(broker_log_converter ${BROKER_LOG_CONVERTER_SOURCES})
  target_compile_definitions(broker_log_converter PRIVATE ${BROKER_DEFS} BROKER_LOG_CONVERTER)
  target_link_libraries(broker_log_converter LINK_PRIVATE cubridcs)
  if(WIN32)
    target_link_libraries(broker_log_converter LINK_PRIVATE ws2_32)
  endif(WIN32)

  set(BROKER_LOG_RUNNER_SOURCES
    ${BROKER_DIR}/cas_runner.c
    ${BROKER_DIR}/broker_log_util.c
    ${BROKER_DIR}/log_top_string.c
    )
  if(WIN32)
    list(APPEND BROKER_LOG_RUNNER_SOURCES ${BASE_DIR}/porting.c)
    SET_SOURCE_FILES_PROPERTIES(
      ${BROKER_LOG_RUNNER_SOURCES}
      PROPERTIES LANGUAGE CXX
    )
    list(APPEND BROKER_LOG_RUNNER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
  else()
    SET_SOURCE_FILES_PROPERTIES(
      ${BROKER_LOG_RUNNER_SOURCES}
      PROPERTIES LANGUAGE CXX
    )
  endif(WIN32)
  add_executable(broker_log_runner ${BROKER_LOG_RUNNER_SOURCES})
  target_compile_definitions(broker_log_runner PRIVATE ${BROKER_DEFS} BROKER_LOG_RUNNER)
  target_link_libraries(broker_log_runner LINK_PRIVATE cubridcs cascci)
  if(WIN32)
    target_link_libraries(broker_log_runner LINK_PRIVATE ws2_32)
  endif(WIN32)
  if(UNIX)
    target_link_libraries(broker_log_runner LINK_PRIVATE m)
  endif(UNIX)


  set(BROKER_TESTER_SOURCES
    ${BROKER_DIR}/broker_tester.c
    ${BROKER_DIR}/broker_config.c
    ${BROKER_DIR}/broker_shm.c
    ${BROKER_DIR}/broker_error.c
    ${BROKER_DIR}/broker_util.c
    ${BROKER_DIR}/broker_filename.c
    ${BROKER_DIR}/shard_metadata.c
    ${BROKER_DIR}/shard_shm.c
    )
  if(WIN32)
    list(APPEND BROKER_TESTER_SOURCES ${BASE_DIR}/porting.c)
    list(APPEND BROKER_TESTER_SOURCES ${BROKER_DIR}/broker_filename.c)
    list(APPEND BROKER_TESTER_SOURCES ${BROKER_DIR}/broker_list.c)
    list(APPEND BROKER_TESTER_SOURCES ${BROKER_DIR}/broker_util.c)
    SET_SOURCE_FILES_PROPERTIES(
      ${BROKER_TESTER_SOURCES}
      PROPERTIES LANGUAGE CXX
    )
    list(APPEND BROKER_TESTER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
  else()
    SET_SOURCE_FILES_PROPERTIES(
      ${BROKER_TESTER_SOURCES}
      PROPERTIES LANGUAGE CXX
    )
  endif(WIN32)
  add_executable(broker_tester ${BROKER_TESTER_SOURCES})
  target_compile_definitions(broker_tester PRIVATE ${BROKER_DEFS} BROKER_TESTER)
  target_link_libraries(broker_tester LINK_PRIVATE cubridcs cascci)
  if(WIN32)
    target_link_libraries(broker_tester LINK_PRIVATE ws2_32)
  endif(WIN32)
endif(WITH_CCI)

set(BROKER_LOG_TOP_SOURCES
  ${BROKER_DIR}/broker_log_top.c
  ${BROKER_DIR}/cas_query_info.c
  ${BROKER_DIR}/broker_log_time.c
  ${BROKER_DIR}/broker_log_sql_list.c
  ${BROKER_DIR}/broker_log_util.c
  ${BROKER_DIR}/log_top_string.c
  ${BROKER_DIR}/broker_log_top_tran.c
  )
SET_SOURCE_FILES_PROPERTIES(
  ${BROKER_LOG_TOP_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
if(WIN32)
  list(APPEND BROKER_LOG_TOP_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(broker_log_top ${BROKER_LOG_TOP_SOURCES})
target_compile_definitions(broker_log_top PRIVATE ${BROKER_DEFS} BROKER_LOG_TOP)
target_link_libraries(broker_log_top LINK_PRIVATE cubridcs)

if(WITH_CCI)
  set(CUBRID_REPLAY_SOURCES
    ${BROKER_DIR}/broker_log_replay.c
    ${BROKER_DIR}/broker_log_util.c
    ${BROKER_DIR}/log_top_string.c
    )
  SET_SOURCE_FILES_PROPERTIES(
      ${CUBRID_REPLAY_SOURCES}
      PROPERTIES LANGUAGE CXX
    )
  if(WIN32)
    list(APPEND CUBRID_REPLAY_SOURCES ${BASE_DIR}/porting.c)
    SET_SOURCE_FILES_PROPERTIES(
      ${BASE_DIR}/porting.c
      PROPERTIES LANGUAGE CXX
    )
    list(APPEND CUBRID_REPLAY_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
  endif(WIN32)
  add_executable(cubrid_replay ${CUBRID_REPLAY_SOURCES})
  target_compile_definitions(cubrid_replay PRIVATE ${BROKER_DEFS} CUBRID_REPLAY)
  target_link_libraries(cubrid_replay LINK_PRIVATE cubridcs cascci)
  if(WIN32)
    target_link_libraries(cubrid_replay LINK_PRIVATE ws2_32)
  endif(WIN32)
endif(WITH_CCI)

set(BROKERADMIN_SOURCES
  ${BROKER_DIR}/broker_admin_so.c
  ${BROKER_DIR}/broker_admin_pub.c
  ${BROKER_DIR}/broker_shm.c
  ${BROKER_DIR}/broker_error.c
  ${BROKER_DIR}/broker_util.c
  ${BROKER_DIR}/broker_config.c
  ${BROKER_DIR}/broker_max_heap.c
  ${BROKER_DIR}/broker_process_size.c
  ${BROKER_DIR}/broker_process_info.c
  ${BROKER_DIR}/broker_filename.c
  ${BROKER_DIR}/broker_acl.c
  ${BROKER_DIR}/shard_metadata.c
  ${BROKER_DIR}/shard_shm.c
  ${BROKER_DIR}/shard_key_func.c
  )
SET_SOURCE_FILES_PROPERTIES(
  ${BROKERADMIN_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
if(WIN32)
  list(APPEND BROKERADMIN_SOURCES ${BASE_DIR}/porting.c)
  list(APPEND BROKERADMIN_SOURCES ${BROKER_DIR}/broker_list.c)
  list(APPEND BROKERADMIN_SOURCES ${BROKER_DIR}/broker_wsa_init.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${BASE_DIR}/porting.c
    ${BROKER_DIR}/broker_list.c
    ${BROKER_DIR}/broker_wsa_init.c
    PROPERTIES LANGUAGE CXX
  )
endif(WIN32)
add_library(brokeradmin SHARED ${BROKERADMIN_SOURCES})
set_target_properties(brokeradmin PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")
target_compile_definitions(brokeradmin PRIVATE ${BROKER_DEFS} _UC_ADMIN_SO_)
target_link_libraries(brokeradmin LINK_PRIVATE cubridcs)
if(WIN32)
  target_link_libraries(brokeradmin LINK_PRIVATE ws2_32)
endif(WIN32)

if(WITH_CCI)
  install(TARGETS
    broker_log_converter
    broker_log_runner
    broker_tester
    cubrid_replay
    RUNTIME DESTINATION ${CUBRID_BINDIR} COMPONENT Application
    LIBRARY DESTINATION ${CUBRID_LIBDIR} COMPONENT Library
    PUBLIC_HEADER DESTINATION ${CUBRID_INCLUDEDIR} COMPONENT Header
    )
endif(WITH_CCI)

install(TARGETS
  cub_broker
  cub_gateway
  broker_monitor
  gateway_monitor
  broker_changer
  cubrid_broker
  cubrid_gateway
  cub_cas
  cub_cas_cgw
  cub_proxy
  broker_log_top
  brokeradmin
  RUNTIME DESTINATION ${CUBRID_BINDIR} COMPONENT Application
  LIBRARY DESTINATION ${CUBRID_LIBDIR} COMPONENT Library
  PUBLIC_HEADER DESTINATION ${CUBRID_INCLUDEDIR} COMPONENT Header
  )


# TODO: compat uc script?

# install pdb files for debugging on windows
if(WIN32)
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/
    DESTINATION ${CUBRID_BINDIR} COMPONENT Debuginfo
    FILES_MATCHING PATTERN "*.pdb"
    )
endif(WIN32)
