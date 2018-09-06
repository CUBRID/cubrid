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
      std::size_t m_row;
      std::size_t m_child;
      cubxasl::json_table::node *m_node;
      JSON_DOC *m_input_doc;            // used for non-array / single row
      const JSON_DOC *m_row_doc;        // used only for arrays and multiple rows
      const JSON_DOC *m_process_doc;    // is either input_doc or row doc
      bool m_is_row_fetched;

      void advance_row_cursor();
      void set_json_iterator (const JSON_DOC &document);

      cursor (void);
    };

    scanner::cursor::cursor (void)
      : m_input_doc (NULL)
      , m_process_doc (NULL)
      , m_child (0)
      , m_row (0)
      , m_is_row_fetched (false)
    {
      //
    }

    void
    scanner::cursor::advance_row_cursor()
    {
      // advance with row
      m_row++;
      m_is_row_fetched = false;

      // advance also with ordinality
      if (m_node->m_need_inc_ordinality)
	{
	  m_node->m_ordinality++;
	}

      if (m_node->check_need_expand())
	{
	  if (db_json_iterator_has_next (*m_node->m_iterator))
	    {
	      db_json_iterator_next (*m_node->m_iterator);
	    }
	}

      // reset child to first branch
      m_child = 0;
    }

    void
    scanner::cursor::set_json_iterator (const JSON_DOC &document)
    {
      int error_code = NO_ERROR;
      DB_JSON_TYPE input_doc_type = DB_JSON_TYPE::DB_JSON_UNKNOWN;
      JSON_DOC *result_doc = NULL;

      // extract the document from parent path
      error_code = db_json_extract_document_from_path (&document, m_node->m_path.c_str(), result_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR();
	  return;
	}

      input_doc_type = db_json_get_type (result_doc);

      if (m_node->check_need_expand())
	{
	  if ((m_node->m_expand_type == json_table_expand_type::JSON_TABLE_ARRAY_EXPAND &&
	       input_doc_type == DB_JSON_TYPE::DB_JSON_ARRAY) ||
	      (m_node->m_expand_type == json_table_expand_type::JSON_TABLE_OBJECT_EXPAND &&
	       input_doc_type == DB_JSON_TYPE::DB_JSON_OBJECT))
	    {
	      db_json_set_iterator (m_node->m_iterator, *result_doc);
	    }
	  else
	    {
	      db_json_reset_iterator (m_node->m_iterator);
	    }
	}

      db_json_delete_doc (result_doc);
    }

    int
    scanner::fetch_columns (const JSON_DOC &document, std::vector<cubxasl::json_table::column> &columns,
			    const cubxasl::json_table::node &node)
    {
      int error_code = NO_ERROR;
      for (auto &col : columns)
	{
	  error_code = col.evaluate (document, node.m_ordinality);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR();
	      return error_code;
	    }
	}

      return NO_ERROR;
    }

    std::size_t
    scanner::get_row_count (cursor &cursor)
    {
      // if multiple rows ([*] or .*) count the number of members from parent path
      if (cursor.m_node->check_need_expand())
	{
	  return db_json_iterator_count_members (*cursor.m_node->m_iterator);
	}

      return 1;
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
      m_specp->m_root_node->clear_columns();
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
	      cursor.m_row = 0;
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
    scanner::set_input_document (cursor &cursor, const cubxasl::json_table::node &node, const JSON_DOC &document)
    {
      int error_code = NO_ERROR;

      if (node.check_need_expand ())
	{
	  // set the input document and the iterator
	  cursor.set_json_iterator (document);
	}
      else
	{
	  error_code = db_json_extract_document_from_path (&document,
		       // here we can use the unprocessed node path
		       node.m_path.c_str(),
		       cursor.m_input_doc);

	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR();
	      return error_code;
	    }
	}

      return NO_ERROR;
    }

    int
    scanner::init_cursor (const JSON_DOC &doc, cubxasl::json_table::node &node, cursor &cursor_out)
    {
      cursor_out.m_is_row_fetched = false;
      cursor_out.m_row = 0;
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
      size_t total_rows_number = 0;

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

      total_rows_number = get_row_count (this_cursor);

      // now we need to advance to a leaf level to compute a row
      // iterate through nodes on the same level
      while (this_cursor.m_row < total_rows_number)
	{
	  if (!this_cursor.m_is_row_fetched)
	    {
	      // if we need to expand continue to process where we left, else use the whole input_doc
	      if (this_cursor.m_node->check_need_expand())
		{
		  this_cursor.m_process_doc = db_json_iterator_get (*this_cursor.m_node->m_iterator);
		}
	      else
		{
		  this_cursor.m_process_doc = this_cursor.m_input_doc;
		}

	      if (this_cursor.m_process_doc != nullptr)
		{
		  // fetch other columns too
		  error_code = fetch_columns (*this_cursor.m_process_doc, this_cursor.m_node->m_output_columns,
					      *this_cursor.m_node);

		  if (error_code != NO_ERROR)
		    {
		      ASSERT_ERROR();
		      return error_code;
		    }
		  this_cursor.m_is_row_fetched = true;

		  this_cursor.m_node->m_need_inc_ordinality = true;
		}
	      else
		{
		  clear_node_columns (*this_cursor.m_node);
		  // if we can not expand this node we should not increase its ordinality
		  this_cursor.m_node->m_need_inc_ordinality = false;
		}

	      // fall
	    }

	  // if we are in a leaf node or we finished to evaluate all children
	  if (this_cursor.m_child == this_cursor.m_node->m_nested_nodes.size())
	    {
	      this_cursor.advance_row_cursor();
	      has_row = true;
	      return NO_ERROR;
	    }

	  // create cursor for next child
	  error_code = set_next_cursor (this_cursor, depth + 1);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR();
	      return error_code;
	    }

	  cursor &next_cursor = m_scan_cursor[depth + 1];
	  if (db_json_iterator_is_empty (*next_cursor.m_node->m_iterator))
	    {
	      clear_node_columns (*next_cursor.m_node);

	      if (this_cursor.m_child < this_cursor.m_node->m_nested_nodes.size() - 1)
		{
		  this_cursor.m_child++;
		  continue;
		}

	      this_cursor.advance_row_cursor();

	      has_row = true;
	      return NO_ERROR;
	    }

	  // advance current level in tree
	  m_scan_cursor_depth++;

	  error_code = scan_next_internal (thread_p, depth + 1, has_row);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  if (!has_row)
	    {
	      // try another child
	      this_cursor.m_child++;
	      continue;
	    }

	  // found a row
	  return NO_ERROR;
	}

      // no more rows...
      has_row = false;

      // set columns values to NULL
      clear_node_columns (*this_cursor.m_node);

      if (m_scan_cursor_depth > 0)
	{
	  // remove this cursor
	  m_scan_cursor_depth--;
	  // advance row in parent when finished current branch
	  cursor &parent = m_scan_cursor[m_scan_cursor_depth];

	  if (parent.m_child < parent.m_node->m_nested_nodes.size() - 1)
	    {
	      parent.m_child++;
	    }
	  else
	    {
	      parent.advance_row_cursor();
	    }
	}

      return NO_ERROR;
    }

    SCAN_PRED &scanner::get_predicate()
    {
      return m_scan_predicate;
    }
  } // namespace json_table
} // namespace cubscan
