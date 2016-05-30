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

install(FILES ${CMAKE_SOURCE_DIR}/demo/demodb_objects ${CMAKE_SOURCE_DIR}/demo/demodb_schema
  DESTINATION ${CUBRID_DEMODIR}) 

if(UNIX)
  set(DEMODB_SCRIPT make_cubrid_demo.sh)
else(UNIX)
  set(DEMODB_SCRIPT make_cubrid_demo.bat)
endif(UNIX)
if(USE_CUBRID_ENV OR WIN32)
  install(PROGRAMS ${CMAKE_SOURCE_DIR}/demo/${DEMODB_SCRIPT}
    DESTINATION ${CUBRID_DEMODIR})
else(USE_CUBRID_ENV OR WIN32)
  file(READ ${CMAKE_SOURCE_DIR}/demo/${DEMODB_SCRIPT} scr)
  string(REPLACE "-z \"$CUBRID\"" "! -d ${CUBRID_PREFIXDIR}/${CUBRID_DEMODIR}" mod_scr "${scr}")
  string(REPLACE "$CUBRID/demo" "${CUBRID_PREFIXDIR}/${CUBRID_DEMODIR}" mod_scr "${mod_scr}" )
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${DEMODB_SCRIPT} ${mod_scr})
  install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${DEMODB_SCRIPT}
    DESTINATION ${CUBRID_DEMODIR})
endif(USE_CUBRID_ENV OR WIN32)
