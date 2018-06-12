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

set (TEST_MEMORY_ALLOC_SOURCES
  test_db_private_alloc.cpp
  test_extensible_array.cpp
  test_main.cpp
  test_memory_alloc_helper.cpp
  )
set (TEST_MEMORY_ALLOC_HEADERS
  test_db_private_alloc.hpp
  test_extensible_array.hpp
  test_memory_alloc_helper.hpp
  )

SET_SOURCE_FILES_PROPERTIES(
    ${TEST_MEMORY_ALLOC_SOURCES}
    PROPERTIES LANGUAGE CXX
  )

add_executable(test_memory_alloc
  ${TEST_MEMORY_ALLOC_SOURCES}
  ${TEST_MEMORY_ALLOC_HEADERS}
  )

target_compile_definitions(test_memory_alloc PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

message (STATUS "test includes:  ${TEST_INCLUDES}")
target_include_directories(test_memory_alloc PRIVATE
  ${TEST_INCLUDES}
#  ${EP_INCLUDES}
  )

target_link_libraries(test_memory_alloc LINK_PRIVATE
  test_common
  )

if(UNIX)
  target_link_libraries(test_memory_alloc LINK_PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_memory_alloc LINK_PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Memory alloc unit test is only for unix/windows")
endif ()
