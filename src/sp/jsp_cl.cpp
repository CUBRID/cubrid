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

/*
 * jsp_cl.cpp - Java Stored Procedure Client Module Source
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#if !defined(WINDOWS)
#include <sys/socket.h>
#else /* not WINDOWS */
#include <winsock2.h>
#endif /* not WINDOWS */

#include <vector>
#include <functional>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "authenticate.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "dbtype.h"
#include "parser.h"
#include "parser_message.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "db.h"
#include "object_accessor.h"
#include "set_object.h"
#include "locator_cl.h"
#include "transaction_cl.h"
#include "schema_manager.h"
#include "numeric_opfunc.h"
#include "jsp_cl.h"
#include "system_parameter.h"
#include "network_interface_cl.h"
#include "unicode_support.h"
#include "dbtype.h"
#include "pl_comm.h"
#include "pl_struct_compile.hpp"
#include "sp_catalog.hpp"
#include "authenticate_access_auth.hpp"
#include "pl_signature.hpp"
#include "oid.h"
#include "string_buffer.hpp"
#include "db_value_printer.hpp"
#include "execute_statement.h"

#define PT_NODE_PKG_COMMENT(node) \
  (((node)->info.pkg.comment == NULL) ? "" : \
   (char *) (node)->info.pkg.comment->info.value.data_value.str->bytes)

#define PT_NODE_PKG_NAME(node) \
  (((node)->info.pkg.name == NULL) ? "" : \
   (node)->info.pkg.name->info.name.original)

#define PT_NODE_SP_NAME(node) \
  (((node)->info.sp.name == NULL) ? "" : \
   (node)->info.sp.name->info.name.original)

#define PT_NODE_SP_TYPE(node) \
  ((node)->info.sp.type)

#define PT_NODE_SP_RETURN_TYPE(node) \
  ((node)->info.sp.ret_type->info.name.original)

#define PT_NODE_SP_BODY(node) \
  ((node)->info.sp.body)

#define PT_NODE_SP_LANG(node) \
  ((node)->info.sp.body->info.sp_body.lang)

#define PT_NODE_SP_ARGS(node) \
  ((node)->info.sp.param_list)

#define PT_NODE_SP_IMPL(node) \
  ((node)->info.sp.body->info.sp_body.impl->info.value.data_value.str->bytes)

#define PT_NODE_SP_JAVA_METHOD(node) \
  ((node)->info.sp.body->info.sp_body.decl->info.value.data_value.str->bytes)

#define PT_NODE_SP_AUTHID(node) \
  ((node)->info.sp.auth_id)

#define PT_NODE_SP_DETERMINISTIC_TYPE(node) \
  ((node)->info.sp.dtrm_type)

#define PT_NODE_SP_COMMENT(node) \
  (((node)->info.sp.comment == NULL) ? "" : \
   (char *) (node)->info.sp.comment->info.value.data_value.str->bytes)

#define PT_NODE_SP_ARG_NAME(node) \
  (((node)->info.sp_param.name == NULL) ? "" : \
   (node)->info.sp_param.name->info.name.original)

#define PT_NODE_SP_ARG_COMMENT(node) \
  (((node)->info.sp_param.comment == NULL) ? "" : \
   (char *) (node)->info.sp_param.comment->info.value.data_value.str->bytes)

#define MAX_CALL_COUNT  16

#define MAX_ARG_COUNT 64

static int server_port = -1;
static int call_cnt = 0;
static bool is_prepare_call[MAX_CALL_COUNT] = { false, };

static SP_TYPE_ENUM jsp_map_pt_misc_to_sp_type (PT_MISC_TYPE pt_enum);
static SP_MODE_ENUM jsp_map_pt_misc_to_sp_mode (PT_MISC_TYPE pt_enum);
static PT_MISC_TYPE jsp_map_sp_type_to_pt_misc (SP_TYPE_ENUM sp_type);
static SP_DIRECTIVE_ENUM jsp_map_pt_to_sp_authid (PT_MISC_TYPE pt_authid);
static SP_DIRECTIVE_ENUM jsp_map_pt_to_sp_dtrm_type (PT_MISC_TYPE pt_dtrm_type, SP_DIRECTIVE_ENUM directive);

static char *jsp_check_stored_procedure_name (const char *str);
static int jsp_check_overflow_args (PARSER_CONTEXT *parser, PT_NODE *node, int num_params, int num_args);
static int jsp_check_out_param_in_query (PARSER_CONTEXT *parser, PT_NODE *node, int arg_mode);
static int jsp_check_param_type_supported  (DB_TYPE type, int mode);

static int jsp_drop_stored_procedure (const char *name, SP_TYPE_ENUM expected_type);
static int jsp_drop_stored_procedure_code (const char *name);

static int jsp_check_execute_authorization (const MOP sp_obj, const DB_AUTH au_type);

extern bool ssl_client;

static MOP
jsp_find_pkg (const char *unique_name, DB_AUTH purpose)
{
  MOP mop = NULL;
  DB_VALUE value;
  int save, err = NO_ERROR;

  if (!unique_name || unique_name[0] == '\0')
    {
      return NULL;
    }

  AU_DISABLE (save);

  db_make_string (&value, unique_name);
  mop = db_find_unique (db_find_class (CT_PACKAGE_NAME), PKG_ATTR_UNIQUE_NAME, &value);

  if (er_errid () == ER_OBJ_OBJECT_NOT_FOUND)
    {
      er_clear ();
      AU_ENABLE (save);
      return NULL;
    }

  if (mop)
    {
      err = jsp_check_execute_authorization (mop, purpose);
    }

  if (err != NO_ERROR)
    {
      mop = NULL;
    }

  AU_ENABLE (save);

  return mop;
}

/*
 * jsp_is_exist_stored_procedure
 *   return: name is exist then return true
 *                         else return false
 *   name(in): find java stored procedure name
 *
 * Note:
 */

int
jsp_is_exist_stored_procedure (const char *name)
{
  MOP mop = jsp_find_stored_procedure (name, DB_AUTH_NONE);
  er_clear ();
  return mop != NULL;
}

/*
 * jsp_find_stored_procedure
 *   return: MOP
 *   name(in): find java stored procedure name
 *   purpose(in): DB_AUTH_NONE or DB_AUTH_SELECT
 *
 * Note:
 */

MOP
jsp_find_stored_procedure (const char *name, DB_AUTH purpose)
{
  MOP mop = NULL;
  DB_VALUE value;
  int save, err = NO_ERROR;
  char *checked_name;

  if (!name)
    {
      return NULL;
    }

  AU_DISABLE (save);

  checked_name = jsp_check_stored_procedure_name (name);
  db_make_string (&value, checked_name);
  mop = db_find_unique (db_find_class (CT_STORED_PROC_NAME), SP_ATTR_UNIQUE_NAME, &value);

  if (er_errid () == ER_OBJ_OBJECT_NOT_FOUND)
    {
      er_clear ();

      /* This is the case when the loaddb utility is executed with the --no-user-specified-name option as the dba user. */
      if (db_client_type_is_loaddb_compat () /*latest compat client type */ )
	{
	  err = jsp_find_sp_of_another_owner (checked_name, &mop);
	}
      else
	{
	  err = ER_SP_NOT_EXIST;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, err, 1, checked_name);
	}
    }

  if (mop)
    {
      err = jsp_check_execute_authorization (mop, purpose);
    }

  if (err != NO_ERROR)
    {
      mop = NULL;
    }

  free_and_init (checked_name);
  AU_ENABLE (save);

  return mop;
}

/*
 * jsp_find_stored_procedure_code
 *   return: MOP
 *   name(in): find java stored procedure name
 *
 * Note:
 */

MOP
jsp_find_stored_procedure_code (const char *name)
{
  MOP mop = NULL;
  DB_VALUE value;
  int save;

  if (!name)
    {
      return NULL;
    }

  AU_DISABLE (save);

  db_make_string (&value, name);
  mop = db_find_unique (db_find_class (CT_STORED_PROC_CODE_NAME), SP_CODE_ATTR_NAME, &value);

  if (er_errid () == ER_OBJ_OBJECT_NOT_FOUND)
    {
      er_clear ();
    }

  AU_ENABLE (save);

  return mop;
}

/*
 * jsp_find_sp_of_another_owner
 *   return: if fail return error code
 *   name(in): find java stored procedure name
 *   return_mop(in): retrieves the name of a java stored procedure and returns its MOP value.
 *
 * Note: This is a function for finding the unique_name of an SP when running the loaddb utility with the --no-user-specified-name option as a dba user.
 */

int
jsp_find_sp_of_another_owner (const char *name, MOP *return_mop)
{
  int error = NO_ERROR;
  DB_VALUE value;
  char other_class_name[DB_MAX_IDENTIFIER_LENGTH];
  other_class_name[0] = '\0';
  *return_mop = NULL;

  error = do_find_stored_procedure_by_query (name, other_class_name, DB_MAX_IDENTIFIER_LENGTH);
  if (other_class_name[0] != '\0')
    {
      if (db_get_client_statement_type () == CUBRID_STMT_CREATE_STORED_PROCEDURE)
	{
	  /* maybe unloaded from version 11.4+ or later */
	  db_set_client_type (DB_CLIENT_TYPE_LOADDB_UTILITY);

	  error = ER_SP_NOT_EXIST;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, name);

	  return error;
	}

      db_make_string (&value, other_class_name);
      *return_mop = db_find_unique (db_find_class (CT_STORED_PROC_NAME), SP_ATTR_UNIQUE_NAME, &value);
      if (er_errid () == ER_OBJ_OBJECT_NOT_FOUND)
	{
	  error = ER_SP_NOT_EXIST;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1, other_class_name);
	}
    }

  return error;
}

/*
 * jsp_check_out_param_in_query
 *
 * Note:
 */

static int
jsp_check_out_param_in_query (PARSER_CONTEXT *parser, PT_NODE *node, int arg_mode)
{
  int error = NO_ERROR;

  assert ((node) && (node)->node_type == PT_METHOD_CALL);

  if (node->info.method_call.call_or_expr != PT_IS_CALL_STMT)
    {
      // check out parameters
      if (arg_mode != SP_MODE_IN)
	{
	  PT_ERRORmf (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_SP_OUT_ARGS_EXISTS_IN_QUERY,
		      node->info.method_call.method_name->info.name.original);
	  error = ER_PT_SEMANTIC;
	}
    }

  return error;
}

/*
 * jsp_check_param_type_supported
 *
 * Note:
 */

static int
jsp_check_param_type_supported (DB_TYPE type, int mode)
{
  switch (type)
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
      else if (!jsp_is_prepare_call ())
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_RETURN_RESULTSET, 0);
	}
      else
	{
	  return NO_ERROR;
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_SUPPORTED_ARG_TYPE, 1, pr_type_name (type));
      break;
    }

  return er_errid ();
}

/*
 * jsp_check_return_type_supported
 *
 * Note:
 */

int
jsp_check_return_type_supported (DB_TYPE type)
{
  switch (type)
    {
    case DB_TYPE_NULL:
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
    case DB_TYPE_RESULTSET:
      if (!jsp_is_prepare_call ())
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_RETURN_RESULTSET, 0);
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_SUPPORTED_RETURN_TYPE, 1, pr_type_name (type));
      break;
    }

  return er_errid ();
}

/*
 * jsp_get_return_type - Return Java Stored Procedure Type
 *   return: if fail return error code
 *           else return Java Stored Procedure Type
 *   name(in): java stored procedure name
 *
 * Note:
 */

int
jsp_get_return_type (const char *name)
{
  DB_OBJECT *mop_p;
  DB_VALUE return_type;
  int err;
  int save;

  AU_DISABLE (save);

  mop_p = jsp_find_stored_procedure (name, DB_AUTH_NONE);
  if (mop_p == NULL)
    {
      AU_ENABLE (save);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  err = db_get (mop_p, SP_ATTR_RETURN_TYPE, &return_type);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return err;
    }

  AU_ENABLE (save);
  return db_get_int (&return_type);
}

/*
 * jsp_get_sp_type - Return Java Stored Procedure Type
 *   return: if fail return error code
 *           else return Java Stored Procedure Type
 *   name(in): java stored procedure name
 *
 * Note:
 */

int
jsp_get_sp_type (const char *name)
{
  DB_OBJECT *mop_p;
  DB_VALUE sp_type_val;
  int err;
  int save;

  AU_DISABLE (save);

  mop_p = jsp_find_stored_procedure (name, DB_AUTH_NONE);
  if (mop_p == NULL)
    {
      AU_ENABLE (save);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* check type */
  err = db_get (mop_p, SP_ATTR_SP_TYPE, &sp_type_val);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return err;
    }

  AU_ENABLE (save);
  return jsp_map_sp_type_to_pt_misc ((SP_TYPE_ENUM) db_get_int (&sp_type_val));
}

MOP
jsp_get_owner (MOP mop_p)
{
  int save;
  DB_VALUE value;

  AU_DISABLE (save);

  /* check type */
  int err = db_get (mop_p, SP_ATTR_OWNER, &value);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return NULL;
    }

  MOP owner = db_get_object (&value);

  AU_ENABLE (save);
  return owner;
}

char *
jsp_get_unique_name (MOP mop_p, char *buf, int buf_size)
{
  int save;
  DB_VALUE value;
  int err = NO_ERROR;

  assert (buf != NULL);
  assert (buf_size > 0);

  if (mop_p == NULL)
    {
      ERROR_SET_WARNING (err, ER_SM_INVALID_ARGUMENTS);
      buf[0] = '\0';
      return NULL;
    }

  AU_DISABLE (save);

  /* check type */
  err = db_get (mop_p, SP_ATTR_UNIQUE_NAME, &value);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return NULL;
    }

  strncpy (buf, db_get_string (&value), buf_size);
  pr_clear_value (&value);

  AU_ENABLE (save);
  return buf;
}

/*
 * jsp_get_owner_name - Return Java Stored Procedure'S Owner nmae
 *   return: if fail return MULL
 *           else return Java Stored Procedure Type
 *   name(in): java stored procedure name
 *
 * Note:
 */

char *
jsp_get_owner_name (const char *name, char *buf, int buf_size)
{
  DB_OBJECT *mop_p;
  DB_VALUE value;
  int err;
  int save;

  assert (buf != NULL);
  assert (buf_size > 0);

  if (name == NULL || name[0] == '\0')
    {
      ERROR_SET_WARNING (err, ER_SM_INVALID_ARGUMENTS);
      buf[0] = '\0';
      return NULL;
    }

  AU_DISABLE (save);

  mop_p = jsp_find_stored_procedure (name, DB_AUTH_NONE);
  if (mop_p == NULL)
    {
      AU_ENABLE (save);

      assert (er_errid () != NO_ERROR);
      return NULL;
    }

  /* check type */
  err = db_get (mop_p, SP_ATTR_OWNER, &value);
  if (err != NO_ERROR)
    {
      AU_ENABLE (save);
      return NULL;
    }

  MOP owner = db_get_object (&value);
  if (owner != NULL)
    {
      DB_VALUE value2;
      err = db_get (owner, "name", &value2);
      if (err == NO_ERROR)
	{
	  strncpy (buf, db_get_string (&value2), buf_size);
	}
      pr_clear_value (&value2);
    }
  pr_clear_value (&value);

  AU_ENABLE (save);
  return buf;
}



static PT_MISC_TYPE
jsp_map_sp_type_to_pt_misc (SP_TYPE_ENUM sp_type)
{
  if (sp_type == SP_TYPE_PROCEDURE)
    {
      return PT_SP_PROCEDURE;
    }
  else
    {
      return PT_SP_FUNCTION;
    }
}

static int
jsp_evaluate_arguments (PARSER_CONTEXT *parser, PT_NODE *statement,
			std::vector <std::reference_wrapper <DB_VALUE>> &args)
{
  assert (statement);
  assert (statement->node_type == PT_METHOD_CALL);

  PT_NODE *vc = statement->info.method_call.arg_list;
  while (vc)
    {
      DB_VALUE *db_value;

      /*
       * Don't clone host vars; they may actually be acting as output variables (e.g., a character array that is
       * intended to receive bytes from the method), and cloning will ensure that the results never make it to the
       * expected area.  Since pt_evaluate_tree() always clones its db_values we must not use pt_evaluate_tree() to
       * extract the db_value from a host variable; instead extract it ourselves. */
      if (PT_IS_CONST (vc))
	{
	  db_value = pt_value_to_db (parser, vc);
	}
      else
	{
	  db_value = (DB_VALUE *) malloc (sizeof (DB_VALUE));
	  if (db_value == NULL)
	    {
	      goto exit_on_error;
	    }

	  db_make_null (db_value);

	  /* must call pt_evaluate_tree */
	  pt_evaluate_tree_having_serial (parser, vc, db_value, 1);
	  if (pt_has_error (parser))
	    {
	      /* to maintain the list to free all the allocated */
	      db_value_clear (db_value);
	      goto exit_on_error;
	    }
	}

      args.emplace_back (std::ref (*db_value));
      vc = vc->next;
    }

  return NO_ERROR;

exit_on_error:

  for (DB_VALUE &val : args)
    {
      db_value_clear (&val);
    }
  args.clear ();

  return ER_FAILED;
}

/*
 * jsp_call_stored_procedure - call java stored procedure in constant folding
 *   return: call jsp failed return error code
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

int
jsp_call_stored_procedure (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  int error = NO_ERROR;
  PT_NODE *method;
  const char *method_name;
  if (!statement || ! (method = statement->info.method_call.method_name) || method->node_type != PT_NAME)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return er_errid ();
    }

  DB_VALUE ret_value;
  db_make_null (&ret_value);

  /* call sp */
  std::vector <std::reference_wrapper <DB_VALUE>> args;
  cubpl::pl_signature sig;
  bool flag_si_datetime = false;
  error = jsp_make_pl_signature (parser, statement, NULL, sig);
  if (error == NO_ERROR)
    {
      PT_NODE *default_next_node_list = jsp_get_default_expr_node_list (parser, sig, &flag_si_datetime);
      if (default_next_node_list != NULL && flag_si_datetime)
	{
	  // consider using 'db_calculate_current_server_time' in db_vdb.c for less server communication
	  error = qp_get_server_info (parser, SI_SYS_DATETIME);
	}
      statement->info.method_call.arg_list = parser_append_node (default_next_node_list,
					     statement->info.method_call.arg_list);
      error = jsp_evaluate_arguments (parser, statement, args);
      if (pt_has_error (parser))
	{
	  pt_report_to_ersys (parser, PT_SEMANTIC);
	  error = er_errid ();
	}
    }

  if (error == NO_ERROR && locator_get_sig_interrupt () == 0)
    {
      std::vector <DB_VALUE> out_args;
      error = pl_call (sig, args, out_args, ret_value);
      if (error == NO_ERROR)
	{
	  for (int i = 0, j = 0; i < sig.arg.arg_size; i++)
	    {
	      if (sig.arg.arg_mode[i] == SP_MODE_IN)
		{
		  continue;
		}

	      DB_VALUE &arg = args[i];
	      DB_VALUE &out_arg = out_args[j++];

	      db_value_clear (&arg);
	      db_value_clone (&out_arg, &arg);
	      db_value_clear (&out_arg);
	    }
	}
    }

  PT_NODE *vc = statement->info.method_call.arg_list;
  for (int i = 0; i < (int) args.size () && vc; i++)
    {
      if (!PT_IS_CONST (vc))
	{
	  DB_VALUE &arg = args[i];
	  db_value_clear (&arg);
	  free (&arg);
	}
      vc = vc->next;
    }

  if (error == NO_ERROR)
    {
      /* Save the method result and its domain */
      statement->etc = (void *) db_value_copy (&ret_value);
      statement = pt_bind_type_from_dbval (parser, statement, &ret_value);

      PT_NODE *into = statement->info.method_call.to_return_var;

      const char *into_label;
      if (into != NULL && into->node_type == PT_NAME && (into_label = into->info.name.original) != NULL)
	{
	  /* create another DB_VALUE of the new instance for the label_table */
	  DB_VALUE *ins_value = db_value_copy (&ret_value);
	  error = pt_associate_label_with_value_check_reference (into_label, ins_value);
	}
    }

#if defined (CS_MODE)
  db_value_clear (&ret_value);
#endif

  return error;
}

/*
 * jsp_drop_stored_procedure - drop java stored procedure
 *   return: Error code
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

int
jsp_drop_stored_procedure (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  const char *name;
  PT_MISC_TYPE type;
  PT_NODE *name_list, *p;
  int i;
  int err = NO_ERROR;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BLOCK_DDL_STMT, 0);
      return ER_BLOCK_DDL_STMT;
    }

  name_list = statement->info.sp.name;
  type = PT_NODE_SP_TYPE (statement);

  for (p = name_list, i = 0; p != NULL; p = p->next)
    {
      name = (char *) p->info.name.original;
      if (name == NULL || name[0] == '\0')
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_NAME, 0);
	  return er_errid ();
	}

      err = jsp_drop_stored_procedure (name, jsp_map_pt_misc_to_sp_type (type));
      if (err != NO_ERROR)
	{
	  break;
	}
    }

  return err;
}

/*
 * jsp_default_value_string
 *   return:
 *   parser(in/out): parser environment
 *   statement(in): a default_value node
 *
 * Note:
 */
static int
jsp_default_value_string (PARSER_CONTEXT *parser, PT_NODE *node, bool &is_null, std::string &out)
{
  int error = NO_ERROR;
  is_null = false;

  DB_DEFAULT_EXPR default_expr;
  pt_get_default_expression_from_data_default_node (parser, node, &default_expr);

  out.clear ();
  if (default_expr.default_expr_type != DB_DEFAULT_NONE)
    {
      if (default_expr.default_expr_type == NULL_DEFAULT_EXPRESSION_OPERATOR)
	{
	  DB_VALUE *value = pt_value_to_db (parser, node->info.data_default.default_value);
	  if (!DB_IS_NULL (value))
	    {
	      string_buffer sb;
	      sb.clear ();
	      db_sprint_value (value, sb);

	      out.append (sb.get_buffer ());
	    }
	  else
	    {
	      // empty out consider as NULL
	      is_null = true;
	    }
	}
      else
	{
	  if (default_expr.default_expr_op == T_TO_CHAR)
	    {
	      out.append ("TO_CHAR(");
	    }

	  const char *default_value_expr_type_string = db_default_expression_string (default_expr.default_expr_type);
	  if (default_value_expr_type_string != NULL)
	    {
	      out.append (default_value_expr_type_string);
	    }
	  else
	    {
	      out.append (parser_print_tree (parser, node));
	    }

	  if (default_expr.default_expr_op == T_TO_CHAR)
	    {
	      if (default_expr.default_expr_format != NULL)
		{
		  out.append (", \'");
		  out.append (default_expr.default_expr_format);
		  out.append ("\'");
		}

	      out.append (")");
	    }
	}
    }
  else
    {
      PT_NODE *default_value = node->info.data_default.default_value;
      DB_VALUE *value = NULL;
      // do not use initialized db value
      if (default_value->info.value.db_value_is_initialized)
	{
	  default_value->info.value.db_value_is_initialized = false;
	}

      value = pt_value_to_db (parser, default_value);

      if (!DB_IS_NULL (value))
	{
	  if (TP_IS_CHAR_TYPE (db_value_domain_type (value)))
	    {
	      if (db_get_string_size (value) > DB_MAX_DEFAULT_EXPR_LENGTH)
		{
		  pt_reset_error (parser);
		  PT_ERRORmf (parser, default_value, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_SP_PARAM_DEFAULT_STR_TOO_BIG,
			      DB_MAX_DEFAULT_EXPR_LENGTH);
		  return ER_SP_PARAM_DEFAULT_STR_TOO_BIG;
		}

	      out.append (db_get_string (value));
	    }
	  else
	    {
	      DB_VALUE tmp_val;
	      error = db_value_coerce (value, &tmp_val, db_type_to_db_domain (DB_TYPE_VARCHAR));
	      if (error == NO_ERROR)
		{
		  out.append (db_get_string (&tmp_val));
		  db_value_clear (&tmp_val);
		}
	    }
	}
      else
	{
	  // empty out is considered NULL
	  is_null = true;
	}
    }

  return error;
}

static MOP
jsp_find_pkg_code (const char *unique_name)
{
  MOP mop;
  DB_VALUE value;
  int save;

  if (!unique_name || !*unique_name)
    {
      return NULL;
    }

  AU_DISABLE (save);

  db_make_string (&value, unique_name);
  mop = db_find_unique (db_find_class (CT_PACKAGE_CODE_NAME), PKG_CODE_ATTR_PKG_UNIQUE_NAME, &value);
  pr_clear_value (&value);
  if (mop == NULL)
    {
      if (er_errid() == ER_OBJ_OBJECT_NOT_FOUND)
	{
	  er_clear();
	}
    }

  AU_ENABLE (save);

  return mop;
}

static int
jsp_get_pkg_scode (const char *unique_name, DB_VALUE *value, bool for_body)
{
  int err;
  MOP mop;
  const char *ret;
  int save;

  err = NO_ERROR;

  mop = jsp_find_pkg_code (unique_name);
  if (mop == NULL)
    {
      if (er_errid() != NO_ERROR)
	{
	  return er_errid();
	}
      db_make_null (value);
      return NO_ERROR;
    }

  AU_DISABLE (save);
  err = db_get (mop, for_body ? PKG_CODE_ATTR_SCODE_BODY : PKG_CODE_ATTR_SCODE_SPEC, value);
  AU_ENABLE (save);

  return err;
}

static int
jsp_get_pkg_scode_spec (const char *unique_name, DB_VALUE *value)
{
  return jsp_get_pkg_scode (unique_name, value, false);
}

static int
jsp_get_pkg_scode_body (const char *unique_name, DB_VALUE *value)
{
  return jsp_get_pkg_scode (unique_name, value, true);
}

static int
jsp_set_pkg_scode_body_and_ocode (const char *unique_name, const char *scode_body, const char *ocode)
{
  int err;
  int save;
  MOP mop;
  DB_OTMPL *obt;
  DB_VALUE value;
  DB_OBJECT *object;

  err = NO_ERROR;
  obt = NULL;

  AU_DISABLE (save);    // side effect 0

  mop = jsp_find_pkg_code (unique_name);
  if (mop == NULL)
    {
      // CREATE PACKAGE has not been executed and object code cannot have been created
      assert (!ocode);

      if (er_errid() != NO_ERROR)
	{
	  err = er_errid();
	  goto cleanup0;
	}

      // insert a new record and set

      DB_OBJECT *classobj;

      classobj = db_find_class (CT_PACKAGE_CODE_NAME);
      if (classobj == NULL)
	{
	  ASSERT_ERROR_AND_SET (err);
	  goto cleanup0;
	}

      obt = dbt_create_object_internal (classobj, false);      // side effect 1
      if (obt == NULL)
	{
	  ASSERT_ERROR_AND_SET (err);
	  goto cleanup0;
	}

      // set the unque_name of the new record
      db_make_string (&value, unique_name);
      err = dbt_put_internal (obt, PKG_CODE_ATTR_PKG_UNIQUE_NAME, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup1;
	}

    }
  else
    {
      // at this point, ocode may or may not be null

      // set in the existing record

      obt = dbt_edit_object (mop);      // side effect 1
      if (obt == NULL)
	{
	  ASSERT_ERROR_AND_SET (err);
	  goto cleanup0;
	}
    }

  db_make_string (&value, scode_body);
  err = dbt_put_internal (obt, PKG_CODE_ATTR_SCODE_BODY, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup1;
    }

  if (ocode)
    {
      db_make_int (&value, SPOC_JAVA_JAR);  // currently, Java Jar only
      err = dbt_put_internal (obt, PKG_CODE_ATTR_OTYPE, &value);
      if (err != NO_ERROR)
	{
	  goto cleanup1;
	}

      db_make_string (&value, ocode);
      err = dbt_put_internal (obt, PKG_CODE_ATTR_OCODE, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup1;
	}
    }

  object = dbt_finish_object (obt);
  if (!object)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup1;
    }
  obt = NULL;   // side effect 1 cleaned

  err = locator_flush_instance (object);
  if (err != NO_ERROR)
    {
      obj_delete (object);
      goto cleanup0;
    }

  AU_ENABLE (save);
  return NO_ERROR;

cleanup1:
  assert (obt);
  dbt_abort_object (obt);

cleanup0:
  AU_ENABLE (save);

  return err;
}

static int
jsp_drop_pkg_body (PARSER_CONTEXT *parser, const char *unique_name, const char *owner_name, MOP owner_mop)
{
  int err;
  MOP pkg_code_mop;
  DB_VALUE scode_body_value, scode_spec_value, ocode_value;
  DB_OTMPL *obt;
  int save;

  err = NO_ERROR;
  obt = NULL;
  pkg_code_mop = NULL;

  AU_DISABLE (save);    // side effect 0

  // check if scode_body has been set
  {
    bool has_scode_body;

    pkg_code_mop = jsp_find_pkg_code (unique_name);
    if (pkg_code_mop)
      {
	err = db_get (pkg_code_mop, PKG_CODE_ATTR_SCODE_BODY, &scode_body_value);
	if (err != NO_ERROR)
	  {
	    goto cleanup0;
	  }

	if (DB_IS_NULL (&scode_body_value))
	  {
	    has_scode_body = false;
	  }
	else
	  {
	    has_scode_body = true;
	    pr_clear_value (&scode_body_value);
	  }
      }
    else
      {
	if (er_errid() != NO_ERROR)
	  {
	    err = er_errid();
	    goto cleanup0;
	  }
	has_scode_body = false;
      }

    if (!has_scode_body)
      {
	err = ER_PKG_BODY_NOT_EXIST;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, unique_name);
	goto cleanup0;
      }
  }

  assert (pkg_code_mop);

  // check whether the spec code exists or not.
  // if exists, then recompile the spec without the body
  // if not exists, then drop the _db_package_code record
  {
    err = db_get (pkg_code_mop, PKG_CODE_ATTR_SCODE_SPEC, &scode_spec_value);
    if (err != NO_ERROR)
      {
	goto cleanup0;
      }

    if (DB_IS_NULL (&scode_spec_value))
      {
	// scode_spec is null. just drop the _db_package_code record.
	// all the other records related to this pkg must have not been created.
	// in other words, CREATE PACKAGE has not been executed

	err = obj_delete (pkg_code_mop);
	if (err != NO_ERROR)
	  {
	    goto cleanup0;
	  }
      }
    else
      {
	// scode_spec has been set (CREATE PACKAGE has been executed.)
	// recompile and get the ocode with only the spec

	PLCSQL_COMPILE_REQUEST pkg_compile_request;
	PLCSQL_COMPILE_RESPONSE pkg_compile_response;
	DB_OBJECT *object;

	// get a new ocode with just the scode_spec
	{
	  pkg_compile_request.type = PLCSQL_COMPILE_TYPE_PKG_SPEC;
	  pkg_compile_request.code.assign (db_get_string (&scode_spec_value));
	  pr_clear_value (&scode_spec_value);
	  pkg_compile_request.owner.assign (owner_name);

	  au_perform_push_user (owner_mop);
	  err = plcsql_compile (pkg_compile_request, pkg_compile_response);
	  au_perform_pop_user ();

	  if (err == NO_ERROR && pkg_compile_response.err_code == NO_ERROR)
	    {
	      db_make_string (&ocode_value, pkg_compile_response.compiled_code.data());       // side effect 1
	    }
	  else
	    {
	      err = ER_PKG_COMPILE_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, pkg_compile_response.err_msg.c_str ());
	      pt_record_error (parser, parser->statement_number, pkg_compile_response.err_line, pkg_compile_response.err_column,
			       er_msg (), NULL);
	      goto cleanup0;
	    }
	}

	obt = dbt_edit_object (pkg_code_mop);
	if (obt == NULL)
	  {
	    ASSERT_ERROR_AND_SET (err);
	    goto cleanup1;
	  }     // side effect 2

	// set null to the scode_body column
	db_make_null (&scode_body_value);
	err = dbt_put_internal (obt, PKG_CODE_ATTR_SCODE_BODY, &scode_body_value);
	if (err != NO_ERROR)
	  {
	    goto cleanup2;
	  }

	// set the new ocode to the column
	err = dbt_put_internal (obt, PKG_CODE_ATTR_OCODE, &ocode_value);
	if (err != NO_ERROR)
	  {
	    goto cleanup2;
	  }

	object = dbt_finish_object (obt);
	if (!object)
	  {
	    ASSERT_ERROR_AND_SET (err);
	    goto cleanup2;
	  } // side effect 2 cleaned
	pr_clear_value (&ocode_value);  // side effect 1 cleaned

	err = locator_flush_instance (object);
	if (err != NO_ERROR)
	  {
	    obj_delete (object);
	    goto cleanup0;
	  }
      }
  }

  AU_ENABLE (save); // side effect 0 cleaned
  return NO_ERROR;

cleanup2:
  assert (obt);
  dbt_abort_object (obt);

cleanup1:
  pr_clear_value (&ocode_value);

cleanup0:
  AU_ENABLE (save);

  return err;
}

static int
jsp_drop_pkg_member_sp (MOP pkg_mop)
{

  int err, args_cnt, proc_cnt;
  DB_VALUE args_cnt_val, proc_cnt_val, args_seq_val, procs_val, sp_elem, arg_elem;
  DB_SET *procs, *args_seq;
  MOP sp_mop, sp_code_mop, sp_arg_mop;

  err = db_get (pkg_mop, PKG_ATTR_PROCEDURES_CNT, &proc_cnt_val);
  if (err != NO_ERROR)
    {
      goto return_;
    }
  proc_cnt = db_get_int (&proc_cnt_val);

  err = db_get (pkg_mop, PKG_ATTR_PROCEDURES, &procs_val);
  if (err != NO_ERROR)
    {
      goto return_;
    }
  procs = db_get_set (&procs_val);  // side effect 0

  db_make_null (&sp_elem);
  for (int i = 0; i < proc_cnt; i++)
    {
      // find sp
      set_get_element (procs, i, &sp_elem);       // side effect 1
      sp_mop = db_get_object (&sp_elem);

      // NOTE: Package member procedures/functions do not have their records in _db_stored_procedure_code.
      //       Information about code is stored in _db_package_code for package member procedures/functions

      // drop from _db_stored_procedure_args
      {
	err = db_get (sp_mop, SP_ATTR_ARG_COUNT, &args_cnt_val);
	if (err != NO_ERROR)
	  {
	    goto cleanup1;
	  }
	args_cnt = db_get_int (&args_cnt_val);

	err = db_get (sp_mop, SP_ATTR_ARGS, &args_seq_val);
	if (err != NO_ERROR)
	  {
	    goto cleanup1;
	  }
	args_seq = db_get_set (&args_seq_val);

	for (int j = 0; j < args_cnt; j++)
	  {
	    set_get_element (args_seq, j, &arg_elem);
	    sp_arg_mop = db_get_object (&arg_elem);
	    err = obj_delete (sp_arg_mop);
	    pr_clear_value (&arg_elem);
	    if (err != NO_ERROR)
	      {
		pr_clear_value (&args_seq_val);
		goto cleanup1;
	      }
	  }

	pr_clear_value (&args_seq_val);
      }

      // drop from _db_stored_procedure
      err = obj_delete (sp_mop);
      pr_clear_value (&sp_elem);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

cleanup1:
  pr_clear_value (&sp_elem);

cleanup0:
  pr_clear_value (&procs_val);

return_:
  return err;
}

static int
jsp_drop_pkg_members (MOP pkg_mop, const char *cnt_attr, const char *members_attr)
{

  int err, cnt, i;
  DB_VALUE cnt_val, seq_val, elem;
  DB_SET *seq;
  MOP mop;

  err = db_get (pkg_mop, cnt_attr, &cnt_val);
  if (err != NO_ERROR)
    {
      return err;
    }
  cnt = db_get_int (&cnt_val);

  err = db_get (pkg_mop, members_attr, &seq_val);
  if (err != NO_ERROR)
    {
      return err;
    }

  seq = db_get_set (&seq_val);

  for (i = 0; i < cnt; i++)
    {
      set_get_element (seq, i, &elem);
      mop = db_get_object (&elem);
      pr_clear_value (&elem);
      err = obj_delete (mop);
      if (err != NO_ERROR)
	{
	  pr_clear_value (&seq_val);
	  return err;
	}
    }

  pr_clear_value (&seq_val);

  return NO_ERROR;
}

static int
jsp_drop_pkg (const char *unique_name, MOP pkg_mop, MOP owner)
{
  MOP mop;
  int err, save;
  DB_OTMPL *obt;

  err = NO_ERROR;

  AU_DISABLE (save);    // side effect 0

  // clear authorization settings of the dropped package
  {
    MOP save_user;

    save_user = Au_user;
    if (AU_SET_USER (owner) == NO_ERROR)
      {
	err = au_object_revoke_all_privileges (DB_OBJECT_PACKAGE, owner, unique_name);
	if (err != NO_ERROR)
	  {
	    AU_SET_USER (save_user);
	    goto cleanup0;
	  }
      }

    AU_SET_USER (save_user);

    err = au_delete_auth_of_dropping_database_object (DB_OBJECT_PACKAGE, unique_name);
    if (err != NO_ERROR)
      {
	goto cleanup0;
      }
  }

  // drop the record in _db_package_code
  mop = jsp_find_pkg_code (unique_name);
  if (mop)
    {
      err = obj_delete (mop);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }
  else
    {
      if (er_errid () != NO_ERROR)
	{
	  err = er_errid ();
	  goto cleanup0;
	}
      // _db_package exists but _db_package_code doesn't - unreachable state
      assert (false);
      err = ER_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      goto cleanup0;
    }

  // drop the records in _db_stored_procedure
  err = jsp_drop_pkg_member_sp (pkg_mop);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // drop the records in _db_package_var
  err = jsp_drop_pkg_members (pkg_mop, PKG_ATTR_VARIABLES_CNT, PKG_ATTR_VARIABLES);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // drop the records in _db_package_exception
  err = jsp_drop_pkg_members (pkg_mop, PKG_ATTR_EXCEPTIONS_CNT, PKG_ATTR_EXCEPTIONS);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // drop the records in _db_package_cursor
  err = jsp_drop_pkg_members (pkg_mop, PKG_ATTR_CURSORS_CNT, PKG_ATTR_CURSORS);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // drop the records in _db_package_record_type
  err = jsp_drop_pkg_members (pkg_mop, PKG_ATTR_RECORD_TYPES_CNT, PKG_ATTR_RECORD_TYPES);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // drop the record in _db_package
  err = obj_delete (pkg_mop);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  AU_ENABLE (save);
  return NO_ERROR;

cleanup1:
  assert (obt);
  dbt_abort_object (obt);

cleanup0:
  AU_ENABLE (save);

  return err;
}

static int
sp_set_pkg_code (MOP *mop_out, const char *pkg_unique_name, const char *class_name,
		 const char *scode_spec, const char *scode_body, const char *ocode)
{

  DB_OTMPL *obt;
  DB_OBJECT *object, *classobj;
  DB_VALUE value;
  int err;

  // get object template to edit
  {
    MOP mop;
    DB_OBJECT *classobj;

    mop = jsp_find_pkg_code (pkg_unique_name);
    if (mop)
      {
	// it can exist because package body can have already been created
	obt = dbt_edit_object (mop);
	if (!obt)
	  {
	    ASSERT_ERROR_AND_SET (err);
	    goto error;
	  }
      }
    else
      {
	if (er_errid() != NO_ERROR)
	  {
	    err = er_errid();
	    goto error;
	  }

	classobj = db_find_class (CT_PACKAGE_CODE_NAME);
	if (classobj == NULL)
	  {
	    ASSERT_ERROR_AND_SET (err);
	    goto error;
	  }
	obt = dbt_create_object_internal (classobj, false);
	if (!obt)
	  {
	    ASSERT_ERROR_AND_SET (err);
	    goto error;
	  } // side effect 0
      }
  }

  // attribute pkg_unique_name
  db_make_string (&value, pkg_unique_name);
  err = dbt_put_internal (obt, PKG_CODE_ATTR_PKG_UNIQUE_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute name
  db_make_string (&value, class_name);
  err = dbt_put_internal (obt, PKG_CODE_ATTR_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute stype
  db_make_int (&value, SPSC_PLCSQL);  // currently, PLCSQL only
  err = dbt_put_internal (obt, PKG_CODE_ATTR_STYPE, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute scode_spec
  db_make_string (&value, scode_spec);
  err = dbt_put_internal (obt, PKG_CODE_ATTR_SCODE_SPEC, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute scode_body (NOTE: scode_body is optional)
  if (scode_body && scode_body[0])
    {
      db_make_string (&value, scode_body);
      err = dbt_put_internal (obt, PKG_CODE_ATTR_SCODE_BODY, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

  // attribute otype
  db_make_int (&value, SPOC_JAVA_JAR);  // currently, Java Jar only
  err = dbt_put_internal (obt, PKG_CODE_ATTR_OTYPE, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute ocode
  db_make_string (&value, ocode);
  err = dbt_put_internal (obt, PKG_CODE_ATTR_OCODE, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // finish object
  object = dbt_finish_object (obt);
  if (!object)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup0;
    }
  obt = NULL;

  // flush instance
  err = locator_flush_instance (object);
  if (err != NO_ERROR)
    {
      obj_delete (object);
      goto error;
    }

  *mop_out = object;
  return NO_ERROR;

cleanup0:
  assert (obt);
  dbt_abort_object (obt);

error:
  return err;
}

static int
sp_add_pkg_sp_arg (MOP *mop_out, const int idx, const cubpl::pkg_sp_arg arg)
{

  DB_OTMPL *obt;
  DB_OBJECT *object, *classobj;
  DB_VALUE value;
  int err;

  classobj = db_find_class (CT_STORED_PROC_ARGS_NAME);
  if (classobj == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  obt = dbt_create_object_internal (classobj, false);
  if (!obt)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    } // side effect 0

  // attribute sp_of is set later

  // attribute index_of
  db_make_int (&value, idx);
  err = dbt_put_internal (obt, SP_ARG_ATTR_INDEX_OF, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute is_system_generated
  db_make_int (&value, 0);	// 0: hardcoded false
  err = dbt_put_internal (obt, SP_ARG_ATTR_IS_SYSTEM_GENERATED, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute arg_name
  db_make_string (&value, arg.name.data());
  err = dbt_put_internal (obt, SP_ARG_ATTR_ARG_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute data_type
  db_make_int (&value, arg.data_type);
  err = dbt_put_internal (obt, SP_ARG_ATTR_DATA_TYPE, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute mode
  db_make_int (&value, arg.mode);
  err = dbt_put_internal (obt, SP_ARG_ATTR_MODE, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute default_value
  if (arg.default_value.length() > 0)
    {
      db_make_string (&value, arg.default_value.data());
      err = dbt_put_internal (obt, SP_ARG_ATTR_DEFAULT_VALUE, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

  // attribute is_optional
  db_make_int (&value, arg.default_value.empty() ? 0 : 1);
  err = dbt_put_internal (obt, SP_ARG_ATTR_IS_OPTIONAL, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute comment
  if (arg.comment.length() > 0)
    {
      db_make_string (&value, arg.comment.data());
      err = dbt_put_internal (obt, SP_ARG_ATTR_COMMENT, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

  // finish object
  object = dbt_finish_object (obt);
  if (!object)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup0;
    }
  obt = NULL;

  // flush instance
  err = locator_flush_instance (object);
  if (err != NO_ERROR)
    {
      obj_delete (object);
      goto error;
    }

  *mop_out = object;
  return NO_ERROR;

cleanup0:
  assert (obt);
  dbt_abort_object (obt);

error:
  return err;
}

static int
sp_add_pkg_sp (MOP *mop_out, MOP owner, DB_VALUE &current_datetime,
	       const char *pkg_unique_name, const char *pkg_name, const char *class_name, const cubpl::pkg_sp &sp)
{
  DB_OTMPL *obt;
  DB_OBJECT *object, *sp_arg_obj, *classobj, *arg_classobj;
  DB_VALUE value;
  int err, n, args_cnt;
  char buffer[SP_ATTR_UNIQUE_NAME_LEN + 1];
  MOP *mop_list;

  mop_list = NULL;

  classobj = db_find_class (CT_STORED_PROC_NAME);
  if (classobj == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  obt = dbt_create_object_internal (classobj, false);
  if (!obt)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    } // side effect 0

  // attribute unique_name
  n = snprintf (buffer, sizeof (buffer), "%s.%s", pkg_unique_name, sp.name.data());
  if (n >= (int) sizeof (buffer))
    {
      err = ER_PKG_PROC_UNIQ_NAME_TOO_LONG;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      goto cleanup0;
    }
  db_make_string (&value, buffer);
  err = dbt_put_internal (obt, SP_ATTR_UNIQUE_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute sp_name
  db_make_string (&value, sp.name.data());
  err = dbt_put_internal (obt, SP_ATTR_SP_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute sp_type
  db_make_int (&value, sp.type);
  err = dbt_put_internal (obt, SP_ATTR_SP_TYPE, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute return_type
  db_make_int (&value, sp.return_type);
  err = dbt_put_internal (obt, SP_ATTR_RETURN_TYPE, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute lang
  db_make_int (&value, SP_LANG_PLCSQL);
  err = dbt_put_internal (obt, SP_ATTR_LANG, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute pkg_name
  db_make_string (&value, pkg_name);
  err = dbt_put_internal (obt, SP_ATTR_PKG_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute is_system_generated
  db_make_int (&value, 0);      // 0: hardcoded false
  err = dbt_put_internal (obt, SP_ATTR_IS_SYSTEM_GENERATED, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute directive
  db_make_int (&value, sp.directive);
  err = dbt_put_internal (obt, SP_ATTR_DIRECTIVE, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute target_class
  db_make_string (&value, class_name);
  err = dbt_put_internal (obt, SP_ATTR_TARGET_CLASS, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute target_method
  db_make_string (&value, sp.java_signature.data());
  err = dbt_put_internal (obt, SP_ATTR_TARGET_METHOD, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute owner
  db_make_object (&value, owner);
  err = dbt_put_internal (obt, SP_ATTR_OWNER, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute sql_data_access
  db_make_int (&value, sp.sql_data_access);
  err = dbt_put_internal (obt, SP_ATTR_SQL_DATA_ACCESS, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute comment
  if (sp.comment.length() > 0)
    {
      db_make_string (&value, sp.comment.data());
      err = dbt_put_internal (obt, SP_ATTR_COMMENT, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

  // attribute created_time
  err = dbt_put_internal (obt, SP_ATTR_CREATED_TIME, &current_datetime);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute updated_time
  err = dbt_put_internal (obt, SP_ATTR_UPDATED_TIME, &current_datetime);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute arg_count
  args_cnt = sp.args.size();
  db_make_int (&value, args_cnt);
  err = dbt_put_internal (obt, SP_ATTR_ARG_COUNT, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  if (args_cnt > 0)
    {

      // attribute args
      mop_list = (MOP *) malloc (args_cnt * sizeof (MOP));	// side effect 1
      if (!mop_list)
	{
	  err = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, args_cnt * sizeof (MOP));
	  goto cleanup0;
	}

      DB_SET *seq;
      int i;
      DB_VALUE v;

      seq = set_create_sequence (0);
      if (!seq)
	{
	  ASSERT_ERROR_AND_SET (err);
	  goto cleanup1;
	}

      i = 0;
      for (const cubpl::pkg_sp_arg &a: sp.args)
	{
	  err = sp_add_pkg_sp_arg (&mop_list[i], i, a);
	  if (err != NO_ERROR)
	    {
	      set_free (seq);
	      goto cleanup1;
	    }

	  db_make_object (&v, mop_list[i]);
	  err = set_put_element (seq, i, &v);
	  pr_clear_value (&v);
	  if (err != NO_ERROR)
	    {
	      set_free (seq);
	      goto cleanup1;
	    }

	  i++;
	}

      db_make_sequence (&value, seq);
      err = dbt_put_internal (obt, SP_ATTR_ARGS, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup1;
	}
    }
  else
    {

      DB_SET *seq = set_create_sequence (0);
      if (!seq)
	{
	  ASSERT_ERROR_AND_SET (err);
	  goto cleanup0;
	}

      db_make_sequence (&value, seq);
      err = dbt_put_internal (obt, SP_ATTR_ARGS, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

  // finish object
  object = dbt_finish_object (obt);
  if (!object)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup1;
    }
  obt = NULL;

  // flush instance
  err = locator_flush_instance (object);
  if (err != NO_ERROR)
    {
      obj_delete (object);
      goto cleanup1;
    }

  // update _db_stored_procedure_code.sp_of of sp arguments
  for (int i = 0; i < args_cnt; i++)
    {

      obt = dbt_edit_object (mop_list[i]);
      if (!obt)
	{
	  ASSERT_ERROR_AND_SET (err);
	  goto cleanup1;
	}

      db_make_object (&value, object);
      err = dbt_put_internal (obt, SP_ARG_ATTR_SP_OF, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup1;
	}

      sp_arg_obj = dbt_finish_object (obt);
      if (!sp_arg_obj)
	{
	  ASSERT_ERROR_AND_SET (err);
	  goto cleanup1;
	}
      obt = NULL;

      err = locator_flush_instance (sp_arg_obj);
      if (err != NO_ERROR)
	{
	  obj_delete (sp_arg_obj);
	  goto cleanup1;
	}
    }
  free (mop_list);

  *mop_out = object;
  return NO_ERROR;

cleanup1:
  free (mop_list);

cleanup0:
  if (obt)
    {
      dbt_abort_object (obt);
    }

error:
  return err;
}

static int
sp_add_pkg_var (MOP *mop_out, const char *pkg_unique_name, const cubpl::pkg_var &var)
{

  DB_OTMPL *obt;
  DB_OBJECT *object, *classobj;
  DB_VALUE value;
  int err;

  classobj = db_find_class (CT_PACKAGE_VAR_NAME);
  if (classobj == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  obt = dbt_create_object_internal (classobj, false);
  if (!obt)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    } // side effect 0

  // attribute pkg_unique_name
  db_make_string (&value, pkg_unique_name);
  err = dbt_put_internal (obt, PKG_VAR_ATTR_PKG_UNIQUE_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute name
  db_make_string (&value, var.name.data());
  err = dbt_put_internal (obt, PKG_VAR_ATTR_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute data_type
  db_make_int (&value, var.data_type);
  err = dbt_put_internal (obt, PKG_VAR_ATTR_DATA_TYPE, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute prec
  db_make_int (&value, var.prec);
  err = dbt_put_internal (obt, PKG_VAR_ATTR_PREC, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute scale
  db_make_int (&value, var.scale);
  err = dbt_put_internal (obt, PKG_VAR_ATTR_SCALE, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute flags
  db_make_int (&value, var.flags);
  err = dbt_put_internal (obt, PKG_VAR_ATTR_FLAGS, &value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute comment
  if (var.comment.length() > 0)
    {
      db_make_string (&value, var.comment.data());
      err = dbt_put_internal (obt, PKG_VAR_ATTR_COMMENT, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

  // finish object
  object = dbt_finish_object (obt);
  if (!object)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup0;
    }
  obt = NULL;

  // flush instance
  err = locator_flush_instance (object);
  if (err != NO_ERROR)
    {
      obj_delete (object);
      goto error;
    }

  *mop_out = object;
  return NO_ERROR;

cleanup0:
  assert (obt);
  dbt_abort_object (obt);

error:
  return err;
}

static int
sp_add_pkg_exception (MOP *mop_out, const char *pkg_unique_name,
		      const cubpl::pkg_exception &exception)
{

  DB_OTMPL *obt;
  DB_OBJECT *object, *classobj;
  DB_VALUE value;
  int err;

  classobj = db_find_class (CT_PACKAGE_EXCEPTION_NAME);
  if (classobj == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  obt = dbt_create_object_internal (classobj, false);
  if (!obt)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    } // side effect 0

  // attribute pkg_unique_name
  db_make_string (&value, pkg_unique_name);
  err = dbt_put_internal (obt, PKG_EXCEPTION_ATTR_PKG_UNIQUE_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute name
  db_make_string (&value, exception.name.data());
  err = dbt_put_internal (obt, PKG_EXCEPTION_ATTR_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute comment
  if (exception.comment.length() > 0)
    {
      db_make_string (&value, exception.comment.data());
      err = dbt_put_internal (obt, PKG_EXCEPTION_ATTR_COMMENT, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

  // finish object
  object = dbt_finish_object (obt);
  if (!object)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup0;
    }
  obt = NULL;

  // flush instance
  err = locator_flush_instance (object);
  if (err != NO_ERROR)
    {
      obj_delete (object);
      goto error;
    }

  *mop_out = object;
  return NO_ERROR;

cleanup0:
  assert (obt);
  dbt_abort_object (obt);

error:
  return err;
}

static int
sp_add_pkg_cursor (MOP *mop_out, const char *pkg_unique_name, const cubpl::pkg_cursor &cursor)
{

  DB_OTMPL *obt;
  DB_OBJECT *object, *classobj;
  DB_VALUE value;
  int err;

  classobj = db_find_class (CT_PACKAGE_CURSOR_NAME);
  if (classobj == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  obt = dbt_create_object_internal (classobj, false);
  if (!obt)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    } // side effect 0

  // attribute pkg_unique_name
  db_make_string (&value, pkg_unique_name);
  err = dbt_put_internal (obt, PKG_CURSOR_ATTR_PKG_UNIQUE_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute name
  db_make_string (&value, cursor.name.data());
  err = dbt_put_internal (obt, PKG_CURSOR_ATTR_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute record_type
  db_make_string (&value, cursor.record_type.data());
  err = dbt_put_internal (obt, PKG_CURSOR_ATTR_RECORD_TYPE, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute parameters
  {
    DB_SET *seq;
    int i;
    DB_VALUE v;

    seq = set_create_sequence (0);
    if (!seq)
      {
	ASSERT_ERROR_AND_SET (err);
	goto cleanup0;
      }

    i = 0;
    for (const std::string &p: cursor.parameters)
      {

	db_make_string (&v, p.data());
	err = set_put_element (seq, i, &v);
	pr_clear_value (&v);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup0;
	  }

	i++;
      }
    db_make_sequence (&value, seq);
    err = dbt_put_internal (obt, PKG_CURSOR_ATTR_PARAMETERS, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto cleanup0;
      }
  }

  // attribute comment
  if (cursor.comment.length() > 0)
    {
      db_make_string (&value, cursor.comment.data());
      err = dbt_put_internal (obt, PKG_CURSOR_ATTR_COMMENT, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

  // finish object
  object = dbt_finish_object (obt);
  if (!object)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup0;
    }
  obt = NULL;

  // flush instance
  err = locator_flush_instance (object);
  if (err != NO_ERROR)
    {
      obj_delete (object);
      goto error;
    }

  *mop_out = object;
  return NO_ERROR;

cleanup0:
  assert (obt);
  dbt_abort_object (obt);

error:
  return err;
}

static int
sp_add_pkg_rec_type (MOP *mop_out, const char *pkg_unique_name,
		     const cubpl::pkg_rec_type &rec_type)
{

  DB_OTMPL *obt;
  DB_OBJECT *object, *classobj;
  DB_VALUE value;
  int err;

  classobj = db_find_class (CT_PACKAGE_RECORD_TYPE_NAME);
  if (classobj == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  obt = dbt_create_object_internal (classobj, false);
  if (!obt)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    } // side effect 0

  // attribute pkg_unique_name
  db_make_string (&value, pkg_unique_name);
  err = dbt_put_internal (obt, PKG_RECORD_TYPE_ATTR_PKG_UNIQUE_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute name
  db_make_string (&value, rec_type.name.data());
  err = dbt_put_internal (obt, PKG_RECORD_TYPE_ATTR_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup0;
    }

  // attribute fields
  {
    DB_SET *seq;
    int i;
    DB_VALUE v;

    seq = set_create_sequence (0);
    if (!seq)
      {
	ASSERT_ERROR_AND_SET (err);
	goto cleanup0;
      }

    i = 0;
    for (const std::string &f: rec_type.fields)
      {

	db_make_string (&v, f.data());
	err = set_put_element (seq, i, &v);
	pr_clear_value (&v);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup0;
	  }

	i++;
      }
    db_make_sequence (&value, seq);
    err = dbt_put_internal (obt, PKG_RECORD_TYPE_ATTR_FIELDS, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto cleanup0;
      }
  }

  // attribute comment
  if (rec_type.comment.length() > 0)
    {
      db_make_string (&value, rec_type.comment.data());
      err = dbt_put_internal (obt, PKG_RECORD_TYPE_ATTR_COMMENT, &value);
      pr_clear_value (&value);
      if (err != NO_ERROR)
	{
	  goto cleanup0;
	}
    }

  // finish object
  object = dbt_finish_object (obt);
  if (!object)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup0;
    }
  obt = NULL;

  // flush instance
  err = locator_flush_instance (object);
  if (err != NO_ERROR)
    {
      obj_delete (object);
      goto error;
    }

  *mop_out = object;
  return NO_ERROR;

cleanup0:
  assert (obt);
  dbt_abort_object (obt);

error:
  return err;
}

static int
sp_add_pkg_and_related (const char *unique_name, const char *owner_name, MOP owner,
			const char *scode_spec, const char *scode_body, const char *comment,
			const PLCSQL_COMPILE_RESPONSE &pkg_compile_response)
{
  DB_OBJECT *classobj, *object;
  DB_OTMPL *obt;
  DB_VALUE value, current_datetime, v;
  int save, err, size, i;
  const char *pkg_name, *class_name;
  DB_SET *seq;
  MOP mop;

  err = NO_ERROR;
  obt = NULL;
  pkg_name = unique_name + strlen (owner_name) + 1;	// +1: dot in <user>.<package>
  class_name = pkg_compile_response.class_name.data();

  err = db_sys_datetime (&current_datetime);
  if (err != NO_ERROR)
    {
      return err;
    }

  AU_DISABLE (save);    // side effect 1

  classobj = db_find_class (CT_PACKAGE_NAME);
  if (classobj == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup1;
    }

  obt = dbt_create_object_internal (classobj, false);
  if (!obt)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup1;
    } // side effect 2

  // attribute unique_name
  db_make_string (&value, unique_name);
  err = dbt_put_internal (obt, PKG_ATTR_UNIQUE_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup2;
    }

  // attribute pkg_name
  db_make_string (&value, pkg_name);
  err = dbt_put_internal (obt, PKG_ATTR_PKG_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup2;
    }

  // attribute flags
  db_make_int (&value, 0);  // bit0 == 0: not a system generated package
  err = dbt_put_internal (obt, PKG_ATTR_FLAGS, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup2;
    }

  // attribute owner
  db_make_object (&value, owner);
  err = dbt_put_internal (obt, PKG_ATTR_OWNER, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup2;
    }

  // insert or update into _db_package_code
  {
    const char *ocode = pkg_compile_response.compiled_code.data();
    err = sp_set_pkg_code (&mop, unique_name, class_name, scode_spec, scode_body, ocode);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }

    db_make_object (&v, mop);
    err = dbt_put_internal (obt, PKG_ATTR_CODE, &v);
    pr_clear_value (&v);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }
  }

  // insert into _db_stored_procedure and _db_stored_procedure_args (NOTE: but not into _db_stored_procedure_code)
  {
    seq = set_create_sequence (0);
    if (!seq)
      {
	ASSERT_ERROR_AND_SET (err);
	goto cleanup2;
      }

    i = 0;
    for (const cubpl::pkg_sp &sp: pkg_compile_response.sp)
      {
	err = sp_add_pkg_sp (&mop, owner, current_datetime, unique_name, pkg_name, class_name, sp);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	db_make_object (&v, mop);
	err = set_put_element (seq, i, &v);
	pr_clear_value (&v);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	i++;
      }
    db_make_sequence (&value, seq);
    err = dbt_put_internal (obt, PKG_ATTR_PROCEDURES, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }

    db_make_int (&value, i);
    err = dbt_put_internal (obt, PKG_ATTR_PROCEDURES_CNT, &value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }
  }

  // insert into _db_package_var
  {
    seq = set_create_sequence (0);
    if (!seq)
      {
	ASSERT_ERROR_AND_SET (err);
	goto cleanup2;
      }

    i = 0;
    for (const cubpl::pkg_var &var: pkg_compile_response.var)
      {

	err = sp_add_pkg_var (&mop, unique_name, var);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	db_make_object (&v, mop);
	err = set_put_element (seq, i, &v);
	pr_clear_value (&v);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	i++;
      }
    db_make_sequence (&value, seq);
    err = dbt_put_internal (obt, PKG_ATTR_VARIABLES, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }

    db_make_int (&value, i);
    err = dbt_put_internal (obt, PKG_ATTR_VARIABLES_CNT, &value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }
  }

  // insert into _db_package_exception
  {
    seq = set_create_sequence (0);
    if (!seq)
      {
	ASSERT_ERROR_AND_SET (err);
	goto cleanup2;
      }

    i = 0;
    for (const cubpl::pkg_exception &exc: pkg_compile_response.exception)
      {

	err = sp_add_pkg_exception (&mop, unique_name, exc);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	db_make_object (&v, mop);
	err = set_put_element (seq, i, &v);
	pr_clear_value (&v);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	i++;
      }
    db_make_sequence (&value, seq);
    err = dbt_put_internal (obt, PKG_ATTR_EXCEPTIONS, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }

    db_make_int (&value, i);
    err = dbt_put_internal (obt, PKG_ATTR_EXCEPTIONS_CNT, &value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }
  }

  // insert into _db_package_cursor
  {
    seq = set_create_sequence (0);
    if (!seq)
      {
	ASSERT_ERROR_AND_SET (err);
	goto cleanup2;
      }

    i = 0;
    for (const cubpl::pkg_cursor &cr: pkg_compile_response.cursor)
      {

	err = sp_add_pkg_cursor (&mop, unique_name, cr);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	db_make_object (&v, mop);
	err = set_put_element (seq, i, &v);
	pr_clear_value (&v);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	i++;
      }
    db_make_sequence (&value, seq);
    err = dbt_put_internal (obt, PKG_ATTR_CURSORS, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }

    db_make_int (&value, i);
    err = dbt_put_internal (obt, PKG_ATTR_CURSORS_CNT, &value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }
  }

  // insert into _db_package_record_type
  {
    seq = set_create_sequence (0);
    if (!seq)
      {
	ASSERT_ERROR_AND_SET (err);
	goto cleanup2;
      }

    i = 0;
    for (const cubpl::pkg_rec_type &rt: pkg_compile_response.rec_type)
      {

	err = sp_add_pkg_rec_type (&mop, unique_name, rt);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	db_make_object (&v, mop);
	err = set_put_element (seq, i, &v);
	pr_clear_value (&v);
	if (err != NO_ERROR)
	  {
	    set_free (seq);
	    goto cleanup2;
	  }

	i++;
      }
    db_make_sequence (&value, seq);
    err = dbt_put_internal (obt, PKG_ATTR_RECORD_TYPES, &value);
    pr_clear_value (&value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }

    db_make_int (&value, i);
    err = dbt_put_internal (obt, PKG_ATTR_RECORD_TYPES_CNT, &value);
    if (err != NO_ERROR)
      {
	goto cleanup2;
      }
  }

  // attribute comment
  if (comment && comment[0])
    {
      db_make_string (&value, comment);
    }
  else
    {
      db_make_null (&value);
    }
  err = dbt_put_internal (obt, PKG_ATTR_COMMENT, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto cleanup2;
    }

  // attribute created_time
  err = dbt_put_internal (obt, PKG_ATTR_CREATED_TIME, &current_datetime);
  if (err != NO_ERROR)
    {
      goto cleanup2;
    }

  // attribute updated_time
  err = dbt_put_internal (obt, PKG_ATTR_UPDATED_TIME, &current_datetime);
  if (err != NO_ERROR)
    {
      goto cleanup2;
    }

  // finish object
  object = dbt_finish_object (obt);
  if (!object)
    {
      ASSERT_ERROR_AND_SET (err);
      goto cleanup2;
    }
  obt = NULL;

  // flush instance
  err = locator_flush_instance (object);
  if (err != NO_ERROR)
    {
      obj_delete (object);
      goto cleanup1;
    }

cleanup2:
  if (obt)
    {
      dbt_abort_object (obt);
    }

cleanup1:

  AU_ENABLE (save);
  pr_clear_value (&current_datetime);

  return err;
}

static int
jsp_create_pkg_body (PARSER_CONTEXT *parser, PT_NODE *statement, const char *unique_name, const char *owner_name,
		     MOP owner_mop)
{
  int err;
  PLCSQL_COMPILE_REQUEST pkg_compile_request;
  PLCSQL_COMPILE_RESPONSE pkg_compile_response;
  DB_VALUE value;

  // does it already exist?
  err = jsp_get_pkg_scode_body (unique_name, &value);
  if (err != NO_ERROR)
    {
      goto error_exit;
    }
  if (!DB_IS_NULL (&value))
    {
      pr_clear_value (&value);
      if (statement->info.pkg.or_replace)
	{
	  // OK. it will be overwritten
	}
      else
	{
	  err = ER_PKG_BODY_ALREADY_EXIST;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, unique_name);
	  goto error_exit;
	}
    }

  // send package compile request with only body code set to PL server and receive the response
  assert (statement->sql_user_text && statement->sql_user_text_len);
  pkg_compile_request.body_code.assign (statement->sql_user_text, statement->sql_user_text_len);
  pkg_compile_request.owner.assign (owner_name);

  err = jsp_get_pkg_scode_spec (unique_name, &value);
  if (err != NO_ERROR)
    {
      goto error_exit;
    }
  if (DB_IS_NULL (&value))
    {
      pkg_compile_request.type = PLCSQL_COMPILE_TYPE_PKG_BODY;
    }
  else
    {
      pkg_compile_request.type = PLCSQL_COMPILE_TYPE_PKG_SPEC;
      pkg_compile_request.code.assign (db_get_string (&value));
      pr_clear_value (&value);
    }

  au_perform_push_user (owner_mop);
  err = plcsql_compile (pkg_compile_request, pkg_compile_response);
  au_perform_pop_user ();

  if (err == NO_ERROR && pkg_compile_response.err_code == NO_ERROR)
    {
      const char *ocode;
      if (pkg_compile_request.type == PLCSQL_COMPILE_TYPE_PKG_SPEC)
	{
	  ocode = pkg_compile_response.compiled_code.data();
	}
      else
	{
	  ocode = NULL;
	}

      // package spec has not been updated, and hence no spec-related updates in system tables
      err = jsp_set_pkg_scode_body_and_ocode (unique_name, pkg_compile_request.body_code.data(), ocode);
      if (err != NO_ERROR)
	{
	  goto error_exit;
	}
    }
  else
    {
      err = ER_PKG_COMPILE_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, pkg_compile_response.err_msg.c_str ());
      pt_record_error (parser, parser->statement_number, pkg_compile_response.err_line, pkg_compile_response.err_column,
		       er_msg (),
		       NULL);
      goto error_exit;
    }

  return NO_ERROR;

error_exit:

  return err;
}

static int
jsp_create_pkg_spec (PARSER_CONTEXT *parser, PT_NODE *statement, const char *unique_name, const char *owner_name,
		     MOP owner_mop,
		     const char *comment)
{
  int err;
  PLCSQL_COMPILE_REQUEST pkg_compile_request;
  PLCSQL_COMPILE_RESPONSE pkg_compile_response;
  DB_VALUE scode_body_value;
  MOP pkg_mop;

  err = NO_ERROR;

  // read and keep the body code, if any
  err = jsp_get_pkg_scode_body (unique_name, &scode_body_value);
  if (err != NO_ERROR)
    {
      goto error_exit;
    }

  // does it already exist?
  pkg_mop = jsp_find_pkg (unique_name, DB_AUTH_NONE);
  if (pkg_mop)
    {
      if (statement->info.pkg.or_replace)
	{
	  // drop existing package (spec and body)
	  err = jsp_drop_pkg (unique_name, pkg_mop, owner_mop);
	  if (err != NO_ERROR)
	    {
	      pr_clear_value (&scode_body_value);
	      goto error_exit;
	    }
	}
      else
	{
	  err = ER_PKG_ALREADY_EXIST;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, unique_name);
	  pr_clear_value (&scode_body_value);
	  goto error_exit;
	}
    }

  // send package compile request to PL server and receive the response

  assert (statement->sql_user_text && statement->sql_user_text_len);
  pkg_compile_request.type = PLCSQL_COMPILE_TYPE_PKG_SPEC;
  pkg_compile_request.code.assign (statement->sql_user_text, statement->sql_user_text_len);
  pkg_compile_request.owner.assign (owner_name);
  if (!DB_IS_NULL (&scode_body_value))
    {
      pkg_compile_request.body_code.assign (db_get_string (&scode_body_value));
      pr_clear_value (&scode_body_value);
    }

  au_perform_push_user (owner_mop);
  err = plcsql_compile (pkg_compile_request, pkg_compile_response);
  au_perform_pop_user ();

  if (err == NO_ERROR && pkg_compile_response.err_code == NO_ERROR)
    {
      err = sp_add_pkg_and_related (unique_name, owner_name, owner_mop,
				    pkg_compile_request.code.data(), pkg_compile_request.body_code.data(), comment, pkg_compile_response);
      if (err != NO_ERROR)
	{
	  goto error_exit;
	}
    }
  else
    {
      err = ER_PKG_COMPILE_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, pkg_compile_response.err_msg.c_str ());
      pt_record_error (parser, parser->statement_number, pkg_compile_response.err_line, pkg_compile_response.err_column,
		       er_msg (), NULL);
      goto error_exit;
    }

  return NO_ERROR;

error_exit:
  return err;
}

/*
 * jsp_create_package
 *   return: if failed return error code else execute jsp_add_stored_procedure
 *           function
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

#define SAVEPOINT_CREATE_PACKAGE "CREATEPACKAGE"

int
jsp_create_package (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  int err;
  const char *unique_name;
  char owner_name[DB_MAX_USER_LENGTH];
  MOP owner;

  err = NO_ERROR;
  owner_name[0] = '\0';

  CHECK_MODIFICATION_ERROR ();
  assert (!prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT));  // unreachable here if it is true

  // get unique_name, owner_name, and owner
  {
    unique_name = PT_NODE_PKG_NAME (statement);
    if (sm_qualifier_name (unique_name, owner_name, DB_MAX_USER_LENGTH) == NULL)
      {
	ASSERT_ERROR ();
	goto error_exit;
      }
    assert (owner_name[0]);     // package name has its owner name at this point of execution
    owner = db_find_user (owner_name);
    if (owner == NULL)
      {
	// for safeguard: it is already checked in pt_check_create_stored_procedure ()
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER_NAME, 1, owner_name);
	goto error_exit;
      }
  }

  err = tran_system_savepoint (SAVEPOINT_CREATE_PACKAGE);
  if (err != NO_ERROR)
    {
      goto error_exit;
    }

  if (statement->info.pkg.for_body)
    {
      err = jsp_create_pkg_body (parser, statement, unique_name, owner_name, owner);
      if (err != NO_ERROR)
	{
	  goto rollback;
	}
    }
  else
    {
      const char *comment = PT_NODE_PKG_COMMENT (statement);
      err = jsp_create_pkg_spec (parser, statement, unique_name, owner_name, owner, comment);
      if (err != NO_ERROR)
	{
	  goto rollback;
	}
    }

  return NO_ERROR;

rollback:
  tran_abort_upto_system_savepoint (SAVEPOINT_CREATE_PACKAGE);

error_exit:
  return (err == NO_ERROR) ? er_errid () : err;
}

/*
 * jsp_alter_package
 *   return: if failed return error code else execute jsp_add_stored_procedure
 *           function
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

int
jsp_alter_package (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  // TODO package
  return NO_ERROR;
}

/*
 * jsp_drop_package
 *   return: if failed return error code else execute jsp_add_stored_procedure
 *           function
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

#define SAVEPOINT_DROP_PACKAGE "DROPPACKAGE"

int
jsp_drop_package (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  int err = NO_ERROR;
  char owner_name[DB_MAX_USER_LENGTH];
  MOP owner_mop, pkg_mop;

  CHECK_MODIFICATION_ERROR ();
  assert (!prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT));  // unreachable here if it is true

  // validate all packages before setting a savepoint
  for (PT_NODE *name_node = statement->info.pkg.name; name_node != NULL; name_node = name_node->next)
    {
      const char *unique_name = name_node->info.name.original;

      // check for duplicate names in the list
      for (PT_NODE *prev_node = statement->info.pkg.name; prev_node != name_node; prev_node = prev_node->next)
	{
	  if (intl_identifier_casecmp (unique_name, prev_node->info.name.original) == 0)
	    {
	      err = ER_PKG_DUPLICATE_NAME;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, unique_name);
	      goto error_exit;
	    }
	}

      owner_name[0] = '\0';

      if (sm_qualifier_name (unique_name, owner_name, DB_MAX_USER_LENGTH) == NULL)
	{
	  ASSERT_ERROR_AND_SET (err);
	  goto error_exit;
	}
      assert (owner_name[0]);     // package name has its owner name at this point of execution

      owner_mop = db_find_user (owner_name);
      if (owner_mop == NULL)
	{
	  // for safeguard: it is already checked in pt_check_create_stored_procedure ()
	  err = ER_AU_INVALID_USER_NAME;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, owner_name);
	  goto error_exit;
	}

      // only the owner or a dba group member can drop it
      if (!ws_is_same_object (owner_mop, Au_user) && !au_is_dba_group_member (Au_user))
	{
	  err = ER_PKG_DDL_NOT_ALLOWED_PRIVILEGES;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, "drop");
	  goto error_exit;
	}

      // check if it is system generated
      pkg_mop = jsp_find_pkg (unique_name, DB_AUTH_NONE);
      if (pkg_mop)
	{
	  DB_VALUE value;

	  err = db_get (pkg_mop, PKG_ATTR_FLAGS, &value);
	  if (err != NO_ERROR)
	    {
	      goto error_exit;
	    }

	  int flags = db_get_int (&value);
	  if (flags & PKG_FLAGS_SYSTEM_GENERATED)
	    {
	      err = ER_PKG_DROP_NOT_ALLOWED_SYSTEM_GENERATED;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
	      goto error_exit;
	    }
	}
      else
	{
	  if (!statement->info.pkg.for_body)    // if it is dropping the spec
	    {
	      err = ER_PKG_NOT_EXIST;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, unique_name);
	      goto error_exit;
	    }
	}
    }

  err = tran_system_savepoint (SAVEPOINT_DROP_PACKAGE);
  if (err != NO_ERROR)
    {
      goto error_exit;
    }

  // drop all packages; rollback everything if any one fails
  for (PT_NODE *name_node = statement->info.pkg.name; name_node != NULL; name_node = name_node->next)
    {
      const char *unique_name = name_node->info.name.original;

      owner_name[0] = '\0';
      sm_qualifier_name (unique_name, owner_name, DB_MAX_USER_LENGTH);
      owner_mop = db_find_user (owner_name);

      if (statement->info.pkg.for_body)
	{
	  err = jsp_drop_pkg_body (parser, unique_name, owner_name, owner_mop);
	}
      else
	{
	  pkg_mop = jsp_find_pkg (unique_name, DB_AUTH_NONE);
	  err = jsp_drop_pkg (unique_name, pkg_mop, owner_mop);
	}

      if (err != NO_ERROR)
	{
	  goto rollback;
	}
    }

  return NO_ERROR;

rollback:
  tran_abort_upto_system_savepoint (SAVEPOINT_DROP_PACKAGE);

error_exit:
  return err;
}

/*
 * jsp_create_stored_procedure
 *   return: if failed return error code else execute jsp_add_stored_procedure
 *           function
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

#define SAVEPOINT_CREATE_STORED_PROC "CREATESTOREDPROC"

int
jsp_create_stored_procedure (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  const char *decl = NULL, *comment = NULL;
  char owner_name[DB_MAX_USER_LENGTH];
  owner_name[0] = '\0';

  PT_NODE *param_list, *p;
  PT_TYPE_ENUM ret_type = PT_TYPE_NONE;
  int lang;
  int err = NO_ERROR;
  bool has_savepoint = false;

  PLCSQL_COMPILE_REQUEST pl_sp_compile_request;
  PLCSQL_COMPILE_RESPONSE pl_sp_compile_response;

  SP_INFO sp_info;
  char *temp;
  DB_VALUE current_datetime;

  CHECK_MODIFICATION_ERROR ();
  assert (!prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT));    // unreachable here if it is true

  // check PL/CSQL's AUTHID with CURRENT_USER
  sp_info.directive = jsp_map_pt_to_sp_authid (PT_NODE_SP_AUTHID (statement));
  sp_info.lang = (SP_LANG_ENUM) PT_NODE_SP_LANG (statement);
  if (sp_info.directive == SP_DIRECTIVE_RIGHTS_CALLER && sp_info.lang == SP_LANG_PLCSQL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVOKERS_RIGHTS_NOT_SUPPORTED, 0);
      return er_errid ();
    }

  temp = jsp_check_stored_procedure_name (PT_NODE_SP_NAME (statement));
  sp_info.unique_name = temp;
  free (temp);
  if (sp_info.unique_name.empty ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_NAME, 0);
      return er_errid ();
    }

  sp_info.sp_name = sm_remove_qualifier_name (sp_info.unique_name.data ());
  if (sp_info.sp_name.empty ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_NAME, 0);
      return er_errid ();
    }

  sp_info.sp_type = jsp_map_pt_misc_to_sp_type (PT_NODE_SP_TYPE (statement));
  if (sp_info.sp_type == SP_TYPE_FUNCTION)
    {
      sp_info.return_type = pt_type_enum_to_db (statement->info.sp.ret_type);
      // check the deterministic_type of function
      sp_info.directive = jsp_map_pt_to_sp_dtrm_type (PT_NODE_SP_DETERMINISTIC_TYPE (statement), sp_info.directive);
    }
  else
    {
      sp_info.return_type = DB_TYPE_NULL;
    }

  // set rows for _db_stored_procedure_args
  int param_count = 0;
  param_list = PT_NODE_SP_ARGS (statement);
  for (p = param_list; p != NULL; p = p->next)
    {
      SP_ARG_INFO arg_info (sp_info.unique_name);

      arg_info.index_of = param_count++;
      arg_info.arg_name = PT_NODE_SP_ARG_NAME (p);
      arg_info.data_type = pt_type_enum_to_db (p->type_enum);
      arg_info.mode = jsp_map_pt_misc_to_sp_mode (p->info.sp_param.mode);

      // default value
      // coerciable is already checked in semantic_check
      PT_NODE *default_value = p->info.sp_param.default_value;
      if (default_value)
	{
	  bool is_null;
	  std::string default_value_str;
	  if (jsp_default_value_string (parser, default_value, is_null, default_value_str) == NO_ERROR)
	    {
	      if (!is_null)
		{
		  db_make_string (&arg_info.default_value, ws_copy_string (default_value_str.c_str ()));
		}
	      else
		{
		  db_make_null (&arg_info.default_value);
		}

	      arg_info.is_optional = true;
	    }
	  else
	    {
	      // MSGCAT_SEMANTIC_PREC_TOO_BIG
	      goto error_exit;
	    }
	}
      else
	{
	  db_make_null (&arg_info.default_value);
	  arg_info.is_optional = false; // explicitly
	}

      arg_info.comment = (char *) PT_NODE_SP_ARG_COMMENT (p);

      // check # of args constraint
      if (param_count > MAX_ARG_COUNT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TOO_MANY_ARG_COUNT, 1, sp_info.unique_name.data ());
	  goto error_exit;
	}

      sp_info.args.push_back (arg_info);
    }

  if (sm_qualifier_name (sp_info.unique_name.data (), owner_name, DB_MAX_USER_LENGTH) == NULL)
    {
      ASSERT_ERROR ();
      goto error_exit;
    }

  assert (owner_name[0]);       // package name has its owner name at this point of execution
  sp_info.owner = db_find_user (owner_name);
  if (sp_info.owner == NULL)
    {
      // for safeguard: it is already checked in pt_check_create_stored_procedure ()
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_INVALID_USER_NAME, 1, owner_name);
      goto error_exit;
    }

  if (sp_info.lang == SP_LANG_PLCSQL)
    {
      assert (statement->sql_user_text && statement->sql_user_text_len);
      pl_sp_compile_request.type = PLCSQL_COMPILE_TYPE_SP;
      pl_sp_compile_request.code.assign (statement->sql_user_text, statement->sql_user_text_len);
      pl_sp_compile_request.owner.assign ((owner_name[0] == '\0') ? au_get_current_user_name () : owner_name);

      // TODO: Only the owner's rights is supported for PL/CSQL
      au_perform_push_user (sp_info.owner);
      err = plcsql_compile (pl_sp_compile_request, pl_sp_compile_response);
      au_perform_pop_user ();

      if (err == NO_ERROR && pl_sp_compile_response.err_code == NO_ERROR)
	{
	  decl = pl_sp_compile_response.java_signature.c_str ();
	}
      else
	{
	  err = ER_SP_COMPILE_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_COMPILE_ERROR, 1, pl_sp_compile_response.err_msg.c_str ());
	  pt_record_error (parser, parser->statement_number, pl_sp_compile_response.err_line, pl_sp_compile_response.err_column,
			   er_msg (),
			   NULL);
	  goto error_exit;
	}
      sp_info.sql_data_access = (SP_SQL_DATA_ACCESS_TYPE) pl_sp_compile_response.sql_data_access;
    }
  else				/* SP_LANG_JAVA */
    {
      decl = (const char *) PT_NODE_SP_JAVA_METHOD (statement);
      sp_info.sql_data_access = SP_SQL_TYPE_UNKNOWN;
    }

  if (decl)
    {
      std::string target = decl;
      sp_split_target_signature (target, sp_info.target_class, sp_info.target_method);
    }

  sp_info.comment = (char *) PT_NODE_SP_COMMENT (statement);

  if (db_sys_datetime (&current_datetime) != NO_ERROR)
    {
      goto error_exit;
    }
  sp_info.created_time = *db_get_datetime (&current_datetime);
  sp_info.updated_time = *db_get_datetime (&current_datetime);

  /* check already exists */
  if (jsp_is_exist_stored_procedure (sp_info.unique_name.data ()))
    {
      if (statement->info.sp.or_replace)
	{
	  /* drop existing stored procedure */
	  err = tran_system_savepoint (SAVEPOINT_CREATE_STORED_PROC);
	  if (err != NO_ERROR)
	    {
	      return err;
	    }
	  has_savepoint = true;

	  err = jsp_drop_stored_procedure (sp_info.unique_name.data (), sp_info.sp_type);
	  if (err != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_ALREADY_EXIST, 1, sp_info.unique_name.data ());
	  goto error_exit;
	}
    }

  err = sp_add_stored_procedure (sp_info);
  if (err != NO_ERROR)
    {
      goto error_exit;
    }

  if (!pl_sp_compile_request.code.empty ())
    {
      assert (sp_info.lang == SP_LANG_PLCSQL);
      SP_CODE_INFO code_info;

      auto now = std::chrono::system_clock::now();
      auto converted_timep = std::chrono::system_clock::to_time_t (now);
      std::stringstream stm;
      stm << std::put_time (localtime (&converted_timep), "%Y%m%d%H%M%S");


      // CBRD-26513, CBRD-26514: rewrite the user code without the user name and the comment
      const char *rewritten_code;
      {
	int custom_print_saved = parser->custom_print;

	parser->custom_print |= PT_PRINT_NO_SPECIFIED_USER_NAME;
	parser->flag.is_unloading_plcsql_def = 1;
	rewritten_code = parser_print_tree (parser, statement);
	parser->flag.is_unloading_plcsql_def = 0;

	parser->custom_print = custom_print_saved;
      }

      code_info.name = sp_info.target_class;
      code_info.created_time = stm.str ();
      code_info.stype = SPSC_PLCSQL;
      code_info.scode.assign (rewritten_code, strlen (rewritten_code));
      code_info.otype = SPOC_JAVA_JAR;
      code_info.ocode = pl_sp_compile_response.compiled_code;
      code_info.owner = sp_info.owner;

      err = sp_add_stored_procedure_code (code_info);
      if (err != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  return NO_ERROR;

error_exit:
  if (has_savepoint)
    {
      tran_abort_upto_system_savepoint (SAVEPOINT_CREATE_STORED_PROC);
    }

  return (err == NO_ERROR) ? er_errid () : err;
}

/*
 * jsp_alter_stored_procedure
 *   return: if failed return error code else NO_ERROR
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

int
jsp_alter_stored_procedure (PARSER_CONTEXT *parser, PT_NODE *statement)
{
  int err = NO_ERROR, sp_recompile, save, lang;
  PT_NODE *sp_name = NULL, *sp_owner = NULL, *sp_comment = NULL;
  const char *name_str = NULL, *owner_str = NULL, *comment_str = NULL, *target_cls = NULL;
  char downcase_owner_name[DB_MAX_USER_LENGTH];
  downcase_owner_name[0] = '\0';
  PT_MISC_TYPE type;
  SP_TYPE_ENUM real_type;
  MOP sp_mop = NULL, new_owner_mop = NULL, owner_mop = NULL;
  DB_VALUE user_val, sp_type_val, sp_lang_val, target_cls_val;

  assert (statement != NULL);

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BLOCK_DDL_STMT, 0);
      return ER_BLOCK_DDL_STMT;
    }

  db_make_null (&user_val);
  db_make_null (&sp_type_val);
  db_make_null (&sp_lang_val);
  db_make_null (&target_cls_val);

  type = PT_NODE_SP_TYPE (statement);
  sp_name = statement->info.sp.name;
  assert (sp_name != NULL);

  sp_owner = statement->info.sp.owner;
  sp_recompile = statement->info.sp.recompile;
  sp_comment = statement->info.sp.comment;

  assert (sp_owner != NULL || sp_comment != NULL || sp_recompile);

  name_str = sp_name->info.name.original;
  assert (name_str != NULL);

  if (sp_owner != NULL)
    {
      owner_str = sp_owner->info.name.original;
      assert (owner_str != NULL);
    }

  comment_str = (char *) PT_NODE_SP_COMMENT (statement);

  AU_DISABLE (save);

  /* existence of sp */
  sp_mop = jsp_find_stored_procedure (name_str, DB_AUTH_SELECT);
  if (sp_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  /* check type */
  err = db_get (sp_mop, SP_ATTR_SP_TYPE, &sp_type_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  real_type = (SP_TYPE_ENUM) db_get_int (&sp_type_val);
  if (real_type != jsp_map_pt_misc_to_sp_type (type))
    {
      err = ER_SP_INVALID_TYPE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 2, name_str,
	      real_type == SP_TYPE_FUNCTION ? "FUNCTION" : "PROCEDURE");
      goto error;
    }

  /* change the owner */
  if (sp_owner != NULL)
    {
      /* existence of new owner */
      new_owner_mop = db_find_user (owner_str);
      if (new_owner_mop == NULL)
	{
	  err = ER_OBJ_OBJECT_NOT_FOUND;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, owner_str);
	  goto error;
	}

      err = au_change_sp_owner_with_transfer_privileges (parser, sp_mop, new_owner_mop);
      if (err != NO_ERROR)
	{
	  goto error;
	}
    }

  /* authentication */
  owner_mop = jsp_get_owner (sp_mop);
  if (owner_mop == NULL)
    {
      err = ER_FAILED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      goto error;
    }

  if (!ws_is_same_object (owner_mop, Au_user) && !au_is_dba_group_member (Au_user))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DDL_NOT_ALLOWED_PRIVILEGES, 1, "alter");
      err = er_errid ();
      goto error;
    }

  /* pl/csql compile */
  if (sp_recompile)
    {
      /* check lang */
      err = db_get (sp_mop, SP_ATTR_LANG, &sp_lang_val);
      if (err != NO_ERROR)
	{
	  goto error;
	}

      lang = db_get_int (&sp_lang_val);
      if (lang == SP_LANG_PLCSQL)
	{
	  err = db_get (sp_mop, SP_ATTR_TARGET_CLASS, &target_cls_val);
	  if (err != NO_ERROR)
	    {
	      goto error;
	    }
	  target_cls = db_get_string (&target_cls_val);

	  owner_str = sm_qualifier_name (name_str, downcase_owner_name, DB_MAX_USER_LENGTH);

	  err = alter_stored_procedure_code (parser, sp_mop, target_cls, owner_str, sp_recompile);
	  if (err != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  /* change the comment */
  if (sp_comment != NULL)
    {
      db_make_string (&user_val, comment_str);
      err = obj_set (sp_mop, SP_ATTR_COMMENT, &user_val);
      if (err != NO_ERROR)
	{
	  goto error;
	}
    }

  err = db_update_obj_timestamp (sp_mop);
  if (err != NO_ERROR)
    {
      goto error;
    }

error:

  pr_clear_value (&user_val);
  pr_clear_value (&sp_type_val);
  pr_clear_value (&sp_lang_val);
  pr_clear_value (&target_cls_val);
  AU_ENABLE (save);

  return err;
}

/*
 * jsp_map_pt_misc_to_sp_type
 *   return : stored procedure type ( Procedure or Function )
 *   pt_enum(in): Misc Types
 *
 * Note:
 */

static SP_TYPE_ENUM
jsp_map_pt_misc_to_sp_type (PT_MISC_TYPE pt_enum)
{
  if (pt_enum == PT_SP_PROCEDURE)
    {
      return SP_TYPE_PROCEDURE;
    }
  else
    {
      return SP_TYPE_FUNCTION;
    }
}

/*
 * jsp_map_pt_misc_to_sp_mode
 *   return : stored procedure mode ( input or output or inout )
 *   pt_enum(in) : Misc Types
 *
 * Note:
 */

static SP_MODE_ENUM
jsp_map_pt_misc_to_sp_mode (PT_MISC_TYPE pt_enum)
{
  if (pt_enum == PT_INPUT || pt_enum == PT_NOPUT)
    {
      return SP_MODE_IN;
    }
  else if (pt_enum == PT_OUTPUT)
    {
      return SP_MODE_OUT;
    }
  else
    {
      return SP_MODE_INOUT;
    }
}

static SP_DIRECTIVE_ENUM
jsp_map_pt_to_sp_authid (PT_MISC_TYPE pt_authid)
{
  assert (pt_authid == PT_AUTHID_OWNER || pt_authid == PT_AUTHID_CALLER);
  return (pt_authid == PT_AUTHID_OWNER ? SP_DIRECTIVE_ENUM::SP_DIRECTIVE_RIGHTS_OWNER :
	  SP_DIRECTIVE_ENUM::SP_DIRECTIVE_RIGHTS_CALLER);
}

static SP_DIRECTIVE_ENUM
jsp_map_pt_to_sp_dtrm_type (PT_MISC_TYPE pt_dtrm_type, SP_DIRECTIVE_ENUM directive)
{
  assert (pt_dtrm_type == PT_NOT_DETERMINISTIC || pt_dtrm_type == PT_DETERMINISTIC);

  if (pt_dtrm_type == PT_DETERMINISTIC)
    {
      directive = static_cast<SP_DIRECTIVE_ENUM> (static_cast<int> (directive) | static_cast<int>
		  (SP_DIRECTIVE_ENUM::SP_DIRECTIVE_DETERMINISTIC));
    }

  return directive;
}

/*
 * jsp_check_stored_procedure_name -
 *   return: java stored procedure name
 *   str(in) :
 *
 * Note: convert lowercase
 */

static char *
jsp_check_stored_procedure_name (const char *str)
{
  char buffer[SM_MAX_IDENTIFIER_LENGTH + 2];
  char tmp[SM_MAX_IDENTIFIER_LENGTH + 2];
  char *name = NULL;
  static const int dbms_output_len = strlen ("dbms_output.");


  if (strncasecmp (str, "dbms_output.", dbms_output_len) == 0)
    {
      sprintf (buffer, "public.dbms_output.%s",
	       sm_downcase_name (str + dbms_output_len, tmp, SM_MAX_IDENTIFIER_LENGTH));
    }
  else
    {
      sm_user_specified_name (str, buffer, SM_MAX_IDENTIFIER_LENGTH);
    }

  name = strdup (buffer);

  return name;
}

/*
 * jsp_drop_stored_procedure -
 *   return: Error code
 *   name(in): jsp name
 *   expected_type(in):
 *
 * Note:
 */

static int
jsp_drop_stored_procedure (const char *name, SP_TYPE_ENUM expected_type)
{
  MOP sp_mop, arg_mop, owner, save_user;
  DB_VALUE sp_type_val, arg_cnt_val, args_val, owner_val, generated_val, target_cls_val, lang_val, temp;
  SP_TYPE_ENUM real_type;
  std::string class_name;
  const char *target_cls;
  DB_SET *arg_set_p;
  int save, i, arg_cnt, lang;
  int err;
  char unique_name[DB_MAX_IDENTIFIER_LENGTH + 1];
  unique_name[0] = '\0';

  AU_DISABLE (save);

  db_make_null (&args_val);
  db_make_null (&owner_val);

  sp_mop = jsp_find_stored_procedure (name, DB_AUTH_SELECT);
  if (sp_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  err = db_get (sp_mop, SP_ATTR_OWNER, &owner_val);
  if (err != NO_ERROR)
    {
      goto error;
    }
  owner = db_get_object (&owner_val);

  if (!ws_is_same_object (owner, Au_user) && !au_is_dba_group_member (Au_user))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DDL_NOT_ALLOWED_PRIVILEGES, 1, "drop");
      err = er_errid ();
      goto error;
    }

  err = db_get (sp_mop, SP_ATTR_IS_SYSTEM_GENERATED, &generated_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  if (1 == db_get_int (&generated_val))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DROP_NOT_ALLOWED_SYSTEM_GENERATED, 0);
      err = er_errid ();
      goto error;
    }

  err = db_get (sp_mop, SP_ATTR_SP_TYPE, &sp_type_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  real_type = (SP_TYPE_ENUM) db_get_int (&sp_type_val);
  if (real_type != expected_type)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_TYPE, 2, name,
	      real_type == SP_TYPE_FUNCTION ? "FUNCTION" : "PROCEDURE");
      err = er_errid ();
      goto error;
    }

  // delete _db_stored_procedure_code

  err = db_get (sp_mop, SP_ATTR_LANG, &lang_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  lang = db_get_int (&lang_val);
  if (lang == SP_LANG_PLCSQL)
    {
      err = db_get (sp_mop, SP_ATTR_TARGET_CLASS, &target_cls_val);
      if (err != NO_ERROR)
	{
	  goto error;
	}

      target_cls = db_get_string (&target_cls_val);
      err = jsp_drop_stored_procedure_code (target_cls);
      if (err != NO_ERROR)
	{
	  goto error;
	}
    }

  err = db_get (sp_mop, SP_ATTR_ARG_COUNT, &arg_cnt_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  arg_cnt = db_get_int (&arg_cnt_val);

  err = db_get (sp_mop, SP_ATTR_ARGS, &args_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  arg_set_p = db_get_set (&args_val);

  for (i = 0; i < arg_cnt; i++)
    {
      set_get_element (arg_set_p, i, &temp);
      arg_mop = db_get_object (&temp);
      err = obj_delete (arg_mop);
      pr_clear_value (&temp);
      if (err != NO_ERROR)
	{
	  goto error;
	}
    }

  /* before deleting an object, all permissions are revoked. */
  if (jsp_get_unique_name (sp_mop, unique_name, DB_MAX_IDENTIFIER_LENGTH) == NULL)
    {
      assert (er_errid () != NO_ERROR);
    }

  save_user = Au_user;
  if (AU_SET_USER (owner) == NO_ERROR)
    {
      err = au_object_revoke_all_privileges (DB_OBJECT_PROCEDURE, owner, unique_name);
      if (err != NO_ERROR)
	{
	  AU_SET_USER (save_user);
	  goto error;
	}
    }

  AU_SET_USER (save_user);

  err = au_delete_auth_of_dropping_database_object (DB_OBJECT_PROCEDURE, name);
  if (err != NO_ERROR)
    {
      goto error;
    }

  err = obj_delete (sp_mop);

error:
  AU_ENABLE (save);

  pr_clear_value (&args_val);
  pr_clear_value (&owner_val);

  return err;
}

/*
 * jsp_drop_stored_procedure_code -
 *   return: Error code
 *   name(in): jsp name
 *
 * Note:
 */

static int
jsp_drop_stored_procedure_code (const char *name)
{
  MOP code_mop, owner;
  DB_VALUE owner_val, generated_val;
  int save;
  int err;

  AU_DISABLE (save);

  db_make_null (&owner_val);

  code_mop = jsp_find_stored_procedure_code (name);
  if (code_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  err = db_get (code_mop, SP_ATTR_OWNER, &owner_val);
  if (err != NO_ERROR)
    {
      goto error;
    }
  owner = db_get_object (&owner_val);

  if (!ws_is_same_object (owner, Au_user) && !au_is_dba_group_member (Au_user))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DDL_NOT_ALLOWED_PRIVILEGES, 1, "drop");
      err = er_errid ();
      goto error;
    }

  err = db_get (code_mop, SP_ATTR_IS_SYSTEM_GENERATED, &generated_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  if (!DB_IS_NULL (&generated_val) && 1 == db_get_int (&generated_val))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DROP_NOT_ALLOWED_SYSTEM_GENERATED, 0);
      err = er_errid ();
      goto error;
    }

  // TODO: If a unreloadable SP is deleted, mark a flag in PL server to block calling the deleted SP
  err = obj_delete (code_mop);

error:
  AU_ENABLE (save);

  pr_clear_value (&owner_val);

  return err;
}

/*
 * alter_stored_procedure_code -
 *   return: Error code
 *   name(in): jsp name
 *
 * Note:
 */

int
alter_stored_procedure_code (PARSER_CONTEXT *parser, MOP sp_mop, const char *name, const char *owner_str,
			     int sp_recompile)
{
  const char *scode = NULL, *decl = NULL;
  int scode_len, save, err;
  MOP code_mop;
  DB_VALUE scode_val, value;
  PLCSQL_COMPILE_REQUEST pl_sp_compile_request;
  PLCSQL_COMPILE_RESPONSE pl_sp_compile_response;
  SP_INFO sp_info;
  SP_CODE_INFO code_info;
  DB_OBJECT *object_p;
  DB_OTMPL *obt_p = NULL;

  AU_DISABLE (save);

  db_make_null (&scode_val);
  db_make_null (&value);

  code_mop = jsp_find_stored_procedure_code (name);
  if (code_mop == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  err = db_get (code_mop, SP_CODE_ATTR_SCODE, &scode_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  scode = db_get_string (&scode_val);
  scode_len = db_get_string_size (&scode_val);

  sp_info.owner = db_find_user (owner_str);

  assert (scode && scode_len);
  pl_sp_compile_request.type = PLCSQL_COMPILE_TYPE_SP;
  pl_sp_compile_request.code.assign (scode, scode_len);
  pl_sp_compile_request.owner.assign (owner_str);
  pr_clear_value (&scode_val);

  // TODO: Only the owner's rights is supported for PL/CSQL
  au_perform_push_user (sp_info.owner);
  err = plcsql_compile (pl_sp_compile_request, pl_sp_compile_response);
  au_perform_pop_user ();

  if (err == NO_ERROR && pl_sp_compile_response.err_code == NO_ERROR)
    {
      decl = pl_sp_compile_response.java_signature.c_str ();
    }
  else
    {
      err = ER_SP_COMPILE_ERROR;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_COMPILE_ERROR, 1, pl_sp_compile_response.err_msg.c_str ());
      pt_record_error (parser, parser->statement_number, pl_sp_compile_response.err_line, pl_sp_compile_response.err_column,
		       er_msg (),
		       NULL);
      goto error;
    }

  if (decl)
    {
      std::string target = decl;
      sp_split_target_signature (target, sp_info.target_class, sp_info.target_method);
    }

  code_info.name = sp_info.target_class;
  code_info.ocode = pl_sp_compile_response.compiled_code;

  if (sp_recompile == 1)
    {
      /* recompile */
      code_info.owner = NULL;
    }
  else
    {
      /* owner to */
      code_info.owner = sp_info.owner;
    }

  err = sp_edit_stored_procedure_code (code_mop, code_info);
  if (err != NO_ERROR)
    {
      goto error;
    }

  /* Update the target_class column in the _db_stored_procedure catalog. */
  obt_p = dbt_edit_object (sp_mop);
  if (obt_p == NULL)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }

  db_make_string (&value, sp_info.target_class.data ());
  err = dbt_put_internal (obt_p, SP_ATTR_TARGET_CLASS, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_string (&value, sp_info.target_method.data ());
  err = dbt_put_internal (obt_p, SP_ATTR_TARGET_METHOD, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  err = db_update_otmpl_timestamp (obt_p);
  if (err != NO_ERROR)
    {
      goto error;
    }

  object_p = dbt_finish_object (obt_p);
  if (!object_p)
    {
      ASSERT_ERROR_AND_SET (err);
      goto error;
    }
  obt_p = NULL;

  err = locator_flush_instance (object_p);
  if (err != NO_ERROR)
    {
      obj_delete (object_p);
      goto error;
    }

error:
  AU_ENABLE (save);

  pr_clear_value (&scode_val);
  pr_clear_value (&value);

  return err;
}

/*
 * jsp_set_prepare_call -
 *   return: none
 *
 * Note:
 */

void
jsp_set_prepare_call (void)
{
  int depth = tran_get_libcas_depth ();
  is_prepare_call[depth] = true;
}

/*
 * jsp_unset_prepare_call -
 *   return: none
 *
 * Note:
 */

void
jsp_unset_prepare_call (void)
{
  int depth = tran_get_libcas_depth ();
  is_prepare_call[depth] = false;
}

/*
 * jsp_is_prepare_call -
 *   return: bool
 *
 * Note:
 */

bool
jsp_is_prepare_call ()
{
  int depth = tran_get_libcas_depth ();
  return is_prepare_call[depth];
}

static int
jsp_check_overflow_args (PARSER_CONTEXT *parser, PT_NODE *node, int num_params, int num_args)
{
  if (num_args > num_params)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_PARAM_COUNT, 2, num_params, num_args);
      return er_errid ();
    }
  else if (num_args < num_params)
    {
      // there are trailing default arguments
      return num_params - num_args;
    }
  return 0;
}

/*
 * pt_to_method_arglist () - converts a parse expression tree list of
 *                           method call arguments to method argument array
 *   return: A NULL on error occurred
 *   parser(in):
 *   target(in):
 *   node_list(in): should be parse name nodes
 *   subquery_as_attr_list(in):
 */
static int *
pt_to_method_arglist (PARSER_CONTEXT *parser, PT_NODE *target, PT_NODE *node_list, PT_NODE *subquery_as_attr_list)
{
  int *arg_list = NULL;
  int i = 1;
  int num_args = pt_length_of_list (node_list) + 1;
  PT_NODE *node;

  arg_list = (int *) db_private_alloc (NULL, num_args);
  if (!arg_list)
    {
      return NULL;
    }

  if (subquery_as_attr_list != NULL)
    {
      if (target != NULL)
	{
	  /* the method call target is the first element in the array */
	  arg_list[0] = pt_find_attribute (parser, target, subquery_as_attr_list);
	  if (arg_list[0] == -1)
	    {
	      return NULL;
	    }
	}
      else
	{
	  i = 0;
	}

      for (node = node_list; node != NULL; node = node->next)
	{
	  arg_list[i] = pt_find_attribute (parser, node, subquery_as_attr_list);
	  if (arg_list[i] == -1)
	    {
	      return NULL;
	    }
	  i++;
	}
    }
  else
    {
      for (node = node_list; node != NULL; node = node->next)
	{
	  arg_list[i] = i;
	  i++;
	}
    }

  return arg_list;
}

/*
 * jsp_make_pl_signature () - converts a parse expression tree list of pl calls to pl_signature struct
 *   return: error_code
 *   parser(in):
 *   node_list(in): should be parse pl nodes
 *   subquery_as_attr_list(in):
 *   sig(out): pl_signature struct
 */
int
jsp_make_pl_signature (PARSER_CONTEXT *parser, PT_NODE *node, PT_NODE *subquery_as_attr_list, cubpl::pl_signature &sig)
{
  int save = 0;
  int error = NO_ERROR;
  char user_name_buffer [DB_MAX_USER_LENGTH + 1];
  DB_OBJECT *mop_p = NULL;

  assert (node);

  sp_entry entry (NUM_SP_ATTR);

  {
    PT_NODE *method_name_node = node->info.method_call.method_name;

    const char *name;
    if (PT_NAME_RESOLVED (method_name_node))
      {
	int custom_print_saved = parser->custom_print;
	parser->custom_print |= PT_SUPPRESS_QUOTES;
	parser->custom_print &= ~PT_PRINT_QUOTES;
	name = parser_print_tree (parser, method_name_node);
	parser->custom_print = custom_print_saved;
      }
    else
      {
	name = PT_NAME_ORIGINAL (method_name_node);
      }

    sig.name = db_private_strdup (NULL, name);
    if (PT_IS_METHOD (node))
      {
	sig.type = PT_IS_CLASS_METHOD (node) ? PL_TYPE_CLASS_METHOD : PL_TYPE_INSTANCE_METHOD;
      }
    else
      {
	mop_p = jsp_find_stored_procedure (name, DB_AUTH_EXECUTE);
	if (mop_p == NULL)
	  {
	    error = er_errid ();
	    assert (error != NO_ERROR);
	    goto exit;
	  }

	AU_DISABLE (save);
	entry.oid = *WS_OID (mop_p);

	for (int i = 0; i < NUM_SP_ATTR; i++)
	  {
	    error = obj_get (mop_p, sp_get_entry_name (i).data (), &entry.vals[i]);
	    if (error != NO_ERROR)
	      {
		goto exit;
	      }
	  }

	int lang = db_get_int (&entry.vals[INDEX_SP_ATTR_LANG]);
	sig.type = (lang == SP_LANG_PLCSQL) ? PL_TYPE_PLCSQL : PL_TYPE_JAVA_SP;

	/* semantic check */
	int directive = db_get_int (&entry.vals[INDEX_SP_ATTR_DIRECTIVE]);
	const char *auth_name = (! (directive & SP_DIRECTIVE_ENUM::SP_DIRECTIVE_RIGHTS_CALLER) ? jsp_get_owner_name (
					 name, user_name_buffer, DB_MAX_USER_LENGTH) : au_get_current_user_name ());

	int result_type = db_get_int (&entry.vals[INDEX_SP_ATTR_RETURN_TYPE]);
	error = jsp_check_return_type_supported ((DB_TYPE) result_type);
	if (error != NO_ERROR)
	  {
	    goto exit;
	  }

	// args
	int num_params = db_get_int (&entry.vals[INDEX_SP_ATTR_ARG_COUNT]);
	DB_SET *param_set = db_get_set (&entry.vals[INDEX_SP_ATTR_ARGS]);
	error = jsp_make_pl_args (parser, node, num_params, param_set, sig);
	if (error != NO_ERROR)
	  {
	    goto exit;
	  }

#if defined (CS_MODE)
	sig.auth = db_private_strdup (NULL, auth_name);
	if (directive & SP_DIRECTIVE_ENUM::SP_DIRECTIVE_DETERMINISTIC)
	  {
	    sig.is_deterministic = true;
	  }
	else
	  {
	    sig.is_deterministic = false;
	  }
#endif

	sig.result_type = result_type;
	if (! (directive & SP_DIRECTIVE_ENUM::SP_DIRECTIVE_RIGHTS_CALLER))
	  {
	    jsp_get_owner_name (name, user_name_buffer, DB_MAX_USER_LENGTH);
	    sig.auth = db_private_strndup (NULL, user_name_buffer, DB_MAX_USER_LENGTH);
	  }
	else
	  {
	    sig.auth = db_private_strdup (NULL, au_get_current_user_name ());
	  }
      }

    // make pl_ext
    if (PT_IS_METHOD (node))
      {
	PT_NODE *dt = node->info.method_call.on_call_target->data_type;
	/* beware of virtual classes */

	sig.ext.method.class_name = (dt->info.data_type.virt_object) ? (char *) db_get_class_name (
					    dt->info.data_type.virt_object) : (char *) dt->info.data_type.entity->info.name.original;
	sig.arg.set_arg_size (pt_length_of_list (node->info.method_call.arg_list) + 1);
	sig.ext.method.arg_pos = pt_to_method_arglist (parser, node->info.method_call.on_call_target,
				 node->info.method_call.arg_list, subquery_as_attr_list);
      }
    else
      {
	sig.ext.sp.target_class_name = db_private_strdup (NULL, db_get_string (&entry.vals[INDEX_SP_ATTR_TARGET_CLASS]));
	sig.ext.sp.target_method_name = db_private_strdup (NULL, db_get_string (&entry.vals[INDEX_SP_ATTR_TARGET_METHOD]));
	if (sig.ext.sp.target_class_name != NULL)
	  {
	    MOP code_mop = jsp_find_stored_procedure_code (sig.ext.sp.target_class_name);
	    if (code_mop)
	      {
		sig.ext.sp.code_oid = *WS_OID (code_mop);
	      }
	    else
	      {
		// Java SP
		sig.ext.sp.code_oid = OID_INITIALIZER;
	      }
	  }
      }
  }

exit:
  if (mop_p != NULL)
    {
      AU_ENABLE (save);
    }
  return error;
}

int
jsp_make_pl_args (PARSER_CONTEXT *parser, PT_NODE *node, int num_params, DB_SET *param_set, cubpl::pl_signature &sig)
{
  int error = NO_ERROR;

  DB_VALUE temp;
  db_make_null (&temp);

  {
    sig.arg.set_arg_size (num_params);

    // check default arguments
    int num_args = pt_length_of_list (node->info.method_call.arg_list);
    int num_trailing_default_args = jsp_check_overflow_args (parser, node, num_params, num_args);
    if (num_trailing_default_args < 0)
      {
	error = er_errid ();
	goto exit_on_error;
      }

    sp_entry entry (NUM_SP_ARG_ATTR);

    for (int i = 0; i < num_params; i++)
      {
	set_get_element (param_set, i, &temp);

	MOP arg_mop_p = db_get_object (&temp);
	if (arg_mop_p == NULL)
	  {
	    error = er_errid ();
	    assert (error != NO_ERROR);
	    goto exit_on_error;
	  }

	for (int i = 0; i < NUM_SP_ARG_ATTR; i++)
	  {
	    error = obj_get (arg_mop_p, sp_args_get_entry_name (i).data (), &entry.vals[i]);
	    if (error != NO_ERROR)
	      {
		goto exit_on_error;
	      }
	  }

	int arg_mode = db_get_int (&entry.vals[INDEX_SP_ARG_ATTR_MODE]);
	error = jsp_check_out_param_in_query (parser, node, arg_mode);
	if (error != NO_ERROR)
	  {
	    goto exit_on_error;
	  }

	int arg_type = db_get_int (&entry.vals[INDEX_SP_ARG_ATTR_DATA_TYPE]);
	error = jsp_check_param_type_supported ((DB_TYPE) arg_type, arg_mode);
	if (error != NO_ERROR)
	  {
	    goto exit_on_error;
	  }

	const char *default_value_str = NULL;
	int default_value_size = PL_ARG_DEFAULT_NONE;

	int num_required_args = num_params - num_trailing_default_args;

	if (i >= num_required_args)
	  {
	    int is_optional = db_get_int (&entry.vals[INDEX_SP_ARG_ATTR_IS_OPTIONAL]);
	    if (is_optional == 1)
	      {
		const DB_VALUE &default_val = entry.vals[INDEX_SP_ARG_ATTR_DEFAULT_VALUE];
		if (!DB_IS_NULL (&default_val))
		  {
		    default_value_size = db_get_string_size (&default_val); // null character
		    if (default_value_size > 0)
		      {
			default_value_str = db_get_string (&default_val);
		      }
		  }
		else
		  {
		    default_value_size = PL_ARG_DEFAULT_NULL; // special value when default value is *NULL*
		  }
	      }
	    else
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_PARAM_COUNT, 2, num_params, num_args);
		error = er_errid ();
		goto exit_on_error;
	      }
	  }

	sig.arg.arg_mode[i] = arg_mode;
	sig.arg.arg_type[i] = arg_type;

	sig.arg.arg_default_value_size[i] = default_value_size;
	if (default_value_str)
	  {
	    sig.arg.arg_default_value[i] = db_private_strndup (NULL, default_value_str, default_value_size);
	  }
      }
  }

exit_on_error:
  return error;
}

static int
jsp_check_execute_authorization (const MOP sp_obj, const DB_AUTH au_type)
{
  int error = NO_ERROR;
  MOP owner_mop = NULL;
  DB_VALUE owner;

  if (au_type != DB_AUTH_EXECUTE)
    {
      return NO_ERROR;
    }

  if (au_is_dba_group_member (Au_user))
    {
      return NO_ERROR;
    }

// check execute authorization (Au_user is granted by owner)
  if (au_check_procedure_authorization (sp_obj) == NO_ERROR)
    {
      return NO_ERROR;
    }

  return ER_FAILED;
}

PT_NODE *
jsp_get_default_expr_node_list (PARSER_CONTEXT *parser, cubpl::pl_signature &sig, bool *flag_si_datetime)
{
  PT_NODE *default_next_node_list = NULL;
  PT_NODE *default_next_node = NULL;
  for (int i = 0; i < sig.arg.arg_size; i++)
    {
      if (sig.arg.arg_default_value_size[i] == PL_ARG_DEFAULT_NULL)
	{
	  default_next_node = pt_make_string_value (parser, NULL);
	}
      else if (sig.arg.arg_default_value_size[i] == 0)
	{
	  default_next_node = pt_make_string_value (parser, "");
	}
      else if (sig.arg.arg_default_value_size[i] > 0)
	{
	  DB_DEFAULT_EXPR default_expr;
	  pt_get_default_expression_from_string (parser, sig.arg.arg_default_value[i], sig.arg.arg_default_value_size[i],
						 &default_expr);
	  if (flag_si_datetime && !*flag_si_datetime && (DB_IS_DEFAULT_DATETIME_EXPR (default_expr.default_expr_type)
	      || DB_IS_DEFAULT_UUID_TIMEBASE_EXPR (default_expr.default_expr_type)))
	    {
	      *flag_si_datetime = true;
	    }

	  if (default_expr.default_expr_type != DB_DEFAULT_NONE)
	    {
	      default_next_node = pt_make_default_value_tree_from_default_expr (parser, &default_expr);
	      if (default_next_node == NULL)
		{
		  PT_ERRORm (parser, NULL, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
		  return NULL;
		}
	    }
	  else
	    {
	      default_next_node = pt_make_string_value (parser, sig.arg.arg_default_value[i]);
	    }
	}

      if (default_next_node != NULL)
	{
	  default_next_node = pt_semantic_type (parser, default_next_node, NULL);
	  if (default_next_node == NULL)
	    {
	      return NULL;
	    }

	  default_next_node_list = parser_append_node (default_next_node, default_next_node_list);
	}
    }

  return default_next_node_list;
}
