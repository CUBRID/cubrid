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
 * fileline_location.cpp - implementation of file & line location
 */

#include "fileline_location.hpp"

#include <cstring>

namespace cubbase
{
  fileline_location::fileline_location (const char *fn_arg /* = "" */, int l_arg /* = 0 */)
    : m_file {}
    , m_line (0)
  {
    set (fn_arg, l_arg);
  }

  void
  fileline_location::set (const char *fn_arg, int l_arg)
  {
    // find filename from full path; get last character of '\\' or '/'
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

    m_line = l_arg;
  }

  std::ostream &
  operator<< (std::ostream &os, const fileline_location &fileline)
  {
    os << fileline.m_file << ":" << fileline.m_line;
    return os;
  }
} // namespace cubbase

