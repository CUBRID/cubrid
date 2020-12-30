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

add_custom_target(jdbc_build ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory src
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/${VERSION_FILE} src/CUBRID-JDBC-${BUILD_NUMBER}
  COMMAND ${ANT} dist-cubrid -buildfile ${CMAKE_SOURCE_DIR}/jdbc/build.xml -Dbasedir=. -Dversion=${BUILD_NUMBER} -Dsrc=${JDBC_DIR}
  COMMENT "Build JDBC driver with Ant ..."
  )
if(UNIX)
  add_custom_command(TARGET jdbc_build POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink JDBC-${BUILD_NUMBER}-cubrid.jar cubrid_jdbc.jar
    )
else(UNIX)
  add_custom_command(TARGET jdbc_build POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy JDBC-${BUILD_NUMBER}-cubrid.jar cubrid_jdbc.jar
    )
endif(UNIX)


install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/JDBC-${BUILD_NUMBER}-cubrid.jar
  ${CMAKE_CURRENT_BINARY_DIR}/cubrid_jdbc.jar
  ${CMAKE_CURRENT_BINARY_DIR}/JDBC-${BUILD_NUMBER}-cubrid-src.jar
  DESTINATION ${CUBRID_JDBCDIR} COMPONENT JDBC)
