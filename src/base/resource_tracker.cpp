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
    : m_file {}
    , m_line (l_arg)
  {
    // find filename from full path; get last charachter of '\\' or '/'
    const char *start_chp;
    for (start_chp = fn_arg + std::strlen (fn_arg); start_chp >= fn_arg; start_chp--)
      {
	if (*start_chp == '/' || *start_chp == '\\')
	  {
	    start_chp++;
	    break;
	  }
      }
    std::strncpy (m_file, start_chp, MAX_FILENAME_SIZE);
  }

  std::ostream &
  operator<< (std::ostream &os, const fileline_location &fileline)
  {
    os << fileline.m_file << ":" << fileline.m_line;
    return os;
  }

  resource_tracker_item::resource_tracker_item (const char *fn_arg, int l_arg, int amount)
    : m_first_location (fn_arg, l_arg)
    , m_amount (amount)
  {
    //
  }

  std::ostream &
  operator<< (std::ostream &os, const resource_tracker_item &item)
  {
    os << "amount=" << item.m_amount << " | first_caller=" << item.m_first_location;
    return os;
  }

  //////////////////////////////////////////////////////////////////////////
  // debugging
  bool Restrack_has_error = false;
  bool Restrack_suppress_assert = false;

  bool
  restrack_pop_error (void)
  {
    bool ret = Restrack_has_error;
    Restrack_has_error = false;
    return ret;
  }

  void
  restrack_set_error (bool error)
  {
    Restrack_has_error = Restrack_has_error || error;
  }

  void
  restrack_set_suppress_assert (bool suppress)
  {
    Restrack_suppress_assert = suppress;
  }

  bool
  restrack_is_assert_suppressed (void)
  {
    return Restrack_suppress_assert;
  }

} // namespace cubbase
