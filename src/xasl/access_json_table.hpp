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
class regu_variable_node;
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

	void init ();
	int evaluate (const JSON_DOC &input, size_t ordinality);
	void clear_xasl (bool is_final_clear = true);

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
      column *m_output_columns;   // columns part of output only
      size_t m_output_columns_size;
      node *m_nested_nodes;       // nested nodes
      size_t m_nested_nodes_size;
      size_t m_id;                            // identifier for each node
      JSON_ITERATOR *m_iterator;
      bool m_is_iterable_node;

      node (void);

      void init ();
      void clear_columns (bool is_final_clear);
      void clear_iterators (bool is_final_clear);
      void clear_xasl (bool is_final_clear = true);
      void init_iterator ();
      void init_ordinality ();
    };

    struct spec_node
    {
      node *m_root_node;
      regu_variable_node *m_json_reguvar;
      std::size_t m_node_count;               // the total number of nodes

      spec_node ();

      void init ();
      void clear_xasl (bool is_final_clear = true);
    };

  } // namespace json_table
} // namespace cubxasl

// to be used outside namespace
using json_table_column = cubxasl::json_table::column;
using json_table_node = cubxasl::json_table::node;
using json_table_spec_node = cubxasl::json_table::spec_node;

#endif // _ACCESS_JSON_TABLE_H_
