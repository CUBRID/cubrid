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

set(LIBCUBRIDESQL_SOURCES
  ${EXECUTABLES_DIR}/esql_gadget.c
  ${EXECUTABLES_DIR}/esql_cli.c
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
if(UNIX)
  add_dependencies(cubridesql ${EP_TARGETS})
endif(UNIX)
add_dependencies(cubridesql cubridcs)

set(CSQL_SOURCES
  ${EXECUTABLES_DIR}/csql_launcher.c
  ${EXECUTABLES_DIR}/util_support.c
  ${BASE_DIR}/porting.c
  ${BASE_DIR}/getopt_long.c
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


set(CUB_MASTER_SOURCES
  ${EXECUTABLES_DIR}/master.c
  ${EXECUTABLES_DIR}/master_request.c
  ${EXECUTABLES_DIR}/master_util.c
  )
if(UNIX)
  list(APPEND CUB_MASTER_SOURCES ${EXECUTABLES_DIR}/master_heartbeat.c)
else(UNIX)
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
if(WIN32)
  list(APPEND CUB_SERVER_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cub_server ${CUB_SERVER_SOURCES})
target_include_directories(cub_server PRIVATE ${EP_INCLUDES})
target_link_libraries(cub_server LINK_PRIVATE cubrid ${CURSES_LIBRARIES})
if(WIN32)
  target_link_libraries(cub_server LINK_PRIVATE dbghelp)
endif(WIN32)


set(CUBRID_SOURCES
  ${EXECUTABLES_DIR}/util_service.c
  ${EXECUTABLES_DIR}/util_support.c
  )
if(WIN32)
  list(APPEND CUBRID_SOURCES ${BASE_DIR}/getopt_long.c)
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
if(WIN32)
  list(APPEND CUB_COMMDB_SOURCES ${BASE_DIR}/getopt_long.c)
  list(APPEND CUB_COMMDB_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cub_commdb ${CUB_COMMDB_SOURCES})
target_link_libraries(cub_commdb LINK_PRIVATE cubridcs)
if(WIN32)
  target_link_libraries(cub_commdb LINK_PRIVATE ws2_32)
endif(WIN32)


set(CUBRID_REL_SOURCES ${EXECUTABLES_DIR}/cubrid_version.c)
if(WIN32)
  list(APPEND CUBRID_REL_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cubrid_rel ${CUBRID_REL_SOURCES})
target_link_libraries(cubrid_rel LINK_PRIVATE cubridsa)


set(LOADJAVA_SOURCES ${EXECUTABLES_DIR}/loadjava.c)
if(WIN32)
  list(APPEND LOADJAVA_SOURCES ${BASE_DIR}/getopt_long.c)
  list(APPEND LOADJAVA_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(loadjava ${LOADJAVA_SOURCES})
if(UNIX)
  target_link_libraries(loadjava LINK_PRIVATE cubridsa)
else(UNIX)
  target_link_libraries(loadjava LINK_PRIVATE cubridcs)
endif(UNIX)


set(CUB_ADMIN_SOURCES
  ${EXECUTABLES_DIR}/util_admin.c
  ${EXECUTABLES_DIR}/util_support.c
  ${BASE_DIR}/getopt_long.c
  ${BASE_DIR}/porting.c
  )
if(WIN32)
  list(APPEND CUB_ADMIN_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cub_admin ${CUB_ADMIN_SOURCES})
target_include_directories(cub_admin PRIVATE ${EP_INCLUDES})
target_link_libraries(cub_admin LINK_PRIVATE ${CMAKE_DL_LIBS})
if(UNIX)
  add_dependencies(cub_admin ${EP_TARGETS})
else(UNIX)
  target_link_libraries(cub_admin LINK_PRIVATE ws2_32)
endif(UNIX)

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
if(WIN32)
  list(APPEND CUBRID_ESQL_SOURCES ${BASE_DIR}/getopt_long.c)
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
if(USE_BISON_FLEX)
  add_dependencies(cubrid_esql gen_esql_grammar gen_esql_lexer)
endif(USE_BISON_FLEX)


set(GENCAT_SOURCES
  ${BASE_DIR}/porting.c
  ${EXECUTABLES_DIR}/gencat.c
  )
if(WIN32)
  list(APPEND GENCAT_SOURCES ${BASE_DIR}/getopt_long.c)
  list(APPEND GENCAT_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(gencat ${GENCAT_SOURCES})
if(WIN32)
  # TODO: move queue.h to src/?
  target_include_directories(gencat PRIVATE ${CMAKE_SOURCE_DIR}/win/gencat)
  target_link_libraries(gencat LINK_PRIVATE ws2_32)
endif(WIN32)


set(CONVERT_PASSWORD_SOURCES ${EXECUTABLES_DIR}/convert_password.c)
if(WIN32)
  list(APPEND CONVERT_PASSWORD_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(convert_password ${CONVERT_PASSWORD_SOURCES})
target_include_directories(convert_password PRIVATE ${EP_INCLUDES})
target_link_libraries(convert_password LINK_PRIVATE cubridsa)


if(UNIX)
  set(UNITTESTS_LF_SOURCES
    ${BASE_DIR}/porting.c
    ${BASE_DIR}/lock_free.c
    ${EXECUTABLES_DIR}/unittests_lf.c
    )
  add_executable(unittests_lf ${UNITTESTS_LF_SOURCES})
  target_compile_definitions(unittests_lf PRIVATE UNITTEST_LF SERVER_MODE ${COMMON_DEFS})
  target_include_directories(unittests_lf PRIVATE ${EP_INCLUDES})
  target_link_libraries(unittests_lf LINK_PRIVATE cubridcs)


  set(UNITTESTS_AREA_SOURCES
    ${EXECUTABLES_DIR}/unittests_area.c
    )
  add_executable(unittests_area ${UNITTESTS_AREA_SOURCES})
  target_compile_definitions(unittests_area PRIVATE SERVER_MODE ${COMMON_DEFS})
  target_include_directories(unittests_area PRIVATE ${EP_INCLUDES})
  target_link_libraries(unittests_area LINK_PRIVATE cubrid ${CURSES_LIBRARIES})

  set(UNITTESTS_CQUEUE_SOURCES
    ${EXECUTABLES_DIR}/unittests_cqueue.c
    )
  add_executable(unittests_cqueue ${UNITTESTS_CQUEUE_SOURCES})
  target_compile_definitions(unittests_cqueue PRIVATE UNITTEST_CQ SERVER_MODE ${COMMON_DEFS})
  target_include_directories(unittests_cqueue PRIVATE ${EP_INCLUDES})
  target_link_libraries(unittests_cqueue LINK_PRIVATE cubrid ${CURSES_LIBRARIES})
  
  set(UNITTESTS_SNAPSHOT_SOURCES
      ${EXECUTABLES_DIR}/unittests_snapshot.c
      )
    add_executable(unittests_snapshot ${UNITTESTS_SNAPSHOT_SOURCES})
    target_compile_definitions(unittests_snapshot PRIVATE SERVER_MODE ${COMMON_DEFS})
    target_include_directories(unittests_snapshot PRIVATE ${EP_INCLUDES})
    target_link_libraries(unittests_snapshot LINK_PRIVATE cubrid ${CURSES_LIBRARIES})
    
  set(UNITTESTS_BIT_SOURCES
    ${EXECUTABLES_DIR}/unittests_bit.c
    )
  add_executable(unittests_bit ${UNITTESTS_BIT_SOURCES})
  target_compile_definitions(unittests_bit PRIVATE ${COMMON_DEFS})
  target_include_directories(unittests_bit PRIVATE ${EP_INCLUDES})
  target_link_libraries(unittests_bit LINK_PRIVATE cubrid ${CURSES_LIBRARIES})
endif(UNIX)


# compat utils
if(UNIX)
  add_executable(addvoldb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(backupdb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(checkdb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(commdb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(compactdb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(copydb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(createdb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(deletedb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(installdb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(killtran ${EXECUTABLES_DIR}/util_front.c)
  add_executable(loaddb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(lockdb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(optimizedb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(renamedb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(restoredb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(spacedb ${EXECUTABLES_DIR}/util_front.c)
  add_executable(sqlx ${EXECUTABLES_DIR}/util_front.c)
  add_executable(unloaddb ${EXECUTABLES_DIR}/util_front.c)
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
  cub_admin
  cubrid_esql
  convert_password
  RUNTIME DESTINATION ${CUBRID_BINDIR} COMPONENT Application
  LIBRARY DESTINATION ${CUBRID_LIBDIR} COMPONENT Library
  PUBLIC_HEADER DESTINATION ${CUBRID_INCLUDEDIR} COMPONENT Header
  )

# install compats
if(UNIX)
  install(TARGETS
    addvoldb
    backupdb
    checkdb
    commdb
    compactdb
    copydb
    createdb
    deletedb
    installdb
    killtran
    loaddb
    lockdb
    optimizedb
    renamedb
    restoredb
    spacedb
    sqlx
    unloaddb
    RUNTIME DESTINATION ${CUBRID_COMPATDIR} COMPONENT Application
    )
  # TODO: compat scripts start|stop_cubrid?
endif(UNIX)


# install pdb files for debugging on windows
if(WIN32)
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/
    DESTINATION ${CUBRID_BINDIR} COMPONENT Debuginfo
    FILES_MATCHING PATTERN "*.pdb"
    )
endif(WIN32)
