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

#include <strings.h>

#include "identifier_store.hpp"

#include "chartype.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  identifier_store::identifier_store (const std::vector <std::string> &string_vec)
    : m_identifiers (string_vec.begin(), string_vec.end ())
  {
  }

  bool
  identifier_store::is_exists (const std::string_view str) const
  {
    return m_identifiers.find (str) != m_identifiers.end ();
  }

  bool
  identifier_store::check_identifier_is_valid (const std::string_view str)
  {
    // check _db
    if (strncasecmp (str.data(), SYSTEM_CLASS_PREFIX.c_str(), SYSTEM_CLASS_PREFIX.length()) != 0)
      {
	int size = str.size ();
	for (int i = 0; i < size; i++)
	  {
	    if ((i == 0 && !char_isalpha (str[0]))
		|| (i >= 1 && (!char_isalnum (str[i]) && str[i] != '_' && str[i] != '.')))
	      {
		return false;
	      }
	  }
      }
    return true;
  }
}
