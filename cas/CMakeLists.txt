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
