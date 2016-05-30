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

set(EXECUTABLE_SOURCES
  ${BISON_loader_grammar_OUTPUTS}
  ${FLEX_loader_lexer_OUTPUTS}
  ${EXECUTABLES_DIR}/compactdb_cl.c
  ${EXECUTABLES_DIR}/csql.c
  ${EXECUTABLES_DIR}/csql_result.c
  ${EXECUTABLES_DIR}/csql_result_format.c
  ${EXECUTABLES_DIR}/csql_session.c
  ${EXECUTABLES_DIR}/csql_support.c
  ${EXECUTABLES_DIR}/file_hash.c
  ${EXECUTABLES_DIR}/load_object.c
  ${EXECUTABLES_DIR}/loaddb.c
  ${EXECUTABLES_DIR}/loader.c
  ${EXECUTABLES_DIR}/loader_object_table.c
  ${EXECUTABLES_DIR}/unload_object.c
  ${EXECUTABLES_DIR}/unload_schema.c
  ${EXECUTABLES_DIR}/unloaddb.c
  ${EXECUTABLES_DIR}/util_common.c
  ${EXECUTABLES_DIR}/util_cs.c
  )

set(COMPAT_SOURCES
  ${COMPAT_DIR}/cnv.c
  ${COMPAT_DIR}/cnvlex.c
  ${COMPAT_DIR}/db_admin.c
  ${COMPAT_DIR}/db_class.c
  ${COMPAT_DIR}/db_date.c
  ${COMPAT_DIR}/db_elo.c
  ${COMPAT_DIR}/db_info.c
  ${COMPAT_DIR}/db_macro.c
  ${COMPAT_DIR}/db_obj.c
  ${COMPAT_DIR}/db_old.c
  ${COMPAT_DIR}/db_query.c
  ${COMPAT_DIR}/db_set.c
  ${COMPAT_DIR}/db_temp.c
  ${COMPAT_DIR}/db_vdb.c
  ${COMPAT_DIR}/db_virt.c
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
  ${BASE_DIR}/error_manager.c
  ${BASE_DIR}/fault_injection.c
  ${BASE_DIR}/fixed_alloc.c
  ${BASE_DIR}/getopt_long.c
  ${BASE_DIR}/ini_parser.c
  ${BASE_DIR}/intl_support.c
  ${BASE_DIR}/language_support.c
  ${BASE_DIR}/locale_support.c
  ${BASE_DIR}/lock_free.c
  ${BASE_DIR}/md5.c
  ${BASE_DIR}/memory_alloc.c
  ${BASE_DIR}/memory_hash.c
  ${BASE_DIR}/message_catalog.c
  ${BASE_DIR}/misc_string.c
  ${BASE_DIR}/mprec.c
  ${BASE_DIR}/perf.c
  ${BASE_DIR}/perf_monitor.c
  ${BASE_DIR}/porting.c
  ${BASE_DIR}/release_string.c
  ${BASE_DIR}/sha1.c
  ${BASE_DIR}/stack_dump.c
  ${BASE_DIR}/system_parameter.c
  ${BASE_DIR}/tsc_timer.c
  ${BASE_DIR}/tz_support.c
  ${BASE_DIR}/uca_support.c
  ${BASE_DIR}/unicode_support.c
  ${BASE_DIR}/util_func.c
  ${BASE_DIR}/variable_string.c
  ${BASE_DIR}/xml_parser.c
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
  ${COMMUNICATION_DIR}/network.c
  )

set(PARSER_SOURCES
  ${BISON_csql_grammar_OUTPUTS}
  ${FLEX_csql_lexer_OUTPUTS}
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
  )

set(OPTIMIZER_SOURCES
  ${OPTIMIZER_DIR}/query_bitset.c
  ${OPTIMIZER_DIR}/query_graph.c
  ${OPTIMIZER_DIR}/query_planner.c
  ${OPTIMIZER_DIR}/plan_generation.c
  ${OPTIMIZER_DIR}/query_rewrite.c
  )

set(QUERY_SOURCES
  ${QUERY_DIR}/execute_schema.c
  ${QUERY_DIR}/execute_statement.c
  ${QUERY_DIR}/arithmetic.c
  ${QUERY_DIR}/cursor.c
  ${QUERY_DIR}/query_dump.c
  ${QUERY_DIR}/query_method.c
  ${QUERY_DIR}/numeric_opfunc.c
  ${QUERY_DIR}/query.c
  ${QUERY_DIR}/string_opfunc.c
  ${QUERY_DIR}/xasl_to_stream.c
  ${QUERY_DIR}/crypt_opfunc.c
  )

set(OBJECT_SOURCES
  ${OBJECT_DIR}/elo.c
  ${OBJECT_DIR}/authenticate.c
  ${OBJECT_DIR}/object_print.c
  ${OBJECT_DIR}/object_accessor.c
  ${OBJECT_DIR}/object_template.c
  ${OBJECT_DIR}/set_object.c
  ${OBJECT_DIR}/trigger_manager.c
  ${OBJECT_DIR}/virtual_object.c
  ${OBJECT_DIR}/class_object.c
  ${OBJECT_DIR}/schema_manager.c
  ${OBJECT_DIR}/schema_template.c
  ${OBJECT_DIR}/object_representation.c
  ${OBJECT_DIR}/object_primitive.c
  ${OBJECT_DIR}/quick_fit.c
  ${OBJECT_DIR}/transform_cl.c
  ${OBJECT_DIR}/transform.c
  ${OBJECT_DIR}/object_domain.c
  ${OBJECT_DIR}/work_space.c
  )

set(JSP_SOURCES
  ${JSP_DIR}/jsp_cl.c
  )

set(TRANSACTION_SOURCES
  ${TRANSACTION_DIR}/log_compress.c
  ${TRANSACTION_DIR}/boot_cl.c
  ${TRANSACTION_DIR}/locator.c
  ${TRANSACTION_DIR}/locator_cl.c
  ${TRANSACTION_DIR}/lock_table.c
  ${TRANSACTION_DIR}/log_comm.c
  ${TRANSACTION_DIR}/transaction_cl.c
  ${TRANSACTION_DIR}/log_writer.c
  )

set(STORAGE_SOURCES
  ${STORAGE_DIR}/storage_common.c
  ${STORAGE_DIR}/oid.c
  ${STORAGE_DIR}/statistics_cl.c
  ${STORAGE_DIR}/file_io.c
  ${STORAGE_DIR}/es_common.c
  ${STORAGE_DIR}/es.c
  ${STORAGE_DIR}/es_posix.c
  )

if(UNIX)
  list(APPEND EXECUTABLE_SOURCES ${EXECUTABLES_DIR}/checksumdb.c)
  list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/tcp.c)
  list(APPEND CONNECTION_SOURCES ${CONNECTION_DIR}/heartbeat.c)
  list(APPEND BASE_SOURCES ${BASE_DIR}/dynamic_load.c)
  list(APPEND TRANSACTION_SOURCES ${TRANSACTION_DIR}/log_applier_sql_log.c)
  list(APPEND TRANSACTION_SOURCES ${TRANSACTION_DIR}/log_applier.c)
  list(APPEND STORAGE_SOURCES ${STORAGE_DIR}/es_owfs.c)
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

add_library(cubridcs SHARED
  ${EXECUTABLE_SOURCES}
  ${COMPAT_SOURCES}
  ${BASE_SOURCES}
  ${HEAPLAYER_SOURCES}
  ${CONNECTION_SOURCES}
  ${CONNECTION_TCP_SOURCE}
  ${COMMUNICATION_SOURCES}
  ${PARSER_SOURCES}
  ${OPTIMIZER_SOURCES}
  ${QUERY_SOURCES}
  ${OBJECT_SOURCES}
  ${JSP_SOURCES}
  ${TRANSACTION_SOURCES}
  ${STORAGE_SOURCES}
  ${ES_SOURCES}
  ${PROBES_OBJECT}
  )
set_target_properties(cubridcs PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")

target_compile_definitions(cubridcs PRIVATE CS_MODE ${COMMON_DEFS})
if(NOT USE_CUBRID_ENV)
  target_compile_definitions(cubridcs PRIVATE ${DIR_DEFS})
endif(NOT USE_CUBRID_ENV)
if(WIN32)
  set_source_files_properties(${HEAPLAYERS_DIR}/customheaps.cpp PROPERTIES COMPILE_FLAGS -Od)
  target_compile_definitions(cubridcs PRIVATE YY_NO_UNISTD_H)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    target_compile_definitions(cubridcs PRIVATE _USE_32BIT_TIME_T)
  endif(TARGET_PLATFORM_BITS EQUAL 32)
  target_link_libraries(cubridcs LINK_PRIVATE ws2_32)
  set_target_properties(cubridcs PROPERTIES LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/win/cubridcs/cubridcs.def\"" LINK_FLAGS_RELEASE "/NODEFAULTLIB:libcmt.lib"  LINK_FLAGS_DEBUG "/NODEFAULTLIB:msvcrt.lib")
endif(WIN32)
target_include_directories(cubridcs PRIVATE ${CMAKE_SOURCE_DIR}/src/heaplayers/util ${EP_INCLUDES})
if(UNIX)
  target_link_libraries(cubridcs LINK_PRIVATE -Wl,-whole-archive cas ${EP_LIBS} -Wl,-no-whole-archive ${CURSES_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
else(UNIX)
  target_link_libraries(cubridcs LINK_PRIVATE cas ${EP_LIBS})
endif(UNIX)
if(USE_BISON_FLEX)
  add_dependencies(cubridcs gen_csql_grammar gen_csql_lexer gen_loader_grammar gen_loader_lexer)
endif(USE_BISON_FLEX)
if(UNIX)
  add_dependencies(cubridcs ${EP_TARGETS})
endif(UNIX)

if(ENABLE_SYSTEMTAP)
  target_include_directories(cubridcs PRIVATE ${CMAKE_BINARY_DIR})
  add_dependencies(cubridcs gen_probes)
endif(ENABLE_SYSTEMTAP)

install(TARGETS cubridcs
  RUNTIME DESTINATION ${CUBRID_BINDIR}
  LIBRARY DESTINATION ${CUBRID_LIBDIR}
  )


# install pdb files for debugging on windows
if(WIN32)
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/
    DESTINATION ${CUBRID_BINDIR}
    FILES_MATCHING PATTERN "*.pdb"
    )
endif(WIN32)
