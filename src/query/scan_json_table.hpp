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

#include "query_evaluator.h"

// forward definitions
// access_json_table.hpp
namespace cubxasl
{
  namespace json_table
  {
    struct spec_node;
    struct nested_node;
  }
}
// db_json.hpp
class JSON_DOC;
// query_list.h
struct qfile_list_id;
struct qfile_list_scan_id;
struct qfile_tuple_record;
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

	void init (scan_id_struct &sid, cubxasl::json_table::spec_node &spec);
	void clear (void);

	int open (cubthread::entry *thread_p);
	void end (cubthread::entry *thread_p);

      private:
	// scan_node
	//
	//  description
	//    cubxasl::json_table::nested_node extended with m_eval_function. nested_node is common to client, while
	//    PR_EVAL_FNC is restricted to server.
	struct scan_node
	{
	  cubxasl::json_table::nested_node &m_nested_node;
	  PR_EVAL_FNC m_eval_function;

	  scan_node () = delete;
	  scan_node (cubxasl::json_table::nested_node &nested_node);
	};

	int process_row (cubthread::entry *thread_p, const JSON_DOC &value, scan_node &node);
	int process_doc (cubthread::entry *thread_p, const JSON_DOC &value, scan_node &node);

	scan_id_struct *m_scanid;
	cubxasl::json_table::spec_node *m_specp;
	scan_node *m_scan_root;
	qfile_list_id *m_list_file;
	qfile_list_scan_id *m_list_scan;
	qfile_tuple_record *m_tuple;
    };
  } // namespace json_table
} // namespace cubscan

// naming convention of SCAN_ID's
using JSON_TABLE_SCAN_ID = cubscan::json_table::scanner;

#endif // _SCAN_JSON_TABLE_HPP_
