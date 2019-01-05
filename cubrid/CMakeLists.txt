#
# Copyright (C) 2016 Search Solution Corporation. All rights reserved.
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

set(COMPAT_SOURCES
  ${COMPAT_DIR}/cnv.c
  ${COMPAT_DIR}/cnvlex.c
  ${COMPAT_DIR}/db_date.c
  ${COMPAT_DIR}/db_elo.c
  ${COMPAT_DIR}/db_json.cpp
  ${COMPAT_DIR}/db_macro.c
  ${COMPAT_DIR}/db_set.c
  ${COMPAT_DIR}/db_value_printer.cpp
  )
set (COMPAT_HEADERS
  ${COMPAT_DIR}/dbtype_def.h
  ${COMPAT_DIR}/dbtype_function.i
  ${COMPAT_DIR}/dbtype_function.h
  )

set(THREAD_SOURCES
  ${THREAD_DIR}/critical_section.c
  ${THREAD_DIR}/critical_section_tracker.cpp
  ${THREAD_DIR}/thread_daemon.cpp
  ${THREAD_DIR}/thread_entry.cpp
  ${THREAD_DIR}/thread_entry_task.cpp
  ${THREAD_DIR}/thread_looper.cpp
  ${THREAD_DIR}/thread_manager.cpp
  ${THREAD_DIR}/thread_waiter.cpp
  ${THREAD_DIR}/thread_worker_pool.cpp
  )
set(THREAD_HEADERS
  ${THREAD_DIR}/critical_section_tracker.hpp
  ${THREAD_DIR}/thread_compat.hpp
  ${THREAD_DIR}/thread_daemon.hpp
  ${THREAD_DIR}/thread_entry.hpp
  ${THREAD_DIR}/thread_entry_task.hpp
  ${THREAD_DIR}/thread_task.hpp
  ${THREAD_DIR}/thread_looper.hpp
  ${THREAD_DIR}/thread_manager.hpp
  ${THREAD_DIR}/thread_waiter.hpp
  ${THREAD_DIR}/thread_worker_pool.hpp
  )

set(STREAM_SOURCES
  ${BASE_DIR}/cubstream.cpp
  ${BASE_DIR}/multi_thread_stream.cpp
  )
set (STREAM_HEADERS
  ${BASE_DIR}/bip_buffer.hpp
  ${BASE_DIR}/collapsable_circular_queue.hpp
  ${BASE_DIR}/cubstream.hpp
  ${BASE_DIR}/multi_thread_stream.hpp
  ${BASE_DIR}/stream_entry.hpp
  ${BASE_DIR}/stream_io.hpp
  )

set(BASE_SOURCES
  ${BASE_DIR}/adjustable_array.c
  ${BASE_DIR}/area_alloc.c
  ${BASE_DIR}/base64.c
  ${BASE_DIR}/binaryheap.c
  ${BASE_DIR}/bit.c
  ${BASE_DIR}/chartype.c
  ${BASE_DIR}/condition_handler.c
  ${BASE_DIR}/databases_file.c
  ${BASE_DIR}/dtoa.c
  ${BASE_DIR}/dynamic_array.c	
  ${BASE_DIR}/encryption.c
  ${BASE_DIR}/environment_variable.c
  ${BASE_DIR}/error_context.cpp
  ${BASE_DIR}/error_manager.c
  ${BASE_DIR}/event_log.c
  ${BASE_DIR}/fault_injection.c
  ${BASE_DIR}/fileline_location.cpp
  ${BASE_DIR}/fixed_alloc.c
  ${BASE_DIR}/ini_parser.c
  ${BASE_DIR}/intl_support.c
  ${BASE_DIR}/language_support.c
  ${BASE_DIR}/locale_support.c
  ${BASE_DIR}/lock_free.c
  ${BASE_DIR}/md5.c
  ${BASE_DIR}/mem_block.cpp
  ${BASE_DIR}/memory_alloc.c
  ${BASE_DIR}/memory_hash.c
  ${BASE_DIR}/memory_private_allocator.cpp
  ${BASE_DIR}/message_catalog.c
  ${BASE_DIR}/misc_string.c
  ${BASE_DIR}/mprec.c
  ${BASE_DIR}/object_representation_sr.c
  ${BASE_DIR}/packer.cpp
  ${BASE_DIR}/get_clockfreq.c
  ${BASE_DIR}/perf.cpp
  ${BASE_DIR}/pinnable_buffer.cpp
  ${BASE_DIR}/pinning.cpp
  ${BASE_DIR}/perf_monitor.c
  ${BASE_DIR}/porting.c
  ${BASE_DIR}/process_util.c
  ${BASE_DIR}/release_string.c
  ${BASE_DIR}/resource_tracker.cpp
  ${BASE_DIR}/sha1.c
  ${BASE_DIR}/stack_dump.c
  ${BASE_DIR}/string_buffer.cpp
  ${BASE_DIR}/system_parameter.c
  ${BASE_DIR}/tsc_timer.c
  ${BASE_DIR}/tz_support.c
  ${BASE_DIR}/uca_support.c
  ${BASE_DIR}/unicode_support.c
  ${BASE_DIR}/util_func.c
  ${BASE_DIR}/xml_parser.c
  )
set (BASE_HEADERS
  ${BASE_DIR}/error_code.h
  ${BASE_DIR}/error_context.hpp
  ${BASE_DIR}/error_manager.h
  ${BASE_DIR}/extensible_array.hpp
  ${BASE_DIR}/fileline_location.hpp
  ${BASE_DIR}/mem_block.hpp
  ${BASE_DIR}/memory_private_allocator.hpp
  ${BASE_DIR}/packer.hpp
  ${BASE_DIR}/perf.hpp
  ${BASE_DIR}/perf_def.hpp
  ${BASE_DIR}/pinning.hpp
  ${BASE_DIR}/pinnable_buffer.hpp
  ${BASE_DIR}/packable_object.hpp
  ${BASE_DIR}/process_util.h
  ${BASE_DIR}/resource_tracker.hpp
  ${BASE_DIR}/string_buffer.hpp
  )

set(EXECUTABLE_SOURCES
  ${EXECUTABLES_DIR}/util_common.c
  )

set(HEAPLAYER_SOURCES
  ${HEAPLAYERS_DIR}/customheaps.cpp
  ${HEAPLAYERS_DIR}/lea_heap.c
  )

set(CONNECTION_SOURCES
  ${CONNECTION_DIR}/connection_sr.c
  ${CONNECTION_DIR}/connection_list_sr.c
  ${CONNECTION_DIR}/connection_globals.c
  ${CONNECTION_DIR}/server_support.c
  ${CONNECTION_DIR}/connection_support.c
  )

set(COMMUNICATION_SOURCES
  ${COMMUNICATION_DIR}/network_interface_sr.c
  ${COMMUNICATION_DIR}/network_sr.c
  ${COMMUNICATION_DIR}/communication_channel.cpp
  ${COMMUNICATION_DIR}/communication_server_channel.cpp
  ${COMMUNICATION_DIR}/stream_transfer_receiver.cpp
  ${COMMUNICATION_DIR}/stream_transfer_sender.cpp
  )

set(COMMUNICATION_HEADERS
  ${COMMUNICATION_DIR}/communication_channel.hpp
  ${COMMUNICATION_DIR}/communication_server_channel.hpp
  ${COMMUNICATION_DIR}/stream_transfer_receiver.hpp
  ${COMMUNICATION_DIR}/stream_transfer_sender.hpp
  )

set(MONITOR_SOURCES
  ${MONITOR_DIR}/monitor_collect.cpp
  ${MONITOR_DIR}/monitor_registration.cpp
  ${MONITOR_DIR}/monitor_statistic.cpp
  ${MONITOR_DIR}/monitor_transaction.cpp
)

set(MONITOR_HEADERS
  ${MONITOR_DIR}/monitor_collect.hpp
  ${MONITOR_DIR}/monitor_definition.hpp
  ${MONITOR_DIR}/monitor_registration.hpp
  ${MONITOR_DIR}/monitor_statistic.hpp
  ${MONITOR_DIR}/monitor_transaction.hpp
)

set (REPLICATION_HEADERS
  ${REPLICATION_DIR}/log_generator.hpp
  ${REPLICATION_DIR}/log_consumer.hpp
  ${REPLICATION_DIR}/replication_master_senders_manager.hpp
  ${REPLICATION_DIR}/replication_object.hpp
  ${REPLICATION_DIR}/replication_stream_entry.hpp
  )

set(QUERY_SOURCES
  ${QUERY_DIR}/arithmetic.c
  ${QUERY_DIR}/crypt_opfunc.c
  ${QUERY_DIR}/fetch.c
  ${QUERY_DIR}/filter_pred_cache.c
  ${QUERY_DIR}/list_file.c
  ${QUERY_DIR}/method_scan.c
  ${QUERY_DIR}/numeric_opfunc.c
  ${QUERY_DIR}/partition.c
  ${QUERY_DIR}/query_dump.c
  ${QUERY_DIR}/query_evaluator.c
  ${QUERY_DIR}/query_executor.c
  ${QUERY_DIR}/query_manager.c
  ${QUERY_DIR}/query_opfunc.c
  ${QUERY_DIR}/scan_json_table.cpp
  ${QUERY_DIR}/scan_manager.c
  ${QUERY_DIR}/serial.c
  ${QUERY_DIR}/set_scan.c
  ${QUERY_DIR}/show_scan.c
  ${QUERY_DIR}/stream_to_xasl.c
  ${QUERY_DIR}/string_opfunc.c
  ${QUERY_DIR}/vacuum.c
  ${QUERY_DIR}/xasl_cache.c
  )
set(QUERY_HEADERS
  ${QUERY_DIR}/scan_json_table.hpp
  )

set(OBJECT_SOURCES
  ${OBJECT_DIR}/elo.c
  ${OBJECT_DIR}/object_print.c
  ${OBJECT_DIR}/set_object.c
  ${OBJECT_DIR}/object_representation.c
  ${OBJECT_DIR}/object_primitive.c
  ${OBJECT_DIR}/transform.c
  ${OBJECT_DIR}/object_domain.c
  )
  
set(REPLICATION_SOURCES
  ${REPLICATION_DIR}/log_consumer.cpp
  ${REPLICATION_DIR}/log_generator.cpp
  ${REPLICATION_DIR}/replication_object.cpp
  ${REPLICATION_DIR}/replication_master_senders_manager.cpp  
  ${REPLICATION_DIR}/replication_stream_entry.cpp
  )

set(JSP_SOURCES
  ${JSP_DIR}/jsp_sr.c
  )

set(TRANSACTION_SOURCES
  ${TRANSACTION_DIR}/log_compress.c
  ${TRANSACTION_DIR}/boot_sr.c
  ${TRANSACTION_DIR}/locator.c
  ${TRANSACTION_DIR}/locator_sr.c
  ${TRANSACTION_DIR}/lock_table.c
  ${TRANSACTION_DIR}/lock_manager.c
  ${TRANSACTION_DIR}/log_manager.c
  ${TRANSACTION_DIR}/log_2pc.c
  ${TRANSACTION_DIR}/log_global.c
  ${TRANSACTION_DIR}/log_page_buffer.c
  ${TRANSACTION_DIR}/log_recovery.c
  ${TRANSACTION_DIR}/log_tran_table.c
  ${TRANSACTION_DIR}/log_comm.c
  ${TRANSACTION_DIR}/replication.c
  ${TRANSACTION_DIR}/recovery.c
  ${TRANSACTION_DIR}/transaction_sr.c
  ${TRANSACTION_DIR}/wait_for_graph.c
  ${TRANSACTION_DIR}/log_writer.c
  ${TRANSACTION_DIR}/mvcc.c
  )

set(STORAGE_SOURCES
  ${STORAGE_DIR}/btree.c
  ${STORAGE_DIR}/btree_load.c
  ${STORAGE_DIR}/catalog_class.c
  ${STORAGE_DIR}/compactdb_sr.c
  ${STORAGE_DIR}/double_write_buffer.c
  ${STORAGE_DIR}/disk_manager.c
  ${STORAGE_DIR}/es.c
  ${STORAGE_DIR}/es_common.c
  ${STORAGE_DIR}/es_posix.c
  ${STORAGE_DIR}/extendible_hash.c
  ${STORAGE_DIR}/external_sort.c
  ${STORAGE_DIR}/file_io.c
  ${STORAGE_DIR}/file_manager.c
  ${STORAGE_DIR}/heap_file.c
  ${STORAGE_DIR}/oid.c
  ${STORAGE_DIR}/overflow_file.c
  ${STORAGE_DIR}/page_buffer.c
  ${STORAGE_DIR}/record_descriptor.cpp
  ${STORAGE_DIR}/slotted_page.c
  ${STORAGE_DIR}/statistics_sr.c
  ${STORAGE_DIR}/storage_common.c
  ${STORAGE_DIR}/system_catalog.c
  )
set(STORAGE_HEADERS
  ${STORAGE_DIR}/record_descriptor.hpp
)

set(SESSION_SOURCES
  ${SESSION_DIR}/session.c
  ${SESSION_DIR}/session_sr.c
  )

set(XASL_SOURCES
  ${XASL_DIR}/access_json_table.cpp
  ${XASL_DIR}/xasl_stream.cpp
  )
set(XASL_HEADERS
  ${XASL_DIR}/access_json_table.hpp
  ${XASL_DIR}/compile_context.h
  ${XASL_DIR}/xasl_stream.hpp
  )

list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/heartbeat.c)
if(UNIX)
  list(APPEND BASE_SOURCES ${BASE_DIR}/cubrid_getopt_long.c)
  list(APPEND BASE_SOURCES ${BASE_DIR}/variable_string.c)
  list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/tcp.c)
  list(APPEND STORAGE_SOURCES ${STORAGE_DIR}/es_owfs.c)
else(UNIX)
  list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/wintcp.c)
  list(APPEND BASE_SOURCES ${BASE_DIR}/cubrid_getopt_long.c)
  list(APPEND BASE_SOURCES ${BASE_DIR}/rand.c)
endif(UNIX)

set_source_files_properties(${PROBES_OBJECT} PROPERTIES EXTERNAL_OBJECT true GENERATED true)

if(UNIX)
set_source_files_properties(${COMPAT_DIR}/db_json.cpp PROPERTIES COMPILE_FLAGS -Wno-unknown-pragmas)
endif(UNIX)

SET_SOURCE_FILES_PROPERTIES(
  ${BASE_SOURCES}
  ${COMMUNICATION_SOURCES}
  ${COMPAT_SOURCES}
  ${CONNECTION_SOURCES}
  ${CONNECTION_TCP_SOURCE}
  ${ES_SOURCES}
  ${EXECUTABLE_SOURCES}
  ${HEAPLAYER_SOURCES}
  ${JSP_SOURCES}
  ${MONITOR_SOURCES}
  ${OBJECT_SOURCES}
  ${PARSER_SOURCES}
  ${PROBES_OBJECT}
  ${QUERY_SOURCES}
  ${REPLICATION_SOURCES}
  ${SESSION_SOURCES}
  ${STORAGE_SOURCES}
  ${THREAD_SOURCES}
  ${TRANSACTION_SOURCES}
  ${XASL_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

SET_SOURCE_FILES_PROPERTIES(
  ${BASE_DIR}/md5.c
  PROPERTIES LANGUAGE C
  )

set (CUBRID_LIB_SOURCES
  ${BASE_SOURCES}
  ${COMMUNICATION_SOURCES}
  ${COMPAT_SOURCES}
  ${CONNECTION_SOURCES}
  ${CONNECTION_TCP_SOURCE}
  ${ES_SOURCES}
  ${EXECUTABLE_SOURCES}
  ${HEAPLAYER_SOURCES}
  ${JSP_SOURCES}
  ${MONITOR_SOURCES}
  ${OBJECT_SOURCES}
  ${PROBES_OBJECT}
  ${QUERY_SOURCES}
  ${REPLICATION_SOURCES}  
  ${SESSION_SOURCES}
  ${STORAGE_SOURCES}
  ${STREAM_SOURCES}
  ${THREAD_SOURCES}
  ${TRANSACTION_SOURCES}
  ${XASL_SOURCES}
  )

set (CUBRID_LIB_HEADERS
  ${BASE_HEADERS}
  ${COMMUNICATION_HEADERS}
  ${COMPAT_HEADERS}
  ${MONITOR_HEADERS}
  ${QUERY_HEADERS}
  ${REPLICATION_HEADERS}
  ${THREAD_HEADERS}
  ${STORAGE_HEADERS}
  ${STREAM_HEADERS}
  ${XASL_HEADERS}
  )

add_library(cubrid SHARED
  ${CUBRID_LIB_SOURCES}
  ${CUBRID_LIB_HEADERS}
  )
set_target_properties(cubrid PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")
if(WIN32)
  # change library file name to libcubrid
  set_target_properties(cubrid PROPERTIES OUTPUT_NAME libcubrid)
endif(WIN32)

target_compile_definitions(cubrid PRIVATE SERVER_MODE ${COMMON_DEFS})
if(NOT USE_CUBRID_ENV)
  target_compile_definitions(cubrid PRIVATE ${DIR_DEFS})
endif(NOT USE_CUBRID_ENV)
if(WIN32)
  set_source_files_properties(${HEAPLAYERS_DIR}/customheaps.cpp PROPERTIES COMPILE_FLAGS -Od)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    target_compile_definitions(cubrid PRIVATE _USE_32BIT_TIME_T)
  endif(TARGET_PLATFORM_BITS EQUAL 32)
  target_link_libraries(cubrid LINK_PRIVATE ws2_32 ${JVM_LIBS})
  set_target_properties(cubrid PROPERTIES LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/win/libcubrid/libcubrid.def\"" LINK_FLAGS_RELEASE "/NODEFAULTLIB:libcmt.lib" LINK_FLAGS_DEBUG "/NODEFAULTLIB:msvcrt.lib")
endif(WIN32)
target_include_directories(cubrid PRIVATE ${CMAKE_SOURCE_DIR}/src/heaplayers/util ${JAVA_INC} ${EP_INCLUDES})
if(UNIX)
  target_link_libraries(cubrid LINK_PRIVATE -Wl,-whole-archive ${EP_LIBS} -Wl,-no-whole-archive)
  target_link_libraries(cubrid LINK_PUBLIC ${CURSES_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS})
else(UNIX)
  target_link_libraries(cubrid LINK_PRIVATE ${EP_LIBS})
endif(UNIX)
add_dependencies(cubrid ${EP_TARGETS})

if(AT_LEAST_ONE_UNIT_TEST)
  if (WIN32)
  # in order to avoid adding all the functions used by unit tests in def file, it is more convenient to build a static
  # lib too. more compile time, but less head-ache
  # todo - is there any nicer way of doing this??
  add_library (cubrid-win-lib STATIC
    ${CUBRID_LIB_SOURCES}
    ${CUBRID_LIB_HEADERS}
    )

  target_compile_definitions(cubrid-win-lib PRIVATE 
    SERVER_MODE
    ${COMMON_DEFS}
    )

  target_link_libraries(cubrid-win-lib PRIVATE
    ws2_32
    ${JVM_LIBS}
    ${EP_LIBS}
    )

  target_include_directories(cubrid-win-lib PRIVATE
    ${CMAKE_SOURCE_DIR}/src/heaplayers/util
    ${JAVA_INC}
    ${EP_INCLUDES}
    )

  add_dependencies(cubrid-win-lib ${EP_TARGETS})

  endif (WIN32)
endif (AT_LEAST_ONE_UNIT_TEST)

if(ENABLE_SYSTEMTAP)
  target_include_directories(cubrid PRIVATE ${CMAKE_BINARY_DIR})
  add_dependencies(cubrid gen_probes)
endif(ENABLE_SYSTEMTAP)

install(TARGETS cubrid
  RUNTIME DESTINATION ${CUBRID_BINDIR} COMPONENT Library
  LIBRARY DESTINATION ${CUBRID_LIBDIR} COMPONENT Library
  )

install(FILES
  ${COMPAT_DIR}/dbi_compat.h
  DESTINATION ${CUBRID_INCLUDEDIR} COMPONENT Header  
  RENAME dbi.h
  )
install(FILES
  ${COMPAT_DIR}/dbtype_def.h
  ${COMPAT_DIR}/dbtype_function.h
  ${COMPAT_DIR}/db_date.h
  ${COMPAT_DIR}/db_elo.h
  ${COMPAT_DIR}/cache_time.h
  DESTINATION ${CUBRID_INCLUDEDIR} COMPONENT Header  
  )
install(FILES
  ${BASE_DIR}/error_code.h
  DESTINATION ${CUBRID_INCLUDEDIR} COMPONENT Header  
  )  

# install pdb files for debugging on windows
if(WIN32)
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/
    DESTINATION ${CUBRID_BINDIR} COMPONENT Debuginfo
    FILES_MATCHING PATTERN "*.pdb"
    )
endif(WIN32)
