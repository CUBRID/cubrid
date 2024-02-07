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

/*
 * sp_catalog.cpp - Implement stored procedure related system catalog's row sturcture and initializer
*/

#include "sp_catalog.hpp"

#include <vector>

#include "jsp_cl.h"
#include "authenticate.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "db.h"
#include "object_accessor.h"
#include "set_object.h"
#include "locator_cl.h"
#include "transaction_cl.h"
#include "schema_manager.h"
#include "dbtype.h"

// memory representation of built-in stored procedures
static std::vector <sp_info> sp_builtin_definition;

static int sp_add_stored_procedure_internal (SP_INFO &info, bool has_savepoint);
static int sp_builtin_init ();

// TODO
static int sp_builtin_init ()
{
  if (sp_builtin_definition.size () > 0)
    {
      // already initialized
      return 0;
    }

  sp_info v;
  sp_arg_info a;

  // common
  v.is_system_generated = true;
  v.lang = SP_LANG_PLCSQL;
  v.owner = Au_public_user;
  v.comment = "";
  v.directive = SP_DIRECTIVE_RIGHTS_OWNER;

  a.is_system_generated = true;

  // DBMS_OUTPUT.enable
  v.sp_name = "enable";
  v.pkg_name = "DBMS_OUTPUT";
  v.sp_type = SP_TYPE_PROCEDURE;
  v.return_type = DB_TYPE_NULL;
  v.target = "com.cubrid.plcsql.builtin.DBMS_OUTPUT.enable(int)";

  // arg(0) of enable
  a.sp_name = v.sp_name;
  a.pkg_name = "DBMS_OUTPUT";
  a.index_of = 0;
  a.arg_name = "s";
  a.data_type = DB_TYPE_INTEGER;
  a.mode = SP_MODE_IN;
  a.comment  = "";

  v.args.push_back (a);

  //
  sp_builtin_definition.push_back (v);
  v.args.clear ();
  //

  // DBMS_OUTPUT.disable
  v.sp_name = "disable";
  v.pkg_name = "DBMS_OUTPUT";
  v.sp_type = SP_TYPE_PROCEDURE;
  v.return_type = DB_TYPE_NULL;
  v.target = "com.cubrid.plcsql.builtin.DBMS_OUTPUT.disable()";

  //
  sp_builtin_definition.push_back (v);
  v.args.clear ();
  //

  // DBMS_OUTPUT.put
  v.sp_name = "put";
  v.pkg_name = "DBMS_OUTPUT";
  v.sp_type = SP_TYPE_PROCEDURE;
  v.return_type = DB_TYPE_NULL;
  v.target = "com.cubrid.plcsql.builtin.DBMS_OUTPUT.put(java.lang.String)";

  // arg(0) of put
  a.sp_name = v.sp_name;
  a.pkg_name = "DBMS_OUTPUT";
  a.index_of = 0;
  a.arg_name = "str";
  a.data_type = DB_TYPE_STRING;
  a.mode = SP_MODE_IN;
  a.comment  = "";

  v.args.push_back (a);

  //
  sp_builtin_definition.push_back (v);
  v.args.clear ();
  //

  // DBMS_OUTPUT.put_line
  v.sp_name = "put_line";
  v.pkg_name = "DBMS_OUTPUT";
  v.sp_type = SP_TYPE_PROCEDURE;
  v.return_type = DB_TYPE_NULL;
  v.target = "com.cubrid.plcsql.builtin.DBMS_OUTPUT.putLine(java.lang.String)";

  // arg(0) of put_line
  a.sp_name = v.sp_name;
  a.pkg_name = "DBMS_OUTPUT";
  a.index_of = 0;
  a.arg_name = "str";
  a.data_type = DB_TYPE_STRING;
  a.mode = SP_MODE_IN;
  a.comment  = "";

  v.args.push_back (a);

  //
  sp_builtin_definition.push_back (v);
  v.args.clear ();
  //

  // DBMS_OUTPUT.new_line
  v.sp_name = "new_line";
  v.pkg_name = "DBMS_OUTPUT";
  v.sp_type = SP_TYPE_PROCEDURE;
  v.return_type = DB_TYPE_NULL;
  v.target = "com.cubrid.plcsql.builtin.DBMS_OUTPUT.newLine()";

  //
  sp_builtin_definition.push_back (v);
  v.args.clear ();
  //

  // DBMS_OUTPUT.get_line
  v.sp_name = "get_line";
  v.pkg_name = "DBMS_OUTPUT";
  v.sp_type = SP_TYPE_PROCEDURE;
  v.return_type = DB_TYPE_NULL;
  v.target = "com.cubrid.plcsql.builtin.DBMS_OUTPUT.getLine(java.lang.String[], int[])";

  // arg(0) of get_line
  a.sp_name = v.sp_name;
  a.pkg_name = "DBMS_OUTPUT";
  a.index_of = 0;
  a.arg_name = "line";
  a.data_type = DB_TYPE_STRING;
  a.mode = SP_MODE_OUT;
  a.comment  = "";

  v.args.push_back (a);

  // arg(1) of get_line
  a.sp_name = v.sp_name;
  a.pkg_name = "DBMS_OUTPUT";
  a.index_of = 1;
  a.arg_name = "status";
  a.data_type = DB_TYPE_INTEGER;
  a.mode = SP_MODE_OUT;
  a.comment  = "";

  v.args.push_back (a);

  //
  sp_builtin_definition.push_back (v);
  v.args.clear ();
  //

  // DBMS_OUTPUT.get_lines
  v.sp_name = "get_lines";
  v.pkg_name = "DBMS_OUTPUT";
  v.sp_type = SP_TYPE_PROCEDURE;
  v.return_type = DB_TYPE_NULL;
  v.target = "com.cubrid.plcsql.builtin.DBMS_OUTPUT.getLines(java.lang.String[], int[])";

  // arg(0) of get_lines
  a.sp_name = v.sp_name;
  a.pkg_name = "DBMS_OUTPUT";
  a.index_of = 0;
  a.arg_name = "lines";
  a.data_type = DB_TYPE_STRING;
  a.mode = SP_MODE_OUT;
  a.comment  = "";

  v.args.push_back (a);

  // arg(1) of get_line
  a.sp_name = v.sp_name;
  a.pkg_name = "DBMS_OUTPUT";
  a.index_of = 1;
  a.arg_name = "cnt";
  a.data_type = DB_TYPE_INTEGER;
  a.mode = SP_MODE_OUT;
  a.comment  = "";

  v.args.push_back (a);

  //
  sp_builtin_definition.push_back (v);
  v.args.clear ();
  //

  return sp_builtin_definition.size ();
}

int sp_builtin_install ()
{
  (void) sp_builtin_init ();

  int error = NO_ERROR;
  for (sp_info &info : sp_builtin_definition)
    {
      error = sp_add_stored_procedure_internal (info, false);
      assert (error == NO_ERROR);
    }
  return error;
}

int
sp_check_param_type_supported (DB_TYPE domain_type, SP_MODE_ENUM mode)
{
  switch (domain_type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_STRING:
    case DB_TYPE_OBJECT:
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_DATE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_SHORT:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_CHAR:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DATETIME:
      return NO_ERROR;
      break;

    case DB_TYPE_RESULTSET:
      if (mode != SP_MODE_OUT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_INPUT_RESULTSET, 0);
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_SUPPORTED_ARG_TYPE, 1, pr_type_name (domain_type));
      break;
    }

  return er_errid ();
}

int
sp_add_stored_procedure (SP_INFO &info)
{
  return sp_add_stored_procedure_internal (info, true);
}

static int
sp_add_stored_procedure_internal (SP_INFO &info, bool has_savepoint)
{
  DB_OBJECT *classobj_p, *object_p;
  DB_OTMPL *obt_p = NULL;
  DB_VALUE value;
  DB_SET *param = NULL;
  int save, err;

  AU_DISABLE (save);

  {
    classobj_p = db_find_class (SP_CLASS_NAME);
    if (classobj_p == NULL)
      {
	assert (er_errid () != NO_ERROR);
	err = er_errid ();
	goto error;
      }

    if (has_savepoint)
      {
	err = tran_system_savepoint (SAVEPOINT_ADD_STORED_PROC);
	if (err != NO_ERROR)
	  {
	    has_savepoint = false;
	    goto error;
	  }
      }

    obt_p = dbt_create_object_internal (classobj_p);
    if (!obt_p)
      {
	assert (er_errid () != NO_ERROR);
	err = er_errid ();
	goto error;
      }

    sp_normalize_name (info.sp_name);
    db_make_string (&value, info.sp_name.data ());
    err = dbt_put_internal (obt_p, SP_ATTR_NAME, &value);
    pr_clear_value (&value);

    if (err != NO_ERROR)
      {
	goto error;
      }

    db_make_int (&value, info.sp_type);
    err = dbt_put_internal (obt_p, SP_ATTR_SP_TYPE, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto error;
      }

    if (jsp_check_return_type_supported (info.return_type) != NO_ERROR)
      {
	err = er_errid ();
	goto error;
      }

    db_make_int (&value, (int) info.return_type);
    err = dbt_put_internal (obt_p, SP_ATTR_RETURN_TYPE, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto error;
      }

    if (!info.pkg_name.empty ())
      {
	sp_normalize_name (info.pkg_name);
	db_make_string (&value, info.pkg_name.data ());
      }
    err = dbt_put_internal (obt_p, SP_ATTR_PKG, &value);
    pr_clear_value (&value);

    if (err != NO_ERROR)
      {
	goto error;
      }

    db_make_int (&value, info.is_system_generated ? 1 : 0);
    err = dbt_put_internal (obt_p, SP_ATTR_IS_SYSTEM_GENERATED, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto error;
      }

    db_make_int (&value, info.directive);
    err = dbt_put_internal (obt_p, SP_ATTR_DIRECTIVE, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto error;
      }

    // args (_db_stored_procedure_args) begin
    param = set_create_sequence (0);
    if (param == NULL)
      {
	assert (er_errid () != NO_ERROR);
	err = er_errid ();
	goto error;
      }

    int i = 0;
    for (sp_arg_info &arg: info.args)
      {
	DB_VALUE v;
	MOP mop = NULL;

	if (sp_check_param_type_supported (arg.data_type, arg.mode) != NO_ERROR)
	  {
	    err = er_errid ();
	    goto error;
	  }

	arg.sp_name = info.sp_name;
	arg.pkg_name = info.pkg_name;

	err = sp_add_stored_procedure_argument (&mop, arg);
	if (err != NO_ERROR)
	  {
	    goto error;
	  }

	db_make_object (&v, mop);
	err = set_put_element (param, i++, &v);
	pr_clear_value (&v);

	if (err != NO_ERROR)
	  {
	    goto error;
	  }
      }
    db_make_sequence (&value, param);
    err = dbt_put_internal (obt_p, SP_ATTR_ARGS, &value);
    pr_clear_value (&value);
    param = NULL;
    if (err != NO_ERROR)
      {
	goto error;
      }
    // args (_db_stored_procedure_args) end

    db_make_int (&value, (int) info.args.size ());
    err = dbt_put_internal (obt_p, SP_ATTR_ARG_COUNT, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto error;
      }

    db_make_int (&value, info.lang);
    err = dbt_put_internal (obt_p, SP_ATTR_LANG, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto error;
      }

    db_make_string (&value, info.target.data ());
    err = dbt_put_internal (obt_p, SP_ATTR_TARGET, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto error;
      }

    db_make_object (&value, info.owner);
    err = dbt_put_internal (obt_p, SP_ATTR_OWNER, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto error;
      }

    if (!info.comment.empty ())
      {
	db_make_string (&value, info.comment.data ());
      }
    err = dbt_put_internal (obt_p, SP_ATTR_COMMENT, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto error;
      }

    object_p = dbt_finish_object (obt_p);
    if (!object_p)
      {
	assert (er_errid () != NO_ERROR);
	err = er_errid ();
	goto error;
      }
    obt_p = NULL;

    err = locator_flush_instance (object_p);
    if (err != NO_ERROR)
      {
	assert (er_errid () != NO_ERROR);
	err = er_errid ();
	obj_delete (object_p);
	goto error;
      }
  }

  AU_ENABLE (save);
  return NO_ERROR;

error:
  if (param)
    {
      set_free (param);
    }

  if (obt_p)
    {
      dbt_abort_object (obt_p);
    }

  if (has_savepoint)
    {
      tran_abort_upto_system_savepoint (SAVEPOINT_ADD_STORED_PROC);
    }

  AU_ENABLE (save);

  return err;
}

int
sp_add_stored_procedure_argument (MOP *mop_p, SP_ARG_INFO &info)
{
  DB_OBJECT *classobj_p, *object_p;
  DB_OTMPL *obt_p = NULL;
  DB_VALUE value;
  int save;
  int err;

  AU_DISABLE (save);

  classobj_p = db_find_class (SP_ARG_CLASS_NAME);
  if (classobj_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  obt_p = dbt_create_object_internal (classobj_p);
  if (obt_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  db_make_string (&value, info.sp_name.data ());
  err = dbt_put_internal (obt_p, SP_ATTR_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  if (!info.pkg_name.empty ())
    {
      db_make_string (&value, info.pkg_name.data ());
    }
  err = dbt_put_internal (obt_p, SP_ATTR_PKG, &value);
  pr_clear_value (&value);

  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_string (&value, info.arg_name.data ());
  err = dbt_put_internal (obt_p, SP_ATTR_ARG_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, info.index_of);
  err = dbt_put_internal (obt_p, SP_ATTR_INDEX_OF_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, info.is_system_generated ? 1 : 0);
  err = dbt_put_internal (obt_p, SP_ATTR_IS_SYSTEM_GENERATED, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, info.data_type);
  err = dbt_put_internal (obt_p, SP_ATTR_DATA_TYPE, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, info.mode);
  err = dbt_put_internal (obt_p, SP_ATTR_MODE, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  if (!info.comment.empty ())
    {
      db_make_string (&value, info.comment.data ());
    }
  err = dbt_put_internal (obt_p, SP_ATTR_ARG_COMMENT, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  object_p = dbt_finish_object (obt_p);
  if (!object_p)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }
  obt_p = NULL;

  err = locator_flush_instance (object_p);
  if (err != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      obj_delete (object_p);
      goto error;
    }

  *mop_p = object_p;

  AU_ENABLE (save);
  return NO_ERROR;

error:
  if (obt_p)
    {
      dbt_abort_object (obt_p);
    }

  AU_ENABLE (save);
  return err;
}

int
sp_add_stored_procedure_code (SP_CODE_INFO &info)
{
  DB_OBJECT *classobj_p, *object_p;
  DB_OTMPL *obt_p = NULL;
  DB_VALUE value;
  int save;
  int err;

  AU_DISABLE (save);

  classobj_p = db_find_class (SP_CODE_CLASS_NAME);
  if (classobj_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  obt_p = dbt_create_object_internal (classobj_p);
  if (obt_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  db_make_string (&value, info.sp_name.data ());
  err = dbt_put_internal (obt_p, SP_ATTR_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, info.stype);
  err = dbt_put_internal (obt_p, SP_ATTR_SOURCE_TYPE, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_string (&value, info.scode.data ());
  err = dbt_put_internal (obt_p, SP_ATTR_SOURCE_CODE, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }


  db_make_int (&value, info.otype);
  err = dbt_put_internal (obt_p, SP_ATTR_OBJECT_TYPE, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_string (&value, info.ocode.data ());
  err = dbt_put_internal (obt_p, SP_ATTR_OBJECT_CODE, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  object_p = dbt_finish_object (obt_p);
  if (!object_p)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }
  obt_p = NULL;

  err = locator_flush_instance (object_p);
  if (err != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      obj_delete (object_p);
      goto error;
    }

  AU_ENABLE (save);
  return NO_ERROR;

error:
  if (obt_p)
    {
      dbt_abort_object (obt_p);
    }

  AU_ENABLE (save);
  return err;
}

void sp_normalize_name (std::string &s)
{
  s.resize (SM_MAX_IDENTIFIER_LENGTH);
  sm_downcase_name (s.data (), s.data (), SM_MAX_IDENTIFIER_LENGTH);
}
