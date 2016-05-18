
install(FILES ${CMAKE_SOURCE_DIR}/demo/demodb_objects ${CMAKE_SOURCE_DIR}/demo/demodb_schema
  DESTINATION ${CUBRID_DEMODIR}) 

if(UNIX)
  set(demodb_script make_cubrid_demo.sh)
else(UNIX)
  set(demodb_script make_cubrid_demo.bat)
endif(UNIX)
if(USE_CUBRID_ENV OR WIN32)
  install(PROGRAMS ${CMAKE_SOURCE_DIR}/demo/${demodb_script}
    DESTINATION ${CUBRID_DEMODIR})
else(USE_CUBRID_ENV OR WIN32)
  file(READ ${CMAKE_SOURCE_DIR}/demo/${demodb_script} scr)
  string(REPLACE "-z \"$CUBRID\"" "! -d ${CUBRID_PREFIXDIR}/${CUBRID_DEMODIR}" mod_scr "${scr}")
  string(REPLACE "$CUBRID/demo" "${CUBRID_PREFIXDIR}/${CUBRID_DEMODIR}" mod_scr "${mod_scr}" )
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${demodb_script} ${mod_scr})
  install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${demodb_script}
    DESTINATION ${CUBRID_DEMODIR})
endif(USE_CUBRID_ENV OR WIN32)
