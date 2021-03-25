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

project (test_recovery)

set (RECOVERY_SRC
  ${TRANSACTION_DIR}/log_recovery_redo_parallel.cpp
  main.cpp
  )

set (RECOVERY_H
  ${TRANSACTION_DIR}/log_recovery_redo.hpp
  ${TRANSACTION_DIR}/log_recovery_redo_parallel.hpp
  ut_database.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${RECOVERY_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_recovery
  ${RECOVERY_SRC}
  ${RECOVERY_H}
  )

target_compile_options(test_recovery PRIVATE ${compile_warning_flags})
target_compile_definitions(test_recovery PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_recovery PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_recovery PRIVATE test_common )
if(UNIX)
  target_link_libraries(test_recovery PRIVATE cubrid )
elseif(WIN32)
  target_link_libraries(test_recovery PRIVATE cubrid-win-lib )
else()
  message( SEND_ERROR "Recovery only works on UNIX/WIN32")
endif ()

