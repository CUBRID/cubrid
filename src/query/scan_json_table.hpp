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
// scan_json_table.hpp - interface json table scanning
//

#ifndef _SCAN_JSON_TABLE_HPP_
#define _SCAN_JSON_TABLE_HPP_

#include <vector>

#include "dbtype_def.h"
#include "query_evaluator.h"

// forward definitions
// access_json_table.hpp
namespace cubxasl
{
  namespace json_table
  {
    struct spec_node;
    struct node;
    struct column;
  }
}
// db_json.hpp
class JSON_DOC;
class JSON_ITERATOR;
// scan_manager.h
struct scan_id_struct;

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubscan
{
  namespace json_table
  {
    class scanner
    {
      public:

	void init (cubxasl::json_table::spec_node &spec);
	void clear (xasl_node *xasl_p, bool is_final);

	int open (cubthread::entry *thread_p);
	void end (cubthread::entry *thread_p);

	int next_scan (cubthread::entry *thread_p, scan_id_struct &sid);

	scanner () = default;

      private:
	// scan_node
	//
	//  description
	//    cubxasl::json_table::nested_node extended with m_eval_function. nested_node is common to client, while
	//    PR_EVAL_FNC is restricted to server.
	struct cursor;

	int fetch_columns (const JSON_DOC &document, std::vector<cubxasl::json_table::column> &columns,
			   const cubxasl::json_table::node &node);
	int evaluate (cubxasl::json_table::node &node, cubthread::entry *thread_p, const JSON_DOC &document,
		      DB_LOGICAL &logical_output);
	std::size_t get_row_count (cursor &cursor);
	bool check_need_expand (const cubxasl::json_table::node &node);
	bool str_ends_with (const std::string &str, const std::string &end);
	const char *get_parent_path (const cubxasl::json_table::node &node);
	int set_next_cursor (const cursor &current_cursor, int next_depth);
	int set_input_document (cursor &cursor, const cubxasl::json_table::node &node, const JSON_DOC &document);
	void init_eval_functions (const cubxasl::json_table::node &node);
	size_t get_tree_height (const cubxasl::json_table::node &node);
	void clear_columns (std::vector<cubxasl::json_table::column> &columns);
	void clear_node_columns (cubxasl::json_table::node &node);

	int next_internal (cubthread::entry *thread_p, int depth, bool &success);

	//scan_id_struct *m_scanid;
	cubxasl::json_table::spec_node *m_specp;
	cubxasl::json_table::node *m_scan_root;
	cursor *m_scan_cursor;
	size_t m_scan_cursor_depth;     // the current level where the cursor was left
	size_t m_tree_height;           // will be used to initialize cursor vector
	PR_EVAL_FNC *m_eval_functions;  // each node will have its associated function based on node.id
    };
  } // namespace json_table
} // namespace cubscan

// naming convention of SCAN_ID's
using JSON_TABLE_SCAN_ID = cubscan::json_table::scanner;

#endif // _SCAN_JSON_TABLE_HPP_
