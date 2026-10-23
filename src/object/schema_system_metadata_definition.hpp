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
 * schema_system_metadata_definition.hpp
 */

#ifndef _SCHEMA_SYSTEM_METADATA_DEFINITION_HPP_
#define _SCHEMA_SYSTEM_METADATA_DEFINITION_HPP_

#include <string>
#include <string_view>
#include <vector>
#include <functional>

#include "dbtype_def.h"

namespace cubschema
{
  enum class attribute_kind
  {
    COLUMN,

    /* for system class */
    CLASS_METHOD,

    /* for system view */
    QUERY_SPEC
  };

  struct attribute
  {
    using default_value_init_type = std::function<int (DB_VALUE *)>;

    attribute_kind kind;
    std::string name;
    std::string type;
    default_value_init_type dvalue_func;

    // attribute_kind::COLUMN
    attribute (const std::string &name, const std::string &type, default_value_init_type dval_f = nullptr);
    attribute (const attribute_kind kind, const std::string &name, const std::string &type = "");
  };

  // TODO: implement formatting utility function for std::string (like fmt library)
  inline std::string format_varchar (const int size)
  {
    return "varchar(" + std::to_string (size) + ")";
  }

  inline std::string format_varbit (const int size)
  {
    return "bit varying(" + std::to_string (size) + ")";
  }

  inline std::string format_numeric (const int prec, const int scale)
  {
    return "numeric(" + std::to_string (prec) + "," + std::to_string (scale) + ")";
  }

  inline std::string format_sequence (const std::string_view type)
  {
    return type.empty () ? "sequence of" : "sequence of " + std::string (type);
  }

  inline std::string format_set (const std::string_view type)
  {
    return type.empty () ? "set of" : "set of " + std::string (type);
  }

  struct constraint
  {
    DB_CONSTRAINT_TYPE type;
    std::string name;
    std::vector<const char *> attribute_names;
  };

  struct grant
  {
    MOP target_user;
    DB_AUTH auth;
    bool with_grant_option;
  };

  struct authorization
  {
    MOP owner;
    std::vector<grant> grants;
  };

  authorization make_authorization (MOP owner, const std::vector<MOP> &grantees, DB_AUTH auth);

  struct system_metadata_definition
  {
    using attr_vec_type = std::vector <attribute>;
    using cstr_vec_type = std::vector <constraint>;
    using row_init_type = std::function<int (MOP)>;

    const std::string name;
    const attr_vec_type attributes;
    const cstr_vec_type constraints;
    const authorization auth;
    const row_init_type row_initializer;

    system_metadata_definition (const std::string &n, const attr_vec_type &attrs,
				const cstr_vec_type &cts,
				const authorization &au,
				row_init_type ri);
    system_metadata_definition (const std::string &n, const attr_vec_type &attrs, const authorization &au);
  };
}

#endif // _SCHEMA_SYSTEM_METADATA_DEFINITION_HPP_
