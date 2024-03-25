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

set(LIBCUBRIDESQL_SOURCES
  ${EXECUTABLES_DIR}/esql_gadget.c
  ${EXECUTABLES_DIR}/esql_cli.c
  )
SET_SOURCE_FILES_PROPERTIES(
  ${LIBCUBRIDESQL_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
add_library(cubridesql SHARED ${LIBCUBRIDESQL_SOURCES})
set_target_properties(cubridesql PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")
set_target_properties(cubridesql PROPERTIES PUBLIC_HEADER "${EXECUTABLES_DIR}/cubrid_esql.h")
target_include_directories(cubridesql PRIVATE ${EP_INCLUDES})
target_compile_definitions(cubridesql PRIVATE ${COMMON_DEFS} PRODUCE_ANSI_CODE UCI_TEMPORARY)
if(WIN32)
  set_target_properties(cubridesql PROPERTIES OUTPUT_NAME libesql)
  target_compile_definitions(cubridesql PRIVATE YY_CHAR=char)
  set_target_properties(cubridesql PROPERTIES LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/win/libesql/libesql.def\"")
  target_link_libraries(cubridesql LINK_PRIVATE cubridcs)
endif(WIN32)

add_dependencies(cubridesql ${EP_TARGETS})
add_dependencies(cubridesql cubridcs)

set(CSQL_SOURCES
  ${EXECUTABLES_DIR}/csql_launcher.c
  ${EXECUTABLES_DIR}/util_support.c
  ${BASE_DIR}/porting.c
  ${BASE_DIR}/cubrid_getopt_long.c
  )

SET_SOURCE_FILES_PROPERTIES(
  ${CSQL_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
if(WIN32)
  list(APPEND CSQL_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(csql ${CSQL_SOURCES})
target_compile_definitions(csql PRIVATE ${COMMON_DEFS})
target_include_directories(csql PRIVATE ${EP_INCLUDES})
if(UNIX)
  target_link_libraries(csql LINK_PRIVATE ${CMAKE_DL_LIBS})
else(UNIX)
  target_compile_definitions(csql PRIVATE _CONSOLE)
  target_link_libraries(csql LINK_PRIVATE ws2_32)
endif(UNIX)
if(UNIX)
  add_dependencies(csql ${EP_TARGETS})
endif(UNIX)
add_dependencies(csql cubridsa cubridcs)

set(CUB_MASTER_SOURCES
  ${EXECUTABLES_DIR}/master.c
  ${EXECUTABLES_DIR}/master_request.c
  ${EXECUTABLES_DIR}/master_util.c
  )
if(UNIX)
  list(APPEND CUB_MASTER_SOURCES ${EXECUTABLES_DIR}/master_heartbeat.c)
  SET_SOURCE_FILES_PROPERTIES(
	${CUB_MASTER_SOURCES}
	PROPERTIES LANGUAGE CXX
  )
else(UNIX)
  SET_SOURCE_FILES_PROPERTIES(
	${CUB_MASTER_SOURCES}
	PROPERTIES LANGUAGE CXX
  )
  list(APPEND CUB_MASTER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(UNIX)



add_executable(cub_master ${CUB_MASTER_SOURCES})
target_compile_definitions(cub_master PRIVATE ${COMMON_DEFS})
target_include_directories(cub_master PRIVATE ${EP_INCLUDES})
target_link_libraries(cub_master LINK_PRIVATE cubridcs)
if(WIN32)
  target_link_libraries(cub_master LINK_PRIVATE ws2_32)
endif(WIN32)


set(CUB_SERVER_SOURCES ${EXECUTABLES_DIR}/server.c)

SET_SOURCE_FILES_PROPERTIES(
  ${CUB_SERVER_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

if(WIN32)
  list(APPEND CUB_SERVER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cub_server ${CUB_SERVER_SOURCES})
target_compile_definitions(cub_server PRIVATE SERVER_MODE ${COMMON_DEFS})
target_include_directories(cub_server PRIVATE ${EP_INCLUDES})
target_link_libraries(cub_server LINK_PRIVATE cubrid)
if(WIN32)
  target_link_libraries(cub_server LINK_PRIVATE dbghelp)
endif(WIN32)


set(CUBRID_SOURCES
  ${EXECUTABLES_DIR}/util_service.c
  ${EXECUTABLES_DIR}/util_support.c
  )

SET_SOURCE_FILES_PROPERTIES(
  ${CUBRID_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
if(WIN32)
  list(APPEND CUBRID_SOURCES ${BASE_DIR}/cubrid_getopt_long.c)
  SET_SOURCE_FILES_PROPERTIES(
    ${BASE_DIR}/cubrid_getopt_long.c
    PROPERTIES LANGUAGE CXX
  )
  list(APPEND CUBRID_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cubrid-bin ${CUBRID_SOURCES})

if(WIN32)
  if(MSVC_VERSION LESS 1600)
    # for VS 2008 (9.0) or less version
    set_target_properties(cubrid-bin PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
  else(MSVC_VERSION LESS 1600)
    set_target_properties(cubrid-bin PROPERTIES LINK_FLAGS "/level='requireAdministrator' /uiAccess='false'")
  endif(MSVC_VERSION LESS 1600)
endif(WIN32)
set_target_properties(cubrid-bin PROPERTIES OUTPUT_NAME cubrid)
target_link_libraries(cubrid-bin LINK_PRIVATE cubridsa)


set(CUB_COMMDB_SOURCES
  ${EXECUTABLES_DIR}/commdb.c
  ${EXECUTABLES_DIR}/util_support.c
  ${EXECUTABLES_DIR}/master_util.c
  )

SET_SOURCE_FILES_PROPERTIES(
  ${CUB_COMMDB_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
  
if(WIN32)
  list(APPEND CUB_COMMDB_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)

add_executable(cub_commdb ${CUB_COMMDB_SOURCES})

target_link_libraries(cub_commdb LINK_PRIVATE cubridcs)
if(WIN32)
  target_link_libraries(cub_commdb LINK_PRIVATE ws2_32)
endif(WIN32)


set(CUBRID_REL_SOURCES ${EXECUTABLES_DIR}/cubrid_version.c)
SET_SOURCE_FILES_PROPERTIES(
    ${CUBRID_REL_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
if(WIN32)
  list(APPEND CUBRID_REL_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cubrid_rel ${CUBRID_REL_SOURCES})
target_link_libraries(cubrid_rel LINK_PRIVATE cubridsa)


set(LOADJAVA_SOURCES ${EXECUTABLES_DIR}/loadjava.cpp)
SET_SOURCE_FILES_PROPERTIES(
  ${LOADJAVA_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
if(WIN32)
  list(APPEND LOADJAVA_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(loadjava ${LOADJAVA_SOURCES})
if(UNIX)
  target_link_libraries(loadjava LINK_PRIVATE cubridsa)
  target_link_libraries(loadjava PUBLIC stdc++fs)
else(UNIX)
  target_link_libraries(loadjava LINK_PRIVATE cubridcs)
endif(UNIX)

set(JAVASP_SERVER_SOURCES 
  ${EXECUTABLES_DIR}/javasp.cpp
)
SET_SOURCE_FILES_PROPERTIES(
  ${JAVASP_SERVER_SOURCES}
  PROPERTIES LANGUAGE CXX
)
if(WIN32)
  list(APPEND JAVASP_SERVER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cub_javasp ${JAVASP_SERVER_SOURCES})
target_link_libraries(cub_javasp LINK_PRIVATE cubridsa)

set(CUB_ADMIN_SOURCES
  ${EXECUTABLES_DIR}/util_admin.c
  ${EXECUTABLES_DIR}/util_support.c
  ${BASE_DIR}/cubrid_getopt_long.c
  ${BASE_DIR}/porting.c
  )
SET_SOURCE_FILES_PROPERTIES(
  ${CUB_ADMIN_SOURCES}
  PROPERTIES LANGUAGE CXX
  )
if(WIN32)
  list(APPEND CUB_ADMIN_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cub_admin ${CUB_ADMIN_SOURCES})
target_include_directories(cub_admin PRIVATE ${EP_INCLUDES})
target_link_libraries(cub_admin LINK_PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(cub_admin ${EP_TARGETS})
if(UNIX)
else(UNIX)
  target_link_libraries(cub_admin LINK_PRIVATE ws2_32)
endif(UNIX)
add_dependencies(cub_admin cubridsa cubridcs)


set(CUBRID_ESQL_SOURCES
  ${BISON_esql_grammar_OUTPUTS}
  ${FLEX_esql_lexer_OUTPUTS}
  ${EXECUTABLES_DIR}/esql_symbol_table.c
  ${EXECUTABLES_DIR}/esql_declare.c
  ${EXECUTABLES_DIR}/esql_cursor.c
  ${EXECUTABLES_DIR}/esql_misc.c
  ${EXECUTABLES_DIR}/esql_whenever.c
  ${EXECUTABLES_DIR}/esql_hash.c
  ${EXECUTABLES_DIR}/esql_translate.c
  ${EXECUTABLES_DIR}/esql_host_variable.c
  )
SET_SOURCE_FILES_PROPERTIES(
  ${EXECUTABLES_DIR}/esql_symbol_table.c
  ${EXECUTABLES_DIR}/esql_declare.c
  ${EXECUTABLES_DIR}/esql_cursor.c
  ${EXECUTABLES_DIR}/esql_misc.c
  ${EXECUTABLES_DIR}/esql_whenever.c
  ${EXECUTABLES_DIR}/esql_hash.c
  ${EXECUTABLES_DIR}/esql_translate.c
  ${EXECUTABLES_DIR}/esql_host_variable.c
  PROPERTIES LANGUAGE CXX
  )  
if(WIN32)
  list(APPEND CUBRID_ESQL_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
set_source_files_properties(${BISON_esql_grammar_OUTPUTS} PROPERTIES GENERATED true)
set_source_files_properties(${FLEX_esql_lexer_OUTPUTS} PROPERTIES GENERATED true)

add_executable(cubrid_esql ${CUBRID_ESQL_SOURCES})

target_include_directories(cubrid_esql PRIVATE ${CMAKE_BINARY_DIR} ${EP_INCLUDES})
target_compile_definitions(cubrid_esql PRIVATE ${COMMON_DEFS} PRODUCE_ANSI_CODE UCI_TEMPORARY)
if(WIN32)
  target_compile_definitions(cubrid_esql PRIVATE YY_NO_UNISTD_H)
endif(WIN32)
target_link_libraries(cubrid_esql LINK_PRIVATE cubridcs)

add_dependencies(cubrid_esql gen_esql_grammar gen_esql_lexer)

set(GENCAT_SOURCES
  ${BASE_DIR}/porting.c
  ${EXECUTABLES_DIR}/gencat.c
  )
if(WIN32)
  list(APPEND GENCAT_SOURCES ${BASE_DIR}/cubrid_getopt_long.c)
  list(APPEND GENCAT_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(gencat ${GENCAT_SOURCES})
if(WIN32)
  # TODO: move queue.h to src/?
  target_include_directories(gencat PRIVATE ${CMAKE_SOURCE_DIR}/win/gencat)
  target_link_libraries(gencat LINK_PRIVATE ws2_32)
endif(WIN32)

if(UNIX)
  set(UNITTESTS_LF_SOURCES
    ${BASE_DIR}/porting.c
    ${BASE_DIR}/lock_free.c
    ${BASE_DIR}/memory_monitor_api.cpp
    ${BASE_DIR}/memory_monitor_sr.cpp
    ${EXECUTABLES_DIR}/unittests_lf.c
    )
  SET_SOURCE_FILES_PROPERTIES(
    ${UNITTESTS_LF_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
  add_executable(unittests_lf ${UNITTESTS_LF_SOURCES})
  target_compile_definitions(unittests_lf PRIVATE UNITTEST_LF SERVER_MODE ${COMMON_DEFS})
  target_include_directories(unittests_lf PRIVATE ${EP_INCLUDES})
  target_link_libraries(unittests_lf LINK_PRIVATE cubridcs)


  set(UNITTESTS_AREA_SOURCES
    ${BASE_DIR}/memory_monitor_api.cpp
    ${BASE_DIR}/memory_monitor_sr.cpp
    ${EXECUTABLES_DIR}/unittests_area.c
    )
  SET_SOURCE_FILES_PROPERTIES(
    ${UNITTESTS_AREA_SOURCES}
    PROPERTIES LANGUAGE CXX
  )
  add_executable(unittests_area ${UNITTESTS_AREA_SOURCES})
  target_compile_definitions(unittests_area PRIVATE SERVER_MODE ${COMMON_DEFS})
  target_include_directories(unittests_area PRIVATE ${EP_INCLUDES})
  target_link_libraries(unittests_area LINK_PRIVATE cubrid)

  set(UNITTESTS_SNAPSHOT_SOURCES
      ${BASE_DIR}/memory_monitor_api.cpp
      ${BASE_DIR}/memory_monitor_sr.cpp
      ${EXECUTABLES_DIR}/unittests_snapshot.c
      )
      
    SET_SOURCE_FILES_PROPERTIES(
      ${UNITTESTS_SNAPSHOT_SOURCES}
      PROPERTIES LANGUAGE CXX
    )
    
    add_executable(unittests_snapshot ${UNITTESTS_SNAPSHOT_SOURCES})
    target_compile_definitions(unittests_snapshot PRIVATE SERVER_MODE ${COMMON_DEFS})
    target_include_directories(unittests_snapshot PRIVATE ${EP_INCLUDES})
    target_link_libraries(unittests_snapshot LINK_PRIVATE cubrid)
    
  set(UNITTESTS_BIT_SOURCES
    ${BASE_DIR}/memory_monitor_api.cpp
    ${BASE_DIR}/memory_monitor_sr.cpp
    ${EXECUTABLES_DIR}/unittests_bit.c
    )
    SET_SOURCE_FILES_PROPERTIES(
      ${UNITTESTS_BIT_SOURCES}
      PROPERTIES LANGUAGE CXX
    )
  add_executable(unittests_bit ${UNITTESTS_BIT_SOURCES})
  target_compile_definitions(unittests_bit PRIVATE ${COMMON_DEFS})
  target_include_directories(unittests_bit PRIVATE ${EP_INCLUDES})
  target_link_libraries(unittests_bit LINK_PRIVATE cubrid)
endif(UNIX)

# install
install(TARGETS
  cubridesql
  csql
  cub_master
  cub_server
  cubrid-bin
  cub_commdb
  cubrid_rel
  loadjava
  cub_javasp
  cub_admin
  cubrid_esql
  RUNTIME DESTINATION ${CUBRID_BINDIR} COMPONENT Application
  LIBRARY DESTINATION ${CUBRID_LIBDIR} COMPONENT Library
  PUBLIC_HEADER DESTINATION ${CUBRID_INCLUDEDIR} COMPONENT Header
  )

# install pdb files for debugging on windows
if(WIN32)
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/
    DESTINATION ${CUBRID_BINDIR} COMPONENT Debuginfo
    FILES_MATCHING PATTERN "*.pdb"
    )
endif(WIN32)
