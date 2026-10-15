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

#include "schema_information_schema_definition.hpp"

namespace cubschema
{
  information_schema_definition::information_schema_definition (const std::string &n, const attr_vec_type &attrs,
      const cstr_vec_type &cts,
      const authorization &au,
      row_init_type ri)
    : name {n}
    , attributes {attrs}
    , constraints {cts}
    , auth {au}
    , row_initializer {ri}
  {
    //
  }
}
