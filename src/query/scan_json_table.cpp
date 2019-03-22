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

#include <algorithm>

namespace cubscan
{
  namespace json_table
  {
    struct scanner::cursor
    {
      std::size_t m_child;                // current child
      cubxasl::json_table::node *m_node;  // pointer to access node
      JSON_DOC_STORE m_input_doc;         // input JSON document value
      const JSON_DOC *m_process_doc;      // for no expand, it matched input document. when node is expanded, it will
      // point iterator value
      bool m_is_row_fetched;              // set to true when current row is fetched
      bool m_need_advance_row;            // set to true when next node action is to advance row
      bool m_is_node_consumed;            // set to true when all node rows (based on current input) are consumed
      bool m_iteration_started;           // set to true when iteration was started by at least one child.
      // note: when all children are consumed, if row was never expanded, it is
      //       generated by leaving all children values as nil

      void advance_row_cursor (void);     // advance to next row
      void start_json_iterator (void);    // start json iteration of changing input document
      int fetch_row (void);               // fetch current row (if not fetched)
      void end (void);                    // finish current node scan

      cursor (void);
    };

    scanner::cursor::cursor (void)
      : m_child (0)
      , m_node (NULL)
      , m_input_doc ()
      , m_process_doc (NULL)
      , m_is_row_fetched (false)
      , m_need_advance_row (false)
      , m_is_node_consumed (true)
      , m_iteration_started (false)
    {
      //
    }

    void

    scanner::cursor::advance_row_cursor ()
    {
      // don't advance again in row
      m_need_advance_row = false;

      // reset row expansion
      m_iteration_started = false;

      if (m_node->m_iterator == NULL || !db_json_iterator_has_next (*m_node->m_iterator))
	{
	  end ();
	  return;
	}

      // advance with row
      db_json_iterator_next (*m_node->m_iterator);
      m_is_row_fetched = false;

      // advance also with ordinality
      m_node->m_ordinality++;

      // reset child to first branch
      m_child = 0;
    }

    void
    scanner::cursor::start_json_iterator (void)
    {
      m_is_node_consumed = false;
      if (m_node->m_is_iterable_node)
	{
	  assert (db_json_get_type (m_input_doc.get_mutable_reference ()) == DB_JSON_ARRAY);
	  db_json_set_iterator (m_node->m_iterator, *m_input_doc.get_mutable_reference ());
	}
    }

    int
    scanner::cursor::fetch_row (void)
    {
      if (m_is_row_fetched)
	{
	  // already fetched
	  return NO_ERROR;
	}

      // if we have an iterator, value is obtained from iterator. otherwise, use m_input_doc
      if (m_node->m_iterator != NULL)
	{
	  m_process_doc = db_json_iterator_get_document (*m_node->m_iterator);
	}
      else
	{
	  assert (!m_node->m_is_iterable_node);
	  // todo: is it guaranteed we do not use m_process_doc after we delete input_doc?
	  m_process_doc = m_input_doc.get_mutable_reference ();
	}

      if (m_process_doc == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      int error_code = NO_ERROR;
      for (size_t i = 0; i < m_node->m_output_columns_size; ++i)
	{
	  error_code = m_node->m_output_columns[i].evaluate (*m_process_doc, m_node->m_ordinality);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	}

      return NO_ERROR;
    }

    void
    scanner::cursor::end (void)
    {
      m_is_node_consumed = true;

      db_json_reset_iterator (m_node->m_iterator);

      m_process_doc = NULL;
      m_node->clear_columns (false);
    }

    size_t
    scanner::get_tree_height (const cubxasl::json_table::node &node)
    {
      size_t max_child_height = 0;

      for (size_t i = 0; i < node.m_nested_nodes_size; ++i)
	{
	  const cubxasl::json_table::node &child = node.m_nested_nodes[i];
	  max_child_height = std::max (max_child_height, get_tree_height (child));
	}

      return 1 + max_child_height;
    }

    void
    scanner::init (cubxasl::json_table::spec_node &spec)
    {
      m_specp = &spec;

      assert (m_specp->m_node_count > 0);

      m_tree_height = get_tree_height (*m_specp->m_root_node);
      m_scan_cursor_depth = 0;

      m_scan_cursor = new cursor[m_tree_height];

      // init cursor nodes to left-most branch
      json_table_node *t = m_specp->m_root_node;
      m_scan_cursor[0].m_node = t;
      for (int i = 1; t->m_nested_nodes_size != 0; t = &t->m_nested_nodes[0], ++i)
	{
	  m_scan_cursor[i].m_node = t;
	}

      init_iterators (*m_specp->m_root_node);
    }

    void
    scanner::clear (xasl_node *xasl_p, bool is_final, bool is_final_clear)
    {
      // columns should be released every time
      m_specp->m_root_node->clear_xasl (is_final_clear);
      reset_ordinality (*m_specp->m_root_node);

      // all json documents should be released depending on is_final
      if (is_final)
	{
	  for (size_t i = 0; i < m_tree_height; ++i)
	    {
	      cursor &cursor = m_scan_cursor[i];
	      cursor.m_input_doc.clear ();

	      cursor.m_child = 0;
	      cursor.m_is_row_fetched = false;
	      cursor.m_need_advance_row = false;
	      cursor.m_is_node_consumed = true;
	      cursor.m_iteration_started = false;
	    }

	  m_specp->m_root_node->clear_iterators (is_final_clear);

	  if (is_final_clear)
	    {
	      delete [] m_scan_cursor;
	    }
	}
    }

    int
    scanner::open (cubthread::entry *thread_p)
    {
      int error_code = NO_ERROR;
      const JSON_DOC *document = NULL;

      // we need the starting value to expand into a list of records
      DB_VALUE *value_p = NULL;
      error_code = fetch_peek_dbval (thread_p, m_specp->m_json_reguvar, m_vd, NULL, NULL, NULL, &value_p);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      if (value_p == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      if (db_value_is_null (value_p))
	{
	  assert (m_scan_cursor[0].m_is_node_consumed);
	  return NO_ERROR;
	}

      // build m_scan_cursor

      if (db_value_type (value_p) == DB_TYPE_JSON)
	{
	  document = db_get_json_document (value_p);

	  error_code = init_cursor (*document, *m_specp->m_root_node, m_scan_cursor[0]);
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

	  // we should use explicit coercion, implicit coercion is not allowed between char and json
	  tp_domain_status status = tp_value_cast (value_p, &json_cast_value, &tp_Json_domain, false);
	  if (status != DOMAIN_COMPATIBLE)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      return error_code;
	    }

	  document = db_get_json_document (&json_cast_value);

	  error_code = init_cursor (*document, *m_specp->m_root_node, m_scan_cursor[0]);

	  pr_clear_value (&json_cast_value);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	}

      // if we gather expr from another table, for each row we need to reset the ordinality
      reset_ordinality (*m_specp->m_root_node);
      m_scan_cursor_depth = 0;

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
      bool has_row = false;
      int error_code = NO_ERROR;
      DB_LOGICAL logical = V_FALSE;

      if (sid.position == S_BEFORE)
	{
	  error_code = open (thread_p);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  sid.position = S_ON;
	  sid.status = S_STARTED;
	}
      else if (sid.position != S_ON)
	{
	  assert (false);
	  sid.status = S_ENDED;
	  sid.position = S_AFTER;
	  return ER_FAILED;
	}

      while (true)
	{
	  error_code = scan_next_internal (thread_p, 0, has_row);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  if (!has_row)
	    {
	      sid.status = S_ENDED;
	      sid.position = S_AFTER;
	      break;
	    }

	  if (m_scan_predicate.pred_expr == NULL)
	    {
	      break;
	    }

	  logical = m_scan_predicate.pr_eval_fnc (thread_p, m_scan_predicate.pred_expr, sid.vd, NULL);
	  if (logical == V_TRUE)
	    {
	      break;
	    }
	  if (logical == V_ERROR)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      return error_code;
	    }
	}

      return NO_ERROR;
    }

    int
    scanner::set_input_document (cursor &cursor_arg, const cubxasl::json_table::node &node, const JSON_DOC &document)
    {
      int error_code = NO_ERROR;
      cursor_arg.m_input_doc.clear ();

      // extract input document
      error_code = db_json_extract_document_from_path (&document, {node.m_path}, cursor_arg.m_input_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      if (!cursor_arg.m_input_doc.is_mutable ())
	{
	  // cannot retrieve input_doc from path
	  cursor_arg.m_is_node_consumed = true;
	}
      else
	{
	  // start cursor based on input document
	  cursor_arg.start_json_iterator ();
	}

      return NO_ERROR;
    }

    int
    scanner::init_cursor (const JSON_DOC &doc, cubxasl::json_table::node &node, cursor &cursor_out)
    {
      cursor_out.m_is_row_fetched = false;
      cursor_out.m_child = 0;
      cursor_out.m_node = &node;

      return set_input_document (cursor_out, node, doc);
    }

    int
    scanner::set_next_cursor (const cursor &current_cursor, int next_depth)
    {
      return init_cursor (*current_cursor.m_process_doc,
			  current_cursor.m_node->m_nested_nodes[current_cursor.m_child],
			  m_scan_cursor[next_depth]);
    }

    void
    scanner::clear_node_columns (cubxasl::json_table::node &node)
    {
      for (size_t i = 0; i < node.m_output_columns_size; ++i)
	{
	  (void) pr_clear_value (node.m_output_columns[i].m_output_value_pointer);
	  (void) db_make_null (node.m_output_columns[i].m_output_value_pointer);
	}
    }

    void
    scanner::init_iterators (cubxasl::json_table::node &node)
    {
      node.init_iterator ();

      for (size_t i = 0; i < node.m_nested_nodes_size; ++i)
	{
	  init_iterators (node.m_nested_nodes[i]);
	}
    }

    void
    scanner::reset_ordinality (cubxasl::json_table::node &node)
    {
      node.init_ordinality ();

      for (size_t i = 0; i < node.m_nested_nodes_size; ++i)
	{
	  reset_ordinality (node.m_nested_nodes[i]);
	}
    }

    int
    scanner::scan_next_internal (cubthread::entry *thread_p, int depth, bool &found_row_output)
    {
      int error_code = NO_ERROR;
      cursor &this_cursor = m_scan_cursor[depth];

      // check if cursor is already in child node
      if (m_scan_cursor_depth >= depth + 1)
	{
	  // advance to child
	  error_code = scan_next_internal (thread_p, depth + 1, found_row_output);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  if (found_row_output)
	    {
	      // advance to new child
	      return NO_ERROR;
	    }
	  else
	    {
	      this_cursor.m_child++;
	    }
	}

      // get the cursor from the current depth
      assert (this_cursor.m_node != NULL);

      // loop through node's rows and children until all possible rows are generated
      while (!this_cursor.m_is_node_consumed)
	{
	  // note - do not loop without taking new action
	  // an action is either advancing to new row or advancing to new child
	  if (this_cursor.m_need_advance_row)
	    {
	      this_cursor.advance_row_cursor ();
	      if (this_cursor.m_is_node_consumed)
		{
		  break;
		}
	    }

	  // first things first, fetch current row
	  error_code = this_cursor.fetch_row ();
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }

	  // if this is leaf node, then we have a new complete row
	  if (this_cursor.m_node->m_nested_nodes_size == 0)
	    {
	      found_row_output = true;
	      // next time, cursor will have to be incremented
	      this_cursor.m_need_advance_row = true;
	      return NO_ERROR;
	    }

	  // non-leaf
	  // advance to current child
	  if (this_cursor.m_child == this_cursor.m_node->m_nested_nodes_size)
	    {
	      // next time, cursor will have to be incremented
	      this_cursor.m_need_advance_row = true;

	      if (this_cursor.m_iteration_started)
		{
		  continue;
		}

	      found_row_output = true;
	      return NO_ERROR;
	    }

	  // create cursor for next child
	  error_code = set_next_cursor (this_cursor, depth + 1);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  cursor &next_cursor = m_scan_cursor[depth + 1];

	  if (!next_cursor.m_is_node_consumed)
	    {
	      // advance current level in tree
	      m_scan_cursor_depth++;

	      this_cursor.m_iteration_started = true;

	      error_code = scan_next_internal (thread_p, depth + 1, found_row_output);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }
	  else
	    {
	      this_cursor.m_child++;
	      continue;
	    }

	  if (found_row_output)
	    {
	      // found a row; scan is stopped
	      return NO_ERROR;
	    }
	  else
	    {
	      // child could not generate a row. advance to next
	      this_cursor.m_child++;
	    }
	}

      // no more rows...
      found_row_output = false;

      if (m_scan_cursor_depth > 0)
	{
	  // remove this cursor
	  m_scan_cursor_depth--;
	}

      return NO_ERROR;
    }

    SCAN_PRED &
    scanner::get_predicate ()
    {
      return m_scan_predicate;
    }

    void
    scanner::set_value_descriptor (val_descr *vd)
    {
      m_vd = vd;
    }
  } // namespace json_table
} // namespace cubscan
