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

set(CMSTAT_SOURCES
  ${CM_COMMON_DIR}/cm_mem_cpu_stat.c
  ${CM_COMMON_DIR}/cm_broker_admin.c
  ${CM_COMMON_DIR}/cm_errmsg.c
  ${CM_COMMON_DIR}/cm_utils.c
  ${CM_COMMON_DIR}/cm_broker_jni.c
  )
if(WIN32)
  list(APPEND ${CMAKE_SOURCE_DIR}/win/cmstat/version.rc)
endif(WIN32)
add_library(cmstat SHARED ${CMSTAT_SOURCES})
set_target_properties(cmstat PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")
set_target_properties(cmstat PROPERTIES PUBLIC_HEADER "${CM_COMMON_DIR}/cm_stat.h")
target_include_directories(cmstat PRIVATE ${JAVA_INC})
if(NOT USE_CUBRID_ENV)
  target_compile_definitions(cmstat PRIVATE ${DIR_DEFS})
endif(NOT USE_CUBRID_ENV)
target_link_libraries(cmstat LINK_PRIVATE brokeradmin)
if(${CMAKE_SYSTEM_NAME} MATCHES "AIX")
  target_link_libraries(cmstat LINK_PRIVATE perfstat)
elseif(WIN32)
  set_target_properties(cmstat PROPERTIES LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/win/cmstat/cmstat.def\"")
  target_link_libraries(cmstat LINK_PRIVATE ws2_32 psapi cubridcs)
endif()


set(CMDEP_SOURCES
  ${CM_COMMON_DIR}/cm_dep_tasks.c
  ${CM_COMMON_DIR}/cm_dstring.c
  ${CM_COMMON_DIR}/cm_nameval.c
  ${CM_COMMON_DIR}/cm_utils.c
  )
add_library(cmdep SHARED ${CMDEP_SOURCES})
set_target_properties(cmdep PROPERTIES SOVERSION "${CUBRID_MAJOR_VERSION}.${CUBRID_MINOR_VERSION}")
set_target_properties(cmdep PROPERTIES PUBLIC_HEADER "${CM_COMMON_DIR}/cm_dep.h")
if(NOT USE_CUBRID_ENV)
  target_compile_definitions(cmdep PRIVATE ${DIR_DEFS})
endif(NOT USE_CUBRID_ENV)
target_link_libraries(cmdep LINK_PRIVATE cubridcs)
if(WIN32)
  set_target_properties(cmdep PROPERTIES LINK_FLAGS "/DEF:\"${CMAKE_SOURCE_DIR}/win/cmdep/cmdep.def\"")
  target_link_libraries(cmdep LINK_PRIVATE ws2_32)
endif()


set(CUB_JOBSA_SOURCES
  ${CM_COMMON_DIR}/cm_class_info_sa.c
  )
if(WIN32)
  list(APPEND CUB_JOBSA_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cub_jobsa ${CUB_JOBSA_SOURCES})
target_link_libraries(cub_jobsa cubridsa)


set(CUB_SAINFO_SOURCES
  ${CM_COMMON_DIR}/cm_trigger_info_sa.c
  )
if(WIN32)
  list(APPEND CUB_SAINFO_SOURCES ${CMAKE_BINARY_DIR}/version.rc)
endif(WIN32)
add_executable(cub_sainfo ${CUB_JOBSA_SOURCES})
target_link_libraries(cub_sainfo cubridsa)

install(TARGETS cmstat cmdep cub_jobsa cub_sainfo
  RUNTIME DESTINATION ${CUBRID_BINDIR}
  LIBRARY DESTINATION ${CUBRID_LIBDIR}
  ARCHIVE DESTINATION ${CUBRID_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CUBRID_INCLUDEDIR}
  )


# install pdb files for debugging on windows
if(WIN32)
  install(DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/
    DESTINATION ${CUBRID_BINDIR}
    FILES_MATCHING PATTERN "*.pdb"
    )
endif(WIN32)
