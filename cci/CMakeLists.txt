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

set(CASCCI_SOURCES
  ${BASE_DIR}/porting.c
  ${CCI_DIR}/cas_cci.c
  ${CCI_DIR}/cci_util.c
  ${CCI_DIR}/cci_query_execute.c
  ${CCI_DIR}/cci_net_buf.c
  ${CCI_DIR}/cci_network.c
  ${CCI_DIR}/cci_handle_mng.c
  ${CCI_DIR}/cci_t_set.c
  ${CCI_DIR}/cci_t_lob.c
  ${CCI_DIR}/cci_common.c
  ${CCI_DIR}/cci_log.cpp
  ${CCI_DIR}/cci_map.cpp
  ${CCI_DIR}/cci_properties.c
  )
if(WIN32)
  list(APPEND CASCCI_SOURCES ${CCI_DIR}/cci_wsa_init.c)
  list(APPEND CASCCI_SOURCES ${BASE_DIR}/rand.c)
endif(WIN32)

SET_SOURCE_FILES_PROPERTIES(
  ${CASCCI_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_library(cascci SHARED ${CASCCI_SOURCES})
set_target_properties(cascci PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")
set_target_properties(cascci PROPERTIES PUBLIC_HEADER "${CCI_DIR}/cas_cci.h;${BROKER_DIR}/cas_error.h")
target_include_directories(cascci PRIVATE ${EP_INCLUDES})
target_link_libraries(cascci LINK_PRIVATE ${EP_LIBS} ${CMAKE_THREAD_LIBS_INIT})
if(WIN32)
  target_compile_definitions(cascci PRIVATE CAS_CCI_DL)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    target_compile_definitions(cascci PRIVATE _USE_32BIT_TIME_T)
  endif(TARGET_PLATFORM_BITS EQUAL 32)
  # TODO: remove version.h in win/ directory
  target_include_directories(cascci PRIVATE ${CMAKE_SOURCE_DIR}/win)
  set_target_properties(cascci PROPERTIES LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/win/cascci/cascci.def\"" LINK_FLAGS_RELEASE "/NODEFAULTLIB:libcmt.lib" LINK_FLAGS_DEBUG "/NODEFAULTLIB:msvcrt.lib")
  target_link_libraries(cascci LINK_PRIVATE ws2_32)
endif(WIN32)

add_dependencies(cascci ${EP_TARGETS})

install(TARGETS cascci
  RUNTIME DESTINATION ${CUBRID_BINDIR} COMPONENT CCI
  LIBRARY DESTINATION ${CUBRID_LIBDIR} COMPONENT CCI
  ARCHIVE DESTINATION ${CUBRID_LIBDIR} COMPONENT CCI
  PUBLIC_HEADER DESTINATION ${CUBRID_INCLUDEDIR} COMPONENT CCI
  )

# install pdb files for debugging on windows
if(WIN32)
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/
    DESTINATION ${CUBRID_BINDIR} COMPONENT Debuginfo
    FILES_MATCHING PATTERN "*.pdb"
    )
endif(WIN32)
