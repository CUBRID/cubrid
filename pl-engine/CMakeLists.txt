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

# Find gradlew
if(UNIX)
  set(GRADLE_EXECUTABLE ${CMAKE_CURRENT_SOURCE_DIR}/gradlew)
else(UNIX)
  set(GRADLE_EXECUTABLE ${CMAKE_CURRENT_SOURCE_DIR}/gradlew.bat)
endif(UNIX)

set(PL_ENGINE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/pl-engine)
set(PL_ENGINE_BUILD_DIR ${CMAKE_BINARY_DIR}/pl-engine)

set(LOCAL_JDBC_PATH_COMMAND)

add_custom_target(pl_build ALL)

if (EXISTS ${CMAKE_SOURCE_DIR}/cubrid-jdbc/src)
  set(JDBC_DIR ${CMAKE_SOURCE_DIR}/cubrid-jdbc)
  set(PL_LIB_DIR ${PL_ENGINE_BUILD_DIR}/lib)

  add_custom_command(TARGET pl_build
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PL_LIB_DIR}
    COMMAND ${CMAKE_COMMAND} -DJDBC_DIR=${JDBC_DIR} -DPL_LIB_DIR=${PL_LIB_DIR} -P ${CMAKE_SOURCE_DIR}/jsp/cmake/copy_submodule_jdbc.cmake
  )
  
  set(LOCAL_JDBC_PATH_COMMAND "-PcubridJdbcPath=${PL_LIB_DIR}")

  add_dependencies(pl_build jdbc_build)
endif(EXISTS ${CMAKE_SOURCE_DIR}/cubrid-jdbc/src)

add_custom_command(TARGET pl_build
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PL_ENGINE_BUILD_DIR}
  COMMAND ${GRADLE_EXECUTABLE} build -p ${PL_ENGINE_SOURCE_DIR} -PbuildDir=${PL_ENGINE_BUILD_DIR} ${LOCAL_JDBC_PATH_COMMAND}
)

add_custom_target(pl_unittest)
add_custom_command(TARGET pl_unittest
  COMMAND ${GRADLE_EXECUTABLE} test -p ${PL_ENGINE_SOURCE_DIR} -DbuildDir=${PL_ENGINE_BUILD_DIR} ${LOCAL_JDBC_PATH_COMMAND}
)
add_dependencies(pl_unittest pl_build)

install(FILES
  ${PL_ENGINE_BUILD_DIR}/libs/pl-server.jar
  ${PL_ENGINE_BUILD_DIR}/resources/main/logging.properties
  DESTINATION ${CUBRID_JAVADIR}
)