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
// access_json_table.cpp - implementation of structures required to access json table spec type.
//

#include "access_json_table.hpp"

#include "db_json.hpp"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "object_primitive.h"

#include <cassert>

namespace cubxasl
{
  namespace json_table
  {

    int
    column::trigger_on_error (int error_code, db_value &value_out)
    {
      (void) pr_clear_value (&value_out);
      (void) db_make_null (&value_out);

      switch (m_on_error.m_behavior)
	{
	case JSON_TABLE_RETURN_NULL:
	  er_clear ();
	  return NO_ERROR;

	case JSON_TABLE_THROW_ERROR:
	  // propagate error
	  ASSERT_ERROR ();
	  return error_code;

	case JSON_TABLE_DEFAULT_VALUE:
	  assert (m_on_error.m_default_value != NULL);
	  er_clear ();
	  if (pr_clone_value (m_on_error.m_default_value, &value_out) != NO_ERROR)
	    {
	      assert (false);
	    }
	  return NO_ERROR;

	default:
	  assert (false);
	  return error_code;
	}
    }

    int
    column::trigger_on_empty (db_value &value_out)
    {
      (void) db_make_null (&value_out);

      switch (m_on_empty.m_behavior)
	{
	case JSON_TABLE_RETURN_NULL:
	  return NO_ERROR;

	case JSON_TABLE_THROW_ERROR:
	  // todo: set a proper error
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_FAILED;

	case JSON_TABLE_DEFAULT_VALUE:
	  assert (m_on_empty.m_default_value != NULL);
	  if (pr_clone_value (m_on_empty.m_default_value, &value_out) != NO_ERROR)
	    {
	      assert (false);
	    }
	  return NO_ERROR;

	default:
	  assert (false);
	  return ER_FAILED;
	}
    }

    column::column (void)
      : m_domain (NULL)
      , m_path ()
      , m_on_error ()
      , m_on_empty ()
      , m_output_value_pointer (NULL)
      , m_function (json_table_column_function::JSON_TABLE_EXTRACT)
    {
      //
    }

    int
    column::evaluate_extract (const JSON_DOC &input)
    {
      int error_code = NO_ERROR;
      JSON_DOC *docp = NULL;
      TP_DOMAIN_STATUS status_cast = TP_DOMAIN_STATUS::DOMAIN_COMPATIBLE;

      error_code = db_json_extract_document_from_path (&input, m_path.c_str(), docp);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR();
	  assert (db_value_is_null (m_output_value_pointer));
	  return ER_FAILED;
	}

      if (docp == NULL)
	{
	  error_code = trigger_on_empty (*m_output_value_pointer);
	  return error_code;
	}

      if (db_make_json (m_output_value_pointer, docp, true) != NO_ERROR)
	{
	  assert (false);
	  return ER_FAILED;
	}

      status_cast = tp_value_cast (m_output_value_pointer, m_output_value_pointer, m_domain, false);
      if (status_cast != TP_DOMAIN_STATUS::DOMAIN_COMPATIBLE)
	{
	  // todo - set error
	  error_code = trigger_on_error (error_code, *m_output_value_pointer);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR();
	      return error_code;
	    }
	}

      return error_code;
    }

    int
    column::evaluate_exists (const JSON_DOC &input)
    {
      int error_code = NO_ERROR;
      bool result = false;
      TP_DOMAIN_STATUS status_cast = TP_DOMAIN_STATUS::DOMAIN_COMPATIBLE;

      error_code = db_json_contains_path (&input, m_path.c_str(), result);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR();
	  assert (db_value_is_null (m_output_value_pointer));
	  return ER_FAILED;
	}

      // the result is an integer type (maybe use short)
      db_make_int (m_output_value_pointer, result ? 1 : 0);

      status_cast = tp_value_cast (m_output_value_pointer, m_output_value_pointer, m_domain, false);

      if (status_cast != TP_DOMAIN_STATUS::DOMAIN_COMPATIBLE)
	{
	  return ER_FAILED;
	}

      return error_code;
    }

    int
    column::evaluate_ordinality (size_t ordinality)
    {
      TP_DOMAIN_STATUS status_cast = TP_DOMAIN_STATUS::DOMAIN_COMPATIBLE;

      db_make_int (m_output_value_pointer, ordinality);

      status_cast = tp_value_cast (m_output_value_pointer, m_output_value_pointer, m_domain, false);

      if (status_cast != TP_DOMAIN_STATUS::DOMAIN_COMPATIBLE)
	{
	  return ER_FAILED;
	}

      return NO_ERROR;
    }

    int
    column::evaluate (const JSON_DOC &input, size_t ordinality)
    {
      // todo: should match MySQL behavior

      assert (m_output_value_pointer != NULL);

      pr_clear_value (m_output_value_pointer);
      db_make_null (m_output_value_pointer);

      int error_code = NO_ERROR;

      switch (m_function)
	{
	case json_table_column_function::JSON_TABLE_EXTRACT:
	  error_code = evaluate_extract (input);
	  break;
	case json_table_column_function::JSON_TABLE_EXISTS:
	  error_code = evaluate_exists (input);
	  break;
	case json_table_column_function::JSON_TABLE_ORDINALITY:
	  error_code = evaluate_ordinality (ordinality);
	  break;
	default:
	  return ER_FAILED;
	}

      return NO_ERROR;
    }

    node::node (void)
      : m_ordinality (1)
      , m_predicate_expression (NULL)
      , m_id (0)
    {
      //
    }

    void
    node::clear_columns()
    {
      for (auto &column : m_predicate_columns)
	{
	  (void)pr_clear_value (column.m_output_value_pointer);
	  (void)db_make_null (column.m_output_value_pointer);
	}

      for (auto &column : m_output_columns)
	{
	  (void)pr_clear_value (column.m_output_value_pointer);
	  (void)db_make_null (column.m_output_value_pointer);
	}

      for (node &child : m_nested_nodes)
	{
	  child.clear_columns();
	}
    }

  } // namespace json_table
} // namespace cubxasl
