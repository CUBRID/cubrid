/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * resource_tracker.cpp - implementation to track resource usage (allocations, page fixes) and detect leaks
 */

#include "resource_tracker.hpp"

namespace cubbase
{

  fileline_location::fileline_location (const char *fn_arg, int l_arg)
    : file {}
    , line (l_arg)
  {
    std::strncpy (file, fn_arg, MAX_FILENAME_SIZE);
  }


  resource_tracker_item::resource_tracker_item (const char *fn_arg, int l_arg)
    : m_first_location (fn_arg, l_arg)
    , m_current_amount (1) // starts as 1
  {
    //
  }

} // namespace cubbase
