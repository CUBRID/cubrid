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
      std::size_t m_child;
      cubxasl::json_table::node *m_node;
      JSON_DOC *m_input_doc;            // used for non-array / single row
      const JSON_DOC *m_process_doc;    // is either input_doc or row doc
      bool m_is_row_fetched;
      bool m_need_advance_row;
      bool m_is_node_consumed;

      void advance_row_cursor (void);
      void start_json_iterator (void);
      int fetch_row (void);
      void end (void);

      cursor (void);
      ~cursor (void);
    };

    scanner::cursor::cursor (void)
      : m_input_doc (NULL)
      , m_process_doc (NULL)
      , m_child (0)
      , m_is_row_fetched (false)
      , m_need_advance_row (false)
      , m_is_node_consumed (false)
    {
      //
    }

    scanner::cursor::~cursor (void)
    {
      db_json_delete_doc (m_input_doc);
    }

    void
    scanner::cursor::advance_row_cursor()
    {
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
      // how it works:
      //
      // based on path definition, we have three cases
      //
      //    [*] - expect and array and expand its elements into rows
      //    .*  - expect an object and the values of its members into rows
      //        - all other paths will just generate one row based on the json object found at path
      //
      //    When array or object expansion must happen, if the input document does not match expected JSON type, no rows
      //    will be generated. Expected types are DB_JSON_ARRAY of array expansion and DB_JSON_OBJECT for object
      //    expansion.

      switch (m_node->m_expand_type)
	{
	case json_table_expand_type::JSON_TABLE_NO_EXPAND:
	  // nothing to do;
	  m_is_node_consumed = false;
	  break;

	case json_table_expand_type::JSON_TABLE_ARRAY_EXPAND:
	  // only DB_JSON_ARRAY can be expanded
	  if (db_json_get_type (m_input_doc) == DB_JSON_ARRAY)
	    {
	      m_is_node_consumed = false;
	      db_json_set_iterator (m_node->m_iterator, *m_input_doc);
	    }
	  break;

	case json_table_expand_type::JSON_TABLE_OBJECT_EXPAND:
	  // only DB_JSON_OBJECT can be expanded
	  if (db_json_get_type (m_input_doc) == DB_JSON_OBJECT)
	    {
	      m_is_node_consumed = false;
	      db_json_set_iterator (m_node->m_iterator, *m_input_doc);
	    }
	  break;

	default:
	  assert (false);
	  break;
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
	  assert (m_node->m_expand_type == json_table_expand_type::JSON_TABLE_NO_EXPAND);
	  m_process_doc = m_input_doc;
	}

      if (m_process_doc == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      int error_code = NO_ERROR;
      for (auto &col : m_node->m_output_columns)
	{
	  error_code = col.evaluate (*m_process_doc, m_node->m_ordinality);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR();
	      return error_code;
	    }
	}

      return NO_ERROR;
    }

    void
    scanner::cursor::end (void)
    {
      m_is_node_consumed = true;
      if (m_node->m_iterator != NULL)
	{
	  db_json_reset_iterator (m_node->m_iterator);
	}
      m_process_doc = NULL;
      m_node->clear_columns ();
      m_need_advance_row = false;
    }

    size_t
    scanner::get_tree_height (const cubxasl::json_table::node &node)
    {
      size_t max_child_height = 0;

      for (const cubxasl::json_table::node &child : node.m_nested_nodes)
	{
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

      m_scan_cursor = new cursor[m_tree_height];

      // init cursor nodes to left-most first branch
      json_table_node *t = m_specp->m_root_node;
      m_scan_cursor[0].m_node = t;
      for (int i = 1; !t->m_nested_nodes.empty(); t = &t->m_nested_nodes[0], ++i)
	{
	  m_scan_cursor[i].m_node = t;
	}

      init_iterators (*m_specp->m_root_node);
    }

    void
    scanner::clear (xasl_node *xasl_p, bool is_final)
    {
      // columns should be released every time
      m_specp->m_root_node->clear_tree ();
      reset_ordinality (*m_specp->m_root_node);

      // all json documents should be release depending on is_final
      if (is_final)
	{
	  for (size_t i = 0; i < m_tree_height; ++i)
	    {
	      cursor &cursor = m_scan_cursor[i];
	      db_json_delete_doc (cursor.m_input_doc);
	      //db_json_delete_json_iterator (cursor.m_json_iterator);

	      cursor.m_child = 0;
	      cursor.m_is_row_fetched = false;
	    }
	}

    }

    int
    scanner::open (cubthread::entry *thread_p)
    {
      int error_code = NO_ERROR;
      const JSON_DOC *document = NULL;

      // so... we need to generate the whole list file

      // we need the starting value to expand into a list of records
      DB_VALUE *value_p = NULL;
      error_code = fetch_peek_dbval (thread_p, m_specp->m_json_reguvar, NULL, NULL, NULL, NULL, &value_p);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR();
	  return error_code;
	}
      if (value_p == NULL || db_value_is_null (value_p))
	{
	  assert (false);
	  return ER_FAILED;
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
	      ASSERT_ERROR();
	      return error_code;
	    }
	}

      // if we gather expr from another table, for each row we need to reset the ordinality
      reset_ordinality (*m_specp->m_root_node);

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
      bool has_row = true;
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
	      ASSERT_ERROR();
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

      // extract input document
      error_code = db_json_extract_document_from_path (&document, node.m_path.c_str (), cursor_arg.m_input_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      // start cursor based on input document
      cursor_arg.start_json_iterator ();
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
      for (auto &column : node.m_output_columns)
	{
	  (void) pr_clear_value (column.m_output_value_pointer);
	  (void) db_make_null (column.m_output_value_pointer);
	}
    }

    void
    scanner::init_iterators (cubxasl::json_table::node &node)
    {
      node.init_iterator();

      for (cubxasl::json_table::node &child : node.m_nested_nodes)
	{
	  init_iterators (child);
	}
    }

    void
    scanner::reset_ordinality (cubxasl::json_table::node &node)
    {
      node.m_ordinality = 1;
      for (cubxasl::json_table::node &child : node.m_nested_nodes)
	{
	  reset_ordinality (child);
	}
    }

    int
    scanner::scan_next_internal (cubthread::entry *thread_p, int depth, bool &has_row)
    {
      int error_code = NO_ERROR;

      // check if cursor is already in child node
      if (m_scan_cursor_depth >= depth + 1)
	{
	  // advance to child
	  error_code = scan_next_internal (thread_p, depth + 1, has_row);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  if (has_row)
	    {
	      return NO_ERROR;
	    }
	}

      // get the cursor from the current depth
      cursor &this_cursor = m_scan_cursor[depth];
      assert (this_cursor.m_node != NULL);

      // loop through node's rows and children until all possible rows are generated
      while (true)
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
	  if (this_cursor.m_node->m_nested_nodes.empty ())
	    {
	      has_row = true;
	      // next time, cursor will have to be incremented
	      this_cursor.m_need_advance_row = true;
	      return NO_ERROR;
	    }

	  // advance to current child
	  if (this_cursor.m_child == this_cursor.m_node->m_nested_nodes.size ())
	    {
	      // advance to next row
	      this_cursor.m_need_advance_row = true;
	      continue;
	    }

	  // create cursor for next child
	  error_code = set_next_cursor (this_cursor, depth + 1);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR();
	      return error_code;
	    }
	  cursor &next_cursor = m_scan_cursor[depth + 1];

	  if (!next_cursor.m_is_node_consumed)
	    {
	      // advance current level in tree
	      m_scan_cursor_depth++;

	      error_code = scan_next_internal (thread_p, depth + 1, has_row);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }

	  if (has_row)
	    {
	      // found a row; scan is stopped
	      return NO_ERROR;
	    }
	  else
	    {
	      // child could not generate a row. advance to next
	      this_cursor.m_child++;
	      return ER_FAILED;
	    }
	}

      // no more rows...
      has_row = false;

      if (m_scan_cursor_depth > 0)
	{
	  // remove this cursor
	  m_scan_cursor_depth--;
	}

      return NO_ERROR;
    }

    SCAN_PRED &scanner::get_predicate()
    {
      return m_scan_predicate;
    }
  } // namespace json_table
} // namespace cubscan
