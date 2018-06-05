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
  ${MONITOR_DIR}/monitor_collect.cpp
  #${MONITOR_DIR}/monitor_transaction.cpp     # engine dependencies
  )
set (TEST_MONITOR_HEADERS
  ${MONITOR_DIR}/monitor_collect.hpp
  ${MONITOR_DIR}/monitor_collect_forward.hpp
  ${MONITOR_DIR}/monitor_definition.hpp
  #${MONITOR_DIR}/monitor_transaction.hpp     # engine dependencies
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
  )
