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
  ${EXECUTABLES_DIR}/compactdb_cl.c
  ${EXECUTABLES_DIR}/compactdb_common.c
  ${EXECUTABLES_DIR}/csql.c
  ${EXECUTABLES_DIR}/csql_result.c
  ${EXECUTABLES_DIR}/csql_result_format.c
  ${EXECUTABLES_DIR}/csql_session.c
  ${EXECUTABLES_DIR}/csql_support.c
  ${EXECUTABLES_DIR}/extract_schema.cpp
  ${EXECUTABLES_DIR}/file_hash.c
  ${EXECUTABLES_DIR}/unload_object.c
  ${EXECUTABLES_DIR}/unload_schema.c
  ${EXECUTABLES_DIR}/unloaddb.c
  ${EXECUTABLES_DIR}/util_common.c
  ${EXECUTABLES_DIR}/util_cs.c
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

set (COMPAT_HEADERS
  ${COMPAT_DIR}/dbtype_def.h
  ${COMPAT_DIR}/db_json.hpp
  ${COMPAT_DIR}/db_json_allocator.hpp
  ${COMPAT_DIR}/db_json_path.hpp
  ${COMPAT_DIR}/db_json_types_internal.hpp
  ${COMPAT_DIR}/db_set.h
  ${COMPAT_DIR}/db_set_function.h
  ${COMPAT_DIR}/dbtype_function.h
  ${COMPAT_DIR}/dbtype_function.i
  ${COMPAT_DIR}/db_admin.h
  )

set(BASE_SOURCES
  ${BASE_DIR}/adjustable_array.c
  ${BASE_DIR}/area_alloc.c
  ${BASE_DIR}/base64.c
  ${BASE_DIR}/chartype.c
  ${BASE_DIR}/condition_handler.c
  ${BASE_DIR}/databases_file.c
  ${BASE_DIR}/dtoa.c
  ${BASE_DIR}/dynamic_array.c
  ${BASE_DIR}/encryption.c
  ${BASE_DIR}/environment_variable.c
  ${BASE_DIR}/error_context.cpp
  ${BASE_DIR}/error_manager.c
  ${BASE_DIR}/fault_injection.c
  ${BASE_DIR}/fileline_location.cpp
  ${BASE_DIR}/fixed_alloc.c
  ${BASE_DIR}/filesys_temp.cpp
  ${BASE_DIR}/cubrid_getopt_long.c
  ${BASE_DIR}/ini_parser.c
  ${BASE_DIR}/intl_support.c
  ${BASE_DIR}/language_support.c
  ${BASE_DIR}/locale_helper.cpp
  ${BASE_DIR}/locale_support.c
  ${BASE_DIR}/lockfree_bitmap.cpp
  ${BASE_DIR}/mem_block.cpp
  ${BASE_DIR}/memory_alloc.c
  ${BASE_DIR}/memory_hash.c
  ${BASE_DIR}/memory_private_allocator.cpp
  ${BASE_DIR}/message_catalog.c
  ${BASE_DIR}/misc_string.c
  ${BASE_DIR}/mprec.c
  ${BASE_DIR}/packer.cpp
  ${BASE_DIR}/get_clock_freq.c
  ${BASE_DIR}/perf.cpp
  ${BASE_DIR}/perf_monitor.c
  ${BASE_DIR}/pinnable_buffer.cpp
  ${BASE_DIR}/pinning.cpp
  ${BASE_DIR}/porting.c
  ${BASE_DIR}/printer.cpp
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
  ${BASE_DIR}/variable_string.c
  ${BASE_DIR}/xml_parser.c
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
  ${BASE_DIR}/lockfree_bitmap.hpp
  ${BASE_DIR}/mem_block.hpp
  ${BASE_DIR}/memory_reference_store.hpp
  ${BASE_DIR}/memory_private_allocator.hpp
  ${BASE_DIR}/msgcat_set_log.hpp
  ${BASE_DIR}/packable_object.hpp
  ${BASE_DIR}/packer.hpp
  ${BASE_DIR}/perf.hpp
  ${BASE_DIR}/perf_def.hpp
  ${BASE_DIR}/pinning.hpp
  ${BASE_DIR}/pinnable_buffer.hpp
  ${BASE_DIR}/porting_inline.hpp
  ${BASE_DIR}/printer.hpp
  ${BASE_DIR}/string_buffer.hpp
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
  ${COMMUNICATION_DIR}/network_cl.c
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
  ${PARSER_DIR}/func_type.cpp
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
  ${QUERY_DIR}/numeric_opfunc.c
  ${QUERY_DIR}/query_cl.c
  ${QUERY_DIR}/query_dump.c
  ${QUERY_DIR}/query_method.c
  ${QUERY_DIR}/regu_var.cpp
  ${QUERY_DIR}/string_opfunc.c
  ${QUERY_DIR}/string_regex.cpp
  ${QUERY_DIR}/xasl_to_stream.c
  )
set(QUERY_HEADERS
  ${QUERY_DIR}/query_monitoring.hpp
  ${QUERY_DIR}/string_regex.hpp
)

set(OBJECT_SOURCES
  ${OBJECT_DIR}/authenticate.c
  ${OBJECT_DIR}/class_description.cpp
  ${OBJECT_DIR}/class_object.c
  ${OBJECT_DIR}/elo.c
  ${OBJECT_DIR}/lob_locator.cpp
  ${OBJECT_DIR}/object_accessor.c
  ${OBJECT_DIR}/object_description.cpp
  ${OBJECT_DIR}/object_domain.c
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
  )

set(TRANSACTION_SOURCES
  ${TRANSACTION_DIR}/boot_cl.c
  ${TRANSACTION_DIR}/client_credentials.cpp
  ${TRANSACTION_DIR}/locator.c
  ${TRANSACTION_DIR}/locator_cl.c
  ${TRANSACTION_DIR}/lock_table.c
  ${TRANSACTION_DIR}/log_comm.c
  ${TRANSACTION_DIR}/log_compress.c
  ${TRANSACTION_DIR}/log_lsa.cpp
  ${TRANSACTION_DIR}/log_writer.c
  ${TRANSACTION_DIR}/transaction_cl.c
  )
set(TRANSACTION_HEADERS
  ${TRANSACTION_DIR}/client_credentials.hpp
  ${TRANSACTION_DIR}/log_archives.hpp
  ${TRANSACTION_DIR}/log_common_impl.h
  ${TRANSACTION_DIR}/log_lsa.hpp
  ${TRANSACTION_DIR}/log_record.hpp
  ${TRANSACTION_DIR}/log_storage.hpp
  ${TRANSACTION_DIR}/log_volids.hpp
  ${TRANSACTION_DIR}/transaction_global.hpp
  )

set(STORAGE_SOURCES
  ${STORAGE_DIR}/storage_common.c
  ${STORAGE_DIR}/oid.c
  ${STORAGE_DIR}/statistics_cl.c
  ${STORAGE_DIR}/file_io.c
  ${STORAGE_DIR}/es_common.c
  ${STORAGE_DIR}/es.c
  ${STORAGE_DIR}/es_posix.c
  ${STORAGE_DIR}/tde.c
  )

set(XASL_SOURCES
  ${XASL_DIR}/access_json_table.cpp
  ${XASL_DIR}/xasl_analytic.cpp
  ${XASL_DIR}/xasl_predicate.cpp
  ${XASL_DIR}/xasl_unpack_info.cpp
  ${XASL_DIR}/xasl_stream.cpp
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
  ${LOADDB_DIR}/load_common.cpp
  ${LOADDB_DIR}/load_db.c
  ${LOADDB_DIR}/load_object.c
  )

set(LOADDB_HEADERS
  ${LOADDB_DIR}/load_common.hpp
  ${LOADDB_DIR}/load_object.h
  )

list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/heartbeat.c)
if(UNIX)
  list(APPEND EXECUTABLE_SOURCES ${EXECUTABLES_DIR}/checksumdb.c)
  list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/tcp.c)
  list(APPEND BASE_SOURCES ${BASE_DIR}/dynamic_load.c)
  list(APPEND TRANSACTION_SOURCES ${TRANSACTION_DIR}/log_applier_sql_log.c)
  list(APPEND TRANSACTION_SOURCES ${TRANSACTION_DIR}/log_applier.c)
  list(APPEND STORAGE_SOURCES ${STORAGE_DIR}/es_owfs.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${EXECUTABLES_DIR}/checksumdb.c
    PROPERTIES LANGUAGE CXX
  )
else(UNIX)
  list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/wintcp.c)
  list(APPEND BASE_SOURCES ${BASE_DIR}/rand.c)
  list(APPEND BASE_SOURCES ${BASE_DIR}/tz_compile.c)
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
  ${EXECUTABLES_DIR}/compactdb_cl.c
  ${EXECUTABLES_DIR}/compactdb_common.c
  ${EXECUTABLES_DIR}/csql.c
  ${EXECUTABLES_DIR}/csql_result.c
  ${EXECUTABLES_DIR}/csql_result_format.c
  ${EXECUTABLES_DIR}/csql_session.c
  ${EXECUTABLES_DIR}/csql_support.c
  ${EXECUTABLES_DIR}/file_hash.c
  ${EXECUTABLES_DIR}/unload_object.c
  ${EXECUTABLES_DIR}/unload_schema.c
  ${EXECUTABLES_DIR}/unloaddb.c
  ${EXECUTABLES_DIR}/util_common.c
  ${EXECUTABLES_DIR}/util_cs.c
  ${COMPAT_SOURCES}
  ${BASE_SOURCES}
  ${HEAPLAYER_SOURCES}
  ${CONNECTION_SOURCES}
  ${CONNECTION_TCP_SOURCE}
  ${COMMUNICATION_SOURCES}
  ${PARSER_DIR}/cnf.c
  ${PARSER_DIR}/compile.c
  ${PARSER_DIR}/double_byte_support.c
  ${PARSER_DIR}/keyword.c
  ${PARSER_DIR}/method_transform.c
  ${PARSER_DIR}/name_resolution.c
  ${PARSER_DIR}/parse_dbi.c
  ${PARSER_DIR}/parse_evaluate.c
  ${PARSER_DIR}/parse_tree.c
  ${PARSER_DIR}/parse_tree_cl.c
  ${PARSER_DIR}/parser_support.c
  ${PARSER_DIR}/query_result.c
  ${PARSER_DIR}/scanner_support.c
  ${PARSER_DIR}/semantic_check.c
  ${PARSER_DIR}/show_meta.c
  ${PARSER_DIR}/type_checking.c
  ${PARSER_DIR}/view_transform.c
  ${PARSER_DIR}/xasl_generation.c
  ${OPTIMIZER_SOURCES}
  ${QUERY_SOURCES}
  ${OBJECT_SOURCES}
  ${JSP_SOURCES}
  ${TRANSACTION_SOURCES}
  ${STORAGE_SOURCES}
  ${ES_SOURCES}
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

add_library(cubridcs SHARED
  ${EXECUTABLE_SOURCES}
  ${EXECUTABLE_HEADERS}
  ${COMPAT_SOURCES}
  ${COMPAT_SOURCES_C}
  ${BASE_HEADERS}
  ${BASE_SOURCES}
  ${ES_SOURCES}
  ${COMMUNICATION_SOURCES}
  ${CONNECTION_SOURCES}
  ${CONNECTION_TCP_SOURCE}
  ${HEAPLAYER_SOURCES}
  ${OBJECT_HEADERS}
  ${OBJECT_SOURCES}
  ${OPTIMIZER_SOURCES}
  ${PARSER_HEADERS}
  ${PARSER_SOURCES}
  ${PROBES_OBJECT}
  ${QUERY_HEADERS}
  ${QUERY_SOURCES}
  ${JSP_SOURCES}
  ${TRANSACTION_HEADERS}
  ${TRANSACTION_SOURCES}
  ${STORAGE_SOURCES}
  ${XASL_HEADERS}
  ${XASL_SOURCES}
  ${LOADDB_SOURCES}
  ${LOADDB_HEADERS}
  )
set_target_properties(cubridcs PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")

target_compile_definitions(cubridcs PRIVATE CS_MODE ${COMMON_DEFS})
if(NOT USE_CUBRID_ENV)
  target_compile_definitions(cubridcs PRIVATE ${DIR_DEFS})
endif(NOT USE_CUBRID_ENV)
if(WIN32)
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
  target_compile_definitions(cubridcs PRIVATE YY_NO_UNISTD_H)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    target_compile_definitions(cubridcs PRIVATE _USE_32BIT_TIME_T)
  endif(TARGET_PLATFORM_BITS EQUAL 32)
  target_link_libraries(cubridcs LINK_PRIVATE ws2_32)
  set_target_properties(cubridcs PROPERTIES LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/win/cubridcs/cubridcs.def\"" LINK_FLAGS_RELEASE "/NODEFAULTLIB:libcmt.lib" LINK_FLAGS_DEBUG "/NODEFAULTLIB:msvcrt.lib")
endif(WIN32)
target_include_directories(cubridcs PRIVATE ${FLEX_INCLUDE_DIRS} ${EP_INCLUDES})
if(UNIX)
  target_link_libraries(cubridcs LINK_PRIVATE -Wl,-whole-archive cas ${EP_LIBS} -Wl,-no-whole-archive)
  target_link_libraries(cubridcs LINK_PUBLIC ${CURSES_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS})
  target_link_libraries(cubridcs PUBLIC stdc++fs)
else(UNIX)
  target_link_libraries(cubridcs LINK_PRIVATE cas ${EP_LIBS})
endif(UNIX)

add_dependencies(cubridcs gen_csql_grammar gen_csql_lexer gen_loader_grammar gen_loader_lexer)
add_dependencies(cubridcs ${EP_TARGETS})

if(ENABLE_SYSTEMTAP)
  target_include_directories(cubridcs PRIVATE ${CMAKE_BINARY_DIR})
  add_dependencies(cubridcs gen_probes)
endif(ENABLE_SYSTEMTAP)

install(TARGETS cubridcs
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
