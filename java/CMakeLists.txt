

add_custom_target(jsp_build ALL
  COMMAND ${ANT} dist -buildfile ${CMAKE_SOURCE_DIR}/java/build.xml -Dbasedir=. -Dversion=${CUBRID_VERSION} -Dsrc=${CMAKE_SOURCE_DIR}/src/jdbc
  COMMENT "Build JSP driver with Ant ..."
  )


install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/jspserver.jar
  DESTINATION ${CUBRID_JAVADIR})
