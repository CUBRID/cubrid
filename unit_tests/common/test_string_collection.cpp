/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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
