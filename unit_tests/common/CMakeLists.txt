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

# Unit test common lib.
#

set (TEST_COMMON_SOURCES
  mock_stream.cpp
  test_output.cpp
  test_perf_compare.cpp
  test_string_collection.cpp
  )
set (TEST_COMMON_HEADERS
  mock_stream.hpp
  test_debug.hpp
  test_output.hpp
  test_perf_compare.hpp
  test_string_collection.hpp
  test_timers.hpp
  )
SET_SOURCE_FILES_PROPERTIES(
    ${TEST_COMMON_SOURCES}
    PROPERTIES LANGUAGE CXX
  )

list (APPEND ${TEST_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR})

add_library(test_common STATIC
  ${TEST_COMMON_SOURCES}
  ${TEST_COMMON_HEADERS}
  )
set_target_properties(test_common PROPERTIES LINKER_LANGUAGE CXX)

target_compile_definitions(test_common PRIVATE
  ${COMMON_DEFS}
  )
