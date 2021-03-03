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

project (request_client_server)

set (TEST_REQUEST_CS_SOURCES
  ${BASE_DIR}/mem_block.cpp
  ${BASE_DIR}/packer.cpp
  comm_channel_mock.cpp
  test_main.cpp
  )
set (TEST_REQUEST_CS_HEADERS
  comm_channel_mock.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
  ${TEST_REQUEST_CS_SOURCES}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_request_cs
  ${TEST_REQUEST_CS_SOURCES}
  ${TEST_REQUEST_CS_HEADERS}
  )

target_compile_options(test_request_cs PRIVATE ${compile_warning_flags})
target_compile_definitions(test_request_cs PRIVATE
  ${COMMON_DEFS}
  )

target_include_directories(test_request_cs PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_request_cs PRIVATE
  test_common
  )
if(WIN32)
  target_link_libraries(test_request_cs PRIVATE
  ws2_32
  )
endif(WIN32)

add_dependencies(test_request_cs
  ${CATCH2_TARGET}
  )
