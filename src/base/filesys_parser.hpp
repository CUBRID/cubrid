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
 * filesys_parser.hpp - parser module for fs (proc, sys...)
 */

#ifndef _FILESYS_PARSER_HPP_
#define _FILESYS_PARSER_HPP_

#include <filesystem>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <charconv>
#include <limits>

#include "error_manager.h"

namespace os::parser
{
  std::vector<std::string> string_to_vector (const std::string str, const char separator);
  std::vector<std::string> string_to_vector (const std::string str, const std::string separator);

  /* e.g., 0 to std::set { 0 }		*/
  /*	   0-2 to std::set { 0, 1, 2 }	*/
  template <typename V>
  std::set<V> range_to_set (const std::string_view chunk)
  {
    std::set<V> set;
    std::size_t delimiter;
    V start, end;
    V i;

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
	    assert (end != std::numeric_limits<V>::max ());

	    for (i = start; i <= end; i++)
	      {
		set.insert (i);
	      }
	  }
      }

    return set;
  }

  /* e.g., 0,4-6 to std::set { 0, 4, 5, 6 }	*/
  template <typename V>
  std::set<V> range_set_to_set (const std::string_view line)
  {
    std::string_view item;
    std::set<V> set;
    V pos, end;

    pos = 0;
    while ((end = line.find (',', pos)) != std::string::npos)
      {
	item = line.substr (pos, end - pos);
	pos = end + 1;

	set.merge (parser::range_to_set<V> (item));
      }
    item = line.substr (pos);
    set.merge (parser::range_to_set<V> (item));

    return set;
  }

  template <typename V>
  std::set<V> intersection (std::set<V> &a, std::set<V> &b)
  {
    std::set<V> set;

    if (a.empty () || b.empty ())
      {
	return set;
      }
    std::set_intersection (a.begin (), a.end (), b.begin (), b.end (), std::inserter (set, set.begin ()));
    return set;
  }
}

#endif
