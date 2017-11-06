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

#ifndef _TEST_STRING_COLLECTION_HPP_
#define _TEST_STRING_COLLECTION_HPP_

#include <string>
#include <vector>

/*
 * string_collection - a class collection of strings
 */

namespace test_common
{

class string_collection
{
  public:
    template <typename ... Args>
    string_collection (Args &... args)
    {
      register_names (args...);
    }

    size_t get_count () const;
    size_t get_max_length () const;
    const char *get_name (size_t name_index) const;

  private:
    string_collection ();

    template <typename FirstArg, typename ... OtherArgs>
    void register_names (FirstArg &first, OtherArgs &... other)
    {
      m_names.push_back (first);
      register_names (other...);
    }

    template <typename OneArg>
    void register_names (OneArg &arg)
    {
      m_names.push_back (arg);
    }

    std::vector<std::string> m_names;
};

}  // namespace test_common

#endif // _TEST_STRING_COLLECTION_HPP_
