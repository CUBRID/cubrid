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
// access_json_table.cpp - implementation of structures required to access json table spec type.
//

#include "access_json_table.hpp"

#include "db_json.hpp"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "memory_private_allocator.hpp"
#include "memory_reference_store.hpp"
#include "object_primitive.h"

#include <cassert>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubxasl
{
  namespace json_table
  {

    int
    column::trigger_on_error (const JSON_DOC &input, const TP_DOMAIN_STATUS &status_cast, db_value &value_out)
    {
      (void) pr_clear_value (&value_out);
      (void) db_make_null (&value_out);

      switch (m_on_error.m_behavior)
	{
	case JSON_TABLE_RETURN_NULL:
	  er_clear ();
	  return NO_ERROR;

	case JSON_TABLE_THROW_ERROR:
	{
	  cubmem::private_unique_ptr<char> unique_ptr_json_body (db_json_get_raw_json_body_from_document (&input), NULL);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_TABLE_ON_ERROR_INCOMP_DOMAIN, 4,
		  unique_ptr_json_body.get (), m_path, m_column_name,
		  pr_type_name (TP_DOMAIN_TYPE (m_domain)));

	  return ER_JSON_TABLE_ON_ERROR_INCOMP_DOMAIN;
	}

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
	  return ER_FAILED;
	}
    }

    int
    column::trigger_on_empty (db_value &value_out)
    {
      (void) pr_clear_value (&value_out);
      (void) db_make_null (&value_out);

      switch (m_on_empty.m_behavior)
	{
	case JSON_TABLE_RETURN_NULL:
	  return NO_ERROR;

	case JSON_TABLE_THROW_ERROR:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_TABLE_ON_EMPTY_ERROR, 1, m_column_name);
	  return ER_JSON_TABLE_ON_EMPTY_ERROR;

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
    {
      init ();
    }

    void
    column::init ()
    {
      m_domain = NULL;
      m_path = NULL;
      m_column_name = NULL;
      m_output_value_pointer = NULL;
      m_function = json_table_column_function::JSON_TABLE_EXTRACT;
      m_on_error.m_default_value = NULL;
      m_on_error.m_behavior = json_table_column_behavior_type::JSON_TABLE_RETURN_NULL;
      m_on_empty.m_default_value = NULL;
      m_on_empty.m_behavior = json_table_column_behavior_type::JSON_TABLE_RETURN_NULL;
    }

    int
    column::evaluate_extract (const JSON_DOC &input)
    {
      int error_code = NO_ERROR;
      JSON_DOC_STORE docp;
      TP_DOMAIN_STATUS status_cast = TP_DOMAIN_STATUS::DOMAIN_COMPATIBLE;

      error_code = db_json_extract_document_from_path (&input, m_path, docp);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  assert (db_value_is_null (m_output_value_pointer));
	  return ER_FAILED;
	}

      if (docp.is_null ())
	{
	  error_code = trigger_on_empty (*m_output_value_pointer);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	    }
	  return error_code;
	}

      // clear previous output_value
      pr_clear_value (m_output_value_pointer);
      db_make_json_from_doc_store_and_release (*m_output_value_pointer, docp);

      status_cast = tp_value_cast (m_output_value_pointer, m_output_value_pointer, m_domain, false);
      if (status_cast != TP_DOMAIN_STATUS::DOMAIN_COMPATIBLE)
	{
	  error_code = trigger_on_error (input, status_cast, *m_output_value_pointer);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
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

      error_code = db_json_contains_path (&input, std::vector<std::string> (1, m_path), false, result);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  assert (db_value_is_null (m_output_value_pointer));
	  return ER_FAILED;
	}

      db_make_short (m_output_value_pointer, result ? 1 : 0);

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
      assert (m_domain->type->id == DB_TYPE_INTEGER);

      db_make_int (m_output_value_pointer, (int) ordinality);

      return NO_ERROR;
    }

    int
    column::evaluate (const JSON_DOC &input, size_t ordinality)
    {
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

      return error_code;
    }

    void
    column::clear_xasl (bool is_final_clear /* = true */)
    {
      if (is_final_clear)
	{
	  (void) pr_clear_value (m_on_empty.m_default_value);
	  (void) pr_clear_value (m_on_error.m_default_value);
	}

      if (m_output_value_pointer != NULL)
	{
	  (void) pr_clear_value (m_output_value_pointer);
	  (void) db_make_null (m_output_value_pointer);
	}
    }

    node::node (void)
    {
      init ();
    }

    void
    node::init ()
    {
      m_path = NULL;
      init_ordinality ();
      m_output_columns = NULL;
      m_output_columns_size = 0;
      m_nested_nodes = NULL;
      m_nested_nodes_size = 0;
      m_id = 0;
      m_iterator = NULL;
      m_is_iterable_node = false;
    }

    void
    node::clear_columns (bool is_final_clear)
    {
      init_ordinality ();
      for (size_t i = 0; i < m_output_columns_size; ++i)
	{
	  m_output_columns[i].clear_xasl (is_final_clear);
	}
    }

    void
    node::clear_iterators (bool is_final_clear)
    {
      if (is_final_clear)
	{
	  db_json_delete_json_iterator (m_iterator);
	}
      else
	{
	  db_json_clear_json_iterator (m_iterator);
	}

      for (size_t i = 0; i < m_nested_nodes_size; ++i)
	{
	  m_nested_nodes[i].clear_iterators (is_final_clear);
	}
    }

    void
    node::clear_xasl (bool is_final_clear /* = true */)
    {
      clear_columns (is_final_clear);

      for (size_t i = 0; i < m_nested_nodes_size; ++i)
	{
	  m_nested_nodes[i].clear_xasl (is_final_clear);
	}
    }

    void
    node::init_iterator ()
    {
      if (m_is_iterable_node)
	{
	  m_iterator = db_json_create_iterator (DB_JSON_TYPE::DB_JSON_ARRAY);
	}
    }

    void
    node::init_ordinality()
    {
      m_ordinality = 1;
    }

    spec_node::spec_node ()
    {
      init ();
    }

    void
    spec_node::init ()
    {
      m_root_node = NULL;
      m_json_reguvar = NULL;
      m_node_count = 0;
    }

    void
    spec_node::clear_xasl (bool is_final_clear /* = true */)
    {
      // todo reguvar
      m_root_node->clear_xasl (is_final_clear);
    }

  } // namespace json_table
} // namespace cubxasl
