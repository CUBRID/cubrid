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
 * filesys_parser.cpp - parser module for fs (proc, sys...)
 */

#include "filesys_parser.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace os::parser
{
  std::vector<std::string> string_to_vector (std::string str, char separator)
  {
    std::vector<std::string> vec;
    std::string_view view (str);
    std::size_t pos, end;

    pos = 0;
    while (pos < view.length ())
      {
	end = view.find (separator, pos);
	if (end == std::string::npos)
	  {
	    vec.emplace_back (view.substr (pos));
	    break;
	  }
	vec.emplace_back (view.substr (pos, end - pos));
	pos = end + 1;
      }

    return vec;
  }

  std::vector<std::string> string_to_vector (std::string str, std::string separator)
  {
    std::vector<std::string> vec;
    std::string_view view (str);
    std::size_t pos, end;

    pos = 0;
    while (pos < view.length ())
      {
	end = view.find (separator, pos);
	if (end == std::string::npos)
	  {
	    vec.emplace_back (view.substr (pos));
	    break;
	  }
	vec.emplace_back (view.substr (pos, end - pos));
	pos = end + separator.length ();
      }

    return vec;
  }
}

