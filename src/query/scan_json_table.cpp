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
#include "fetch.h"
#include "object_primitive.h"
#include "scan_manager.h"

namespace cubscan
{
  namespace json_table
  {
    struct scanner::cursor
    {
      std::size_t m_row;
      std::size_t m_child;
      cubxasl::json_table::node *m_node;
      JSON_DOC *m_input_doc;      // used for non-array / single row
      JSON_DOC *m_row_doc;        // used only for arrays and multiple rows
      JSON_DOC *m_process_doc;    // is either input_doc or row doc
      bool m_is_row_evaluated;
    };

    int
    scanner::fetch_columns (const JSON_DOC &document, std::forward_list<cubxasl::json_table::column> &columns)
    {
      int error_code = NO_ERROR;
      for (auto col : columns)
	{
	  error_code = col.evaluate (document);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR();
	      return error_code;
	    }
	}
      return NO_ERROR;
    }

    int
    scanner::evaluate (cubxasl::json_table::node &node, cubthread::entry *thread_p, const JSON_DOC &document,
		       DB_LOGICAL &logical_output)
    {
      logical_output = V_TRUE;
      PR_EVAL_FNC eval_function = *m_eval_functions[node.m_id];

      if (eval_function == NULL)
	{
	  assert (node.m_predicate_columns.empty());
	  return NO_ERROR;
	}

      int error_code = fetch_columns (document, node.m_predicate_columns);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      logical_output = eval_function (thread_p, node.m_predicate_expression, NULL, NULL);
      if (logical_output == V_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      return NO_ERROR;
    }

    std::size_t
    scanner::get_row_count (cubxasl::json_table::node &node)
    {
      // todo: array
      return 1;
    }

    void
    scanner::init (cubxasl::json_table::spec_node &spec)
    {
      m_specp = &spec;

      assert (m_specp->m_node_count > 0);

      m_eval_functions = new PR_EVAL_FNC[m_specp->m_node_count];
      m_scan_cursor = new cursor[m_specp->m_node_count];      // depth could be used instead of count

      // todo:
      // walk json table tree and initialize evaluation functions
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

      // build m_scan_cursor

      m_scan_cursor[0].m_node = m_scan_root;
      if (db_value_type (value_p) == DB_TYPE_JSON)
	{
	  error_code = db_json_extract_document_from_path (db_get_json_document (value_p),
		       m_scan_root->m_path.c_str (),
		       m_scan_cursor[0].m_input_doc);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
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
	  error_code = db_json_extract_document_from_path (db_get_json_document (&json_cast_value),
		       m_scan_root->m_path.c_str (),
		       m_scan_cursor[0].m_input_doc);
	  pr_clear_value (&json_cast_value);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	}

      // todo
      return NO_ERROR;
    }

    void
    scanner::end (cubthread::entry *thread_p)
    {
      assert (thread_p != NULL);
    }

    int
    scanner::next_scan (cubthread::entry *thread_p, scan_id_struct &sid)
    {
      bool success = true;
      int error_code = next_internal (thread_p, 0, success);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (!success)
	{
	  // todo
	  sid.status = S_ENDED;
	}
      return NO_ERROR;
    }

    int
    scanner::next_internal (cubthread::entry *thread_p, int depth, bool &success)
    {
      int error_code = NO_ERROR;
      DB_LOGICAL logical;

      // check if cursor is already in child node
      if (m_scan_cursor_depth > depth + 1)
	{
	  // advance to child
	  error_code = next_internal (thread_p, depth + 1, success);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  if (success)
	    {
	      return NO_ERROR;
	    }
	}

      cursor &this_cursor = m_scan_cursor[depth];
      assert (this_cursor.m_node != NULL);

      // for each row X for each child
      // todo: fix condition
      while (this_cursor.m_row < get_row_count (*this_cursor.m_node))
	{
	  if (!this_cursor.m_is_row_evaluated)
	    {
	      // todo: set this_cursor.process_doc to input_doc or row_doc
	      // temporary: use input_doc.
	      this_cursor.m_process_doc = this_cursor.m_input_doc;
	      error_code = evaluate (*this_cursor.m_node, thread_p, *this_cursor.m_process_doc, logical);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error_code;
		}
	      if (logical != V_TRUE)
		{
		  // we need another row
		  this_cursor.m_row++;
		  continue;
		}
	      this_cursor.m_is_row_evaluated = true;

	      // fetch other columns too
	      error_code = fetch_columns (*this_cursor.m_process_doc, this_cursor.m_node->m_output_columns);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error_code;
		}

	      // fall
	    }

	  if (this_cursor.m_node->m_nested_nodes.empty ())
	    {
	      success = true;
	      return NO_ERROR;
	    }

	  // create cursor for next child
	  cubxasl::json_table::node &next_node = this_cursor.m_node->m_nested_nodes[this_cursor.m_child];
	  cursor &next_cursor = m_scan_cursor[depth + 1];
	  next_cursor.m_is_row_evaluated = false;
	  next_cursor.m_row = 0;
	  next_cursor.m_child = 0;
	  next_cursor.m_node = &next_node;

	  error_code = db_json_extract_document_from_path (this_cursor.m_process_doc,
		       next_node.m_path.c_str (),
		       this_cursor.m_input_doc);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  m_scan_cursor_depth++;

	  error_code = next_internal (thread_p, depth + 1, success);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  if (!success)
	    {
	      // try another child
	      this_cursor.m_child++;
	      continue;
	    }

	  // found a row
	  return NO_ERROR;
	}

      // no more rows...
      success = false;

      // todo: set columns values to NULL

      // remove this cursor
      m_scan_cursor_depth--;

      return NO_ERROR;
    }
  } // namespace json_table
} // namespace cubscan
