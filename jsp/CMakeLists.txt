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
find_package(Java 1.5 COMPONENTS Development)

find_program(ANT ant PATHS $ENV{ANT_HOME} ENV PATH)
if(NOT ANT)
  message(FATAL_ERROR "The ANT was not found. check PATH or install ant to build JDBC")
endif(NOT ANT)

add_custom_target(jsp_build ALL
  COMMAND ${CMAKE_COMMAND} -E echo "copying ivy.xml to ${CMAKE_BINARY_DIR}/jsp/ivy.xml"
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/jsp/ivy.xml ${CMAKE_BINARY_DIR}/jsp/ivy.xml
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/jsp/ivysettings.xml ${CMAKE_BINARY_DIR}/jsp/ivysettings.xml
  COMMAND ${ANT} dist -buildfile ${CMAKE_SOURCE_DIR}/jsp/build.xml -Dbasedir=. -Dversion=${BUILD_NUMBER} -Dsrc.dir=${JSP_DIR}
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
)
