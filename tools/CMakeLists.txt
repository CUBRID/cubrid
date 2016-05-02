
if(UNIX)
  set(CCI_APPLIER_SOURCES
    ${TOOLS_DIR}/cci_applier.c
    ${BASE_DIR}/getopt_long.c
    )
  add_executable(cci_applier ${CCI_APPLIER_SOURCES})
  target_link_libraries(cci_applier cascci)

  install(TARGETS cci_applier
    RUNTIME DESTINATION ${CUBRID_BINDIR})
endif(UNIX)
