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

#include "schema_system_class.hpp"

#include <unordered_set>
#include <string>

static cubschema::system_class_name_def class_name_def;

bool sm_check_system_class_by_name (const char *str)
{
  return class_name_def.has_name (std::string (str));
}

bool sm_check_system_class_by_name (const std::string &str)
{
  return class_name_def.has_name (str);
}