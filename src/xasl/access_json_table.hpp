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

#include <cstdint>

#include "json_table_def.h"
#include "object_domain.h"

// forward declarations
struct db_value;
struct tp_domain;
struct regu_variable_node;
class JSON_DOC;
class JSON_ITERATOR;

namespace cubxasl
{
  namespace json_table
  {

    struct column
    {
	tp_domain *m_domain;
	char *m_path;
	char *m_column_name;
	json_table_column_behavior m_on_error;
	json_table_column_behavior m_on_empty;
	db_value *m_output_value_pointer;     // should match xasl->outptr_list value pointers

	// there are three types of columns based on how they function:
	// extract from path, exists at path or ordinality
	json_table_column_function m_function;

	column ();
	int evaluate (const JSON_DOC &input, size_t ordinality);

      private:
	int evaluate_extract (const JSON_DOC &input);
	int evaluate_exists (const JSON_DOC &input);
	int evaluate_ordinality (size_t ordinality);

	int trigger_on_error (const JSON_DOC &input, const TP_DOMAIN_STATUS &status_cast, db_value &value_out);
	int trigger_on_empty (db_value &value_out);
    };

    struct node
    {
      char *m_path;
      size_t m_ordinality;                    // will be used to count the row ordinality
      bool m_need_inc_ordinality;
      column *m_output_columns;   // columns part of output only
      size_t m_output_columns_size;
      node *m_nested_nodes;       // nested nodes
      size_t m_nested_nodes_size;
      size_t m_id;                            // identifier for each node
      JSON_ITERATOR *m_iterator;
      json_table_expand_type m_expand_type;

      node (void);

      void clear_columns ();
      void clear_iterators ();
      void clear_tree ();

      bool check_need_expand () const;
      static bool str_ends_with (const std::string &str, const std::string &end);
      void set_parent_path ();
      void init_iterator ();
    };

    struct spec_node
    {
      node *m_root_node;
      regu_variable_node *m_json_reguvar;
      std::size_t m_node_count;               // the total number of nodes
    };

  } // namespace json_table
} // namespace cubxasl

// to be used outside namespace
using json_table_column = cubxasl::json_table::column;
using json_table_node = cubxasl::json_table::node;
using json_table_spec_node = cubxasl::json_table::spec_node;

#endif // _ACCESS_JSON_TABLE_H_
