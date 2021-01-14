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
