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
 * schema_system_catalog_builder.hpp
 */

#ifndef _SCHEMA_SYSTEM_CATALOG_DEFINITION_HPP_
#define _SCHEMA_SYSTEM_CATALOG_DEFINITION_HPP_

#include <string_view>
#include <vector>
#include <optional>

#include "work_space.h" // struct db_object

namespace cubschema
{
  using nullable_string = std::optional<std::string>;

  struct column
  {
    std::string name;
    std::string type;
    DB_VALUE default_value;

    column (const std::string &n, const std::string &t);
    // column (const std::string &name, const std::string &type, const DB_VALUE &default_val);
  };

  struct constraint
  {
    DB_CONSTRAINT_TYPE type;
    std::string name;
    std::vector<const char *> attribute_names;
    bool is_class_attributes;

    constraint (const DB_CONSTRAINT_TYPE &t, std::string_view n, const std::vector<const char *> &attrs,
		bool is_class_attr);
  };

  struct grant
  {
    struct db_object *target_user;
    DB_AUTH auth;
    bool with_grant_option;

    grant (struct db_object *&tu, const DB_AUTH &au, bool grant_opt);
  };

  struct authorization
  {
    struct db_object *owner;
    std::vector<grant> grants;

    authorization (struct db_object *&o, const std::vector<grant> &gs);
  };

  struct system_catalog_definition
  {
    using attr_vec_type = std::vector <column>;
    using cstr_vec_type = std::vector <constraint>;
    using qs_vec_type = std::vector <std::string>;

    const std::string name;
    const attr_vec_type attributes;

    const cstr_vec_type constraints; // for class
    const qs_vec_type query_specs; // for vclass

    const authorization auth;

    system_catalog_definition (const std::string &n, const attr_vec_type &attrs,
			       const cstr_vec_type &cts,
			       const authorization &au);

    system_catalog_definition (const std::string &n, const attr_vec_type &attrs,
			       const qs_vec_type &qs,
			       const authorization &au);
  };
}

#endif // _SCHEMA_SYSTEM_CATALOG_DEFINITION_HPP_
