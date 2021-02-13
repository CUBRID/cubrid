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

project (server_server)
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
  ${BASE_DIR}/locale_helper.cpp
  ${BASE_DIR}/locale_support.c
  ${BASE_DIR}/lock_free.c
  ${BASE_DIR}/lockfree_bitmap.cpp
  ${BASE_DIR}/lockfree_hashmap.cpp
  ${BASE_DIR}/lockfree_transaction_descriptor.cpp
  ${BASE_DIR}/lockfree_transaction_table.cpp
  ${BASE_DIR}/lockfree_transaction_system.cpp
  ${BASE_DIR}/mem_block.cpp
  ${BASE_DIR}/memory_alloc.c
  ${BASE_DIR}/memory_hash.c
  ${BASE_DIR}/memory_private_allocator.cpp
  ${BASE_DIR}/message_catalog.c
  ${BASE_DIR}/misc_string.c
  ${BASE_DIR}/mprec.c
  ${BASE_DIR}/object_representation_sr.c
  ${BASE_DIR}/packer.cpp
  ${BASE_DIR}/get_clock_freq.c
  ${BASE_DIR}/perf.cpp
  ${BASE_DIR}/pinnable_buffer.cpp
  ${BASE_DIR}/pinning.cpp
  ${BASE_DIR}/perf_monitor.c
  ${BASE_DIR}/porting.c
  ${BASE_DIR}/printer.cpp
  ${BASE_DIR}/process_util.c
  ${BASE_DIR}/release_string.c
  ${BASE_DIR}/resource_tracker.cpp
  ${BASE_DIR}/server_type.cpp
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
set(COMPAT_SOURCES
  ${COMPAT_DIR}/cnv.c
  ${COMPAT_DIR}/cnvlex.c
  ${COMPAT_DIR}/db_date.c
  ${COMPAT_DIR}/db_elo.c
  ${COMPAT_DIR}/db_json.cpp
  ${COMPAT_DIR}/db_json_allocator.cpp
  ${COMPAT_DIR}/db_json_path.cpp
  ${COMPAT_DIR}/db_json_types_internal.cpp
  ${COMPAT_DIR}/db_macro.c
  ${COMPAT_DIR}/db_set.c
  ${COMPAT_DIR}/db_value_printer.cpp
  )
set(CONNECTION_SOURCES
  ${CONNECTION_DIR}/connection_sr.c
  ${CONNECTION_DIR}/connection_list_sr.c
  ${CONNECTION_DIR}/connection_globals.c
  ${CONNECTION_DIR}/server_support.c
  ${CONNECTION_DIR}/connection_support.c
  )
set(OBJECT_SOURCES
  ${OBJECT_DIR}/elo.c
  ${OBJECT_DIR}/lob_locator.cpp
  ${OBJECT_DIR}/object_domain.c
  ${OBJECT_DIR}/object_primitive.c
  ${OBJECT_DIR}/object_representation.c
  ${OBJECT_DIR}/set_object.c
  ${OBJECT_DIR}/transform.c
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
  ${QUERY_DIR}/query_aggregate.cpp
  ${QUERY_DIR}/query_hash_scan.c
  ${QUERY_DIR}/query_analytic.cpp
  ${QUERY_DIR}/query_dump.c
  ${QUERY_DIR}/query_evaluator.c
  ${QUERY_DIR}/query_executor.c
  ${QUERY_DIR}/query_manager.c
  ${QUERY_DIR}/query_opfunc.c
  ${QUERY_DIR}/query_reevaluation.cpp
  ${QUERY_DIR}/regu_var.cpp
  ${QUERY_DIR}/scan_json_table.cpp
  ${QUERY_DIR}/scan_manager.c
  ${QUERY_DIR}/serial.c
  ${QUERY_DIR}/set_scan.c
  ${QUERY_DIR}/show_scan.c
  ${QUERY_DIR}/stream_to_xasl.c
  ${QUERY_DIR}/string_opfunc.c
  ${QUERY_DIR}/string_regex.cpp
  ${QUERY_DIR}/vacuum.c
  ${QUERY_DIR}/xasl_cache.c
  )
set(STORAGE_SOURCES
  ${STORAGE_DIR}/btree.c
  ${STORAGE_DIR}/btree_load.c
  ${STORAGE_DIR}/btree_unique.cpp
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
  ${STORAGE_DIR}/tde.c
  )

set(THREAD_SOURCES
  ${THREAD_DIR}/critical_section.c
  ${THREAD_DIR}/critical_section_tracker.cpp
  ${THREAD_DIR}/internal_tasks_worker_pool.cpp
  ${THREAD_DIR}/thread_daemon.cpp
  ${THREAD_DIR}/thread_entry.cpp
  ${THREAD_DIR}/thread_entry_task.cpp
  ${THREAD_DIR}/thread_lockfree_hash_map.cpp
  ${THREAD_DIR}/thread_looper.cpp
  ${THREAD_DIR}/thread_manager.cpp
  ${THREAD_DIR}/thread_waiter.cpp
  ${THREAD_DIR}/thread_worker_pool.cpp
  )
set(TRANSACTION_SOURCES
  ${TRANSACTION_DIR}/boot_sr.c
  ${TRANSACTION_DIR}/client_credentials.cpp
  ${TRANSACTION_DIR}/locator.c
  ${TRANSACTION_DIR}/locator_sr.c
  ${TRANSACTION_DIR}/lock_manager.c
  ${TRANSACTION_DIR}/lock_table.c
  ${TRANSACTION_DIR}/log_2pc.c
  ${TRANSACTION_DIR}/log_append.cpp
  ${TRANSACTION_DIR}/log_comm.c
  ${TRANSACTION_DIR}/log_compress.c
  ${TRANSACTION_DIR}/log_global.c
  ${TRANSACTION_DIR}/log_lsa.cpp
  ${TRANSACTION_DIR}/log_manager.c
  ${TRANSACTION_DIR}/log_page_buffer.c
  ${TRANSACTION_DIR}/log_postpone_cache.cpp
  ${TRANSACTION_DIR}/log_recovery.c
  ${TRANSACTION_DIR}/log_system_tran.cpp
  ${TRANSACTION_DIR}/log_tran_table.c
  ${TRANSACTION_DIR}/log_writer.c
  ${TRANSACTION_DIR}/mvcc.c
  ${TRANSACTION_DIR}/mvcc_active_tran.cpp
  ${TRANSACTION_DIR}/mvcc_table.cpp
  ${TRANSACTION_DIR}/recovery.c
  ${TRANSACTION_DIR}/replication.c
  ${TRANSACTION_DIR}/transaction_sr.c
  ${TRANSACTION_DIR}/transaction_transient.cpp
  ${TRANSACTION_DIR}/wait_for_graph.c
  )

set(S_S_SRC
  comm_channel_mock.cpp
  test_main.cpp
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

SET_SOURCE_FILES_PROPERTIES(
  ${S_S_SRC}
  ${COMPAT_SOURCES}
  ${CONNECTION_SOURCES}
  ${BASE_SOURCES}
  ${OBJECT_SOURCES}
  ${QUERY_SOURCES}
  ${STORAGE_SOURCES}
  ${THREAD_SOURCES}
  ${TRANSACTION_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_server_server
  ${BASE_SOURCES}
  ${COMPAT_SOURCES}
  ${CONNECTION_SOURCES}
  ${OBJECT_SOURCES}
  ${QUERY_SOURCES}
  ${STORAGE_SOURCES}
  ${THREAD_SOURCES}
  ${TRANSACTION_SOURCES}
  ${S_S_SRC}
  )

target_compile_options(test_server_server PRIVATE ${compile_warning_flags})
target_compile_definitions(test_server_server PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_server_server PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_server_server PRIVATE
  ${CMAKE_THREAD_LIBS_INIT}
  ${EP_LIBS}
  test_common
  )

