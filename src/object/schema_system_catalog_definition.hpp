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

#include "work_space.h" // struct db_object

namespace cubschema
{
  struct column
  {
    std::string_view name;
    std::string_view type;
    DB_VALUE default_value;

    column ();
    column (const std::string_view n, const std::string_view t);
    /*
    column (const std::string_view name, const std::string_view type, const DB_VALUE &default_val)
    : name {}, type {}
    {
      if (d)
      db_value_move
      db_value_clear (&default_value);
    }
    */
  };

  struct constraint
  {
    DB_CONSTRAINT_TYPE type;
    std::string_view name;
    std::vector<std::string_view> attribute_names;
    bool is_class_attributes;

    constraint (const DB_CONSTRAINT_TYPE &t, const std::string_view n, const std::vector<std::string_view> &attrs,
		bool is_class_attr);
  };

  struct grant
  {
    struct db_object *target_user;
    struct db_object *classmop;
    DB_AUTH auth;
    bool with_grant_option;

    grant (struct db_object *&tu, struct db_object *&cm, const DB_AUTH &au, bool grant_opt);
  };

  struct authorization
  {
    struct db_object *owner;
    std::vector<grant> grants;

    authorization (struct db_object *&o, const std::vector<grant> &gs);
  };

  struct system_catalog_definition
  {
    const std::string_view name;
    const std::vector <column> attributes;
    const std::vector <constraint> constraints;
    const authorization auth;

    system_catalog_definition (const std::string_view n, const std::vector<column> &attrs,
			       const std::vector<constraint> &cts,
			       const authorization &au);
  };
}

#endif // _SCHEMA_SYSTEM_CATALOG_DEFINITION_HPP_
