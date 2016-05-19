
file(GLOB conf_files "${CMAKE_SOURCE_DIR}/conf/cubrid*.conf*" "${CMAKE_SOURCE_DIR}/conf/shard*.txt")
file(GLOB locale_files "${CMAKE_SOURCE_DIR}/conf/cubrid*.txt")

install(FILES ${conf_files} ${locale_files}
  DESTINATION ${CUBRID_CONFDIR})
