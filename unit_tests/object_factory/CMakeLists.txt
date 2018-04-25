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

project (test_object_factory)

set (TEST_OBJECT_FACTORY_SRC
  test_main.cpp
  test_object_factory.cpp
  )
set (TEST_OBJECT_FACTORY_HPP
  test_object_factory.hpp
  )
SET_SOURCE_FILES_PROPERTIES(
  ${TEST_OBJECT_FACTORY_SRC}
  PROPERTIES LANGUAGE CXX
  )

add_executable(test_object_factory
  ${TEST_OBJECT_FACTORY_SRC}
  ${TEST_OBJECT_FACTORY_HPP}
  )

target_compile_definitions(test_object_factory PRIVATE
  SERVER_MODE
  ${COMMON_DEFS}
  )

target_include_directories(test_object_factory PRIVATE
  ${TEST_INCLUDES}
  )

target_link_libraries(test_object_factory PRIVATE
  test_common
  )
if(UNIX)
  target_link_libraries(test_object_factory PRIVATE
    cubrid
    )
elseif(WIN32)
	target_link_libraries(test_object_factory PRIVATE
    cubrid-win-lib
    )
else()
  message( SEND_ERROR "Object factory unit testing is for unix/windows")
endif ()
