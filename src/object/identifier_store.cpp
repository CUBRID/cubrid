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

#include "identifier_store.hpp"

#include "language_support.h"
#include "chartype.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubbase
{
  identifier_store::identifier_store (const std::vector <std::string> &string_vec, const bool check_valid)
    : m_identifiers (string_vec.begin(), string_vec.end ()), m_size (m_identifiers.size ())
  {
    // this routine checks whether the conditions in the above comment are satisfied and set the m_is_valid variable.
    // If check_valid is false, it is assumed to be valid without checking whether the conditions of the comment are satisfied.
    // Currently only (1) is able to be checked.
    m_is_valid = (check_valid) ? check_identifier_condition () : true;
  }

  identifier_store::~identifier_store ()
  {
    m_identifiers.clear ();
  }

  bool
  identifier_store::is_exists (const std::string_view str) const
  {
    return m_identifiers.find (str) != m_identifiers.end ();
  }

  bool
  identifier_store::is_valid () const
  {
    return m_is_valid;
  }

  int
  identifier_store::get_size () const
  {
    return m_size;
  }

  bool
  identifier_store::check_identifier_condition () const
  {
    // TODO: check_identifier_condition () is not considering the following yet.
    // * Checking unicode letters
    // * Checking reserved keywords
    // * Checking enclosing in Double quotes, Square brackets, or Backtick symobls
    bool is_valid = true;
    for (const std::string_view elem : m_identifiers)
      {
	// Check (1)
	is_valid = check_identifier_is_valid (elem, false);
	if (is_valid == false)
	  {
	    break;
	  }
      }
    return is_valid;
  }

  bool
  identifier_store::is_enclosed (const std::string_view i)
  {
    int size = i.size ();
    if (size >= 2)
      {
	return ((i[0] == '"' || i[0] == '[' || i[0] == '`')
		&& (i[size - 1] == '"' || i[size - 1] == ']' || i[size - 1] == '`'));
      }
    return false;
  }

  bool
  identifier_store::check_identifier_is_valid (const std::string_view str, bool is_enclosed)
  {
    if (is_enclosed)
      {
	// enclosed in Double Quotes, Square Brackets, or Backtick Symbol
	if (std::any_of (str.begin(), str.end(), [] (char c)
	{
	  return (c == '.' || c == '[' || c == ']');
	  }))
	{
	  return false;
	}
      }
    else
      {
	// check _db
	if (str.rfind (SYSTEM_CLASS_PREFIX, 0) != 0)
	  {
	    int size = str.size ();
	    for (int i = 0; i < size; i++)
	      {
		if ((i == 0 && !char_isalpha (str[0])) || (i >= 1 && (!char_isalnum (str[i]) && str[i] != '_')))
		  {
		    return false;
		  }
	      }
	  }


      }
    return true;
  }
}
