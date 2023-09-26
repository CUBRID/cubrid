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
  attribute::attribute (const std::string &n, const std::string &t)
    : kind {attribute_kind::COLUMN}
    , name {n}
    , type {t}
    , dvalue_func {nullptr}
  {
  }

  attribute::attribute (const std::string &n, const std::string &t, default_value_init_type dval_f = nullptr)
    : kind {attribute_kind::COLUMN}
    , name {n}
    , type {t}
    , dvalue_func {dval_f}
  {
  }

  attribute::attribute (const attribute_kind k, const std::string &n, const std::string &t)
    : kind {k}
    , name {n}
    , type {t}
    , dvalue_func {nullptr}
  {
  }

  attribute::attribute (const attribute_kind k, const std::string &n, const std::string &t,
			default_value_init_type dval_f = nullptr)
    : kind {k}
    , name {n}
    , type {t}
    , dvalue_func {dval_f}
  {
  }

  constraint::constraint (const DB_CONSTRAINT_TYPE &t, const std::string_view n,
			  const std::vector<const char *> &attrs, bool is_class_attr)
    : type {t}, name {n.begin(), n.end ()}, attribute_names {attrs}, is_class_attributes {is_class_attr}
  {
    //
  }

  grant::grant (struct db_object *&tu, const DB_AUTH &au, bool grant_opt)
    : target_user {tu}, auth {au}, with_grant_option {grant_opt}
  {
    //
  }

  authorization::authorization (struct db_object *&o, const std::vector<grant> &gs = {})
    : owner {o}, grants {gs}
  {
    //
  }

  system_catalog_definition::system_catalog_definition (const std::string &n, const attr_vec_type &attrs,
      const cstr_vec_type &cts,
      const authorization &au,
      row_init_type ri = nullptr)
    : name {n}
    , attributes {attrs}
    , constraints {cts}
    , auth {au}
    , row_initializer {ri}
  {
    //
  }
}
