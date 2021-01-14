/*
 * Copyright 2008 Search Solution Corporation
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

//
// json_table_def.h - json table common definitions (cross modules)
//

#ifndef _JSON_TABLE_DEF_H_
#define _JSON_TABLE_DEF_H_

// note - this is included in C compiled files

// forward definitions
struct db_value;

enum json_table_column_behavior_type
{
  JSON_TABLE_RETURN_NULL,
  JSON_TABLE_THROW_ERROR,
  JSON_TABLE_DEFAULT_VALUE
};

enum json_table_column_function
{
  JSON_TABLE_EXTRACT,
  JSON_TABLE_EXISTS,
  JSON_TABLE_ORDINALITY
};

struct json_table_column_behavior
{
  enum json_table_column_behavior_type m_behavior;
  struct db_value *m_default_value;
};

enum json_table_expand_type
{
  JSON_TABLE_ARRAY_EXPAND,
  JSON_TABLE_OBJECT_EXPAND,
  JSON_TABLE_NO_EXPAND
};

#endif // _JSON_TABLE_DEF_H_
