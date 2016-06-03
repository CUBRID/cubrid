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

if(UNIX)
  install(PROGRAMS
    make_locale.sh
    DESTINATION ${CUBRID_BINDIR})
  install(PROGRAMS
    loclib/build_locale.sh
    DESTINATION ${CUBRID_SHLOCALESDIR}/loclib)
else(UNIX)
  if(TARGET_PLATFORM_BITS EQUAL 32)
    set(MAKE_LOCALE_SCRIPT make_locale_Win32.bat)
    set(MAKE_LOCLIB_DIR loclib_win_Win32)
  else(TARGET_PLATFORM_BITS EQUAL 32)
    set(MAKE_LOCALE_SCRIPT make_locale_Win32.bat)
    set(MAKE_LOCLIB_DIR loclib_win_x64)
  endif(TARGET_PLATFORM_BITS EQUAL 32)
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
  DESTINATION ${CUBRID_SHLOCALESDIR}/loclib)

