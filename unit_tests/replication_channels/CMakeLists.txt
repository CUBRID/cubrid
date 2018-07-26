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

project (replication_channels)

set (TEST_REPLICATION_CHANNELS_SRC
  test_main.cpp
  master_replication_channel_mock.cpp
  slave_replication_channel_mock.cpp
	cub_master_mock.cpp
  )
set (TEST_REPLICATION_CHANNELS_H
  slave_replication_channel_mock.hpp
  master_replication_channel_mock.hpp
	cub_master_mock.hpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_REPLICATION_CHANNELS_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_replication_channels
  ${TEST_REPLICATION_CHANNELS_SRC}
  ${TEST_REPLICATION_CHANNELS_H}
  )

target_compile_definitions(test_replication_channels PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories(test_replication_channels PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_replication_channels PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_replication_channels PRIVATE
    cubrid
    )
elseif(WIN32)
	target_link_libraries(test_replication_channels PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Replication channel unit testing is for unix/windows")
endif ()

