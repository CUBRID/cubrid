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

set(LIBCAS_SOURCES
  ${BROKER_DIR}/cas.c 
  ${BROKER_DIR}/cas_network.c 
  ${BROKER_DIR}/cas_net_buf.c 
  ${BROKER_DIR}/cas_function.c 
  ${BROKER_DIR}/cas_execute.c 
  ${BROKER_DIR}/cas_handle.c 
  ${BROKER_DIR}/cas_handle_procedure.cpp 
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
  ${BROKER_DIR}/cas_ssl.c
  )
set(LIBCAS_HEADERS
  ${BROKER_DIR}/cas_handle_procedure.hpp
  )

if(WIN32)
  list(APPEND LIBCAS_SOURCES ${BROKER_DIR}/broker_wsa_init.c)
endif(WIN32)

SET_SOURCE_FILES_PROPERTIES(
  ${LIBCAS_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_library(cas STATIC ${LIBCAS_SOURCES} ${LIBCAS_HEADERS})
if(UNIX)
  set_target_properties(cas PROPERTIES COMPILE_FLAGS "-fPIC")
endif(UNIX)

target_compile_definitions(cas PRIVATE LIBCAS_FOR_JSP)
target_include_directories(cas PRIVATE ${EP_INCLUDES})
if(WIN32)
  target_compile_definitions(cas PRIVATE _MT CAS)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    target_compile_definitions(cas PRIVATE _USE_32BIT_TIME_T)
  endif(TARGET_PLATFORM_BITS EQUAL 32)
endif(WIN32)
target_link_libraries(cas ${SYS_LIBS} ${LIBOPENSSL_LIBS})

add_dependencies(cas ${EP_TARGETS})
