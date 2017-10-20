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

add_subdirectory(common)
set (TEST_INCLUDES
  ${EP_INCLUDES}
  ${CMAKE_CURRENT_SOURCE_DIR}/common  # todo: find a better solution
  )

option (WITH_UNIT_TEST_MEMORY_ALLOC "Unit testing for memory allocation" OFF)

if (WITH_UNIT_TEST_MEMORY_ALLOC)
  add_subdirectory(memory_alloc)
endif(WITH_UNIT_TEST_MEMORY_ALLOC)
