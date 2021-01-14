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
// scan_json_table.hpp - interface json table scanning
//
// JSON Table Scanner explained
//
//  Behavior - todo - add manual reference here
//
//    The syntax of JSON table is something like this:
//        ... JSON_TABLE (expression,
//                        '$.[*] COLUMNS(rownum FOR ORDINALITY.
//                                       a STRING PATH '$.a',
//                                       b INT EXISTS '$.b',
//                                       NESTED PATH '$.arr[*]' COLUMNS (c JSON PATH '$.c') as jt
//        WHERE b != 0 and a > d...
//
//    Expression is the input JSON for table. Each element found in the array of COLUMNS path is expanded into a row.
//    (above example expands root of JSON and then the array at $.c).
//
//    For each row found in root ('$'), column 'a' is value of $.a converted to string, column 'b' is 1 if $.b exists
//    and 0 otherwise; these values are repeated for each element found in $.arr, extracting the value of $.arr[*].c
//
//    Rows that do not pass the WHERE check are filtered.
//
//    NOTE: if there are multiple nested paths to the same node, they are not cross-joined. while one nested path is
//          expanded, the values for sibling nested paths will be all null.
//
//
//  Implementation
//
//    A root scan node is always used based on the input JSON (result of expression) and the first COLUMNS path. For
//    each NESTED PATH, a child scan node is generated (a node may have no, one or multiple children scan nodes).
//
//    Each scanner::next_scan call generates one row, or none if it was consumed entirely. It starts by generating a
//    small row for root node. If it is has (nested) children, for each child one by one, it computes the input node by
//    extracting nested node path from its root input and repeats same process until a leaf node reached.
//
//    When a leaf-level node row is generated, the scan row is considered complete and next_scan returns success.
//
//    A "breadcrumb" like cursor is used to remember where last row is generated. It generates a new row on the same
//    leaf node if possible, or clears all values for this node and returns to its parent (non-leaf node).
//
//    The parent will then try to advance to another children, or if all children have been processed, it will generate
//    a new row.
//
//    The process is repeated recursively until all nodes have been consumed and other rows can no longer be generated.
//
//
//  Future
//
//    Rows are filtered after a complete row is generated. We could partition the scan predicate on scan nodes and
//    filter invalid rows at node level, cutting of an entire branch of rows that would all be invalid.
//

#ifndef _SCAN_JSON_TABLE_HPP_
#define _SCAN_JSON_TABLE_HPP_

#include "query_evaluator.h"
#include "storage_common.h"

#include <vector>

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
struct val_descr;
struct xasl_node;

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

	// initialize scanner
	void init (cubxasl::json_table::spec_node &spec);
	// clear scanner
	void clear (xasl_node *xasl_p, bool is_final, bool is_final_clear);

	// open a new scan
	int open (cubthread::entry *thread_p);
	// end a scan
	void end (cubthread::entry *thread_p);

	// next_scan - generate a new row
	//
	// returns error code or NO_ERROR
	//
	// sid (in/out) : status and position is updated based on the success of scan
	int next_scan (cubthread::entry *thread_p, scan_id_struct &sid, SCAN_CODE &sc);

	SCAN_PRED &get_predicate ();
	void set_value_descriptor (val_descr *vd);

	scanner () = default;

      private:
	// cursor used to track scanner progress and resume scan on each scan_next call; implementation in cpp file
	struct cursor;

	// clear columns fetched values
	void clear_node_columns (cubxasl::json_table::node &node);
	// reset node ordinality (row number)
	void reset_ordinality (cubxasl::json_table::node &node);

	// init iterators considering the expansion type
	void init_iterators (cubxasl::json_table::node &node);

	// cursor functions
	int init_cursor (const JSON_DOC &doc, cubxasl::json_table::node &node, cursor &cursor_out);
	int set_next_cursor (const cursor &current_cursor, size_t next_depth);

	// to start scanning a node, an input document is set
	int set_input_document (cursor &cursor, const cubxasl::json_table::node &node, const JSON_DOC &document);

	// compute scan tree height; recursive function
	size_t get_tree_height (const cubxasl::json_table::node &node);

	// recursive scan next called on json table node / cursor
	int scan_next_internal (cubthread::entry *thread_p, size_t depth, bool &found_row_output);

	cubxasl::json_table::spec_node *m_specp;    // pointer to json table spec node in XASL
	cursor *m_scan_cursor;                      // cursor to keep track progress in each scan node
	size_t m_scan_cursor_depth;                 // the current level where the cursor was left
	size_t m_tree_height;                       // will be used to initialize cursor vector
	scan_pred m_scan_predicate;                 // scan predicate to filter generated rows
	val_descr *m_vd;
    };
  } // namespace json_table
} // namespace cubscan

// naming convention of SCAN_ID's
using JSON_TABLE_SCAN_ID = cubscan::json_table::scanner;

#endif // _SCAN_JSON_TABLE_HPP_
