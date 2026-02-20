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
 * schema_information_schema_definition.hpp
 */

#ifndef _SCHEMA_INFORMATION_SCHEMA_DEFINITION_HPP_
#define _SCHEMA_INFORMATION_SCHEMA_DEFINITION_HPP_

#include "schema_system_catalog_definition.hpp"

#include <vector>

namespace cubschema
{
  struct information_schema_definition
  {
    using attr_vec_type = std::vector <attribute>;
    using cstr_vec_type = std::vector <constraint>;
    using row_init_type = std::function<int (struct db_object *)>;

    const std::string name;
    const attr_vec_type attributes;
    const cstr_vec_type constraints;
    const authorization auth;
    const row_init_type row_initializer;

    information_schema_definition (const std::string &n, const attr_vec_type &attrs,
				   const cstr_vec_type &cts,
				   const authorization &au,
				   row_init_type ri = nullptr);
  };
}

#endif /* _SCHEMA_INFORMATION_SCHEMA_DEFINITION_HPP_ */
