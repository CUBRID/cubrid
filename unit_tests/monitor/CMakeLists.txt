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

# Project to test memory allocation.
#
#

set (TEST_MONITOR_SOURCES
  test_monitor_main.cpp
  )
set (TEST_MONITOR_HEADERS
  ${MONITOR_DIR}/monitor_collect.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_MONITOR_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_monitor
  ${TEST_MONITOR_SOURCES}
  ${TEST_MONITOR_HEADERS}
  )

target_compile_definitions(test_monitor PRIVATE
  ${COMMON_DEFS}
  SERVER_MODE
  )

target_include_directories(test_monitor PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_monitor PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_monitor PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_monitor PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Transfer channel unit testing is for unix/windows")
endif ()
