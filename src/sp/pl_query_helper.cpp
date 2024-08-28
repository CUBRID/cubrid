/*
 *
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

#include "parser.h"

#include "authenticate.h"
#include "dbi.h"
#include "dbtype_function.h"
#include "jsp_cl.h"

#include "pl_signature.hpp"



int
pl_make_sp_signature_arg (DB_SET *param_set, cubpl::pl_arg &res)
{
  DB_VALUE value;
  db_make_null (&value);

  for (int i = 0; i < param_cnt; i++)
    {
      set_get_element (param_set, i, &value);
      DB_OBJECT *arg_mop_p = db_get_object (&value);
      if (arg_mop_p)
	{
	  if (db_get (arg_mop_p, SP_ATTR_MODE, &value) != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  arg_mode = db_get_int (&value);

	  if (db_get (arg_mop_p, SP_ATTR_DATA_TYPE, &value) != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  arg_type = db_get_int (&value);

	  if (db_get (arg_mop_p, SP_ATTR_IS_OPTIONAL, &value) != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  int is_optional = db_get_int (&value);
	  if (is_optional == 1)
	    {
	      if (db_get (arg_mop_p, SP_ATTR_DEFAULT_VALUE, &value) != NO_ERROR)
		{
		  goto error_exit;
		}

	      if (!DB_IS_NULL (&value))
		{
		  res.arg_default_value_size[i] =
			  db_get_string_size (&value);
		  if (res.arg_default_value_size[i] > 0)
		    {
		      res.arg_default_value[i] =
			      db_private_strndup (NULL, db_get_string (&value),
						  res.arg_default_value_size[i]);
		    }
		}
	      else
		{
		  // default value is NULL
		  res.arg_default_value_size[i] = -2;	// special value
		}
	    }
	}
    }

  db_value_clear (&value);
  return NO_ERROR;

error_exit:
  db_value_clear (&value);

  if (error == NO_ERROR)
    {
      error = ER_FAILED;
    }

  return error;
}

int
pl_make_sp_signature (MOP mop_p, cubpl::pl_signature &res)
{
  int error = NO_ERROR;

  DB_VALUE value;
  db_make_null (&value);

  // target
  if (db_get (mop_p, SP_ATTR_TARGET, &value) != NO_ERROR)
    {
      goto error_exit;
    }
  res.name = db_private_strndup (db_get_string (&value), db_get_string_size (&value));
  if (res.name)
    {
      std::string target_str (res.name);
      std::string cls_name = jsp_get_class_name_of_target (target_str);
      code_mop = jsp_find_stored_procedure_code (cls_name.c_str ());
    }

  db_value_clear (&value);

  // result type
  if (db_get (mop_p, SP_ATTR_RETURN_TYPE, &value) != NO_ERROR)
    {
      goto error_exit;
    }
  res.result_type = db_get_int (&value);
  if (res.result_type == DB_TYPE_RESULTSET && !jsp_is_prepare_call ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_RETURN_RESULTSET, 0);
      goto error_exit;
    }

  // lang
  if (db_get (mop_p, SP_ATTR_LANG, &value) != NO_ERROR)
    {
      goto error_exit;
    }
  res.pl_type = (db_get_int (&lang) == SP_LANG_PLCSQL) ? METHOD_TYPE_PLCSQL : METHOD_TYPE_JAVA_SP;

  // arg count
  if (db_get (mop_p, SP_ATTR_ARG_COUNT, &value) != NO_ERROR)
    {
      goto error_exit;
    }

  int arg_cnt = db_get_int (&value);
  if (arg_cnt > 0)
    {
      int node_arg_cnt = pt_length_of_list (node->info.method_call.arg_list);
      if (node_arg_cnt > arg_cnt)
	{
	  error = ER_SP_INVALID_PARAM_COUNT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, arg_cnt, node_arg_cnt);
	  goto error_exit;
	}

      if (db_get (mop_p, SP_ATTR_ARGS, &value) != NO_ERROR)
	{
	  goto error_exit;
	}

      res.arg = new cubpl::pl_arg;

      if (pl_make_sp_signature_arg (arg_mop_p, res.arg[i]) != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  db_value_clear (&value);

  return NO_ERROR;

error_exit:
  db_value_clear (&value);

  if (error == NO_ERROR)
    {
      error = ER_FAILED;
    }

  return error;
}

cubpl::pl_signature *
pl_make_signature (PARSER_CONTEXT *parser, PT_NODE *node)
{
  int error = NO_ERROR;
  int save;

  cubpl::pl_signature *res = NULL;

  // common attributes
  const char *name = PT_NAME_ORIGINAL (PT_METHOD_CALL_NAME (node));
  const char *auth = PT_METHOD_CALL_AUTH_NAME (node);

  int num_args = pt_length_of_list (PT_METHOD_ARG_LIST (node));

  if (PT_IS_METHOD (node))
    {
      PT_NODE *dt = node->info.method_call.on_call_target->data_type;
      /* beware of virtual classes */
      if (dt->info.data_type.virt_object)
	{
	  (*tail)->class_name = (char *) db_get_class_name (dt->info.data_type.virt_object);
	}
      else
	{
	  (*tail)->class_name = (char *) dt->info.data_type.entity->info.name.original;
	}
    }
  else if (PT_IS_JAVA_SP (node))
    {
      res = new cubpl::pl_signature;

      error = pl_make_sp_signature (res);
      if (error != NO_ERROR)
	{

	}
    }
  else
    {
      assert (false);
      return ER_FAILED;
    }

  AU_DISABLE (save);
  DB_OBJECT *mop_p = jsp_find_stored_procedure (name);
  if (mop_p == NULL)
    {
      goto end;
    }

  if ((error = db_get (mop_p, SP_ATTR_TARGET, &method)) != NO_ERROR
      || (error = db_get (mop_p, SP_ATTR_ARG_COUNT, &param_cnt_val)) != NO_ERROR
      || (error = db_get (mop_p, SP_ATTR_RETURN_TYPE, &result_type)) != NO_ERROR
      || (error = db_get (mop_p, SP_ATTR_LANG, &lang_val)) != NO_ERROR
      || (error = db_get (mop_p, SP_ATTR_DIRECTIVE, &directive_val)) != NO_ERROR
      || (error = db_get (mop_p, SP_ATTR_ARGS, &args_val)) != NO_ERROR)
    {
      goto end;
    }

  {







  }

end:
  AU_ENABLE (save);

  return error;
}