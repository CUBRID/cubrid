/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

//
// access_json_table.hpp - defines structures required to access json table spec type.
//

#ifndef _ACCESS_JSON_TABLE_H_
#define _ACCESS_JSON_TABLE_H_

#include <string>
#include <vector>

// forward declaration
struct db_value;
struct tp_domain;

// todo: namespace cubxas::json_table
enum class json_table_column_behavior
{
  RETURN_NULL,
  THROW_ERROR,
  DEFAULT_VALUE
};

struct json_table_column_on_error
{
  json_table_column_behavior m_behavior;
  db_value *m_default_value;

  json_table_column_on_error ();
  int trigger (int error_code, db_value &value_out);
};

struct json_table_column_on_empty
{
  json_table_column_behavior m_behavior;
  db_value *m_default_value;

  json_table_column_on_empty ();
  int trigger (db_value &value_out);
};

struct json_table_column
{
  // there are two types of columns based on how they function: extract from path or exists at path
  using function_type = bool;
  static const function_type EXTRACT = true;
  static const function_type EXISTS = false;

  tp_domain *m_domain;
  std::string m_path;
  json_table_column_on_error m_on_error;
  json_table_column_on_empty m_on_empty;
  db_value *m_output_value_pointer;
  function_type m_function;

  json_table_column ();
  int evaluate (db_value &input);
};

#endif // _ACCESS_JSON_TABLE_H_
