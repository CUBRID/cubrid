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

install(FILES 
  ${CMAKE_SOURCE_DIR}/demo/demodb_objects 
  ${CMAKE_SOURCE_DIR}/demo/demodb_schema
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
