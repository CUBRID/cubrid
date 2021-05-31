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

set(EXECUTABLE_SOURCES
  ${EXECUTABLES_DIR}/csql.c
  ${EXECUTABLES_DIR}/csql_session.c
  ${EXECUTABLES_DIR}/csql_result.c
  ${EXECUTABLES_DIR}/csql_support.c
  ${EXECUTABLES_DIR}/csql_result_format.c
  ${EXECUTABLES_DIR}/extract_schema.cpp
  ${EXECUTABLES_DIR}/util_sa.c
  ${EXECUTABLES_DIR}/util_cs.c
  ${EXECUTABLES_DIR}/util_common.c
  ${EXECUTABLES_DIR}/compactdb.c
  ${EXECUTABLES_DIR}/compactdb_common.c
  ${EXECUTABLES_DIR}/unloaddb.c
  ${EXECUTABLES_DIR}/unload_object.c
  ${EXECUTABLES_DIR}/unload_schema.c
  ${EXECUTABLES_DIR}/file_hash.c
  )

set(EXECUTABLE_HEADERS
  ${EXECUTABLES_DIR}/extract_schema.hpp
  )

set(COMPAT_SOURCES
  ${COMPAT_DIR}/cnv.c
  ${COMPAT_DIR}/cnvlex.c
  ${COMPAT_DIR}/db_admin.c
  ${COMPAT_DIR}/db_class.c
  ${COMPAT_DIR}/db_date.c
  ${COMPAT_DIR}/db_elo.c
  ${COMPAT_DIR}/db_info.c
  ${COMPAT_DIR}/db_json.cpp
  ${COMPAT_DIR}/db_json_allocator.cpp
  ${COMPAT_DIR}/db_json_path.cpp
  ${COMPAT_DIR}/db_json_types_internal.cpp
  ${COMPAT_DIR}/db_macro.c
  ${COMPAT_DIR}/db_obj.c
  ${COMPAT_DIR}/db_old.c
  ${COMPAT_DIR}/db_query.c
  ${COMPAT_DIR}/db_set.c
  ${COMPAT_DIR}/db_temp.c
  ${COMPAT_DIR}/db_value_printer.cpp
  ${COMPAT_DIR}/db_vdb.c
  ${COMPAT_DIR}/db_virt.c
  ${COMPAT_DIR}/dbtype_function.c
  )
set(COMPAT_SOURCES_C
  ${COMPAT_DIR}/dbtype_function.c
  )

set(BASE_SOURCES
  ${BASE_DIR}/bit.c
  ${BASE_DIR}/dynamic_array.c
  ${BASE_DIR}/porting.c
  ${BASE_DIR}/area_alloc.c
  ${BASE_DIR}/fixed_alloc.c
  ${BASE_DIR}/filesys_temp.cpp
  ${BASE_DIR}/mem_block.cpp
  ${BASE_DIR}/memory_private_allocator.cpp
  ${BASE_DIR}/memory_alloc.c
  ${BASE_DIR}/databases_file.c
  ${BASE_DIR}/encryption.c
  ${BASE_DIR}/sha1.c
  ${BASE_DIR}/base64.c
  ${BASE_DIR}/error_context.cpp
  ${BASE_DIR}/error_manager.c
  ${BASE_DIR}/language_support.c
  ${BASE_DIR}/locale_support.c
  ${BASE_DIR}/xml_parser.c
  ${BASE_DIR}/uca_support.c
  ${BASE_DIR}/unicode_support.c
  ${BASE_DIR}/memory_hash.c
  ${BASE_DIR}/packer.cpp
  ${BASE_DIR}/get_clock_freq.c
  ${BASE_DIR}/perf.cpp
  ${BASE_DIR}/perf_monitor.c
  ${BASE_DIR}/pinnable_buffer.cpp
  ${BASE_DIR}/pinning.cpp
  ${BASE_DIR}/printer.cpp
  ${BASE_DIR}/ini_parser.c
  ${BASE_DIR}/system_parameter.c
  ${BASE_DIR}/fault_injection.c
  ${BASE_DIR}/release_string.c
  ${BASE_DIR}/stack_dump.c
  ${BASE_DIR}/message_catalog.c
  ${BASE_DIR}/object_representation_sr.c
  ${BASE_DIR}/mprec.c
  ${BASE_DIR}/dtoa.c
  ${BASE_DIR}/adjustable_array.c
  ${BASE_DIR}/chartype.c
  ${BASE_DIR}/condition_handler.c
  ${BASE_DIR}/util_func.c
  ${BASE_DIR}/intl_support.c
  ${BASE_DIR}/environment_variable.c
  ${BASE_DIR}/misc_string.c
  ${BASE_DIR}/string_buffer.cpp
  ${BASE_DIR}/variable_string.c
  ${BASE_DIR}/cubrid_getopt_long.c
  ${BASE_DIR}/binaryheap.c
  ${BASE_DIR}/tsc_timer.c
  ${BASE_DIR}/locale_helper.cpp
  ${BASE_DIR}/lock_free.c
  ${BASE_DIR}/lockfree_bitmap.cpp
  ${BASE_DIR}/lockfree_hashmap.cpp
  ${BASE_DIR}/lockfree_transaction_descriptor.cpp
  ${BASE_DIR}/lockfree_transaction_table.cpp
  ${BASE_DIR}/lockfree_transaction_system.cpp
  ${BASE_DIR}/fileline_location.cpp
  ${BASE_DIR}/resource_tracker.cpp
  ${BASE_DIR}/tz_compile.c
  ${BASE_DIR}/tz_support.c
  ${BASE_DIR}/ddl_log.c
  )
set(BASE_HEADERS
  ${BASE_DIR}/error_code.h
  ${BASE_DIR}/error_context.hpp
  ${BASE_DIR}/error_manager.h
  ${BASE_DIR}/extensible_array.hpp
  ${BASE_DIR}/fileline_location.hpp
  ${BASE_DIR}/filesys.hpp
  ${BASE_DIR}/filesys_temp.hpp
  ${BASE_DIR}/locale_helper.hpp
  ${BASE_DIR}/lockfree_address_marker.hpp
  ${BASE_DIR}/lockfree_bitmap.hpp
  ${BASE_DIR}/lockfree_freelist.hpp
  ${BASE_DIR}/lockfree_hashmap.hpp
  ${BASE_DIR}/lockfree_transaction_def.hpp
  ${BASE_DIR}/lockfree_transaction_descriptor.hpp
  ${BASE_DIR}/lockfree_transaction_reclaimable.hpp
  ${BASE_DIR}/lockfree_transaction_table.hpp
  ${BASE_DIR}/lockfree_transaction_system.hpp
  ${BASE_DIR}/mem_block.hpp
  ${BASE_DIR}/memory_private_allocator.cpp
  ${BASE_DIR}/msgcat_set_log.hpp
  ${BASE_DIR}/packable_object.hpp
  ${BASE_DIR}/packer.hpp
  ${BASE_DIR}/perf.hpp
  ${BASE_DIR}/perf_def.hpp
  ${BASE_DIR}/pinning.hpp
  ${BASE_DIR}/pinnable_buffer.hpp
  ${BASE_DIR}/porting_inline.hpp
  ${BASE_DIR}/printer.hpp
  ${BASE_DIR}/resource_tracker.hpp
  ${BASE_DIR}/semaphore.hpp
  ${BASE_DIR}/ddl_log.h
)

set(HEAPLAYER_SOURCES
  ${HEAPLAYERS_DIR}/customheaps.cpp
  ${HEAPLAYERS_DIR}/lea_heap.c
  )

set(CONNECTION_SOURCES
  ${CONNECTION_DIR}/connection_less.c
  ${CONNECTION_DIR}/connection_cl.c
  ${CONNECTION_DIR}/connection_globals.c
  ${CONNECTION_DIR}/connection_list_cl.c
  ${CONNECTION_DIR}/client_support.c
  ${CONNECTION_DIR}/connection_support.c
  )

set(COMMUNICATION_SOURCES
  ${COMMUNICATION_DIR}/network_interface_cl.c
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

set(PARSER_SOURCES
  ${BISON_csql_grammar_OUTPUTS}
  ${FLEX_csql_lexer_OUTPUTS}
  ${PARSER_DIR}/cnf.c
  ${PARSER_DIR}/compile.c
  ${PARSER_DIR}/double_byte_support.c
  ${PARSER_DIR}/func_type.cpp
  ${PARSER_DIR}/keyword.c
  ${PARSER_DIR}/method_transform.c
  ${PARSER_DIR}/name_resolution.c
  ${PARSER_DIR}/parse_dbi.c
  ${PARSER_DIR}/parse_evaluate.c
  ${PARSER_DIR}/parse_tree.c
  ${PARSER_DIR}/parse_tree_cl.c
  ${PARSER_DIR}/parse_type.cpp
  ${PARSER_DIR}/parser_allocator.cpp
  ${PARSER_DIR}/parser_support.c
  ${PARSER_DIR}/query_result.c
  ${PARSER_DIR}/scanner_support.c
  ${PARSER_DIR}/semantic_check.c
  ${PARSER_DIR}/show_meta.c
  ${PARSER_DIR}/type_checking.c
  ${PARSER_DIR}/view_transform.c
  ${PARSER_DIR}/xasl_regu_alloc.cpp
  ${PARSER_DIR}/xasl_generation.c
  )
set(PARSER_HEADERS
  ${PARSER_DIR}/func_type.hpp
  ${PARSER_DIR}/parse_type.hpp
  ${PARSER_DIR}/parser_allocator.hpp
  ${PARSER_DIR}/xasl_regu_alloc.hpp
  )

set(OPTIMIZER_SOURCES
  ${OPTIMIZER_DIR}/query_bitset.c
  ${OPTIMIZER_DIR}/query_graph.c
  ${OPTIMIZER_DIR}/query_planner.c
  ${OPTIMIZER_DIR}/plan_generation.c
  ${OPTIMIZER_DIR}/query_rewrite.c
  )

set(QUERY_SOURCES
  ${QUERY_DIR}/arithmetic.c
  ${QUERY_DIR}/crypt_opfunc.c
  ${QUERY_DIR}/cursor.c
  ${QUERY_DIR}/execute_schema.c
  ${QUERY_DIR}/execute_statement.c
  ${QUERY_DIR}/fetch.c
  ${QUERY_DIR}/filter_pred_cache.c
  ${QUERY_DIR}/list_file.c
  ${QUERY_DIR}/method_scan.c
  ${QUERY_DIR}/numeric_opfunc.c
  ${QUERY_DIR}/partition.c
  ${QUERY_DIR}/query_aggregate.cpp
  ${QUERY_DIR}/query_hash_scan.c
  ${QUERY_DIR}/query_analytic.cpp
  ${QUERY_DIR}/query_cl.c
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
  ${QUERY_DIR}/xasl_to_stream.c
  )
set(QUERY_HEADERS
  ${QUERY_DIR}/query_aggregate.hpp
  ${QUERY_DIR}/query_hash_scan.h
  ${QUERY_DIR}/query_analytic.hpp
  ${QUERY_DIR}/query_monitoring.hpp
  ${QUERY_DIR}/query_reevaluation.hpp
  ${QUERY_DIR}/scan_json_table.hpp
  ${QUERY_DIR}/string_regex.hpp
  )

set(OBJECT_SOURCES
  ${OBJECT_DIR}/authenticate.c
  ${OBJECT_DIR}/class_description.cpp
  ${OBJECT_DIR}/class_object.c
  ${OBJECT_DIR}/elo.c
  ${OBJECT_DIR}/lob_locator.cpp
  ${OBJECT_DIR}/object_description.cpp
  ${OBJECT_DIR}/object_domain.c
  ${OBJECT_DIR}/object_accessor.c
  ${OBJECT_DIR}/object_primitive.c
  ${OBJECT_DIR}/object_print.c
  ${OBJECT_DIR}/object_printer.cpp
  ${OBJECT_DIR}/object_print_util.cpp
  ${OBJECT_DIR}/object_representation.c
  ${OBJECT_DIR}/object_template.c
  ${OBJECT_DIR}/quick_fit.c
  ${OBJECT_DIR}/schema_manager.c
  ${OBJECT_DIR}/schema_template.c
  ${OBJECT_DIR}/set_object.c
  ${OBJECT_DIR}/transform.c
  ${OBJECT_DIR}/transform_cl.c
  ${OBJECT_DIR}/trigger_description.cpp
  ${OBJECT_DIR}/trigger_manager.c
  ${OBJECT_DIR}/virtual_object.c
  ${OBJECT_DIR}/work_space.c
  )
set(OBJECT_HEADERS
  ${OBJECT_DIR}/lob_locator.hpp
)

set(JSP_SOURCES
  ${JSP_DIR}/jsp_cl.c
  ${JSP_DIR}/jsp_comm.c
  ${JSP_DIR}/jsp_file.c
  ${JSP_DIR}/jsp_sr.c
  )

set(THREAD_SOURCES
  ${THREAD_DIR}/critical_section_tracker.cpp
  ${THREAD_DIR}/thread_entry.cpp
  ${THREAD_DIR}/thread_entry_task.cpp
  ${THREAD_DIR}/thread_lockfree_hash_map.cpp
  ${THREAD_DIR}/thread_manager.cpp
  )
set(THREAD_HEADERS
  ${THREAD_DIR}/critical_section_tracker.hpp
  ${THREAD_DIR}/thread_compat.hpp
  ${THREAD_DIR}/thread_entry.hpp
  ${THREAD_DIR}/thread_lockfree_hash_map.hpp
  ${THREAD_DIR}/thread_manager.hpp
  )

set(TRANSACTION_SOURCES
  ${TRANSACTION_DIR}/boot_cl.c
  ${TRANSACTION_DIR}/boot_sr.c
  ${TRANSACTION_DIR}/client_credentials.cpp
  ${TRANSACTION_DIR}/locator.c
  ${TRANSACTION_DIR}/locator_cl.c
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
  ${TRANSACTION_DIR}/replication.c
  ${TRANSACTION_DIR}/recovery.c
  ${TRANSACTION_DIR}/transaction_cl.c
  ${TRANSACTION_DIR}/transaction_sr.c
  ${TRANSACTION_DIR}/transaction_transient.cpp
  ${TRANSACTION_DIR}/wait_for_graph.c
  )
set(TRANSACTION_HEADERS
  ${TRANSACTION_DIR}/client_credentials.hpp
  ${TRANSACTION_DIR}/log_2pc.h
  ${TRANSACTION_DIR}/log_append.hpp
  ${TRANSACTION_DIR}/log_archives.hpp
  ${TRANSACTION_DIR}/log_common_impl.h
  ${TRANSACTION_DIR}/log_lsa.hpp
  ${TRANSACTION_DIR}/log_postpone_cache.hpp
  ${TRANSACTION_DIR}/log_record.hpp
  ${TRANSACTION_DIR}/log_storage.hpp
  ${TRANSACTION_DIR}/log_system_tran.hpp
  ${TRANSACTION_DIR}/log_volids.hpp
  ${TRANSACTION_DIR}/mvcc.h
  ${TRANSACTION_DIR}/mvcc_active_tran.hpp
  ${TRANSACTION_DIR}/mvcc_table.hpp
  ${TRANSACTION_DIR}/transaction_global.hpp
  ${TRANSACTION_DIR}/transaction_transient.hpp
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
  ${STORAGE_DIR}/es_owfs.c
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
  ${STORAGE_DIR}/statistics_cl.c
  ${STORAGE_DIR}/statistics_sr.c
  ${STORAGE_DIR}/storage_common.c
  ${STORAGE_DIR}/system_catalog.c
  ${STORAGE_DIR}/tde.c
  )
set(STORAGE_HEADERS
  ${STORAGE_DIR}/btree_unique.hpp
  ${STORAGE_DIR}/record_descriptor.hpp
)

set(SESSION_SOURCES
  ${SESSION_DIR}/session.c
  ${SESSION_DIR}/session_sr.c
  )

set(XASL_SOURCES
  ${XASL_DIR}/access_json_table.cpp
  ${XASL_DIR}/xasl_analytic.cpp
  ${XASL_DIR}/xasl_predicate.cpp
  ${XASL_DIR}/xasl_stream.cpp
  ${XASL_DIR}/xasl_unpack_info.cpp
  )
set(XASL_HEADERS
  ${XASL_DIR}/access_json_table.hpp
  ${XASL_DIR}/access_spec.hpp
  ${XASL_DIR}/compile_context.h
  ${XASL_DIR}/xasl_aggregate.hpp
  ${XASL_DIR}/xasl_analytic.hpp
  ${XASL_DIR}/xasl_unpack_info.hpp
  ${XASL_DIR}/xasl_predicate.hpp
  ${XASL_DIR}/xasl_stream.hpp
  )

set(LOADDB_SOURCES
  ${BISON_loader_grammar_OUTPUT_SOURCE}
  ${FLEX_loader_lexer_OUTPUTS}
  ${LOADDB_DIR}/load_common.cpp
  ${LOADDB_DIR}/load_db.c
  ${LOADDB_DIR}/load_driver.cpp
  ${LOADDB_DIR}/load_error_handler.cpp
  ${LOADDB_DIR}/load_object.c
  ${LOADDB_DIR}/load_object_table.c
  ${LOADDB_DIR}/load_sa_loader.cpp
  ${LOADDB_DIR}/load_semantic_helper.cpp
  )

set(LOADDB_HEADERS
  ${BISON_loader_grammar_OUTPUT_HEADER}
  ${LOADDB_DIR}/load_common.hpp
  ${LOADDB_DIR}/load_driver.hpp
  ${LOADDB_DIR}/load_error_handler.hpp
  ${LOADDB_DIR}/load_object.h
  ${LOADDB_DIR}/load_object_table.h
  ${LOADDB_DIR}/load_sa_loader.hpp
  ${LOADDB_DIR}/load_semantic_helper.hpp
  )

if(UNIX)
  list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/tcp.c)
  list(APPEND BASE_SOURCES ${BASE_DIR}/dynamic_load.c)
else(UNIX)
  list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/wintcp.c)
  list(APPEND BASE_SOURCES ${BASE_DIR}/rand.c)
endif(UNIX)

set_source_files_properties(${BISON_csql_grammar_OUTPUTS} PROPERTIES GENERATED true)
set_source_files_properties(${FLEX_csql_lexer_OUTPUTS} PROPERTIES GENERATED true)
set_source_files_properties(${BISON_loader_grammar_OUTPUTS} PROPERTIES GENERATED true)
set_source_files_properties(${FLEX_loader_lexer_OUTPUTS} PROPERTIES GENERATED true)
set_source_files_properties(${PROBES_OBJECT} PROPERTIES EXTERNAL_OBJECT true GENERATED true)

if(UNIX)
  set_source_files_properties(${COMPAT_DIR}/db_json.cpp PROPERTIES COMPILE_FLAGS -Wno-unknown-pragmas)
  set_source_files_properties(${COMPAT_DIR}/db_json_allocator.cpp PROPERTIES COMPILE_FLAGS -Wno-unknown-pragmas)
  set_source_files_properties(${COMPAT_DIR}/db_json_path.cpp PROPERTIES COMPILE_FLAGS -Wno-unknown-pragmas)
  set_source_files_properties(${COMPAT_DIR}/db_json_types_internal.cpp PROPERTIES COMPILE_FLAGS -Wno-unknown-pragmas)
endif(UNIX)

SET_SOURCE_FILES_PROPERTIES(
  ${EXECUTABLES_DIR}/csql.c
  ${EXECUTABLES_DIR}/csql_session.c
  ${EXECUTABLES_DIR}/csql_result.c
  ${EXECUTABLES_DIR}/csql_support.c
  ${EXECUTABLES_DIR}/csql_result_format.c
  ${EXECUTABLES_DIR}/util_sa.c
  ${EXECUTABLES_DIR}/util_cs.c
  ${EXECUTABLES_DIR}/util_common.c
  ${EXECUTABLES_DIR}/compactdb.c
  ${EXECUTABLES_DIR}/compactdb_common.c
  ${EXECUTABLES_DIR}/unloaddb.c
  ${EXECUTABLES_DIR}/unload_object.c
  ${EXECUTABLES_DIR}/unload_schema.c
  ${EXECUTABLES_DIR}/file_hash.c
  ${COMPAT_SOURCES}
  ${BASE_SOURCES}
  ${HEAPLAYER_SOURCES}
  ${CONNECTION_SOURCES}
  ${CONNECTION_TCP_SOURCE}
  ${COMMUNICATION_SOURCES}
  ${MONITOR_SOURCES}
  ${PARSER_DIR}/parser_support.c
  ${PARSER_DIR}/show_meta.c
  ${PARSER_DIR}/double_byte_support.c
  ${PARSER_DIR}/method_transform.c
  ${PARSER_DIR}/cnf.c
  ${PARSER_DIR}/compile.c
  ${PARSER_DIR}/parse_dbi.c
  ${PARSER_DIR}/parse_evaluate.c
  ${PARSER_DIR}/keyword.c
  ${PARSER_DIR}/parse_tree_cl.c
  ${PARSER_DIR}/parse_tree.c
  ${PARSER_DIR}/query_result.c
  ${PARSER_DIR}/name_resolution.c
  ${PARSER_DIR}/semantic_check.c
  ${PARSER_DIR}/type_checking.c
  ${PARSER_DIR}/xasl_generation.c
  ${PARSER_DIR}/scanner_support.c
  ${PARSER_DIR}/view_transform.c
  ${OPTIMIZER_SOURCES}
  ${QUERY_SOURCES}
  ${OBJECT_SOURCES}
  ${JSP_SOURCES}
  ${THREAD_SOURCES}
  ${TRANSACTION_SOURCES}
  ${STORAGE_SOURCES}
  ${ES_SOURCES}
  ${SESSION_SOURCES}
  ${PROBES_OBJECT}
  ${XASL_SOURCES}
  ${LOADDB_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

SET_SOURCE_FILES_PROPERTIES(
  ${CSQL_GRAMMAR_OUTPUT}
  ${CSQL_LEXER_OUTPUT}
  ${ESQL_GRAMMAR_OUTPUT}
  ${ESQL_LEXER_OUTPUT}
  ${COMPAT_SOURCES_C}
  PROPERTIES LANGUAGE C
  )

add_library(cubridsa SHARED
  ${EXECUTABLE_SOURCES}
  ${EXECUTABLE_HEADERS}
  ${COMPAT_SOURCES}
  ${BASE_SOURCES}
  ${BASE_HEADERS}
  ${COMMUNICATION_SOURCES}
  ${COMPAT_SOURCES}
  ${COMPAT_SOURCES_C}
  ${CONNECTION_SOURCES}
  ${CONNECTION_TCP_SOURCE}
  ${ES_SOURCES}
  ${EXECUTABLE_SOURCES}
  ${HEAPLAYER_SOURCES}
  ${JSP_SOURCES}
  ${MONITOR_HEADERS}
  ${MONITOR_SOURCES}
  ${OBJECT_HEADERS}
  ${OBJECT_SOURCES}
  ${OPTIMIZER_SOURCES}
  ${QUERY_HEADERS}
  ${QUERY_SOURCES}
  ${PARSER_HEADERS}
  ${PARSER_SOURCES}
  ${PROBES_OBJECT}
  ${THREAD_SOURCES}
  ${TRANSACTION_HEADERS}
  ${TRANSACTION_SOURCES}
  ${SESSION_SOURCES}
  ${STORAGE_HEADERS}
  ${STORAGE_SOURCES}
  ${XASL_HEADERS}
  ${XASL_SOURCES}
  ${LOADDB_SOURCES}
  ${LOADDB_HEADERS}
  )
set_target_properties(cubridsa PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")

target_compile_definitions(cubridsa PRIVATE SA_MODE ${COMMON_DEFS})
if(NOT USE_CUBRID_ENV)
  target_compile_definitions(cubridsa PRIVATE ${DIR_DEFS})
endif(NOT USE_CUBRID_ENV)
if(WIN32)
  set_source_files_properties(${BASE_DIR}/message_catalog.c PROPERTIES COMPILE_FLAGS -DNDEBUG)
  set_source_files_properties(${HEAPLAYERS_DIR}/customheaps.cpp PROPERTIES COMPILE_FLAGS -Od)
  SET_SOURCE_FILES_PROPERTIES(
    ${CSQL_GRAMMAR_OUTPUT}
    ${LOADER_GRAMMAR_OUTPUT}
    ${CSQL_LEXER_OUTPUT}
    ${LOADER_LEXER_OUTPUT}
    ${ESQL_GRAMMAR_OUTPUT}
    ${ESQL_LEXER_OUTPUT}
    PROPERTIES COMPILE_FLAGS /WX-
  )
  set_source_files_properties(${BISON_csql_grammar_OUTPUTS} PROPERTIES COMPILE_FLAGS "${COMPILE_FLAGS} -Og-")
  target_compile_definitions(cubridsa PRIVATE YY_NO_UNISTD_H)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    target_compile_definitions(cubridsa PRIVATE _USE_32BIT_TIME_T)
  endif(TARGET_PLATFORM_BITS EQUAL 32)
  target_link_libraries(cubridsa LINK_PRIVATE ws2_32 ${JVM_LIBS})
  set_target_properties(cubridsa PROPERTIES LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/win/cubridsa/cubridsa.def\"" LINK_FLAGS_RELEASE "/NODEFAULTLIB:libcmt.lib" LINK_FLAGS_DEBUG "/NODEFAULTLIB:msvcrt.lib")
endif(WIN32)
if(UNIX)
target_external_dependencies(cubridsa
  LIBNCURSES
  )
endif(UNIX)
target_external_dependencies(cubridsa
  LIBEXPAT
  LIBJANSSON
  LIBEXPAT
  LIBEDIT
  LIBOPENSSL
  LZ4
  )
target_include_directories(cubridsa PRIVATE
  ${FLEX_INCLUDE_DIRS}
  ${JAVA_INC}
  )
target_link_libraries(cubridsa LINK_PRIVATE
    cas
  )
if(UNIX)
  # find out what this means:
  # target_link_libraries(cubridsa LINK_PRIVATE -Wl,-whole-archive cas ${EP_LIBS} -Wl,-no-whole-archive)
  target_link_libraries(cubridsa LINK_PUBLIC ${CURSES_LIBRARIES} ${CMAKE_DL_LIBS})
  target_link_libraries(cubridsa PUBLIC stdc++fs)
endif(UNIX)

add_dependencies(cubridsa gen_csql_grammar gen_csql_lexer gen_loader_grammar gen_loader_lexer)
#add_dependencies(cubridsa ${EP_TARGETS})

if(ENABLE_SYSTEMTAP)
  target_include_directories(cubridsa PRIVATE ${CMAKE_BINARY_DIR})
  add_dependencies(cubridsa gen_probes)
endif(ENABLE_SYSTEMTAP)

install(TARGETS cubridsa
  RUNTIME DESTINATION ${CUBRID_BINDIR} COMPONENT Library
  LIBRARY DESTINATION ${CUBRID_LIBDIR} COMPONENT Library
  )

# install pdb files for debugging on windows
if(WIN32)
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/
    DESTINATION ${CUBRID_BINDIR} COMPONENT Debuginfo
    FILES_MATCHING PATTERN "*.pdb"
    )
endif(WIN32)
