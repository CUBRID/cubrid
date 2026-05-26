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
 * schema_information_schema.hpp - SQL INFORMATION_SCHEMA (ISO/IEC 9075-11:2003) metadata interface
 */

#ifndef _SCHEMA_INFORMATION_SCHEMA_HPP_
#define _SCHEMA_INFORMATION_SCHEMA_HPP_

#include <string>
#include <string_view>
#include <vector>

void info_schema_init (void);
int info_schema_install (void);
bool sm_is_information_schema_views (const std::string_view name);

namespace cubschema
{
  const std::vector<std::string> &get_information_schema_view_names ();
}

#endif /* _SCHEMA_INFORMATION_SCHEMA_HPP_ */