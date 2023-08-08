/*
 *
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
 * string_utility.hpp - Utility structures and definitions to support C++ String and related STL containers
 */

#ifndef _STRING_UTILITY_HPP_
#define _STRING_UTILITY_HPP_

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cubbase
{
  /* ========================================================================== */
  /* STRUCT DEFINITIONS */
  /* ========================================================================== */

  struct lowercase_hash
  {
    // for case-insensitive comparision
    size_t operator() (const std::string_view str) const
    {
      std::string lower (str);
      std::transform (str.begin(), str.end(), lower.begin(),
		      [] (char c) -> char { return std::tolower (c); });
      return std::hash<std::string> {} (lower);
    }
  };

  struct lowercase_compare
  {
    // case-insensitive comparision
    bool operator() (const std::string_view l, const std::string_view r) const
    {
      return l.size() == r.size()
	     && std::equal ( l.begin(), l.end(), r.begin(),
			     [] (const auto a, const auto b)
      {
	return tolower (a) == tolower (b);
      }
			   );
    }
  };

  /* ========================================================================== */
  /* ALIAS DEFINITIONS */
  /* ========================================================================== */

  using string_set_ci_lower = std::unordered_set <std::string, lowercase_hash, lowercase_compare>;

}

#endif // _STRING_UTILITY_HPP_
