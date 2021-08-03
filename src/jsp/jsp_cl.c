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
 * jsp_cl.c - Java Stored Procedure Client Module Source
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>
#if !defined(WINDOWS)
#include <sys/socket.h>
#else /* not WINDOWS */
#include <winsock2.h>
#endif /* not WINDOWS */

#include "authenticate.h"
#include "error_manager.h"
#include "memory_alloc.h"
#include "dbtype.h"
#include "parser.h"
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
#include "jsp_comm.h"

#include "jsp_struct.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

#if !defined(INADDR_NONE)
#define INADDR_NONE 0xffffffff
#endif /* !INADDR_NONE */

#define PT_NODE_SP_NAME(node) \
  ((node)->info.sp.name->info.name.original)

#define PT_NODE_SP_TYPE(node) \
  ((node)->info.sp.type)

#define PT_NODE_SP_RETURN_TYPE(node) \
  ((node)->info.sp.ret_type->info.name.original)

#define PT_NODE_SP_JAVA_METHOD(node) \
  ((node)->info.sp.java_method->info.value.data_value.str->bytes)

#define PT_NODE_SP_COMMENT(node) \
  (((node)->info.sp.comment == NULL) ? NULL : \
   (node)->info.sp.comment->info.value.data_value.str->bytes)

#define PT_NODE_SP_ARG_COMMENT(node) \
  (((node)->info.sp_param.comment == NULL) ? NULL : \
   (node)->info.sp_param.comment->info.value.data_value.str->bytes)

#define MAX_CALL_COUNT  16
#define SAVEPOINT_ADD_STORED_PROC "ADDSTOREDPROC"
#define SAVEPOINT_CREATE_STORED_PROC "CREATESTOREDPROC"

static SOCKET sock_fds[MAX_CALL_COUNT] = { INVALID_SOCKET };

static int server_port = -1;
static int call_cnt = 0;
static bool is_prepare_call[MAX_CALL_COUNT];

#if defined(WINDOWS)
static FARPROC jsp_old_hook = NULL;
#endif /* WINDOWS */

static SP_TYPE_ENUM jsp_map_pt_misc_to_sp_type (PT_MISC_TYPE pt_enum);
static int jsp_map_pt_misc_to_sp_mode (PT_MISC_TYPE pt_enum);
static PT_MISC_TYPE jsp_map_sp_type_to_pt_misc (SP_TYPE_ENUM sp_type);

static int jsp_get_argument_count (const SP_ARGS * sp_args);
static int jsp_add_stored_procedure_argument (MOP * mop_p, const char *sp_name, const char *arg_name, int index,
					      PT_TYPE_ENUM data_type, PT_MISC_TYPE mode, const char *arg_comment);
static char *jsp_check_stored_procedure_name (const char *str);
static int jsp_add_stored_procedure (const char *name, const PT_MISC_TYPE type, const PT_TYPE_ENUM ret_type,
				     PT_NODE * param_list, const char *java_method, const char *comment);
static int drop_stored_procedure (const char *name, PT_MISC_TYPE expected_type);
static int jsp_get_value_size (DB_VALUE * value);
static int jsp_get_argument_size (DB_ARG_LIST * args);

extern int libcas_main (SOCKET fd);
extern void *libcas_get_db_result_set (int h_id);
extern void libcas_srv_handle_free (int h_id);

static int jsp_send_call_request (const SOCKET sockfd, const SP_ARGS * sp_args);
static int jsp_alloc_response (const SOCKET sockfd, cubmem::extensible_block & blk);
static int jsp_receive_response (const SOCKET sockfd, const SP_ARGS * sp_args);
static int jsp_receive_result (cubmem::extensible_block & blk, const SP_ARGS * sp_args);
static int jsp_receive_error (cubmem::extensible_block & blk, const SP_ARGS * sp_args);

static int jsp_execute_stored_procedure (const SP_ARGS * args);
static int jsp_do_call_stored_procedure (DB_VALUE * returnval, DB_ARG_LIST * args, const char *name);

static int jsp_make_method_sig_list (PARSER_CONTEXT * parser, PT_NODE * node_list, method_sig_list & sig_list);
static int *jsp_make_method_arglist (PARSER_CONTEXT * parser, PT_NODE * node_list);

extern bool ssl_client;

/*
 * jsp_init - Initialize Java Stored Procedure
 *   return: none
 *
 * Note:
 */

void
jsp_init (void)
{
  int i;
  call_cnt = 0;

  for (i = 0; i < MAX_CALL_COUNT; i++)
    {
      sock_fds[i] = INVALID_SOCKET;
      is_prepare_call[i] = false;
    }

#if defined(WINDOWS)
  windows_socket_startup (jsp_old_hook);
#endif /* WINDOWS */
}

/*
 * jsp_close_connection - Java Stored Procedure Close Connection
 *   return: none
 *
 * Note:
 */

void
jsp_close_connection (void)
{
  if (!IS_INVALID_SOCKET (sock_fds[0]))
    {
      jsp_disconnect_server (sock_fds[0]);
      sock_fds[0] = INVALID_SOCKET;
    }
}

/*
 * jsp_find_stored_procedure
 *   return: MOP
 *   name(in): find java stored procedure name
 *
 * Note:
 */

MOP
jsp_find_stored_procedure (const char *name)
{
  MOP mop = NULL;
  DB_VALUE value;
  int save;
  char *checked_name;

  if (!name)
    {
      return NULL;
    }

  AU_DISABLE (save);

  checked_name = jsp_check_stored_procedure_name (name);
  db_make_string (&value, checked_name);
  mop = db_find_unique (db_find_class (SP_CLASS_NAME), SP_ATTR_NAME, &value);

  if (er_errid () == ER_OBJ_OBJECT_NOT_FOUND)
    {
      er_clear ();
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_EXIST, 1, name);
    }

  free_and_init (checked_name);
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
  MOP mop = NULL;

  mop = jsp_find_stored_procedure (name);
  er_clear ();

  return mop != NULL;
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

  mop_p = jsp_find_stored_procedure (name);
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

  mop_p = jsp_find_stored_procedure (name);
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

/*
 * jsp_call_stored_procedure - call java stored procedure
 *   return: call jsp failed return error code
 *   parser(in/out): parser environment
 *   statement(in): a statement node
 *
 * Note:
 */

int
jsp_call_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  const char *into_label, *proc;
  int error = NO_ERROR;
  DB_VALUE *ins_value, ret_value;
  DB_ARG_LIST *value_list = 0, *vl, **next_value_list;
  PT_NODE *vc, *into, *method;

  if (!statement || !(method = statement->info.method_call.method_name) || method->node_type != PT_NAME
      || !(proc = method->info.name.original))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return er_errid ();
    }

  db_make_null (&ret_value);

  /* Build an argument list. */
  next_value_list = &value_list;
  vc = statement->info.method_call.arg_list;
  while (vc)
    {
      DB_VALUE *db_value;
      bool to_break = false;

      *next_value_list = (DB_ARG_LIST *) calloc (1, sizeof (DB_ARG_LIST));
      if (*next_value_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_ARG_LIST));
	  return er_errid ();
	}

      (*next_value_list)->next = (DB_ARG_LIST *) 0;

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
	  db_value = (DB_VALUE *) calloc (1, sizeof (DB_VALUE));
	  if (db_value == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE));
	      return er_errid ();
	    }

	  /* must call pt_evaluate_tree */
	  pt_evaluate_tree (parser, vc, db_value, 1);
	  if (pt_has_error (parser))
	    {
	      /* to maintain the list to free all the allocated */
	      to_break = true;
	    }
	}

      (*next_value_list)->val = db_value;

      next_value_list = &(*next_value_list)->next;
      vc = vc->next;

      if (to_break)
	{
	  break;
	}
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error = er_errid ();
    }
  else
    {
      /* call sp */
      error = jsp_do_call_stored_procedure (&ret_value, value_list, proc);
    }

  vc = statement->info.method_call.arg_list;
  while (value_list && vc)
    {
      vl = value_list->next;
      if (!PT_IS_CONST (vc))
	{
	  db_value_clear (value_list->val);
	  free_and_init (value_list->val);
	}
      free_and_init (value_list);
      value_list = vl;
      vc = vc->next;
    }

  if (error == NO_ERROR)
    {
      /* Save the method result. */
      statement->etc = (void *) db_value_copy (&ret_value);
      into = statement->info.method_call.to_return_var;

      if (into != NULL && into->node_type == PT_NAME && (into_label = into->info.name.original) != NULL)
	{
	  /* create another DB_VALUE of the new instance for the label_table */
	  ins_value = db_value_copy (&ret_value);
	  error = pt_associate_label_with_value_check_reference (into_label, ins_value);
	}
    }

  db_value_clear (&ret_value);
  return error;
}

int
jsp_call_stored_procedure_ng (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *method;
  const char *method_name;
  if (!statement || !(method = statement->info.method_call.method_name) || method->node_type != PT_NAME
      || !(method_name = method->info.name.original))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return er_errid ();
    }

  DB_VALUE ret_value;
  db_make_null (&ret_value);

  std::vector < DB_VALUE * >args;
  PT_NODE *vc = statement->info.method_call.arg_list;
  while (vc)
    {
      DB_VALUE *db_value;
      bool to_break = false;

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
	  db_value = (DB_VALUE *) calloc (1, sizeof (DB_VALUE));
	  if (db_value == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE));
	      return er_errid ();
	    }

	  /* must call pt_evaluate_tree */
	  pt_evaluate_tree (parser, vc, db_value, 1);
	  if (pt_has_error (parser))
	    {
	      /* to maintain the list to free all the allocated */
	      to_break = true;
	    }
	}

      args.push_back (db_value);
      vc = vc->next;

      if (to_break)
	{
	  break;
	}
    }

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error = er_errid ();
    }
  else
    {
      /* call sp */
      method_sig_list sig_list;

      sig_list.method_sig = nullptr;
      sig_list.num_methods = 0;

      error = jsp_make_method_sig_list (parser, statement, sig_list);
      if (error == NO_ERROR)
	{
	  error = method_invoke_fold_constants (sig_list, args, ret_value);
	}
      sig_list.freemem ();
      // error = jsp_do_call_stored_procedure (&ret_value, value_list, proc);
    }

  vc = statement->info.method_call.arg_list;
  for (int i = 0; i < args.size () && vc; i++)
    {
      if (!PT_IS_CONST (vc))
	{
	  db_value_clear (args[i]);
	  free_and_init (args[i]);
	}
      vc = vc->next;
    }

  vc = statement->info.method_call.arg_list;
  if (error == NO_ERROR)
    {
      /* Save the method result. */
      statement->etc = (void *) db_value_copy (&ret_value);
      PT_NODE *into = statement->info.method_call.to_return_var;

      const char *into_label;
      if (into != NULL && into->node_type == PT_NAME && (into_label = into->info.name.original) != NULL)
	{
	  /* create another DB_VALUE of the new instance for the label_table */
	  DB_VALUE *ins_value = db_value_copy (&ret_value);
	  error = pt_associate_label_with_value_check_reference (into_label, ins_value);
	}
    }

  db_value_clear (&ret_value);
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
jsp_drop_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement)
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

      err = drop_stored_procedure (name, type);
      if (err != NO_ERROR)
	{
	  break;
	}
    }

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

int
jsp_create_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  const char *name, *java_method, *comment = NULL;

  PT_MISC_TYPE type;
  PT_NODE *param_list, *p;
  PT_TYPE_ENUM ret_type = PT_TYPE_NONE;
  int param_count;
  int err = NO_ERROR;
  bool has_savepoint = false;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BLOCK_DDL_STMT, 0);
      return ER_BLOCK_DDL_STMT;
    }

  name = (char *) PT_NODE_SP_NAME (statement);
  if (name == NULL || name[0] == '\0')
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_NAME, 0);
      return er_errid ();
    }

  type = PT_NODE_SP_TYPE (statement);
  if (type == PT_SP_FUNCTION)
    {
      ret_type = statement->info.sp.ret_type;
    }

  java_method = (char *) PT_NODE_SP_JAVA_METHOD (statement);
  param_list = statement->info.sp.param_list;

  if (jsp_is_exist_stored_procedure (name))
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

	  err = drop_stored_procedure (name, type);
	  if (err != NO_ERROR)
	    {
	      goto error_exit;
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_ALREADY_EXIST, 1, name);
	  return er_errid ();
	}
    }

  for (p = param_list, param_count = 0; p != NULL; p = p->next, param_count++)
    {
      ;
    }

  if (param_count > MAX_ARG_COUNT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TOO_MANY_ARG_COUNT, 1, name);
      goto error_exit;
    }

  comment = (char *) PT_NODE_SP_COMMENT (statement);

  err = jsp_add_stored_procedure (name, type, ret_type, param_list, java_method, comment);
  if (err != NO_ERROR)
    {
      goto error_exit;
    }
  return NO_ERROR;

error_exit:
  if (has_savepoint)
    {
      tran_abort_upto_system_savepoint (SAVEPOINT_CREATE_STORED_PROC);
    }
  return (er_errid () != NO_ERROR) ? er_errid () : ER_FAILED;
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
jsp_alter_stored_procedure (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;
  PT_NODE *sp_name = NULL, *sp_owner = NULL, *sp_comment = NULL;
  const char *name_str = NULL, *owner_str = NULL, *comment_str = NULL;
  PT_MISC_TYPE type;
  SP_TYPE_ENUM real_type;
  MOP sp_mop = NULL, new_owner = NULL;
  DB_VALUE user_val, sp_type_val;
  int save;

  assert (statement != NULL);

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BLOCK_DDL_STMT, 0);
      return ER_BLOCK_DDL_STMT;
    }

  db_make_null (&user_val);

  type = PT_NODE_SP_TYPE (statement);
  sp_name = statement->info.sp.name;
  assert (sp_name != NULL);

  sp_owner = statement->info.sp.owner;
  sp_comment = statement->info.sp.comment;
  assert (sp_owner != NULL || sp_comment != NULL);

  name_str = sp_name->info.name.original;
  assert (name_str != NULL);

  if (sp_owner != NULL)
    {
      owner_str = sp_owner->info.name.original;
      assert (owner_str != NULL);
    }

  comment_str = (char *) PT_NODE_SP_COMMENT (statement);

  AU_DISABLE (save);

  /* authentication */
  if (!au_is_dba_group_member (Au_user))
    {
      err = ER_AU_DBA_ONLY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, "change stored procedure owner");
      goto error;
    }

  /* existence of sp */
  sp_mop = jsp_find_stored_procedure (name_str);
  if (sp_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  /* existence of new owner */
  if (sp_owner != NULL)
    {
      new_owner = db_find_user (owner_str);
      if (new_owner == NULL)
	{
	  err = ER_OBJ_OBJECT_NOT_FOUND;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 1, owner_str);
	  goto error;
	}
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
      db_make_object (&user_val, new_owner);
      err = obj_set (sp_mop, SP_ATTR_OWNER, &user_val);
      if (err < 0)
	{
	  goto error;
	}
      pr_clear_value (&user_val);
    }

  /* change the comment */
  if (sp_comment != NULL)
    {
      db_make_string (&user_val, comment_str);
      err = obj_set (sp_mop, SP_ATTR_COMMENT, &user_val);
      if (err < 0)
	{
	  goto error;
	}
      pr_clear_value (&user_val);
    }

error:

  pr_clear_value (&user_val);
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

static int
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

/*
 * jsp_get_argument_count
 *   return:  count element from argument list
 *   sp_args(in) : argument list
 *
 * Note:
 */

static int
jsp_get_argument_count (const SP_ARGS * sp_args)
{
  int count = 0;
  DB_ARG_LIST *p;

  for (p = sp_args->args; p != NULL; p = p->next)
    {
      count++;
    }

  return count;
}

/*
 * jsp_add_stored_procedure_argument
 *   return: Error Code
 *   mop_p(in/out) :
 *   sp_name(in) :
 *   arg_name(in) :
 *   index(in) :
 *   data_type(in) :
 *   mode(in) :
 *   arg_comment(in):
 *
 * Note:
 */

static int
jsp_add_stored_procedure_argument (MOP * mop_p, const char *sp_name, const char *arg_name, int index,
				   PT_TYPE_ENUM data_type, PT_MISC_TYPE mode, const char *arg_comment)
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

  db_make_string (&value, sp_name);
  err = dbt_put_internal (obt_p, SP_ATTR_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_string (&value, arg_name);
  err = dbt_put_internal (obt_p, SP_ATTR_ARG_NAME, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, index);
  err = dbt_put_internal (obt_p, SP_ATTR_INDEX_OF_NAME, &value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, pt_type_enum_to_db (data_type));
  err = dbt_put_internal (obt_p, SP_ATTR_DATA_TYPE, &value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, jsp_map_pt_misc_to_sp_mode (mode));
  err = dbt_put_internal (obt_p, SP_ATTR_MODE, &value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_string (&value, arg_comment);
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
  char *name = NULL;

  sm_downcase_name (str, buffer, SM_MAX_IDENTIFIER_LENGTH);
  name = strdup (buffer);

  return name;
}

/*
 * jsp_add_stored_procedure -
 *   return: Error ID
 *   name(in): jsp name
 *   type(in): type
 *   ret_type(in): return type
 *   param_list(in): parameter list
 *   java_method(in):
 *   comment(in):
 *
 * Note:
 */

static int
jsp_add_stored_procedure (const char *name, const PT_MISC_TYPE type, const PT_TYPE_ENUM return_type,
			  PT_NODE * param_list, const char *java_method, const char *comment)
{
  DB_OBJECT *classobj_p, *object_p;
  DB_OTMPL *obt_p = NULL;
  DB_VALUE value, v;
  DB_SET *param = NULL;
  int i, save;
  int err;
  PT_NODE *node_p;
  PT_NAME_INFO name_info;
  bool has_savepoint = false;
  char *checked_name;
  const char *arg_comment;

  if (java_method == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVAILD_JAVA_METHOD, 0);
      return er_errid ();
    }

  AU_DISABLE (save);

  checked_name = jsp_check_stored_procedure_name (name);

  classobj_p = db_find_class (SP_CLASS_NAME);

  if (classobj_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  err = tran_system_savepoint (SAVEPOINT_ADD_STORED_PROC);
  if (err != NO_ERROR)
    {
      goto error;
    }
  has_savepoint = true;

  obt_p = dbt_create_object_internal (classobj_p);
  if (!obt_p)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  db_make_string (&value, checked_name);
  err = dbt_put_internal (obt_p, SP_ATTR_NAME, &value);
  pr_clear_value (&value);

  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, jsp_map_pt_misc_to_sp_type (type));
  err = dbt_put_internal (obt_p, SP_ATTR_SP_TYPE, &value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, pt_type_enum_to_db (return_type));
  err = dbt_put_internal (obt_p, SP_ATTR_RETURN_TYPE, &value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  param = set_create_sequence (0);
  if (param == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  for (node_p = param_list, i = 0; node_p != NULL; node_p = node_p->next)
    {
      MOP mop = NULL;

      if (node_p->type_enum == PT_TYPE_RESULTSET && node_p->info.sp_param.mode != PT_OUTPUT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_INPUT_RESULTSET, 0);
	  err = er_errid ();
	  goto error;
	}
      name_info = node_p->info.sp_param.name->info.name;

      arg_comment = (char *) PT_NODE_SP_ARG_COMMENT (node_p);

      err =
	jsp_add_stored_procedure_argument (&mop, checked_name, name_info.original, i, node_p->type_enum,
					   node_p->info.sp_param.mode, arg_comment);
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

  db_make_int (&value, i);
  err = dbt_put_internal (obt_p, SP_ATTR_ARG_COUNT, &value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_int (&value, SP_LANG_JAVA);
  err = dbt_put_internal (obt_p, SP_ATTR_LANG, &value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_string (&value, java_method);
  err = dbt_put_internal (obt_p, SP_ATTR_TARGET, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_object (&value, Au_user);
  err = dbt_put_internal (obt_p, SP_ATTR_OWNER, &value);
  pr_clear_value (&value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  db_make_string (&value, comment);
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

  free_and_init (checked_name);
  AU_ENABLE (save);
  return NO_ERROR;

error:
  if (param)
    set_free (param);

  if (obt_p)
    {
      dbt_abort_object (obt_p);
    }

  if (has_savepoint)
    {
      tran_abort_upto_system_savepoint (SAVEPOINT_ADD_STORED_PROC);
    }

  free_and_init (checked_name);
  AU_ENABLE (save);

  return err;
}

/*
 * drop_stored_procedure -
 *   return: Error code
 *   name(in): jsp name
 *   expected_type(in):
 *
 * Note:
 */

static int
drop_stored_procedure (const char *name, PT_MISC_TYPE expected_type)
{
  MOP sp_mop, arg_mop, owner;
  DB_VALUE sp_type_val, arg_cnt_val, args_val, owner_val, temp;
  SP_TYPE_ENUM real_type;
  DB_SET *arg_set_p;
  int save, i, arg_cnt;
  int err;

  AU_DISABLE (save);

  db_make_null (&args_val);
  db_make_null (&owner_val);

  sp_mop = jsp_find_stored_procedure (name);
  if (sp_mop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_DROP_NOT_ALLOWED, 0);
      err = er_errid ();
      goto error;
    }

  err = db_get (sp_mop, SP_ATTR_SP_TYPE, &sp_type_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  real_type = (SP_TYPE_ENUM) db_get_int (&sp_type_val);
  if (real_type != jsp_map_pt_misc_to_sp_type (expected_type))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_TYPE, 2, name,
	      real_type == SP_TYPE_FUNCTION ? "FUNCTION" : "PROCEDURE");

      err = er_errid ();
      goto error;
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

  err = obj_delete (sp_mop);

error:
  AU_ENABLE (save);

  pr_clear_value (&args_val);
  pr_clear_value (&owner_val);

  return err;
}

/*
 * jsp_get_value_size -
 *   return: return value size
 *   value(in): input value
 *
 * Note:
 */

static int
jsp_get_value_size (DB_VALUE * value)
{
  char str_buf[NUMERIC_MAX_STRING_SIZE];
  int type, size = 0;

  type = DB_VALUE_TYPE (value);
  switch (type)
    {
    case DB_TYPE_INTEGER:
    case DB_TYPE_SHORT:
      size = sizeof (int);
      break;

    case DB_TYPE_BIGINT:
      size = sizeof (DB_BIGINT);
      break;

    case DB_TYPE_FLOAT:
      size = sizeof (float);	/* need machine independent code */
      break;

    case DB_TYPE_DOUBLE:
    case DB_TYPE_MONETARY:
      size = sizeof (double);	/* need machine independent code */
      break;

    case DB_TYPE_NUMERIC:
      size = or_packed_string_length (numeric_db_value_print (value, str_buf), NULL);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_STRING:
      size = or_packed_string_length (db_get_string (value), NULL);
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      break;

    case DB_TYPE_OBJECT:
    case DB_TYPE_DATE:
    case DB_TYPE_TIME:
      size = sizeof (int) * 3;
      break;

    case DB_TYPE_TIMESTAMP:
      size = sizeof (int) * 6;
      break;

    case DB_TYPE_DATETIME:
      size = sizeof (int) * 7;
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      {
	DB_SET *set;
	int ncol, i;
	DB_VALUE v;

	set = db_get_set (value);
	ncol = set_size (set);
	size += 4;		/* set size */

	for (i = 0; i < ncol; i++)
	  {
	    if (set_get_element (set, i, &v) != NO_ERROR)
	      {
		return 0;
	      }

	    size += jsp_get_value_size (&v);
	    pr_clear_value (&v);
	  }
      }
      break;

    case DB_TYPE_NULL:
    default:
      break;
    }

  size += 16;			/* type + value's size + mode + arg_data_type */
  return size;
}

/*
 * jsp_get_arg_sizes -
 *   return: return do a sum value size of argument list
 *   args(in/out): argument list of jsp
 *
 * Note:
 */

static int
jsp_get_argument_size (DB_ARG_LIST * args)
{
  DB_ARG_LIST *p;
  int size = 0;

  for (p = args; p != NULL; p = p->next)
    {
      size += jsp_get_value_size (p->val);
    }

  return size;
}

/*
 * jsp_send_call_request -
 *   return: error code
 *   sockfd(in): socket description
 *   sp_args(in): jsp argument list
 *
 * Note:
 */

static int
jsp_send_call_request (const SOCKET sockfd, const SP_ARGS * sp_args)
{
  int error_code = NO_ERROR;
  size_t nbytes;

  packing_packer packer;
  packing_packer packer2;

  cubmem::extensible_block header_buf;
  cubmem::extensible_block args_buf;

  packer.set_buffer_and_pack_all (args_buf, *sp_args);

  SP_HEADER header;
  header.command = (int) SP_CODE_INVOKE;
  header.size = args_buf.get_size ();

  packer2.set_buffer_and_pack_all (header_buf, header);
  nbytes = jsp_writen (sockfd, packer2.get_buffer_start (), packer2.get_current_size ());
  if (nbytes != packer2.get_current_size ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
      error_code = er_errid ();
    }

  nbytes = jsp_writen (sockfd, packer.get_buffer_start (), packer.get_current_size ());
  if (nbytes != packer.get_current_size ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
      error_code = er_errid ();
    }

  return error_code;
}

/*
 * jsp_send_destroy_request_all -
 *   return: error code
 *   sockfd(in): socket description
 *
 * Note:
 */

extern int
jsp_send_destroy_request_all ()
{
  for (int i = 0; i < MAX_CALL_COUNT; i++)
    {
      int idx = (MAX_CALL_COUNT - 1) - i;
      if (!IS_INVALID_SOCKET (sock_fds[idx]))
	{
	  jsp_send_destroy_request (sock_fds[idx]);
	  jsp_disconnect_server (sock_fds[idx]);
	  sock_fds[idx] = INVALID_SOCKET;
	}
    }
  return NO_ERROR;
}

extern int
jsp_send_destroy_request (const SOCKET sockfd)
{
  OR_ALIGNED_BUF (OR_INT_SIZE) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);

  or_pack_int (request, (int) SP_CODE_DESTROY);
  int nbytes = jsp_writen (sockfd, request, (int) sizeof (int));
  if (nbytes != (int) sizeof (int))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, "destroy");
      return er_errid ();
    }

  /* read request code */
  int code;
  nbytes = jsp_readn (sockfd, (char *) &code, (int) sizeof (int));
  if (nbytes != (int) sizeof (int))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
      return er_errid ();
    }
  code = ntohl (code);

  if (code == SP_CODE_DESTROY)
    {
      bool mode = ssl_client;
      ssl_client = false;
      tran_begin_libcas_function ();
      libcas_main (sockfd);	/* jdbc call */
      tran_end_libcas_function ();
      ssl_client = mode;
    }
  else
    {
      /* end */
    }

  return NO_ERROR;
}

/*
 * jsp_receive_response -
 *   return: error code
 *   sockfd(in) : socket description
 *   sp_args(in) : stored procedure argument list
 *
 * Note:
 */

static int
jsp_receive_response (const SOCKET sockfd, const SP_ARGS * sp_args)
{
  int nbytes;
  int command_code = -1;
  int error_code = NO_ERROR;

  while (true)
    {
      /* read request command code */
      nbytes = jsp_readn (sockfd, (char *) &command_code, (int) sizeof (int));
      if (nbytes != (int) sizeof (int))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  return ER_SP_NETWORK_ERROR;
	}
      command_code = ntohl (command_code);

      if (command_code == SP_CODE_INTERNAL_JDBC)
	{
	  tran_begin_libcas_function ();
	  error_code = libcas_main (sockfd);	/* jdbc call */
	  tran_end_libcas_function ();
	  if (error_code != NO_ERROR)
	    {
	      break;
	    }
	  continue;
	}
      else if (command_code == SP_CODE_RESULT || command_code == SP_CODE_ERROR)
	{
	  /* read size of buffer to allocate and data */
	  cubmem::extensible_block blk;
	  error_code = jsp_alloc_response (sockfd, blk);
	  if (error_code != NO_ERROR)
	    {
	      break;
	    }

	  switch (command_code)
	    {
	    case SP_CODE_RESULT:
	      error_code = jsp_receive_result (blk, sp_args);
	      break;
	    case SP_CODE_ERROR:
	      error_code = jsp_receive_error (blk, sp_args);
	      break;
	    }
	  break;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, command_code);
	  error_code = ER_SP_NETWORK_ERROR;
	  break;
	}
    }

  return error_code;
}

static int
jsp_alloc_response (const SOCKET sockfd, cubmem::extensible_block & blk)
{
  int nbytes, res_size;
  nbytes = jsp_readn (sockfd, (char *) &res_size, (int) sizeof (int));
  if (nbytes != (int) sizeof (int))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
      return ER_SP_NETWORK_ERROR;
    }
  res_size = ntohl (res_size);

  blk.extend_to (res_size);

  nbytes = jsp_readn (sockfd, blk.get_ptr (), res_size);
  if (nbytes != res_size)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
      return ER_SP_NETWORK_ERROR;
    }

  return NO_ERROR;
}

static int
jsp_receive_result (cubmem::extensible_block & blk, const SP_ARGS * sp_args)
{
  int error_code = NO_ERROR;

  packing_unpacker unpacker;
  unpacker.set_buffer (blk.get_ptr (), blk.get_size ());

  SP_VALUE value_unpacker;
  db_make_null (sp_args->returnval);
  value_unpacker.value = sp_args->returnval;
  value_unpacker.unpack (unpacker);

  DB_VALUE temp;
  int i = 0;
  for (DB_ARG_LIST * arg_list_p = sp_args->args; arg_list_p != NULL; arg_list_p = arg_list_p->next)
    {
      if (sp_args->arg_mode[i++] < SP_MODE_OUT)
	{
	  continue;
	}

      value_unpacker.value = &temp;
      value_unpacker.unpack (unpacker);

      db_value_clear (arg_list_p->val);
      db_value_clone (&temp, arg_list_p->val);
      db_value_clear (&temp);
    }

  return error_code;
}

static int
jsp_receive_error (cubmem::extensible_block & blk, const SP_ARGS * sp_args)
{
  int error_code = NO_ERROR;
  DB_VALUE error_value, error_msg;

  db_make_null (&error_value);
  db_make_null (&error_msg);
  db_make_null (sp_args->returnval);

  packing_unpacker unpacker;
  unpacker.set_buffer (blk.get_ptr (), blk.get_size ());

  SP_VALUE value_unpacker;

  value_unpacker.value = &error_value;
  value_unpacker.unpack (unpacker);

  value_unpacker.value = &error_msg;
  value_unpacker.unpack (unpacker);

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_EXECUTE_ERROR, 1, db_get_string (&error_msg));
  error_code = er_errid ();

  db_value_clear (&error_value);
  db_value_clear (&error_msg);

  return error_code;
}

/*
 * jsp_execute_stored_procedure - Execute Java Stored Procedure
 *   return: Error code
 *   args(in):
 *
 * Note:
 */

static int
jsp_execute_stored_procedure (const SP_ARGS * args)
{
  int error = NO_ERROR;
  SOCKET sock_fd;
  int retry_count = 0;
  bool mode = ssl_client;

retry:
  if (IS_INVALID_SOCKET (sock_fds[call_cnt]))
    {
      if (server_port == -1)	/* try to connect at the first time */
	{
	  server_port = jsp_get_server_port ();
	}

      if (server_port != -1)
	{
	  sock_fds[call_cnt] = jsp_connect_server (server_port);

	  /* ask port number of javasp server from cub_server and try connection again  */
	  if (IS_INVALID_SOCKET (sock_fds[call_cnt]))
	    {
	      server_port = jsp_get_server_port ();
	      sock_fds[call_cnt] = jsp_connect_server (server_port);
	    }

	  /* Java SP Server may have a problem */
	  if (IS_INVALID_SOCKET (sock_fds[call_cnt]))
	    {
	      if (server_port == -1)
		{
		  er_clear ();	/* ER_SP_CANNOT_CONNECT_JVM in jsp_connect_server() */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_RUNNING_JVM, 0);
		}
	      return er_errid ();
	    }
	}
      else
	{
	  /* Java SP Server is not running */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_RUNNING_JVM, 0);
	  return er_errid ();
	}
    }

  sock_fd = sock_fds[call_cnt];
  call_cnt++;

  if (call_cnt >= MAX_CALL_COUNT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TOO_MANY_NESTED_CALL, 0);
      error = er_errid ();
      goto end;
    }

  error = jsp_send_call_request (sock_fd, args);

  if (error != NO_ERROR)
    {
      if (retry_count == 0 && call_cnt == 1 && error == ER_SP_NETWORK_ERROR)
	{
	  call_cnt--;
	  retry_count++;
	  jsp_close_connection ();
	  goto retry;
	}
      else
	{
	  goto end;
	}
    }

  ssl_client = false;
  error = jsp_receive_response (sock_fd, args);
  ssl_client = mode;

end:
  call_cnt--;
  if (error != NO_ERROR || is_prepare_call[call_cnt])
    {
      // jsp_send_destroy_request (sock_fd);
      jsp_disconnect_server (sock_fd);
      sock_fds[call_cnt] = INVALID_SOCKET;
    }

  return error;
}

/*
 * jsp_do_call_stored_procedure -
 *   return: Error Code
 *   returnval(in/out):
 *   args(in/out):
 *   name(in):
 *
 * Note:
 */

static int
jsp_do_call_stored_procedure (DB_VALUE * returnval, DB_ARG_LIST * args, const char *name)
{
  DB_OBJECT *mop_p, *arg_mop_p;
  SP_ARGS sp_args;
  DB_VALUE method, param, param_cnt_val, return_type, temp, mode, arg_type;
  int arg_cnt, param_cnt, i;
  DB_SET *param_set;
  int save;
  int err = NO_ERROR;

  AU_DISABLE (save);

  db_make_null (&method);
  db_make_null (&param);

  mop_p = jsp_find_stored_procedure (name);
  if (!mop_p)
    {
      assert (er_errid () != NO_ERROR);
      err = er_errid ();
      goto error;
    }

  err = db_get (mop_p, SP_ATTR_TARGET, &method);
  if (err != NO_ERROR)
    {
      goto error;
    }

  sp_args.name = db_get_string (&method);
  if (!sp_args.name)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVAILD_JAVA_METHOD, 0);
      err = er_errid ();
      goto error;
    }
  sp_args.returnval = returnval;
  sp_args.args = args;

  err = db_get (mop_p, SP_ATTR_ARG_COUNT, &param_cnt_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  param_cnt = db_get_int (&param_cnt_val);
  arg_cnt = jsp_get_argument_count (&sp_args);
  if (param_cnt != arg_cnt)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_PARAM_COUNT, 2, param_cnt, arg_cnt);
      err = er_errid ();
      goto error;
    }
  sp_args.arg_count = arg_cnt;

  err = db_get (mop_p, SP_ATTR_ARGS, &param);
  if (err != NO_ERROR)
    {
      goto error;
    }

  param_set = db_get_set (&param);

  for (i = 0; i < arg_cnt; i++)
    {
      set_get_element (param_set, i, &temp);
      arg_mop_p = db_get_object (&temp);

      err = db_get (arg_mop_p, SP_ATTR_MODE, &mode);
      if (err != NO_ERROR)
	{
	  pr_clear_value (&temp);
	  goto error;
	}

      sp_args.arg_mode[i] = db_get_int (&mode);

      err = db_get (arg_mop_p, SP_ATTR_DATA_TYPE, &arg_type);
      if (err != NO_ERROR)
	{
	  pr_clear_value (&temp);
	  goto error;
	}

      sp_args.arg_type[i] = db_get_int (&arg_type);
      pr_clear_value (&temp);

      if (sp_args.arg_type[i] == DB_TYPE_RESULTSET && !is_prepare_call[call_cnt])
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_RETURN_RESULTSET, 0);
	  err = er_errid ();
	  goto error;
	}
    }

  err = db_get (mop_p, SP_ATTR_RETURN_TYPE, &return_type);
  if (err != NO_ERROR)
    {
      goto error;
    }

  sp_args.return_type = db_get_int (&return_type);

  if (sp_args.return_type == DB_TYPE_RESULTSET && !is_prepare_call[call_cnt])
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_RETURN_RESULTSET, 0);
      err = er_errid ();
    }

error:
  AU_ENABLE (save);

  if (err == NO_ERROR)
    {
      AU_SAVE_AND_ENABLE (save);
      err = jsp_execute_stored_procedure (&sp_args);
      AU_RESTORE (save);
    }

  pr_clear_value (&method);
  pr_clear_value (&param);

  return err;
}

/*
 * jsp_call_from_server -
 *   return: Error Code
 *   returnval(in/out) : jsp call result
 *   argarray(in/out):
 *   name(in): call jsp
 *   arg_cnt(in):
 *
 * Note:
 */

int
jsp_call_from_server (DB_VALUE * returnval, DB_VALUE ** argarray, const char *name, const int arg_cnt)
{
  DB_ARG_LIST *val_list = 0, *vl, **next_val_list;
  int i;
  int error = NO_ERROR;

  next_val_list = &val_list;
  for (i = 0; i < arg_cnt; i++)
    {
      DB_VALUE *db_val;
      *next_val_list = (DB_ARG_LIST *) calloc (1, sizeof (DB_ARG_LIST));
      if (*next_val_list == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_ARG_LIST));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      (*next_val_list)->next = (DB_ARG_LIST *) 0;

      if (argarray[i] == NULL)
	{
	  return -1;		/* error, clean */
	}
      db_val = argarray[i];
      (*next_val_list)->label = "";	/* check out mode in select statement */
      (*next_val_list)->val = db_val;

      next_val_list = &(*next_val_list)->next;
    }

  error = jsp_do_call_stored_procedure (returnval, val_list, name);

  while (val_list)
    {
      vl = val_list->next;
      free_and_init (val_list);
      val_list = vl;
    }

  return error;
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
  is_prepare_call[call_cnt] = true;
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
  is_prepare_call[call_cnt] = false;
}

/*
 * jsp_get_db_result_set -
 *   return: none
 *   h_id(in):
 *
 * Note: require cubrid cas library
 */

void *
jsp_get_db_result_set (int h_id)
{
  return libcas_get_db_result_set (h_id);
}

/*
 * jsp_srv_handle_free -
 *   return: none
 *   h_id(in):
 *
 * Note:
 */

void
jsp_srv_handle_free (int h_id)
{
  libcas_srv_handle_free (h_id);
}

/*
 * jsp_make_method_sig_list () - converts a parse expression tree list of
 *                            method calls to method signature list
 *   return: A NULL return indicates a (memory) error occurred
 *   parser(in):
 *   node_list(in): should be parse method nodes
 *   subquery_as_attr_list(in):
 */
static int
jsp_make_method_sig_list (PARSER_CONTEXT * parser, PT_NODE * node, method_sig_list & sig_list)
{
  int error = NO_ERROR;
  DB_VALUE method, param_cnt_val, mode, arg_type, temp, result_type;

  int sig_num_args = pt_length_of_list (node->info.method_call.arg_list);
  std::vector < int >sig_arg_mode;
  std::vector < int >sig_arg_type;
  int sig_result_type;

  METHOD_SIG *sig = nullptr;

  {
    char *parsed_method_name = (char *) node->info.method_call.method_name->info.name.original;
    DB_OBJECT *mop_p = jsp_find_stored_procedure (parsed_method_name);
    if (mop_p)
      {
	/* check java stored prcedure target */
	error = db_get (mop_p, SP_ATTR_TARGET, &method);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	/* check arg count */
	error = db_get (mop_p, SP_ATTR_ARG_COUNT, &param_cnt_val);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	int param_cnt = db_get_int (&param_cnt_val);
	if (sig_num_args != param_cnt)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_PARAM_COUNT, 2, param_cnt, sig_num_args);
	    error = er_errid ();
	    goto end;
	  }

	DB_VALUE args;
	/* arg_mode, arg_type */
	error = db_get (mop_p, SP_ATTR_ARGS, &args);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }

	DB_SET *param_set = db_get_set (&args);
	for (int i = 0; i < sig_num_args; i++)
	  {
	    set_get_element (param_set, i, &temp);
	    DB_OBJECT *arg_mop_p = db_get_object (&temp);
	    if (arg_mop_p)
	      {
		if (db_get (arg_mop_p, SP_ATTR_MODE, &mode) == NO_ERROR)
		  {
		    sig_arg_mode.push_back (db_get_int (&mode));
		  }

		if (db_get (arg_mop_p, SP_ATTR_DATA_TYPE, &arg_type) == NO_ERROR)
		  {
		    sig_arg_type.push_back (db_get_int (&arg_type));
		  }

		pr_clear_value (&mode);
		pr_clear_value (&arg_type);
		pr_clear_value (&temp);
	      }
	  }
	pr_clear_value (&args);

	/* result type */
	error = db_get (mop_p, SP_ATTR_RETURN_TYPE, &result_type);
	if (error != NO_ERROR)
	  {
	    goto end;
	  }
	sig_result_type = db_get_int (&result_type);
      }
    else
      {
	error = er_errid ();
	goto end;
      }

    sig = sig_list.method_sig = (METHOD_SIG *) db_private_alloc (NULL, sizeof (METHOD_SIG));
    if (sig)
      {
	sig->next = nullptr;
	sig->num_method_args = sig_num_args;
	sig->method_type = METHOD_TYPE_JAVA_SP;

	const char *method_name = db_get_string (&method);
	int method_name_len = db_get_string_size (&method);

	sig->method_name = (char *) db_private_alloc (NULL, method_name_len + 1);
	if (!sig->method_name)
	  {
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto end;
	  }

	memcpy (sig->method_name, method_name, method_name_len);
	sig->method_name[method_name_len] = 0;


	sig->method_arg_pos = (int *) db_private_alloc (NULL, (sig_num_args + 1) * sizeof (int));
	if (!sig->method_arg_pos)
	  {
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto end;
	  }

	for (int i = 0; i < sig_num_args + 1; i++)
	  {
	    sig->method_arg_pos[i] = i;
	  }


	sig->arg_info.arg_mode = (int *) db_private_alloc (NULL, (sig_num_args + 1) * sizeof (int));
	if (!sig->arg_info.arg_mode)
	  {
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto end;
	  }

	sig->arg_info.arg_type = (int *) db_private_alloc (NULL, (sig_num_args + 1) * sizeof (int));
	if (!sig->arg_info.arg_type)
	  {
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto end;
	  }

	for (int i = 0; i < sig_num_args; i++)
	  {
	    sig->arg_info.arg_mode[i] = sig_arg_mode[i];
	    sig->arg_info.arg_type[i] = sig_arg_type[i];
	  }

	sig->arg_info.result_type = sig_result_type;

	sig_list.num_methods = 1;
      }
    else
      {
	error = ER_OUT_OF_VIRTUAL_MEMORY;
	goto end;
      }
  }

end:

  if (error != NO_ERROR)
    {
      if (sig)
	{
	  sig->freemem ();
	}
      sig_list.method_sig = nullptr;
      sig_list.num_methods = 0;
    }

  pr_clear_value (&method);
  pr_clear_value (&param_cnt_val);
  pr_clear_value (&result_type);

  return error;
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
jsp_make_method_arglist (PARSER_CONTEXT * parser, PT_NODE * node_list)
{
  int *arg_list = NULL;
  int i = 0;
  int num_args = pt_length_of_list (node_list);
  PT_NODE *node;

  arg_list = (int *) db_private_alloc (NULL, num_args * sizeof (int));
  if (!arg_list)
    {
      return NULL;
    }

  for (node = node_list; node != NULL; node = node->next)
    {
      arg_list[i] = i;
      i++;
      /*
         arg_list[i] = pt_find_attribute (parser, node, subquery_as_attr_list);
         if (arg_list[i] == -1)
         {
         return NULL;
         }
         i++;
       */
    }

  return arg_list;
}
