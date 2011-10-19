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

/*
 * jsp_cl.c - Java Stored Procedure Client Module Source
 */

#ident "$Id$"

#include "config.h"

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#else /* not WINDOWS */
#include <winsock2.h>
#include <windows.h>
#endif /* not WINDOWS */

#include "error_manager.h"
#include "memory_alloc.h"
#include "dbtype.h"
#include "dbdef.h"
#include "parser.h"
#include "object_domain.h"
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

#include "dbval.h"		/* this must be the last header file included!!! */

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

#define MAX_ARG_COUNT   64
#define MAX_CALL_COUNT  16
#define SAVEPOINT_ADD_STORED_PROC "ADDSTOREDPROC"

typedef struct db_arg_list
{
  struct db_arg_list *next;
  DB_VALUE *val;
  const char *label;
} DB_ARG_LIST;

typedef struct
{
  char *name;
  DB_VALUE *returnval;
  DB_ARG_LIST *args;
  int arg_count;
  int arg_mode[MAX_ARG_COUNT];
  int arg_type[MAX_ARG_COUNT];
  int return_type;
} SP_ARGS;

static SOCKET sock_fds[MAX_CALL_COUNT] = { INVALID_SOCKET };
static int call_cnt = 0;
static bool is_prepare_call[MAX_CALL_COUNT];

#if defined(WINDOWS)
FARPROC jsp_old_hook = NULL;
static int windows_socket_startup (void);
static void windows_socket_shutdown (void);
#endif /* WINDOWS */

static unsigned int jsp_map_pt_misc_to_sp_type (PT_MISC_TYPE pt_enum);
static int jsp_map_pt_misc_to_sp_mode (PT_MISC_TYPE pt_enum);
static int jsp_get_argument_count (const SP_ARGS * sp_args);
static int jsp_add_stored_procedure_argument (MOP * mop_p,
					      const char *sp_name,
					      const char *arg_name, int index,
					      int data_type, int mode);
static char *jsp_check_stored_procedure_name (const char *str);
static int jsp_add_stored_procedure (const char *name,
				     const PT_MISC_TYPE type,
				     const PT_TYPE_ENUM ret_type,
				     PT_NODE * param_list,
				     const char *java_method);
static int drop_stored_procedure (const char *name,
				  PT_MISC_TYPE expected_type);
static int jsp_writen (SOCKET fd, const void *vptr, int n);
static int jsp_readn (SOCKET fd, void *vptr, int n);
static int jsp_get_value_size (DB_VALUE * value);
static int jsp_get_argument_size (DB_ARG_LIST * args);

static char *jsp_pack_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_int_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_bigint_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_short_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_float_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_double_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_numeric_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_string_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_date_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_time_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_timestamp_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_datetime_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_set_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_object_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_monetary_argument (char *buffer, DB_VALUE * value);
static char *jsp_pack_null_argument (char *buffer);

static char *jsp_unpack_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_int_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_bigint_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_short_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_float_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_double_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_numeric_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_string_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_date_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_time_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_timestamp_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_set_value (char *buffer, int type, DB_VALUE * retval);
static char *jsp_unpack_object_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_monetary_value (char *buffer, DB_VALUE * retval);
static char *jsp_unpack_resultset (char *buffer, DB_VALUE * retval);

extern int libcas_main (SOCKET fd);
extern void *libcas_get_db_result_set (int h_id);
extern void libcas_srv_handle_free (int h_id);

static int jsp_send_call_request (const SOCKET sockfd,
				  const SP_ARGS * sp_args);
static int jsp_receive_response (const SOCKET sockfd,
				 const SP_ARGS * sp_args);

static SOCKET jsp_connect_server (void);
static void jsp_close_internal_connection (const SOCKET sockfd);
static int jsp_execute_stored_procedure (const SP_ARGS * args);
static int jsp_do_call_stored_procedure (DB_VALUE * returnval,
					 DB_ARG_LIST * args,
					 const char *name);

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

  sock_fds[0] = INVALID_SOCKET;
  call_cnt = 0;

  for (i = 0; i < MAX_CALL_COUNT; i++)
    {
      is_prepare_call[i] = false;
    }

#if defined(WINDOWS)
  windows_socket_startup ();
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
      jsp_close_internal_connection (sock_fds[0]);
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

  if (!statement
      || !(method = statement->info.method_call.method_name)
      || method->node_type != PT_NAME || !(proc = method->info.name.original))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (DB_ARG_LIST));
	  return er_errid ();
	}

      (*next_value_list)->next = (DB_ARG_LIST *) 0;

      /*
         Don't clone host vars; they may actually be acting as output
         variables (e.g., a character array that is intended to receive
         bytes from the method), and cloning will ensure that the
         results never make it to the expected area.  Since
         pt_evaluate_tree() always clones its db_values we must not
         use pt_evaluate_tree() to extract the db_value from a host
         variable;  instead extract it ourselves.
       */
      if (PT_IS_CONST (vc))
	{
	  db_value = pt_value_to_db (parser, vc);
	}
      else
	{
	  db_value = (DB_VALUE *) calloc (1, sizeof (DB_VALUE));
	  if (db_value == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE));
	      return er_errid ();
	    }

	  /* must call pt_evaluate_tree */
	  pt_evaluate_tree (parser, vc, db_value, 1);
	  if (parser->error_msgs)
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

  if (parser->error_msgs)
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

      if (into != NULL && into->node_type == PT_NAME
	  && (into_label = into->info.name.original) != NULL)
	{
	  /* create another DB_VALUE of the new instance for the label_table */
	  ins_value = db_value_copy (&ret_value);
	  error = pt_associate_label_with_value_check_reference (into_label,
								 ins_value);
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

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
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
  const char *name, *java_method;

  PT_MISC_TYPE type;
  PT_NODE *param_list, *p;
  PT_TYPE_ENUM ret_type = PT_TYPE_NONE;
  int param_count;

  CHECK_MODIFICATION_ERROR ();

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_ALREADY_EXIST, 1, name);
      return er_errid ();
    }

  for (p = param_list, param_count = 0; p != NULL; p = p->next, param_count++)
    {
      ;
    }

  if (param_count > MAX_ARG_COUNT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TOO_MANY_ARG_COUNT, 1,
	      name);
      return er_errid ();
    }

  return jsp_add_stored_procedure (name, type, ret_type, param_list,
				   java_method);
}

/*
 * jsp_map_pt_misc_to_sp_type
 *   return : stored procedure type ( Procedure or Function )
 *   pt_enum(in): Misc Types
 *
 * Note:
 */

static unsigned int
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
 *
 * Note:
 */

static int
jsp_add_stored_procedure_argument (MOP * mop_p, const char *sp_name,
				   const char *arg_name, int index,
				   int data_type, int mode)
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
      err = er_errid ();
      goto error;
    }

  obt_p = dbt_create_object_internal (classobj_p);
  if (obt_p == NULL)
    {
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

  db_make_int (&value, jsp_map_pt_misc_to_sp_mode ((PT_MISC_TYPE) mode));
  err = dbt_put_internal (obt_p, SP_ATTR_MODE, &value);
  if (err != NO_ERROR)
    {
      goto error;
    }

  object_p = dbt_finish_object (obt_p);
  if (!object_p)
    {
      err = er_errid ();
      goto error;
    }
  obt_p = NULL;

  err = locator_flush_instance (object_p);
  if (err != NO_ERROR)
    {
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
 *
 * Note:
 */

static int
jsp_add_stored_procedure (const char *name, const PT_MISC_TYPE type,
			  const PT_TYPE_ENUM return_type,
			  PT_NODE * param_list, const char *java_method)
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
      err = er_errid ();
      goto error;
    }

  err = tran_savepoint (SAVEPOINT_ADD_STORED_PROC, false);
  if (err != NO_ERROR)
    {
      goto error;
    }
  has_savepoint = true;

  obt_p = dbt_create_object_internal (classobj_p);
  if (!obt_p)
    {
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
      err = er_errid ();
      goto error;
    }

  for (node_p = param_list, i = 0; node_p != NULL; node_p = node_p->next)
    {
      MOP mop = NULL;

      if (node_p->type_enum == PT_TYPE_RESULTSET
	  && node_p->info.sp_param.mode != PT_OUTPUT)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_SP_CANNOT_INPUT_RESULTSET, 0);
	  err = er_errid ();
	  goto error;
	}
      name_info = node_p->info.sp_param.name->info.name;

      err = jsp_add_stored_procedure_argument (&mop, checked_name,
					       name_info.original, i,
					       node_p->type_enum,
					       node_p->info.sp_param.mode);
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

  object_p = dbt_finish_object (obt_p);
  if (!object_p)
    {
      err = er_errid ();
      goto error;
    }
  obt_p = NULL;

  err = locator_flush_instance (object_p);
  if (err != NO_ERROR)
    {
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
      tran_abort_upto_savepoint (SAVEPOINT_ADD_STORED_PROC);
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
  PT_MISC_TYPE real_type;
  DB_SET *arg_set_p;
  int save, i, arg_cnt;
  int err;

  AU_DISABLE (save);

  db_make_null (&args_val);
  db_make_null (&owner_val);

  sp_mop = jsp_find_stored_procedure (name);
  if (sp_mop == NULL)
    {
      err = er_errid ();
      goto error;
    }

  err = db_get (sp_mop, SP_ATTR_OWNER, &owner_val);
  if (err != NO_ERROR)
    {
      goto error;
    }
  owner = DB_GET_OBJECT (&owner_val);

  if (owner != Au_user && !au_is_dba_group_member (Au_user))
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

  real_type = (PT_MISC_TYPE) DB_GET_INT (&sp_type_val);
  if (real_type != jsp_map_pt_misc_to_sp_type (expected_type))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_TYPE, 2, name,
	      real_type == PT_SP_FUNCTION ? "FUNCTION" : "PROCEDURE");

      err = er_errid ();
      goto error;
    }

  err = db_get (sp_mop, SP_ATTR_ARG_COUNT, &arg_cnt_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  arg_cnt = DB_GET_INT (&arg_cnt_val);

  err = db_get (sp_mop, SP_ATTR_ARGS, &args_val);
  if (err != NO_ERROR)
    {
      goto error;
    }

  arg_set_p = DB_GET_SET (&args_val);

  for (i = 0; i < arg_cnt; i++)
    {
      set_get_element (arg_set_p, i, &temp);
      arg_mop = DB_GET_OBJECT (&temp);
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

#if defined(WINDOWS)

/*
 * windows_blocking_hook() -
 *   return: false
 *
 * Note: WINDOWS Code
 */

BOOL
windows_blocking_hook ()
{
  return false;
}

/*
 * windows_socket_startup() -
 *   return: return -1 on error otherwise return 1
 *
 * Note:
 */

static int
windows_socket_startup ()
{
  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  jsp_old_hook = NULL;
  wVersionRequested = 0x101;
  err = WSAStartup (wVersionRequested, &wsaData);
  if (err != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_CSS_WINSOCK_STARTUP, 1, err);
      return (-1);
    }

  /* Establish a blocking "hook" function to prevent Windows messages
   * from being dispatched when we block on reads.
   */
  jsp_old_hook = WSASetBlockingHook ((FARPROC) windows_blocking_hook);
  if (jsp_old_hook == NULL)
    {
      /* couldn't set up our hook */
      err = WSAGetLastError ();
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_CSS_WINSOCK_STARTUP, 1, err);
      (void) WSACleanup ();
      return -1;
    }

  return 1;
}

/*
 * windows_socket_shutdown() -
 *   return:
 *
 * Note:
 */

static void
windows_socket_shutdown ()
{
  int err;

  if (jsp_old_hook != NULL)
    {
      (void) WSASetBlockingHook (jsp_old_hook);
    }

  err = WSACleanup ();
}
#endif /* WINDOWS */

/*
 * jsp_writen
 *   return: fail return -1,
 *   fd(in): Specifies the socket file descriptor.
 *   vptr(in): Points to the buffer containing the message to send.
 *   n(in): Specifies the length of the message in bytes
 *
 * Note:
 */

static int
jsp_writen (SOCKET fd, const void *vptr, int n)
{
  int nleft;
  int nwritten;
  const char *ptr;

  ptr = (const char *) vptr;
  nleft = n;

  while (nleft > 0)
    {
#if defined(WINDOWS)
      nwritten = send (fd, ptr, nleft, 0);
#else
      nwritten = send (fd, ptr, (size_t) nleft, 0);
#endif

      if (nwritten <= 0)
	{
#if defined(WINDOWS)
	  if (nwritten < 0 && errno == WSAEINTR)
#else /* not WINDOWS */
	  if (nwritten < 0 && errno == EINTR)
#endif /* not WINDOWS */
	    {
	      nwritten = 0;	/* and call write() again */
	    }
	  else
	    {
	      return (-1);	/* error */
	    }
	}

      nleft -= nwritten;
      ptr += nwritten;
    }

  return (n - nleft);
}

/*
 * jsp_readn
 *   return: read size
 *   fd(in): Specifies the socket file descriptor.
 *   vptr(in/out): Points to a buffer where the message should be stored.
 *   n(in): Specifies  the  length in bytes of the buffer pointed
 *          to by the buffer argument.
 *
 * Note:
 */

static int
jsp_readn (SOCKET fd, void *vptr, int n)
{
  int nleft;
  int nread;
  char *ptr;

  ptr = (char *) vptr;
  nleft = n;

  while (nleft > 0)
    {
#if defined(WINDOWS)
      nread = recv (fd, ptr, nleft, 0);
#else
      nread = recv (fd, ptr, (size_t) nleft, 0);
#endif

      if (nread < 0)
	{

#if defined(WINDOWS)
	  if (errno == WSAEINTR)
#else /* not WINDOWS */
	  if (errno == EINTR)
#endif /* not WINDOWS */
	    {
	      nread = 0;	/* and call read() again */
	    }
	  else
	    {
	      return (-1);
	    }
	}
      else if (nread == 0)
	{
	  break;		/* EOF */
	}

      nleft -= nread;
      ptr += nread;
    }

  return (n - nleft);		/* return >= 0 */
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
      size = or_packed_string_length (numeric_db_value_print (value), NULL);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_STRING:
      size = or_packed_string_length (DB_GET_STRING (value), NULL);
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

	set = DB_GET_SET (value);
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
 * jsp_pack_int_argument -
 *   return: return packing value
 *   buffer(in/out): buffer
 *   value(in): value of integer type
 *
 * Note:
 */

static char *
jsp_pack_int_argument (char *buffer, DB_VALUE * value)
{
  int v;
  char *ptr;

  ptr = buffer;
  ptr = or_pack_int (ptr, sizeof (int));
  v = DB_GET_INT (value);
  ptr = or_pack_int (ptr, v);

  return ptr;
}

/*
 * jsp_pack_bigint_argument -
 *   return: return packing value
 *   buffer(in/out): buffer
 *   value(in): value of bigint type
 *
 * Note:
 */
static char *
jsp_pack_bigint_argument (char *buffer, DB_VALUE * value)
{
  DB_BIGINT tmp_value;
  char *ptr;

  ptr = or_pack_int (buffer, sizeof (DB_BIGINT));
  tmp_value = DB_GET_BIGINT (value);
  OR_PUT_BIGINT (ptr, &tmp_value);

  return ptr + OR_BIGINT_SIZE;
}

/*
 * jsp_pack_short_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of short type
 *
 * Note:
 */

static char *
jsp_pack_short_argument (char *buffer, DB_VALUE * value)
{
  short v;
  char *ptr;

  ptr = buffer;
  ptr = or_pack_int (ptr, sizeof (int));
  v = DB_GET_SHORT (value);
  ptr = or_pack_short (ptr, v);

  return ptr;
}

/*
 * jsp_pack_float_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of float type
 *
 * Note:
 */

static char *
jsp_pack_float_argument (char *buffer, DB_VALUE * value)
{
  float v;
  char *ptr;

  ptr = buffer;
  ptr = or_pack_int (ptr, sizeof (float));
  v = DB_GET_FLOAT (value);
  ptr = or_pack_float (ptr, v);

  return ptr;
}

/*
 * jsp_pack_double_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of double type
 *
 * Note:
 */

static char *
jsp_pack_double_argument (char *buffer, DB_VALUE * value)
{
  double v, pack_value;
  char *ptr;

  ptr = or_pack_int (buffer, sizeof (double));
  v = DB_GET_DOUBLE (value);
  OR_PUT_DOUBLE (&pack_value, &v);
  memcpy (ptr, (char *) (&pack_value), OR_DOUBLE_SIZE);

  return ptr + OR_DOUBLE_SIZE;
}

/*
 * jsp_pack_numeric_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of numeric type
 *
 * Note:
 */

static char *
jsp_pack_numeric_argument (char *buffer, DB_VALUE * value)
{
  char *v;
  char *ptr;

  ptr = buffer;
  v = numeric_db_value_print (value);
  ptr = or_pack_string (ptr, v);

  return ptr;
}

/*
 * jsp_pack_string_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of string type
 *
 * Note:
 */

static char *
jsp_pack_string_argument (char *buffer, DB_VALUE * value)
{
  char *v;
  char *ptr;

  ptr = buffer;
  v = DB_GET_STRING (value);
  ptr = or_pack_string (ptr, v);

  return ptr;
}

/*
 * jsp_pack_date_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of date type
 *
 * Note:
 */

static char *
jsp_pack_date_argument (char *buffer, DB_VALUE * value)
{
  int year, month, day;
  DB_DATE *date;
  char *ptr;

  ptr = buffer;
  date = DB_GET_DATE (value);
  db_date_decode (date, &month, &day, &year);

  ptr = or_pack_int (ptr, sizeof (int) * 3);
  ptr = or_pack_int (ptr, year);
  ptr = or_pack_int (ptr, month - 1);
  ptr = or_pack_int (ptr, day);

  return ptr;
}

/*
 * jsp_pack_time_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of time type
 *
 * Note:
 */

static char *
jsp_pack_time_argument (char *buffer, DB_VALUE * value)
{
  int hour, min, sec;
  DB_TIME *time;
  char *ptr;

  ptr = buffer;
  time = DB_GET_TIME (value);
  db_time_decode (time, &hour, &min, &sec);

  ptr = or_pack_int (ptr, sizeof (int) * 3);
  ptr = or_pack_int (ptr, hour);
  ptr = or_pack_int (ptr, min);
  ptr = or_pack_int (ptr, sec);

  return ptr;
}

/*
 * jsp_pack_timestamp_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of timestamp type
 *
 * Note:
 */

static char *
jsp_pack_timestamp_argument (char *buffer, DB_VALUE * value)
{
  DB_TIMESTAMP *timestamp;
  DB_DATE date;
  DB_TIME time;
  int year, mon, day, hour, min, sec;
  char *ptr;

  ptr = buffer;
  timestamp = DB_GET_TIMESTAMP (value);
  db_timestamp_decode (timestamp, &date, &time);
  db_date_decode (&date, &mon, &day, &year);
  db_time_decode (&time, &hour, &min, &sec);

  ptr = or_pack_int (ptr, sizeof (int) * 6);
  ptr = or_pack_int (ptr, year);
  ptr = or_pack_int (ptr, mon - 1);
  ptr = or_pack_int (ptr, day);
  ptr = or_pack_int (ptr, hour);
  ptr = or_pack_int (ptr, min);
  ptr = or_pack_int (ptr, sec);

  return ptr;
}

/*
 * jsp_pack_datetime_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of datetime type
 *
 * Note:
 */

static char *
jsp_pack_datetime_argument (char *buffer, DB_VALUE * value)
{
  DB_DATETIME *datetime;
  int year, mon, day, hour, min, sec, msec;
  char *ptr;

  ptr = buffer;
  datetime = DB_GET_DATETIME (value);
  db_datetime_decode (datetime, &mon, &day, &year, &hour, &min, &sec, &msec);

  ptr = or_pack_int (ptr, sizeof (int) * 7);
  ptr = or_pack_int (ptr, year);
  ptr = or_pack_int (ptr, mon - 1);
  ptr = or_pack_int (ptr, day);
  ptr = or_pack_int (ptr, hour);
  ptr = or_pack_int (ptr, min);
  ptr = or_pack_int (ptr, sec);
  ptr = or_pack_int (ptr, msec);

  return ptr;
}

/*
 * jsp_pack_set_argument -
 *   return: return packing value
 *   buffer(in): buffer
 *   value(in): value of set type
 *
 * Note:
 */

static char *
jsp_pack_set_argument (char *buffer, DB_VALUE * value)
{
  DB_SET *set;
  int ncol, i;
  DB_VALUE v;
  char *ptr;

  ptr = buffer;
  set = DB_GET_SET (value);
  ncol = set_size (set);

  ptr = or_pack_int (ptr, sizeof (int));
  ptr = or_pack_int (ptr, ncol);

  for (i = 0; i < ncol; i++)
    {
      if (set_get_element (set, i, &v) != NO_ERROR)
	{
	  break;
	}

      ptr = jsp_pack_argument (ptr, &v);
      pr_clear_value (&v);
    }

  return ptr;
}

/*
 * jsp_pack_object_argument -
 *   return: return packing value
 *   buffer(in/out): buffer
 *   value(in): value of object type
 *
 * Note:
 */

static char *
jsp_pack_object_argument (char *buffer, DB_VALUE * value)
{
  char *ptr;
  OID *oid;
  MOP mop;

  ptr = buffer;
  mop = DB_GET_OBJECT (value);
  if (mop != NULL)
    {
      oid = WS_OID (mop);
    }
  else
    {
      oid = (OID *) (&oid_Null_oid);
    }

  ptr = or_pack_int (ptr, sizeof (int) * 3);
  ptr = or_pack_int (ptr, oid->pageid);
  ptr = or_pack_short (ptr, oid->slotid);
  ptr = or_pack_short (ptr, oid->volid);

  return ptr;
}

/*
 * jsp_pack_monetary_argument -
 *   return: return packing value
 *   buffer(in/out): buffer
 *   value(in): value of monetary type
 *
 * Note:
 */

static char *
jsp_pack_monetary_argument (char *buffer, DB_VALUE * value)
{
  DB_MONETARY *v;
  double pack_value;
  char *ptr;

  ptr = or_pack_int (buffer, sizeof (double));
  v = DB_GET_MONETARY (value);
  OR_PUT_DOUBLE (&pack_value, &v->amount);
  memcpy (ptr, (char *) (&pack_value), OR_DOUBLE_SIZE);

  return ptr + OR_DOUBLE_SIZE;
}

/*
 * jsp_pack_null_argument -
 *   return: return null packing value
 *   buffer(in/out): buffer
 *
 * Note:
 */

static char *
jsp_pack_null_argument (char *buffer)
{
  char *ptr;

  ptr = buffer;
  ptr = or_pack_int (ptr, 0);

  return ptr;
}

/*
 * jsp_pack_argument
 *   return: packing value for send to jsp server
 *   buffer(in/out): contain packng value
 *   value(in): value for packing
 *
 * Note:
 */

static char *
jsp_pack_argument (char *buffer, DB_VALUE * value)
{
  int param_type;
  char *ptr;

  ptr = buffer;
  param_type = DB_VALUE_TYPE (value);
  ptr = or_pack_int (ptr, param_type);

  switch (param_type)
    {
    case DB_TYPE_INTEGER:
      ptr = jsp_pack_int_argument (ptr, value);
      break;

    case DB_TYPE_BIGINT:
      ptr = jsp_pack_bigint_argument (ptr, value);
      break;

    case DB_TYPE_SHORT:
      ptr = jsp_pack_short_argument (ptr, value);
      break;

    case DB_TYPE_FLOAT:
      ptr = jsp_pack_float_argument (ptr, value);
      break;

    case DB_TYPE_DOUBLE:
      ptr = jsp_pack_double_argument (ptr, value);
      break;

    case DB_TYPE_NUMERIC:
      ptr = jsp_pack_numeric_argument (ptr, value);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_STRING:
      ptr = jsp_pack_string_argument (ptr, value);
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      break;

    case DB_TYPE_DATE:
      ptr = jsp_pack_date_argument (ptr, value);
      break;
      /*describe_data(); */

    case DB_TYPE_TIME:
      ptr = jsp_pack_time_argument (ptr, value);
      break;

    case DB_TYPE_TIMESTAMP:
      ptr = jsp_pack_timestamp_argument (ptr, value);
      break;

    case DB_TYPE_DATETIME:
      ptr = jsp_pack_datetime_argument (ptr, value);
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      ptr = jsp_pack_set_argument (ptr, value);
      break;

    case DB_TYPE_MONETARY:
      ptr = jsp_pack_monetary_argument (ptr, value);
      break;

    case DB_TYPE_OBJECT:
      ptr = jsp_pack_object_argument (ptr, value);
      break;

    case DB_TYPE_NULL:
      ptr = jsp_pack_null_argument (ptr);
      break;
    default:
      break;
    }

  return ptr;
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
  int req_code, arg_count, i, strlen;
  int req_size, nbytes;
  DB_ARG_LIST *p;
  char *buffer, *ptr;

  req_size = (int) sizeof (int) * 4
    + or_packed_string_length (sp_args->name, &strlen) +
    jsp_get_argument_size (sp_args->args);

  buffer = (char *) malloc (req_size);
  if (buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      req_size);
      return er_errid ();
    }

  req_code = 0x1;
  ptr = or_pack_int (buffer, req_code);

  ptr = or_pack_string_with_length (ptr, sp_args->name, strlen);

  arg_count = jsp_get_argument_count (sp_args);
  ptr = or_pack_int (ptr, arg_count);

  for (p = sp_args->args, i = 0; p != NULL; p = p->next, i++)
    {
      ptr = or_pack_int (ptr, sp_args->arg_mode[i]);
      ptr = or_pack_int (ptr, sp_args->arg_type[i]);
      ptr = jsp_pack_argument (ptr, p->val);
    }

  ptr = or_pack_int (ptr, sp_args->return_type);
  ptr = or_pack_int (ptr, req_code);

  nbytes = jsp_writen (sockfd, buffer, req_size);
  if (nbytes != req_size)
    {
      free_and_init (buffer);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
	      nbytes);
      return er_errid ();
    }

  free_and_init (buffer);
  return NO_ERROR;
}

/*
 * jsp_unpack_int_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of int type
 *
 * Note:
 */

static char *
jsp_unpack_int_value (char *buffer, DB_VALUE * retval)
{
  int val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_int (ptr, &val);
  db_make_int (retval, val);

  return ptr;
}

/*
 * jsp_unpack_bigint_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of bigint type
 *
 * Note:
 */

static char *
jsp_unpack_bigint_value (char *buffer, DB_VALUE * retval)
{
  DB_BIGINT val;

  memcpy ((char *) (&val), buffer, OR_BIGINT_SIZE);
  OR_GET_BIGINT (&val, &val);
  db_make_bigint (retval, val);

  return buffer + OR_BIGINT_SIZE;
}

/*
 * jsp_unpack_short_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of short type
 *
 * Note:
 */

static char *
jsp_unpack_short_value (char *buffer, DB_VALUE * retval)
{
  short val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_short (ptr, &val);
  db_make_short (retval, val);

  return ptr;
}

/*
 * jsp_unpack_float_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of float type
 *
 * Note:
 */

static char *
jsp_unpack_float_value (char *buffer, DB_VALUE * retval)
{
  float val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_float (ptr, &val);
  db_make_float (retval, val);

  return ptr;
}

/*
 * jsp_unpack_double_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of double type
 *
 * Note:
 */

static char *
jsp_unpack_double_value (char *buffer, DB_VALUE * retval)
{
  double val, result;

  memcpy ((char *) (&val), buffer, OR_DOUBLE_SIZE);
  OR_GET_DOUBLE (&val, &result);
  db_make_double (retval, result);

  return buffer + OR_DOUBLE_SIZE;
}

/*
 * jsp_unpack_numeric_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of numeric type
 *
 * Note:
 */

static char *
jsp_unpack_numeric_value (char *buffer, DB_VALUE * retval)
{
  char *val;
  char *ptr;

  ptr = or_unpack_string_nocopy (buffer, &val);
  if (val == NULL || numeric_coerce_string_to_num (val, strlen (val),
						   retval) != NO_ERROR)
    {
      ptr = NULL;
    }

  return ptr;
}

/*
 * jsp_unpack_string_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of string type
 *
 * Note:
 */

static char *
jsp_unpack_string_value (char *buffer, DB_VALUE * retval)
{
  char *val;
  char *ptr;
  char *invalid_pos = NULL;

  ptr = buffer;
  ptr = or_unpack_string (ptr, &val);
  if (intl_check_string (val, -1, &invalid_pos) != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_CHAR, 1,
	      invalid_pos - val);
      return NULL;
    }
  db_make_string (retval, val);
  retval->need_clear = true;

  return ptr;
}

/*
 * jsp_unpack_date_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of date type
 *
 * Note:
 */

static char *
jsp_unpack_date_value (char *buffer, DB_VALUE * retval)
{
  DB_DATE date;
  char *val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_string_nocopy (ptr, &val);

  if (val == NULL || db_string_to_date (val, &date) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      db_value_put_encoded_date (retval, &date);
    }

  return ptr;
}

/*
 * jsp_unpack_time_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of time type
 *
 * Note:
 */

static char *
jsp_unpack_time_value (char *buffer, DB_VALUE * retval)
{
  DB_TIME time;
  char *val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_string_nocopy (ptr, &val);

  if (val == NULL || db_string_to_time (val, &time) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      db_value_put_encoded_time (retval, &time);
    }

  return ptr;
}

/*
 * jsp_unpack_timestamp_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of timestamp type
 *
 * Note:
 */

static char *
jsp_unpack_timestamp_value (char *buffer, DB_VALUE * retval)
{
  DB_TIMESTAMP timestamp;
  char *val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_string_nocopy (ptr, &val);

  if (val == NULL || db_string_to_timestamp (val, &timestamp) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      db_make_timestamp (retval, timestamp);
    }

  return ptr;
}

/*
 * jsp_unpack_datetime_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of datetime type
 *
 * Note:
 */

static char *
jsp_unpack_datetime_value (char *buffer, DB_VALUE * retval)
{
  DB_DATETIME datetime;
  char *val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_string_nocopy (ptr, &val);

  if (val == NULL || db_string_to_datetime (val, &datetime) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      DB_MAKE_DATETIME (retval, &datetime);
    }

  return ptr;
}

/*
 * jsp_unpack_set_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of set type
 *
 * Note:
 */

static char *
jsp_unpack_set_value (char *buffer, int type, DB_VALUE * retval)
{
  DB_SET *set;
  int ncol, i;
  char *ptr;
  DB_VALUE v;

  ptr = buffer;
  ptr = or_unpack_int (ptr, &ncol);
  set = set_create ((DB_TYPE) type, ncol);

  for (i = 0; i < ncol; i++)
    {
      ptr = jsp_unpack_value (ptr, &v);
      if (ptr == NULL || set_add_element (set, &v) != NO_ERROR)
	{
	  set_free (set);
	  break;
	}
      pr_clear_value (&v);
    }

  if (type == DB_TYPE_SET)
    {
      db_make_set (retval, set);
    }
  else if (type == DB_TYPE_MULTISET)
    {
      db_make_multiset (retval, set);
    }
  else if (type == DB_TYPE_SEQUENCE)
    {
      db_make_sequence (retval, set);
    }

  return ptr;

}

/*
 * jsp_unpack_object_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of object type
 *
 * Note:
 */

static char *
jsp_unpack_object_value (char *buffer, DB_VALUE * retval)
{
  OID oid;
  MOP obj;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_int (ptr, &(oid.pageid));
  ptr = or_unpack_short (ptr, &(oid.slotid));
  ptr = or_unpack_short (ptr, &(oid.volid));

  obj = ws_mop (&oid, NULL);
  db_make_object (retval, obj);

  return ptr;
}

/*
 * jsp_unpack_monetary_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of monetary type
 *
 * Note:
 */

static char *
jsp_unpack_monetary_value (char *buffer, DB_VALUE * retval)
{
  double val, result;
  char *ptr;

  ptr = buffer;
  memcpy ((char *) (&val), buffer, OR_DOUBLE_SIZE);
  OR_GET_DOUBLE (&val, &result);

  if (db_make_monetary (retval, DB_CURRENCY_DEFAULT, result) != NO_ERROR)
    {
      ptr = NULL;
    }
  else
    {
      ptr += OR_DOUBLE_SIZE;
    }

  return ptr;
}

/*
 * jsp_unpack_resultset -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): value of resultset type
 *
 * Note:
 */

static char *
jsp_unpack_resultset (char *buffer, DB_VALUE * retval)
{
  int val;
  char *ptr;

  ptr = buffer;
  ptr = or_unpack_int (ptr, &val);
  db_make_resultset (retval, val);

  return ptr;
}

/*
 * jsp_unpack_value -
 *   return: return unpacking value
 *   buffer(in/out): buffer
 *   retval(in): db value for unpacking
 *
 * Note:
 */

static char *
jsp_unpack_value (char *buffer, DB_VALUE * retval)
{
  char *ptr;
  int type;

  ptr = buffer;
  ptr = or_unpack_int (buffer, &type);

  switch (type)
    {
    case DB_TYPE_INTEGER:
      ptr = jsp_unpack_int_value (ptr, retval);
      break;

    case DB_TYPE_BIGINT:
      ptr = jsp_unpack_bigint_value (ptr, retval);
      break;

    case DB_TYPE_SHORT:
      ptr = jsp_unpack_short_value (ptr, retval);
      break;

    case DB_TYPE_FLOAT:
      ptr = jsp_unpack_float_value (ptr, retval);
      break;

    case DB_TYPE_DOUBLE:
      ptr = jsp_unpack_double_value (ptr, retval);
      break;

    case DB_TYPE_NUMERIC:
      ptr = jsp_unpack_numeric_value (ptr, retval);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_STRING:
      ptr = jsp_unpack_string_value (ptr, retval);
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      break;

    case DB_TYPE_DATE:
      ptr = jsp_unpack_date_value (ptr, retval);
      break;
      /*describe_data(); */

    case DB_TYPE_TIME:
      ptr = jsp_unpack_time_value (ptr, retval);
      break;

    case DB_TYPE_TIMESTAMP:
      ptr = jsp_unpack_timestamp_value (ptr, retval);
      break;

    case DB_TYPE_DATETIME:
      ptr = jsp_unpack_datetime_value (ptr, retval);
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      ptr = jsp_unpack_set_value (ptr, type, retval);
      break;

    case DB_TYPE_OBJECT:
      ptr = jsp_unpack_object_value (ptr, retval);
      break;

    case DB_TYPE_MONETARY:
      ptr = jsp_unpack_monetary_value (ptr, retval);
      break;

    case DB_TYPE_RESULTSET:
      ptr = jsp_unpack_resultset (ptr, retval);
      break;

    case DB_TYPE_NULL:
    default:
      db_make_null (retval);
      break;
    }

  return ptr;
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
  int start_code = -1, end_code = -1;
  int res_size;
  char *buffer, *ptr = NULL;
  int nbytes;
  DB_ARG_LIST *arg_list_p;
  int i;
  DB_VALUE temp;
  int error_code = NO_ERROR;

redo:
  nbytes = jsp_readn (sockfd, (char *) &start_code, (int) sizeof (int));
  if (nbytes != (int) sizeof (int))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
	      nbytes);
      return ER_SP_NETWORK_ERROR;
    }

  start_code = ntohl (start_code);

  if (start_code == 0x08)
    {
      tran_begin_libcas_function ();
      libcas_main (sockfd);     /* jdbc call */
      tran_end_libcas_function ();
      goto redo;
    }

  nbytes = jsp_readn (sockfd, (char *) &res_size, (int) sizeof (int));
  if (nbytes != (int) sizeof (int))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
	      nbytes);
      return ER_SP_NETWORK_ERROR;
    }

  res_size = ntohl (res_size);

  buffer = (char *) malloc (res_size);
  if (!buffer)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      res_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  nbytes = jsp_readn (sockfd, buffer, res_size);
  if (nbytes != res_size)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
	      nbytes);
      free_and_init (buffer);
      return ER_SP_NETWORK_ERROR;
    }

  if (start_code == 0x02)
    {				/* result */
      ptr = jsp_unpack_value (buffer, sp_args->returnval);
      if (ptr == NULL)
	{
	  error_code = er_errid ();
	  goto error;
	}

      for (arg_list_p = sp_args->args, i = 0; arg_list_p != NULL;
	   arg_list_p = arg_list_p->next, i++)
	{
	  if (sp_args->arg_mode[i] < SP_MODE_OUT)
	    {
	      continue;
	    }

	  ptr = jsp_unpack_value (ptr, &temp);
	  if (ptr == NULL)
	    {
	      db_value_clear (&temp);
	      error_code = er_errid ();
	      goto error;
	    }

	  db_value_clear (arg_list_p->val);
	  db_value_clone (&temp, arg_list_p->val);
	  db_value_clear (&temp);
	}
    }
  else if (start_code == 0x04)
    {				/* error */
      DB_VALUE error_value, error_msg;

      db_make_null (sp_args->returnval);
      ptr = jsp_unpack_value (buffer, &error_value);
      if (ptr == NULL)
	{
	  error_code = er_errid ();
	  goto error;
	}

      ptr = jsp_unpack_value (ptr, &error_msg);
      if (ptr == NULL)
	{
	  error_code = er_errid ();
	  goto error;
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_EXECUTE_ERROR, 1,
	      DB_GET_STRING (&error_msg));
      error_code = er_errid ();
      db_value_clear (&error_msg);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
	      start_code);
      free_and_init (buffer);
      return ER_SP_NETWORK_ERROR;
    }

  if (ptr)
    {
      ptr = or_unpack_int (ptr, &end_code);
      if (start_code != end_code)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
		  end_code);
	  free_and_init (buffer);
	  return ER_SP_NETWORK_ERROR;
	}
    }

error:
  free_and_init (buffer);
  return error_code;
}

/*
 * jsp_connect_server
 *   return: connect fail - return Error Code
 *           connection success - return socket fd
 *
 * Note:
 */

static SOCKET
jsp_connect_server (void)
{
  struct sockaddr_in tcp_srv_addr;
  SOCKET sockfd = INVALID_SOCKET;
  int success = -1;
  int server_port = -1;
  unsigned int inaddr;
  int b;
  char *server_host = (char *) "127.0.0.1";	/* assume as local host */

  server_port = jsp_get_server_port ();
  if (server_port < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NOT_RUNNING_JVM, 0);
      return INVALID_SOCKET;
    }

#if defined(CS_MODE)
  /* check for remote host */
  server_host = net_client_get_server_host ();
#endif

  memset ((void *) &tcp_srv_addr, 0, sizeof (tcp_srv_addr));
  tcp_srv_addr.sin_family = AF_INET;

  inaddr = inet_addr (server_host);
  if (inaddr != INADDR_NONE)
    {
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) &inaddr,
	      sizeof (inaddr));
    }
  else
    {
      struct hostent *hp;
      hp = gethostbyname (server_host);

      if (hp == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ERR_CSS_TCP_HOST_NAME_ERROR, 1, server_host);
	  return INVALID_SOCKET;

	}
      memcpy ((void *) &tcp_srv_addr.sin_addr, (void *) hp->h_addr,
	      hp->h_length);
    }

  tcp_srv_addr.sin_port = htons (server_port);

  sockfd = socket (AF_INET, SOCK_STREAM, 0);
  if (IS_INVALID_SOCKET (sockfd))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_CONNECT_JVM, 1,
	      "socket()");
      return INVALID_SOCKET;
    }

  success = connect (sockfd, (struct sockaddr *) &tcp_srv_addr,
		     sizeof (tcp_srv_addr));
  if (success < 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_CONNECT_JVM, 1,
	      "connect()");
      return INVALID_SOCKET;
    }

  b = 1;
  setsockopt (sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &b, sizeof (b));

  return sockfd;
}

/*
 * jsp_close_internal_connection -
 *   return: none
 *   sockfd(in) : close connection
 *
 * Note:
 */

static void
jsp_close_internal_connection (const SOCKET sockfd)
{
  struct linger linger_buffer;

  linger_buffer.l_onoff = 1;
  linger_buffer.l_linger = 0;
  setsockopt (sockfd, SOL_SOCKET, SO_LINGER, (char *) &linger_buffer,
	      sizeof (linger_buffer));
#if defined(WINDOWS)
  closesocket (sockfd);
#else /* not WINDOWS */
  close (sockfd);
#endif /* not WINDOWS */
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

retry:
  if (IS_INVALID_SOCKET (sock_fds[0]) || call_cnt > 0)
    {
      sock_fds[call_cnt] = jsp_connect_server ();
      if (IS_INVALID_SOCKET (sock_fds[call_cnt]))
	{
	  return er_errid ();
	}
    }

  sock_fd = sock_fds[call_cnt];
  call_cnt++;

  if (call_cnt >= MAX_CALL_COUNT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_TOO_MANY_NESTED_CALL,
	      0);
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

  error = jsp_receive_response (sock_fd, args);

end:
  call_cnt--;
  if (call_cnt > 0)
    {
      jsp_close_internal_connection (sock_fd);
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
jsp_do_call_stored_procedure (DB_VALUE * returnval,
			      DB_ARG_LIST * args, const char *name)
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
  memset (&sp_args, 0, sizeof (SP_ARGS));

  mop_p = jsp_find_stored_procedure (name);
  if (!mop_p)
    {
      err = er_errid ();
      goto error;
    }

  err = db_get (mop_p, SP_ATTR_TARGET, &method);
  if (err != NO_ERROR)
    {
      goto error;
    }

  sp_args.name = DB_GET_STRING (&method);
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

  param_cnt = DB_GET_INT (&param_cnt_val);
  arg_cnt = jsp_get_argument_count (&sp_args);
  if (param_cnt != arg_cnt)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_INVALID_PARAM_COUNT, 2,
	      param_cnt, arg_cnt);
      err = er_errid ();
      goto error;
    }
  sp_args.arg_count = arg_cnt;

  err = db_get (mop_p, SP_ATTR_ARGS, &param);
  if (err != NO_ERROR)
    {
      goto error;
    }

  param_set = DB_GET_SET (&param);

  for (i = 0; i < arg_cnt; i++)
    {
      set_get_element (param_set, i, &temp);
      arg_mop_p = DB_GET_OBJECT (&temp);

      err = db_get (arg_mop_p, SP_ATTR_MODE, &mode);
      if (err != NO_ERROR)
	{
	  pr_clear_value (&temp);
	  goto error;
	}

      sp_args.arg_mode[i] = DB_GET_INT (&mode);

      err = db_get (arg_mop_p, SP_ATTR_DATA_TYPE, &arg_type);
      if (err != NO_ERROR)
	{
	  pr_clear_value (&temp);
	  goto error;
	}

      sp_args.arg_type[i] = DB_GET_INT (&arg_type);
      pr_clear_value (&temp);

      if (sp_args.arg_type[i] == DB_TYPE_RESULTSET
	  && !is_prepare_call[call_cnt])
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_SP_CANNOT_RETURN_RESULTSET, 0);
	  err = er_errid ();
	  goto error;
	}
    }

  err = db_get (mop_p, SP_ATTR_RETURN_TYPE, &return_type);
  if (err != NO_ERROR)
    {
      goto error;
    }

  sp_args.return_type = DB_GET_INT (&return_type);

  if (sp_args.return_type == DB_TYPE_RESULTSET && !is_prepare_call[call_cnt])
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_RETURN_RESULTSET,
	      0);
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
jsp_call_from_server (DB_VALUE * returnval, DB_VALUE ** argarray,
		      const char *name, const int arg_cnt)
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
	  return er_errid ();
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
