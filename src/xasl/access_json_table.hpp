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

#include <forward_list>
#include <string>
#include <vector>

#include <cstdint>

// forward declarations
struct db_value;
struct tp_domain;
struct pred_expr;
struct regu_variable_node;
class JSON_DOC;


namespace cubxasl
{
  namespace json_table
  {
    enum class column_behavior
    {
      RETURN_NULL,
      THROW_ERROR,
      DEFAULT_VALUE
    };

    enum class column_function
    {
      EXTRACT,
      EXISTS
    };

    struct column_on_error
    {
      column_behavior m_behavior;
      db_value *m_default_value;

      column_on_error ();
      int trigger (int error_code, db_value &value_out);
    };

    struct column_on_empty
    {
      column_behavior m_behavior;
      db_value *m_default_value;

      column_on_empty ();
      int trigger (db_value &value_out);
    };

    struct column
    {
      // there are two types of columns based on how they function: extract from path or exists at path
      using function_type = bool;
      static const function_type EXTRACT = true;
      static const function_type EXISTS = false;

      tp_domain *m_domain;
      std::string m_path;
      column_on_error m_on_error;
      column_on_empty m_on_empty;
      db_value *m_output_value_pointer;   // todo: should match xasl->outptr_list value pointers
      //       dig xasl_generation SYMBOL_INFO
      function_type m_function;

      column ();
      int evaluate (const JSON_DOC &input);
    };

    struct node
    {
      std::string m_path;
      std::uint32_t m_ordinality;                     // will be used to count the row ordinality
      std::forward_list<column> m_predicate_columns;  // columns part of scan predicate; also part of output
      std::forward_list<column> m_output_columns;     // columns part of output only
      pred_expr *m_predicate_expression;              // predicate expression
      std::vector<node> m_nested_nodes;               // nested nodes
      size_t m_id;                                    // identifier for each node

      node() = default;
    };

    struct spec_node
    {
      node *m_root_node;
      //db_value *m_output_values;  // maybe; or maybe use xasl
      regu_variable_node *m_json_reguvar;
    };

  } // namespace json_table
} // namespace cubxasl

// to be used outside namespace
using json_table_column_behavior = cubxasl::json_table::column_behavior;
const json_table_column_behavior JSON_TABLE_RETURN_NULL = json_table_column_behavior::RETURN_NULL;
const json_table_column_behavior JSON_TABLE_THROW_ERROR = json_table_column_behavior::THROW_ERROR;
const json_table_column_behavior JSON_TABLE_DEFAULT_VALUE = json_table_column_behavior::DEFAULT_VALUE;

using json_table_column_function = cubxasl::json_table::column_function;
const json_table_column_function JSON_TABLE_EXTRACT = json_table_column_function::EXTRACT;
const json_table_column_function JSON_TABLE_EXISTS = json_table_column_function::EXISTS;

using json_table_column = cubxasl::json_table::column;
using json_table_node = cubxasl::json_table::node;
using json_table_spec_node = cubxasl::json_table::spec_node;

#endif // _ACCESS_JSON_TABLE_H_
