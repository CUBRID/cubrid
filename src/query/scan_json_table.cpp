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

#include "scan_json_table.hpp"

#include "access_json_table.hpp"
#include "db_json.hpp"
#include "dbtype.h"
#include "dbtype_def.h"
#include "fetch.h"
#include "object_primitive.h"
#include "query_evaluator.h"
#include "scan_manager.h"

namespace cubscan
{
  namespace json_table
  {
    struct scanner::scan_node
    {
      cubxasl::json_table::nested_node &m_xasl_node;

      PR_EVAL_FNC m_eval_function;
      std::vector<scan_node *> m_children;
      scan_node *m_parent;

      // cursor
      std::size_t m_row_cursor;
      std::size_t m_child_cursor;
      bool m_is_before;
      bool m_is_on_true_row;
      JSON_DOC *m_json_document;

      int open (cubthread::entry *thread_p, const JSON_DOC &json_doc)
      {
	// set document to process
	int error_code = db_json_extract_document_from_path (&json_doc, m_xasl_node.m_path.c_str (), m_json_document);
	if (error_code != NO_ERROR)
	  {
	    ASSERT_ERROR ();
	    return error_code;
	  }
      }

      int fetch_columns (std::forward_list<cubxasl::json_table::column> &columns)
      {
	int error_code = NO_ERROR;
	for (auto col : columns)
	  {
	    error_code = col.evaluate (get_current_row_doc ());
	    if (error_code != NO_ERROR)
	      {
		ASSERT_ERROR ();
		return error_code;
	      }
	  }
      }

      int evaluate_current_row (cubthread::entry *thread_p, DB_LOGICAL &logical_output)
      {
	logical_output = V_TRUE;

	if (m_eval_function == NULL)
	  {
	    return NO_ERROR;
	  }

	logical_output = m_eval_function (thread_p, m_xasl_node.m_predicate_expression, NULL, NULL);
	if (logical_output == V_ERROR)
	  {
	    int error_code;
	    ASSERT_ERROR_AND_SET (error_code);
	    return error_code;
	  }
	return NO_ERROR;
      }

      JSON_DOC &get_current_row_doc (void)
      {
	// todo: array
	return *m_json_document;
      }

      std::size_t get_row_count (void)
      {
	// todo: array
	return 1;
      }

      int next_true_row (cubthread::entry *thread_p)
      {
	DB_LOGICAL logical_result;
	int error_code;

	while (m_row_cursor < get_row_count ())
	  {
	    error_code = evaluate_current_row (thread_p, logical_result);
	    if (error_code != NO_ERROR)
	      {
		ASSERT_ERROR ();
		return error_code;
	      }
	    if (logical_result == V_TRUE)
	      {
		// fetch output columns too
		error_code = fetch_columns (m_xasl_node.m_output_columns);
		if (error_code != NO_ERROR)
		  {
		    ASSERT_ERROR ();
		    return error_code;
		  }
		m_is_on_true_row = true;
		return NO_ERROR;
	      }
	    ++m_row_cursor;
	  }
      }

      int process (cubthread::entry *thread_p, SCAN_CODE &scan_code, scan_node *&cursor_node)
      {

	int error_code;

	// todo: move this part to increment


	if (!m_is_on_true_row)
	  {
	    error_code = next_true_row (thread_p);
	    if (error_code != NO_ERROR)
	      {
		ASSERT_ERROR ();
		return error_code;
	      }
	    if (!m_is_on_true_row)
	      {
		// return to parent
		cursor_node = m_parent;
		return NO_ERROR;
	      }
	  }

	// todo: return child
	cursor_node = m_children[m_child_cursor];
	error_code = cursor_node->open (thread_p, get_current_row_doc ());
	if (error_code != NO_ERROR)
	  {
	    ASSERT_ERROR ();
	    return error_code;
	  }
	return NO_ERROR;
      }

      void increment ()
      {
	if (m_is_before)
	  {
	    // init stuff
	    m_row_cursor = 0;
	    m_child_cursor = 0;
	    m_is_on_true_row = false;
	    m_is_before = false;
	  }
	else
	  {
	    // advance
	    if (m_child_cursor < m_children.size())
	      {
		// advance child
		m_child_cursor++;
	      }
	    else
	      {
		++m_row_cursor;
		m_child_cursor = 0;
		m_is_on_true_row = false;
	      }
	  }
      }
    };

    void
    scanner::init (cubxasl::json_table::spec_node &spec)
    {
      m_specp = &spec;
    }

    void
    scanner::clear (void)
    {
      m_specp = NULL;
    }

    int
    scanner::open (cubthread::entry *thread_p)
    {
      int error_code = NO_ERROR;

      // so... we need to generate the whole list file

      // we need the starting value to expand into a list of records
      DB_VALUE *value_p = NULL;
      error_code = fetch_peek_dbval (thread_p, m_specp->m_json_reguvar, NULL, NULL, NULL, NULL, &value_p);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (value_p == NULL || db_value_is_null (value_p))
	{
	  assert (false);
	  return ER_FAILED;
	}

      if (db_value_type (value_p) == DB_TYPE_JSON)
	{
	  m_scan_root->open (thread_p, *db_get_json_document (value_p));
	}
      else
	{
	  // we need json
	  DB_VALUE json_cast_value;
	  // todo: is this implicit or explicit?
	  error_code = tp_value_cast (value_p, &json_cast_value, &tp_Json_domain, true);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  error_code = m_scan_root->open (thread_p, *db_get_json_document (&json_cast_value));
	  pr_clear_value (&json_cast_value);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	}

      m_scan_cursor = m_scan_root;

      // todo
      return NO_ERROR;
    }

    void
    scanner::end (cubthread::entry *thread_p)
    {
      assert (thread_p != NULL);
    }

    int
    scanner::next_row (cubthread::entry *thread_p, scan_id_struct &sid)
    {
      if (m_scan_cursor == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      // todo:
      // I have complicated things too much, trying to resume from leaf nodes; traversal from root would be a better
      // approach than doing this back and forth walking on scan nodes, with each node trying to keep its last state.
      // we don't expect many levels on these scan trees, so there should be no performance concerns.
      //
      // cursor should maintain a list of positions for all processed nodes.
      // scan_node->process will be called recursively. to reach a node, process calls will stack a number of times
      // equal to node depth in scan tree. always.
      //

      int error_code = NO_ERROR;
      SCAN_CODE scan_code = S_END;
      while (true)
	{
	  if (scan_code == S_SUCCESS && m_scan_cursor->m_children.empty())
	    {
	      // found a row
	      return NO_ERROR;
	    }
	  m_scan_cursor->increment ();
	  error_code = m_scan_cursor->process (thread_p, scan_code, m_scan_cursor);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	}
    }
  } // namespace json_table
} // namespace cubscan
