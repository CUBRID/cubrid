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
 * identifier_store.hpp - Utility class storing identifiers and provide a check function existence of name
 */

#ifndef _IDENTIFIER_STORE_HPP_
#define _IDENTIFIER_STORE_HPP_

#include <vector>

#include "porting.h"
#include "string_utility.hpp"
#include "language_support.h"

namespace cubbase
{
  /*
  * Table name, index name, view name, column name, user name etc. are included in identifier.
  * Identifiers are defined as follows:
  *   (1) An identifier must begin with a letter; it must not begin with a number or a symbol.
  *   (2) It is not case-sensitive.
  *   (3) Reserved Words are not allowed.
  *
  * NOTE: this class is immutable and not thread-safe.
  */
  class EXPORT_IMPORT identifier_store
  {
    public:
      explicit identifier_store (const std::vector <std::string> &string_vec, const bool check_valid)
	: m_identifiers (string_vec.begin(), string_vec.end ()), m_size (m_identifiers.size ())
      {
	// this routine checks whether the conditions in the above comment are satisfied and set the m_is_valid variable.
	// If check_valid is false, it is assumed to be valid without checking whether the conditions of the comment are satisfied.
	// Currently only (1) is able to be checked.
	m_is_valid = (check_valid) ? check_identifier_condition () : true;
      }

      ~identifier_store() = default;

      bool is_exists (const std::string &str) const
      {
	return m_identifiers.find (str) != m_identifiers.end ();
      }

      bool is_valid () const
      {
	return m_is_valid;
      }

      int get_size () const
      {
	return m_size;
      }

    private:
      bool check_identifier_condition ()
      {
	bool is_valid = true;
	for (const std::string &elem : m_identifiers)
	  {
	    // Check (1)
	    is_valid = lang_check_identifier (elem.data (), elem.size ());
	    if (is_valid == false)
	      {
		break;
	      }
	  }
	return is_valid;
      }

      /* see string_utility.hpp */
      const cubbase::string_set_ci_lower m_identifiers;
      const int m_size;

      bool m_is_valid;
  };
}

#endif // _IDENTIFIER_STORE_HPP_
