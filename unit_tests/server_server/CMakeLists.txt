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

project (server_server)

# copied from library cubrid, without communication_channel.cpp, which is mocked here
add_subdirectory(cubrid_wo_cc)

set (TEST_SERVER_SERVER_SOURCES
  comm_channel_mock.cpp
  test_main.cpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_SERVER_SERVER_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_server_server
  ${TEST_SERVER_SERVER_SOURCES}
  )

target_compile_options(test_server_server PRIVATE ${compile_warning_flags})
target_compile_definitions(test_server_server PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories(test_server_server PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_server_server PRIVATE
  cubrid_wo_cc
#  cubrid
  test_common
  )
