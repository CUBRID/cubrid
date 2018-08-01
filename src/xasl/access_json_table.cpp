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

#include "arithmetic.h"
#include "dbtype.h"
#include "error_code.h"
#include "error_manager.h"
#include "object_primitive.h"

#include <cassert>

json_table_column_on_error::json_table_column_on_error (void)
  : m_behavior (json_table_column_behavior::RETURN_NULL)
  , m_default_value (NULL)
{

}

int
json_table_column_on_error::trigger (int error_code, db_value &value_out)
{
  assert (error_code != NO_ERROR);

  (void) db_make_null (&value_out);

  switch (m_behavior)
    {
    case json_table_column_behavior::RETURN_NULL:
      er_clear ();
      return NO_ERROR;

    case json_table_column_behavior::THROW_ERROR:
      // propagate error
      ASSERT_ERROR ();
      return error_code;

    case json_table_column_behavior::DEFAULT_VALUE:
      assert (m_default_value != NULL);
      er_clear ();
      if (pr_clone_value (m_default_value, &value_out) != NO_ERROR)
	{
	  assert (false);
	}
      return NO_ERROR;

    default:
      assert (false);
      return error_code;
    }
}

json_table_column_on_empty::json_table_column_on_empty (void)
  : m_behavior (json_table_column_behavior::RETURN_NULL)
  , m_default_value (NULL)
{
  //
}

int
json_table_column_on_empty::trigger (db_value &value_out)
{
  (void) db_make_null (&value_out);

  switch (m_behavior)
    {
    case json_table_column_behavior::RETURN_NULL:
      return NO_ERROR;

    case json_table_column_behavior::THROW_ERROR:
      // todo: set a proper error
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;

    case json_table_column_behavior::DEFAULT_VALUE:
      assert (m_default_value != NULL);
      if (pr_clone_value (m_default_value, &value_out) != NO_ERROR)
	{
	  assert (false);
	}
      return NO_ERROR;

    default:
      assert (false);
      return ER_FAILED;
    }
}

json_table_column::json_table_column (void)
  : m_domain (NULL)
  , m_path ()
  , m_on_error ()
  , m_on_empty ()
  , m_output_value_pointer (NULL)
  , m_function (EXTRACT)
{
  //
}

int
json_table_column::evaluate (const db_value &input, db_value &output)
{
  if (db_value_is_null (&input))
    {
      // do we consider this empty case??
      return m_on_empty.trigger (output);
    }

  int error_code = NO_ERROR;

  if (m_function == EXTRACT)
    {
      DB_VALUE temp_path_value;
      db_make_string (&temp_path_value, const_cast<char *> (m_path.c_str ()));
      error_code = db_json_extract_dbval (&input, &temp_path_value, &output);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  // fall through
	}
    }
  else
    {
      // what about exists??
      // todo
    }

  if (error_code != NO_ERROR)
    {
      error_code = m_on_error.trigger (error_code, output);
    }
  else if (db_value_is_null (&output))
    {
      error_code = m_on_empty.trigger (output);
    }

  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  error_code = tp_value_cast (&output, &output, m_domain, false);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  return NO_ERROR;
}
