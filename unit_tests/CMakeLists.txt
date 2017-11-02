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

# unit tests logic:
# - all: cmake -DUNIT_TESTS=ON <path>
# - some: cmake -DUNIT_TEST_ABC=ON -DUNIT_TEST_XYZ=ON <path>

project(Test)

set (TEST_INCLUDES
  ${EP_INCLUDES}
  ${CMAKE_CURRENT_SOURCE_DIR}/common  # todo: find a better solution
  )

option (UNIT_TEST_MEMORY_ALLOC  "Unit testing for memory allocation")
option (UNIT_TEST_STRING_BUFFER "Unit testing: string buffer with format")
option (UNIT_TEST_LOCKFREE "Unit testing: lockfree module")
option (UNIT_TEST_THREAD "Unit testing: thread module")

message("  unit_tests/...")

if (AT_LEAST_ONE_UNIT_TEST)
  message("    common")
  add_subdirectory(common)
endif()

if (UNIT_TESTS OR UNIT_TEST_MEMORY_ALLOC)
  message("    memory_alloc")
  add_subdirectory(memory_alloc)
endif()

if (UNIT_TESTS OR UNIT_TEST_STRING_BUFFER)
  message("    string_buffer")
  add_subdirectory(string_buffer)
endif()

if (UNIT_TESTS OR UNIT_TEST_LOCKFREE)
  message("    lockfree")
  add_subdirectory(lockfree)
endif (UNIT_TESTS OR UNIT_TEST_LOCKFREE)

if (UNIT_TESTS OR UNIT_TEST_THREAD)
  message("    thread")
  add_subdirectory(thread)
endif(UNIT_TESTS OR UNIT_TEST_THREAD)