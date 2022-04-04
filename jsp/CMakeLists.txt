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
include(FindJava)
find_package(Java 1.6 COMPONENTS Development)

find_program(ANT ant PATHS $ENV{ANT_HOME} ENV PATH)
if(NOT ANT)
  message(FATAL_ERROR "The ANT was not found. check PATH or install ant to build JDBC")
endif(NOT ANT)

add_custom_target(jsp_build ALL)

if (EXISTS ${CMAKE_SOURCE_DIR}/cubrid-jdbc/src)
  set(JDBC_VERSION_FILE ${JDBC_DIR}/output/VERSION-DIST)
  file(STRINGS ${JDBC_VERSION_FILE} JDBC_VERSION_STR)

  set(JDBC_DIST ${JDBC_DIR}/JDBC-${JDBC_VERSION_STR}-cubrid.jar)

  add_custom_command(TARGET jsp_build PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "copying ${JDBC_DIST}/cubrid-jdbc-${JDBC_VERSION_STR}.jar"
    COMMAND ${CMAKE_COMMAND} -E copy ${JDBC_DIST} ${CMAKE_BINARY_DIR}/jsp/lib/cubrid-jdbc-${JDBC_VERSION_STR}.jar
    MAIN_DEPENDENCY ${JDBC_DIST}
  )
endif(EXISTS ${CMAKE_SOURCE_DIR}/cubrid-jdbc/src)

# download ivy
message("Download ivy ...")
set(IVY_VERSION "2.4.0")
set(IVY_URL "https://archive.apache.org/dist/ant/ivy/${IVY_VERSION}/maven2/${IVY_VERSION}/ivy-${IVY_VERSION}.jar")
set(IVY_DIR "${CMAKE_BINARY_DIR}/jsp/external")
set(IVY_FILENAME "${IVY_DIR}/ivy.jar")
file(DOWNLOAD ${IVY_URL} ${IVY_FILENAME})

add_custom_command(TARGET jsp_build POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "copying ivy.xml to ${CMAKE_BINARY_DIR}/jsp/ivy.xml"
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/jsp/ivy.xml ${CMAKE_BINARY_DIR}/jsp/ivy.xml
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/jsp/ivysettings.xml ${CMAKE_BINARY_DIR}/jsp/ivysettings.xml
  COMMAND ${ANT} dist -buildfile ${CMAKE_SOURCE_DIR}/jsp/build.xml -Dbasedir=. -Dversion=${BUILD_NUMBER} -Dsrc.dir=${JSP_DIR} -Divy.install.dir="${IVY_DIR}"
  COMMENT "Build Java SP Server with Ant ..."
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/jspserver.jar
  ${JSP_DIR}/logging.properties
  DESTINATION ${CUBRID_JAVADIR}
)

install(DIRECTORY
  ${CMAKE_CURRENT_BINARY_DIR}/lib/
  DESTINATION ${CUBRID_JAVADIR}/lib
  FILES_MATCHING PATTERN "*.jar"
)
