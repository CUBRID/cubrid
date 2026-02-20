/*
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
 * parser.hpp - parser module for fs (proc, sys...)
 */

#ifndef _BASE_PARSER_HPP_
#define _BASE_PARSER_HPP_

#include <string>
#include <set>
#include <charconv>

namespace os::parser
{
  /* e.g., 0 to std::set { 0 }		*/
  /*	   0-2 to std::set { 0, 1, 2 }	*/
  template <typename V>
  std::set<V> range_to_set (std::string_view chunk)
  {
    std::set<V> set;
    std::size_t start, end;
    std::size_t delimiter;
    V i;

    set.clear ();
    delimiter = chunk.find ('-');
    if (delimiter == std::string::npos)
      {
	auto [ptr, er] = std::from_chars (chunk.data (), chunk.data () + chunk.size (), i);
	if (er == std::errc ())
	  {
	    set.insert (i);
	  }
      }
    else
      {
	auto [ptr1, er1] = std::from_chars (chunk.data (), chunk.data () + delimiter, start);
	auto [ptr2, er2] = std::from_chars (chunk.data () + delimiter + 1, chunk.data () + chunk.size (), end);
	if (er1 == std::errc () && er2 == std::errc ())
	  {
	    for (i = start; i <= end; i++)
	      {
		set.insert (i);
	      }
	  }
      }

    return set;
  }
}

#endif
