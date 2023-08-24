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

#include "schema_system_catalog_definition.hpp"

#include "db.h"
#include "dbtype_function.h"

namespace cubschema
{
  column::column (const std::string &n, const std::string &t)
    : name {n}, type {t}
  {
    db_make_null (&default_value);
    db_value_clear (&default_value);
  }

  constraint::constraint (const DB_CONSTRAINT_TYPE &t, const std::string_view n,
			  const std::vector<const char *> &attrs, bool is_class_attr)
    : type {t}, name {n.begin(), n.end ()}, attribute_names {attrs}, is_class_attributes {is_class_attr}
  {
    //
  }

  grant::grant (struct db_object *&tu, struct db_object *&cm, const DB_AUTH &au, bool grant_opt)
    : target_user {tu}, classmop {cm}, auth {au}, with_grant_option {grant_opt}
  {
    //
  }

  authorization::authorization (struct db_object *&o, const std::vector<grant> &gs = {})
    : owner {o}, grants {gs}
  {
    //
  }

  system_catalog_definition::system_catalog_definition (const std::string_view n, const std::vector<column> &attrs,
      const std::vector<constraint> &cts,
      const authorization &au)
    : name {n}
    , attributes {attrs}
    , constraints {cts}
    , auth {au}
  {
    //
  }


}
