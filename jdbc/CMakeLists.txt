

add_custom_target(jdbc_build ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory src
  COMMAND ${CMAKE_COMMAND} -E touch src/CUBRID-JDBC-${CUBRID_VERSION}
  COMMAND ${ANT} dist-cubrid -buildfile ${CMAKE_SOURCE_DIR}/jdbc/build.xml -Dbasedir=. -Dversion=${CUBRID_VERSION} -Dsrc=${CMAKE_SOURCE_DIR}/src/jdbc
  COMMENT "Build JDBC driver with Ant ..."
  )
if(UNIX)
  add_custom_command(TARGET jdbc_build POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink JDBC-${CUBRID_VERSION}-cubrid.jar cubrid_jdbc.jar
  )
else(UNIX)
  add_custom_command(TARGET jdbc_build POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy JDBC-${CUBRID_VERSION}-cubrid.jar cubrid_jdbc.jar
  )
endif(UNIX)


install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/JDBC-${CUBRID_VERSION}-cubrid.jar
  ${CMAKE_CURRENT_BINARY_DIR}/cubrid_jdbc.jar
  ${CMAKE_CURRENT_BINARY_DIR}/JDBC-${CUBRID_VERSION}-cubrid-src.jar
  DESTINATION ${CUBRID_JDBCDIR})
