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
      explicit identifier_store (const std::vector <std::string> &string_vec, const bool check_valid);
      ~identifier_store();

      bool is_exists (const std::string &str) const;
      bool is_valid () const;

      int get_size () const;

    private:

      bool check_identifier_condition () const;

      /* see string_utility.hpp */
      cubbase::string_set_ci_lower m_identifiers;
      int m_size;

      bool m_is_valid;
  };
}

#endif // _IDENTIFIER_STORE_HPP_
