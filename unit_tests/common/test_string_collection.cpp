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
 * string_collection - a class collection of strings
 */

#include "test_string_collection.hpp"

namespace test_common
{

size_t
string_collection::get_count () const
{
  return m_names.size ();
}

size_t
string_collection::get_max_length () const
{
  size_t max = 0;
  for (auto name_it = m_names.cbegin (); name_it != m_names.cend (); name_it++)
    {
      if (max < name_it->size ())
        {
          max = name_it->size ();
        }
    }
  return max;
}

const char *
string_collection::get_name (size_t name_index) const
{
  return m_names[name_index].c_str ();
}

} // namespace test_common
