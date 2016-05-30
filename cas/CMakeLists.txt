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

set(LIBCAS_SOURCES
  ${BROKER_DIR}/cas.c 
  ${BROKER_DIR}/cas_network.c 
  ${BROKER_DIR}/cas_net_buf.c 
  ${BROKER_DIR}/cas_function.c 
  ${BROKER_DIR}/cas_execute.c 
  ${BROKER_DIR}/cas_handle.c 
  ${BROKER_DIR}/cas_util.c 
  ${BROKER_DIR}/cas_str_like.c 
  ${BROKER_DIR}/cas_xa.c 
  ${BROKER_DIR}/cas_schema_info.c 
  ${BROKER_DIR}/cas_log.c 
  ${BROKER_DIR}/cas_sql_log2.c 
  ${BROKER_DIR}/broker_filename.c 
  ${BROKER_DIR}/broker_util.c 
  ${BROKER_DIR}/cas_error.c 
  ${BROKER_DIR}/cas_meta.c
  )

if(WIN32)
  list(APPEND LIBCAS_SOURCES ${BROKER_DIR}/broker_wsa_init.c)
endif(WIN32)

add_library(cas STATIC ${LIBCAS_SOURCES})
if(UNIX)
  set_target_properties(cas PROPERTIES COMPILE_FLAGS "-fPIC")
endif(UNIX)

target_compile_definitions(cas PRIVATE LIBCAS_FOR_JSP)
if(WIN32)
  target_compile_definitions(cas PRIVATE _MT CAS)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    target_compile_definitions(cas PRIVATE _USE_32BIT_TIME_T)
  endif(TARGET_PLATFORM_BITS EQUAL 32)
endif(WIN32)
target_link_libraries(cas ${SYS_LIBS})
