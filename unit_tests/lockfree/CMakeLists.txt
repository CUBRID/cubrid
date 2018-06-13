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

set (TEST_LOCKFREE_SOURCES
  test_main.cpp
  test_cqueue_functional.cpp
)
set (TEST_LOCKFREE_HEADERS
  test_cqueue_functional.hpp
)
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_LOCKFREE_SOURCES}
  PROPERTIES LANGUAGE CXX
)

add_executable(test_lockfree
  ${TEST_LOCKFREE_SOURCES}
  ${TEST_LOCKFREE_HEADERS}
  )

target_compile_definitions(test_lockfree PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_lockfree PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_lockfree LINK_PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_lockfree LINK_PRIVATE
    cubrid
    )
elseif(WIN32)
  target_link_libraries(test_lockfree LINK_PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Lockfree unit testing is for unix/windows")
endif ()
