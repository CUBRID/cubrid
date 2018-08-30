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
      JSON_DOC *m_input_doc;            // used for non-array / single row
      const JSON_DOC *m_row_doc;        // used only for arrays and multiple rows
      const JSON_DOC *m_process_doc;    // is either input_doc or row doc
      bool m_is_row_evaluated;
      bool m_need_expand;
      JSON_ITERATOR *m_json_iterator;

      void advance_row_cursor();
      void set_json_iterator (const JSON_DOC &document, const char *parent_path);

      cursor (void);
    };

    scanner::cursor::cursor (void)
      : m_input_doc (NULL)
      , m_process_doc (NULL)
      , m_json_iterator (NULL)
      , m_child (0)
      , m_row (0)
      , m_is_row_evaluated (false)
      , m_need_expand (false)
    {
      //
    }

    void
    scanner::cursor::advance_row_cursor()
    {
      // advance with row
      m_row++;
      m_is_row_evaluated = false;

      // advance also with ordinality
      if (m_node->m_need_inc_ordinality)
	{
	  m_node->m_ordinality++;
	}

      if (m_need_expand)
	{
	  if (db_json_iterator_has_next (*m_json_iterator))
	    {
	      db_json_iterator_next (*m_json_iterator);
	    }
	}

      // reset child to first branch
      m_child = 0;
    }

    void
    scanner::cursor::set_json_iterator (const JSON_DOC &document, const char *parent_path)
    {
      int error_code = NO_ERROR;
      DB_JSON_TYPE input_doc_type = DB_JSON_TYPE::DB_JSON_UNKNOWN;

      // extract the document from parent path
      error_code = db_json_extract_document_from_path (&document, parent_path, m_input_doc);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR();
	  return;
	}

      input_doc_type = db_json_get_type (m_input_doc);

      if (m_need_expand)
	{
	  if ((scanner::str_ends_with (m_node->m_path, "[*]") && input_doc_type != DB_JSON_TYPE::DB_JSON_ARRAY) ||
	      (scanner::str_ends_with (m_node->m_path, ".*") && input_doc_type != DB_JSON_TYPE::DB_JSON_OBJECT))
	    {
	      if (m_json_iterator != NULL)
		{
		  db_json_delete_json_iterator (m_json_iterator);
		}

	      // create empty iterator
	      m_json_iterator = db_json_create_iterator (NULL);

	      return;
	    }
	}

      if (m_json_iterator != NULL)
	{
	  // we can reuse the iterator if it has the same type as the old one, otherwise we need to create a new one
	  DB_JSON_TYPE old_type = db_json_iterator_get_type_of_doc (*m_json_iterator);
	  DB_JSON_TYPE current_type = db_json_get_type (m_input_doc);

	  if (old_type != current_type)
	    {
	      db_json_delete_json_iterator (m_json_iterator);
	      m_json_iterator = db_json_create_iterator (m_input_doc);
	    }
	  else
	    {
	      db_json_reset_iterator (m_json_iterator, *m_input_doc);
	    }
	}
      else
	{
	  m_json_iterator = db_json_create_iterator (m_input_doc);
	}
    }

    int
    scanner::fetch_columns (const JSON_DOC &document, std::vector<cubxasl::json_table::column> &columns,
			    const cubxasl::json_table::node &node)
    {
      int error_code = NO_ERROR;
      for (auto col : columns)
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

      int error_code = fetch_columns (document, node.m_predicate_columns, node);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      // what about the val_descr?
      logical_output = eval_function (thread_p, node.m_predicate_expression, NULL, NULL);
      if (logical_output == V_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}
      return NO_ERROR;
    }

    std::size_t
    scanner::get_row_count (cursor &cursor)
    {
      // if multiple rows ([*] or .*) count the number of members from parent path
      if (cursor.m_need_expand)
	{
	  return db_json_iterator_count_members (*cursor.m_json_iterator);
	}

      return 1;
    }

    size_t
    scanner::get_tree_height (const cubxasl::json_table::node &node)
    {
      size_t max_child_height = 0;

      for (const cubxasl::json_table::node &child : node.m_nested_nodes)
	{
	  max_child_height = MAX (max_child_height, get_tree_height (child));
	}

      return 1 + max_child_height;
    }

    void
    scanner::init_eval_functions (const cubxasl::json_table::node &node)
    {
      pred_expr *pr = node.m_predicate_expression;
      DB_TYPE single_node_type = DB_TYPE_NULL;

      // set predicate function
      m_eval_functions[node.m_id] = (pr) ? eval_fnc (NULL, pr, &single_node_type) : NULL;

      for (const cubxasl::json_table::node &child : node.m_nested_nodes)
	{
	  init_eval_functions (child);
	}
    }

    void
    scanner::init (cubxasl::json_table::spec_node &spec)
    {
      m_specp = &spec;

      assert (m_specp->m_node_count > 0);

      m_eval_functions = new PR_EVAL_FNC[m_specp->m_node_count];

      m_tree_height = get_tree_height (*m_specp->m_root_node);

      m_scan_cursor = new cursor[m_tree_height];

      // init cursor nodes to left-most first branch
      json_table_node *t = m_specp->m_root_node;
      m_scan_cursor[0].m_node = t;
      for (int i = 1; !t->m_nested_nodes.empty(); t = &t->m_nested_nodes[0], ++i)
	{
	  m_scan_cursor[i].m_node = t;
	}

      // walk json table tree and initialize evaluation functions
      init_eval_functions (*m_specp->m_root_node);
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
	      db_json_delete_json_iterator (cursor.m_json_iterator);

	      cursor.m_child = 0;
	      cursor.m_row = 0;
	      cursor.m_is_row_evaluated = false;
	      cursor.m_need_expand = false;
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

	  error_code = set_input_document (m_scan_cursor[0], *m_scan_cursor[0].m_node, *document);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR();
	      return error_code;
	    }

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
	  // todo: is this implicit or explicit?
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
    scanner::next_scan (cubthread::entry *thread_p, scan_id_struct &sid, FILTER_INFO &data_filter)
    {
      // todo: add data_filter as data member

      bool success = true;
      int error_code = NO_ERROR;

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

      error_code = next_internal (thread_p, 0, success, data_filter);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR();
	  return error_code;
	}
      if (!success)
	{
	  // todo
	  sid.status = S_ENDED;
	  sid.position = S_AFTER;
	}
      return NO_ERROR;
    }

    bool
    scanner::str_ends_with (const std::string &str, const std::string &end)
    {
      return end.size() <= str.size() && str.compare (str.size() - end.size(), end.size(), end) == 0;
    }

    bool
    scanner::check_need_expand (const cubxasl::json_table::node &node)
    {
      return str_ends_with (node.m_path, "[*]") || str_ends_with (node.m_path, ".*");
    }

    std::string
    scanner::get_parent_path (const cubxasl::json_table::node &node)
    {
      if (str_ends_with (node.m_path, "[*]"))
	{
	  return node.m_path.substr (0, node.m_path.size() - 3);
	}
      else if (str_ends_with (node.m_path, ".*"))
	{
	  return node.m_path.substr (0, node.m_path.size() - 2);
	}

      return std::string();
    }

    int
    scanner::set_input_document (cursor &cursor, const cubxasl::json_table::node &node, const JSON_DOC &document)
    {
      int error_code = NO_ERROR;

      if (check_need_expand (node))
	{
	  std::string parent_path = get_parent_path (node);

	  cursor.m_need_expand = true;

	  // set the input document and the iterator
	  cursor.set_json_iterator (document, parent_path.c_str());
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

	  cursor.m_need_expand = false;
	}

      return NO_ERROR;
    }

    int
    scanner::init_cursor (const JSON_DOC &doc, cubxasl::json_table::node &node, cursor &cursor_out)
    {
      cursor_out.m_is_row_evaluated = false;
      cursor_out.m_need_expand = false;
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
    scanner::clear_columns (std::vector<cubxasl::json_table::column> &columns)
    {
      for (auto &column : columns)
	{
	  (void)pr_clear_value (column.m_output_value_pointer);
	  (void)db_make_null (column.m_output_value_pointer);
	}
    }

    void
    scanner::clear_node_columns (cubxasl::json_table::node &node)
    {
      clear_columns (node.m_predicate_columns);
      clear_columns (node.m_output_columns);
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
    scanner::next_internal (cubthread::entry *thread_p, int depth, bool &success, FILTER_INFO &data_filter)
    {
      int error_code = NO_ERROR;
      size_t total_rows_number = 0;
      DB_LOGICAL logical;

      // check if cursor is already in child node
      // todo: check later if '>' or '>='
      if (m_scan_cursor_depth >= depth + 1)
	{
	  // advance to child
	  error_code = next_internal (thread_p, depth + 1, success, data_filter);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }
	  if (success)
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
	  if (!this_cursor.m_is_row_evaluated)
	    {
	      // if we need to expand continue to process where we left, else use the whole input_doc
	      if (this_cursor.m_need_expand)
		{
		  this_cursor.m_process_doc = db_json_iterator_get (*this_cursor.m_json_iterator);
		}
	      else
		{
		  this_cursor.m_process_doc = this_cursor.m_input_doc;
		}

	      if (this_cursor.m_process_doc != nullptr)
		{
		  this_cursor.m_node->m_need_inc_ordinality = true;
		  char *raw_json = db_json_get_json_body_from_document (*this_cursor.m_process_doc);

		  // todo: split scan_pred into preds for columns
		  error_code = evaluate (*this_cursor.m_node, thread_p, *this_cursor.m_process_doc, logical);
		  if (error_code != NO_ERROR)
		    {
		      ASSERT_ERROR();
		      return error_code;
		    }
		  if (logical != V_TRUE)
		    {
		      // we need another row
		      this_cursor.advance_row_cursor();
		      continue;
		    }
		  this_cursor.m_is_row_evaluated = true;

		  // fetch other columns too
		  error_code = fetch_columns (*this_cursor.m_process_doc, this_cursor.m_node->m_output_columns,
					      *this_cursor.m_node);

		  // todo: use scan_pred only on nodes with predicates
		  logical = eval_data_filter (thread_p, NULL, NULL, NULL, &data_filter);
		  if (logical != V_TRUE)
		    {
		      // we need another row
		      this_cursor.advance_row_cursor();
		      continue;
		    }

		  if (error_code != NO_ERROR)
		    {
		      ASSERT_ERROR();
		      return error_code;
		    }
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
	      success = true;
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
	  if (db_json_iterator_get_type (*next_cursor.m_json_iterator) == JSON_ITERATOR_TYPE::JSON_ITERATOR_EMPTY)
	    {
	      clear_node_columns (*next_cursor.m_node);

	      if (this_cursor.m_child < this_cursor.m_node->m_nested_nodes.size() - 1)
		{
		  this_cursor.m_child++;
		  continue;
		}

	      this_cursor.advance_row_cursor();

	      success = true;
	      return NO_ERROR;
	    }

	  // advance current level in tree
	  m_scan_cursor_depth++;

	  error_code = next_internal (thread_p, depth + 1, success, data_filter);
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
  } // namespace json_table
} // namespace cubscan
