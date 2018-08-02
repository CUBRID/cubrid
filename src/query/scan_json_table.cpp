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
#include "dbtype_def.h"
#include "fetch.h"
#include "list_file.h"
#include "query_list.h"

namespace cubscan
{
  namespace json_table
  {
    void
    scanner::init (scan_id_struct &sid, cubxasl::json_table::spec_node &spec)
    {
      m_scanid = &sid;
      m_specp = &spec;

      // query file
      m_list_file = new qfile_list_id ();
      m_list_scan = new qfile_list_scan_id ();
      m_tuple = new qfile_tuple_record ();

      QFILE_CLEAR_LIST_ID (m_list_file);
    }

    void
    scanner::clear (void)
    {
      m_specp = NULL;

      delete m_list_file;
      delete m_list_scan;
      delete m_tuple;
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
      if (value_p == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}

      // todo
      return NO_ERROR;
    }

    void
    scanner::end (cubthread::entry *thread_p)
    {
      assert (thread_p != NULL);

      qfile_destroy_list (thread_p, m_list_file);
    }

    int
    scanner::process_row (cubthread::entry *thread_p, const JSON_DOC &value, scan_node &node)
    {
      int error_code = NO_ERROR;

      if (node.m_eval_function != NULL)
	{
	  // evaluate predicate columns
	  for (auto col : node.m_nested_node.m_predicate_columns)
	    {
	      error_code = col.evaluate (value);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error_code;
		}
	    }
	  // we can now evaluate predicate

	  // todo: do we need value descriptor? OID?
	  DB_LOGICAL eval_ret = node.m_eval_function (thread_p, node.m_nested_node.m_predicate_expression, NULL, NULL);
	  if (eval_ret == V_ERROR)
	    {
	      ASSERT_ERROR_AND_SET (error_code);
	      return error_code;
	    }
	  else if (eval_ret == V_FALSE)
	    {
	      // row doesn't satisfy predicate
	      return NO_ERROR;
	    }
	  else if (eval_ret == V_UNKNOWN)
	    {
	      // todo: what what?
	    }
	}

      if (!node.m_nested_node.m_output_columns.empty ())
	{
	  for (auto col : node.m_nested_node.m_output_columns)
	    {
	      error_code = col.evaluate (value);
	      if (error_code != NO_ERROR)
		{
		  ASSERT_ERROR ();
		  return error_code;
		}
	    }
	}

      if (node.m_nested_node.m_nested_nodes.empty ())
	{
	  // this is leaf level. row can be dumped to list file
	  //qfile_
	}

      // todo...
      return NO_ERROR;
    }

    int
    scanner::process_doc (cubthread::entry *thread_p, const JSON_DOC &value, scan_node &node)
    {
      // todo: here we should expand an array into multiple rows
      if (db_json_get_type (&value) == DB_JSON_ARRAY && true)   // replace true
	{
	  // get each element and call process_row
	  return NO_ERROR;
	}
      else
	{
	  // todo: do we expect only objects here?
	  return process_row (thread_p, value, node);
	}
    }

    scanner::scan_node::scan_node (cubxasl::json_table::nested_node &nested_node)
      : m_nested_node (nested_node)
      , m_eval_function (NULL)
    {
      if (nested_node.m_predicate_expression != NULL)
	{
	  assert (!nested_node.m_predicate_columns.empty ());

	  DB_TYPE dummy_type;   // todo: single_node_type; do we need it?
	  m_eval_function = eval_fnc (NULL, nested_node.m_predicate_expression, &dummy_type);
	}
      else
	{
	  assert (nested_node.m_predicate_columns.empty ());
	}
    }
  } // namespace json_table
} // namespace cubscan
