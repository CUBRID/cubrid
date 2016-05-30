#
# Copyright (C) 2016 Search Solution Corporation. All rights reserved.
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

add_custom_target(jdbc_build ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory src
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/${VERSION_FILE} src/CUBRID-JDBC-${BUILD_NUMBER}
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
  DESTINATION ${CUBRID_JDBCDIR})
