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

if(UNIX)
  install(PROGRAMS
    make_locale.sh
    DESTINATION ${CUBRID_BINDIR})
  install(PROGRAMS
    loclib/build_locale.sh
    DESTINATION ${CUBRID_SHLOCALESDIR}/loclib)
else(UNIX)
  set(MAKE_LOCALE_SCRIPT make_locale_x64.bat)
  set(MAKE_LOCLIB_DIR loclib_win_x64)
  install(PROGRAMS
    ${MAKE_LOCALE_SCRIPT}
    DESTINATION ${CUBRID_BINDIR}
    RENAME make_locale.bat)
  install(FILES
    ${MAKE_LOCLIB_DIR}/makefile
    DESTINATION ${CUBRID_SHLOCALESDIR}/loclib)
endif(UNIX)

install(DIRECTORY
  data
  DESTINATION ${CUBRID_SHLOCALESDIR}
  FILES_MATCHING PATTERN "*.txt" PATTERN "*.xml" PATTERN "*.TXT"
  PATTERN "CMakeFiles" EXCLUDE)

install(FILES
  ${CMAKE_SOURCE_DIR}/src/base/locale_lib_common.h
  DESTINATION ${CUBRID_SHLOCALESDIR}/loclib COMPONENT Header)

