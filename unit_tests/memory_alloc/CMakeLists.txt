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
  test_main.cpp
  )
if(WIN32)
set (TEST_MEMORY_ALLOC_HEADERS
  test_db_private_alloc.hpp
  test_memory_alloc_helper.hpp
  )
endif(WIN32)

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

target_include_directories(test_memory_alloc PRIVATE
  ${EP_INCLUDES}
  )

if(UNIX)
  target_link_libraries(test_memory_alloc PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_memory_alloc PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Memory alloc unit test is only for unix/windows")
endif ()
