#
#
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

message ("[INFO] JDBC Driver built at cubrid-jdbc submodule")

set(JDBC_VERSION_FILE ${JDBC_DIR}/output/VERSION-DIST)
file(STRINGS ${JDBC_VERSION_FILE} JDBC_VERSION_STR)
execute_process (
    COMMAND ${CMAKE_COMMAND} -E copy ${JDBC_DIR}/JDBC-${JDBC_VERSION_STR}-cubrid.jar ${JSP_LIB_DIR}/cubrid-jdbc-${JDBC_VERSION_STR}.jar
)

if (NOT EXISTS ${JSP_LIB_DIR}/cubrid-jdbc-${JDBC_VERSION_STR}.jar)
    message (FATAL_ERROR "Could not copy JDBC driver")
endif (NOT EXISTS ${JSP_LIB_DIR}/cubrid-jdbc-${JDBC_VERSION_STR}.jar)
