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
 * execute_statement.c - functions to do execute
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#if defined(WINDOWS)
#include <process.h>		/* for getpid() */
#include <winsock2.h>		/* for struct timeval */
#else /* WINDOWS */
#include <unistd.h>		/* for getpid() */
#include <libgen.h>		/* for dirname, basename() */
#include <sys/time.h>		/* for struct timeval */
#endif /* WINDOWS */
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>


#include "error_manager.h"
#include "db.h"
#include "dbi.h"
#include "dbdef.h"
#include "dbtype.h"
#include "parser.h"
#include "porting.h"
#include "schema_manager.h"
#include "transform.h"
#include "parser_message.h"
#include "system_parameter.h"
#include "execute_statement.h"
#if defined(WINDOWS)
#include "misc_string.h"
#endif

#include "semantic_check.h"
#include "execute_schema.h"
#include "server_interface.h"
#include "transaction_cl.h"
#include "object_print.h"
#include "jsp_cl.h"
#include "optimizer.h"
#include "memory_alloc.h"
#include "object_domain.h"
#include "trigger_manager.h"
#include "release_string.h"
#include "object_accessor.h"
#include "locator_cl.h"
#include "authenticate.h"
#include "xasl_generation.h"
#include "virtual_object.h"
#include "xasl_support.h"
#include "query_opfunc.h"
#include "environment_variable.h"
#include "set_object.h"
#include "intl_support.h"
#include "replication.h"
#include "view_transform.h"
#include "network_interface_cl.h"
#include "arithmetic.h"
#include "serial.h"

/* this must be the last header file included!!! */
#include "dbval.h"

/*
 * Function Group:
 * Do create/alter/drop serial statement
 *
 */

#define PT_NODE_SR_NAME(node)			\
	((node)->info.serial.serial_name->info.name.original)
#define PT_NODE_SR_START_VAL(node)		\
	((node)->info.serial.start_val)
#define PT_NODE_SR_INCREMENT_VAL(node)		\
	((node)->info.serial.increment_val)
#define PT_NODE_SR_MAX_VAL(node)		\
	((node)->info.serial.max_val)
#define PT_NODE_SR_MIN_VAL(node)		\
	((node)->info.serial.min_val)
#define PT_NODE_SR_CYCLIC(node)			\
	((node)->info.serial.cyclic )
#define PT_NODE_SR_NO_MAX(node)			\
	((node)->info.serial.no_max )
#define PT_NODE_SR_NO_MIN(node)			\
	((node)->info.serial.no_min )
#define PT_NODE_SR_NO_CYCLIC(node)		\
	((node)->info.serial.no_cyclic )
#define PT_NODE_SR_CACHED_NUM_VAL(node)		\
	((node)->info.serial.cached_num_val)
#define PT_NODE_SR_NO_CACHE(node)		\
	((node)->info.serial.no_cache)


#define MAX_SERIAL_INVARIANT	8

typedef struct serial_invariant SERIAL_INVARIANT;

/* an invariant which serial must hold */
struct serial_invariant
{
  DB_VALUE val1;
  DB_VALUE val2;
  PT_OP_TYPE cmp_op;
  int val1_msgid;		/* the proper message id for val1.
				   0 means val1 should not be
				   responsible for the invariant
				   voilation */
  int val2_msgid;		/* the proper message id for val2.
				   0 means val2 should not be
				   responsible for the invariant
				   voilation */
  int error_type;		/* ER_QPROC_SERIAL_RANGE_OVERFLOW
				   or ER_INVALID_SERIAL_VALUE */
};

static void initialize_serial_invariant (SERIAL_INVARIANT * invariant,
					 DB_VALUE val1, DB_VALUE val2,
					 PT_OP_TYPE cmp_op, int val1_msgid,
					 int val2_msgid, int error_type);
static int check_serial_invariants (SERIAL_INVARIANT * invariants,
				    int num_invariants, int *ret_msg_id);
static bool is_schema_repl_log_statment (const PT_NODE * node);

/*
 * initialize_serial_invariant() - initialize a serial invariant
 *   return: None
 *   invariant(out):
 *   val1(in):
 *   val2(in):
 *   cmp_op(in):
 *   val1_msgid(in):
 *   val2_msgid(in):
 *   error_type(in):
 *
 * Note:
 */
static void
initialize_serial_invariant (SERIAL_INVARIANT * invariant,
			     DB_VALUE val1, DB_VALUE val2, PT_OP_TYPE cmp_op,
			     int val1_msgid, int val2_msgid, int error_type)
{
  invariant->val1 = val1;
  invariant->val2 = val2;
  invariant->cmp_op = cmp_op;
  invariant->val1_msgid = val1_msgid;
  invariant->val2_msgid = val2_msgid;
  invariant->error_type = error_type;
}

/*
 * check_serial_invariants() - check whether invariants have been violated
 *   return: Error code
 *   invariants(in):
 *   num_invariants(in):
 *   ret_msg_id(out):
 *
 * Note:
 */

static int
check_serial_invariants (SERIAL_INVARIANT * invariants, int num_invariants,
			 int *ret_msg_id)
{
  int i, c;
  int error;
  DB_VALUE cmp_result;

  for (i = 0; i < num_invariants; i++)
    {

      error =
	numeric_db_value_compare (&invariants[i].val1, &invariants[i].val2,
				  &cmp_result);
      if (error != NO_ERROR)
	{
	  return error;
	}

      c = DB_GET_INT (&cmp_result);
      switch (invariants[i].cmp_op)
	{
	case PT_GT:
	  if (c > 0)
	    {
	      /* same as expected */
	      continue;
	    }
	  break;
	case PT_GE:
	  if (c >= 0)
	    {
	      continue;
	    }
	  break;
	case PT_LT:
	  if (c < 0)
	    {
	      continue;
	    }
	  break;
	case PT_LE:
	  if (c <= 0)
	    {
	      continue;
	    }
	  break;
	case PT_EQ:
	  if (c == 0)
	    {
	      continue;
	    }
	  break;
	case PT_NE:
	  if (c != 0)
	    {
	      continue;
	    }
	  break;
	default:
	  /* impossible to get here! */
	  assert (0);
	  break;
	}

      /* get here means invariant violated! */
      if (invariants[i].val1_msgid != 0)
	{
	  *ret_msg_id = invariants[i].val1_msgid;
	  return invariants[i].error_type;
	}

      if (invariants[i].val2_msgid != 0)
	{
	  *ret_msg_id = invariants[i].val2_msgid;
	  return invariants[i].error_type;
	}

      /* impossible to get here! */
      assert (0);
    }

  return NO_ERROR;
}

/*
 * is_schema_repl_log_statment()
 *   return: true if it's a schema replications log statement
 *           otherwise false
 *   node(in):
 */
static bool
is_schema_repl_log_statment (const PT_NODE * node)
{
  /* All DDLs will be replicated via schema replication */
  if (pt_is_ddl_statement (node))
    {
      return true;
    }

  /* some DMLs will also be replicated via schema replication instead of data replication */
  switch (node->node_type)
    {
    case PT_DROP_VARIABLE:
    case PT_TRUNCATE:
      return true;
    default:
      break;
    }

  return false;
}

/*
 * do_evaluate_default_expr() - evaluates the default expressions, if any, for
 *				the attributes of a given class
 *   return: Error code
 *   parser(in):
 *   class_name(in):
 */
int
do_evaluate_default_expr (PARSER_CONTEXT * parser, PT_NODE * class_name)
{
  SM_ATTRIBUTE *att;
  SM_CLASS *smclass;
  int error;
  TP_DOMAIN_STATUS status;
  assert (class_name->node_type == PT_NAME);

  error = au_fetch_class_force (class_name->info.name.db_object, &smclass,
				AU_FETCH_READ);
  if (error != NO_ERROR)
    {
      return error;
    }

  for (att = smclass->attributes; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->default_value.default_expr != DB_DEFAULT_NONE)
	{
	  switch (att->default_value.default_expr)
	    {
	    case DB_DEFAULT_SYSDATE:
	      error = db_value_put_encoded_date (&att->default_value.value,
						 DB_GET_DATE (&parser->
							      sys_datetime));
	      break;
	    case DB_DEFAULT_SYSDATETIME:
	      error = pr_clone_value (&parser->sys_datetime,
				      &att->default_value.value);
	      break;
	    case DB_DEFAULT_SYSTIMESTAMP:
	      error = db_datetime_to_timestamp (&parser->sys_datetime,
						&att->default_value.value);
	      break;
	    case DB_DEFAULT_UNIX_TIMESTAMP:
	      error = db_unix_timestamp (&parser->sys_datetime,
					 &att->default_value.value);
	      break;
	    case DB_DEFAULT_USER:
	      error = db_make_string (&att->default_value.value,
				      db_get_user_and_host_name ());
	      att->default_value.value.need_clear = true;
	      break;
	    case DB_DEFAULT_CURR_USER:
	      error = DB_MAKE_STRING (&att->default_value.value,
				      db_get_user_name ());
	      att->default_value.value.need_clear = true;
	      break;
	    default:
	      break;
	    }

	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	  /* make sure the default value can be used for this attribute */
	  status =
	    tp_value_strict_cast (&att->default_value.value,
				  &att->default_value.value, att->domain);
	  if (status != DOMAIN_COMPATIBLE)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TP_CANT_COERCE, 2,
		      pr_type_name (DB_VALUE_TYPE
				    (&att->default_value.value)),
		      pr_type_name (TP_DOMAIN_TYPE (att->domain)));
	      return ER_FAILED;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * do_create_serial_internal() -
 *   return: Error code
 *   serial_object(out):
 *   serial_name(in):
 *   current_val(in):
 *   inc_val(in):
 *   min_val(in):
 *   max_val(in):
 *   cyclic(in):
 *   started(in):
 *   class_name(in):
 *   att_name(in):
 *
 * Note:
 */
static int
do_create_serial_internal (MOP * serial_object,
			   const char *serial_name,
			   DB_VALUE * current_val,
			   DB_VALUE * inc_val,
			   DB_VALUE * min_val,
			   DB_VALUE * max_val,
			   const int cyclic,
			   const int cached_num,
			   const int started,
			   const char *class_name, const char *att_name)
{
  DB_OBJECT *ret_obj = NULL;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE value;
  DB_OBJECT *serial_class = NULL;
  int au_save, error = NO_ERROR;

  db_make_null (&value);

  /* temporarily disable authorization to access db_serial class */
  AU_DISABLE (au_save);

  serial_class = sm_find_class (CT_SERIAL_NAME);
  if (serial_class == NULL)
    {
      error = ER_QPROC_DB_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  obj_tmpl = dbt_create_object_internal ((MOP) serial_class);
  if (obj_tmpl == NULL)
    {
      error = er_errid ();
      goto end;
    }

  /* name */
  db_make_string (&value, serial_name);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_NAME, &value);
  pr_clear_value (&value);
  if (error < 0)
    {
      goto end;
    }

  /* owner */
  db_make_object (&value, Au_user);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_OWNER, &value);
  pr_clear_value (&value);
  if (error < 0)
    goto end;

  /* current_val */
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_CURRENT_VAL, current_val);
  if (error < 0)
    {
      goto end;
    }

  /* increment_val */
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_INCREMENT_VAL, inc_val);
  if (error < 0)
    {
      goto end;
    }

  /* min_val */
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_MIN_VAL, min_val);
  if (error < 0)
    goto end;

  /* max_val */
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_MAX_VAL, max_val);
  if (error < 0)
    {
      goto end;
    }

  /* cyclic */
  db_make_int (&value, cyclic);	/* always false */
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_CYCLIC, &value);
  pr_clear_value (&value);
  if (error < 0)
    {
      goto end;
    }

  /* started */
  db_make_int (&value, started);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_STARTED, &value);
  pr_clear_value (&value);
  if (error < 0)
    {
      goto end;
    }

  /* class name */
  if (class_name)
    {
      db_make_string (&value, class_name);
      error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_CLASS_NAME, &value);
      pr_clear_value (&value);
      if (error < 0)
	{
	  goto end;
	}
    }

  /* att name */
  if (att_name)
    {
      db_make_string (&value, att_name);
      error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_ATT_NAME, &value);
      pr_clear_value (&value);
      if (error < 0)
	{
	  goto end;
	}
    }

  /* cached num */
  if (cached_num > 0)
    {
      DB_MAKE_INT (&value, cached_num);
      error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_CACHED_NUM, &value);
      pr_clear_value (&value);
      if (error < 0)
	{
	  goto end;
	}
    }

  ret_obj = dbt_finish_object (obj_tmpl);

  if (ret_obj == NULL)
    {
      error = er_errid ();
    }
  else if (serial_object != NULL)
    {
      *serial_object = ret_obj;
    }

end:
  AU_ENABLE (au_save);
  return error;
}

/*
 * do_update_auto_increment_serial_on_rename() -
 *   return: Error code
 *   serial_obj(in/out):
 *   class_name(in):
 *   att_name(in):
 *
 * Note:
 */
int
do_update_auto_increment_serial_on_rename (MOP serial_obj,
					   const char *class_name,
					   const char *att_name)
{
  int error = NO_ERROR;
  DB_OBJECT *serial_object = NULL;
  DB_VALUE value;
  DB_OTMPL *obj_tmpl = NULL;
  char *serial_name = NULL;
  char att_downcase_name[SM_MAX_IDENTIFIER_LENGTH];
  size_t name_len;

  if (!serial_obj || !class_name || !att_name)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  db_make_null (&value);

  serial_object = serial_obj;
  sm_downcase_name (att_name, att_downcase_name, SM_MAX_IDENTIFIER_LENGTH);
  att_name = att_downcase_name;

  /* serial_name : <class_name>_ai_<att_name> */
  name_len = (strlen (class_name) + strlen (att_name)
	      + AUTO_INCREMENT_SERIAL_NAME_EXTRA_LENGTH + 1);
  serial_name = (char *) malloc (name_len);
  if (serial_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, name_len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  SET_AUTO_INCREMENT_SERIAL_NAME (serial_name, class_name, att_name);

  obj_tmpl = dbt_edit_object (serial_object);
  if (obj_tmpl == NULL)
    {
      error = er_errid ();
      goto update_auto_increment_error;
    }

  /* name */
  db_make_string (&value, serial_name);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_NAME, &value);
  if (error < 0)
    {
      goto update_auto_increment_error;
    }

  /* class name */
  db_make_string (&value, class_name);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_CLASS_NAME, &value);
  if (error < 0)
    {
      goto update_auto_increment_error;
    }

  /* att name */
  db_make_string (&value, att_name);
  error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_ATT_NAME, &value);
  if (error < 0)
    {
      goto update_auto_increment_error;
    }

  serial_object = dbt_finish_object (obj_tmpl);

  if (serial_object == NULL)
    {
      error = er_errid ();
      goto update_auto_increment_error;
    }

  free_and_init (serial_name);
  return NO_ERROR;

update_auto_increment_error:
  if (serial_name)
    {
      free_and_init (serial_name);
    }

  return (error);
}

/*
 * do_reset_auto_increment_serial() -
 *   return: Error code
 *   serial_obj(in/out):
 */
int
do_reset_auto_increment_serial (MOP serial_obj)
{
  int error_code = NO_ERROR;
  DB_OBJECT *const serial_object = serial_obj;
  DB_OBJECT *edit_serial_object = NULL;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE start_value;
  DB_VALUE started_flag;

  if (serial_object == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  db_make_null (&start_value);
  db_make_null (&started_flag);

  error_code = db_get (serial_object, SERIAL_ATTR_MIN_VAL, &start_value);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  obj_tmpl = dbt_edit_object (serial_object);
  if (obj_tmpl == NULL)
    {
      error_code = er_errid ();
      goto error_exit;
    }

  error_code =
    dbt_put_internal (obj_tmpl, SERIAL_ATTR_CURRENT_VAL, &start_value);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  db_make_int (&started_flag, 0);
  error_code =
    dbt_put_internal (obj_tmpl, SERIAL_ATTR_STARTED, &started_flag);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  edit_serial_object = dbt_finish_object (obj_tmpl);
  if (edit_serial_object == NULL)
    {
      error_code = er_errid ();
      goto error_exit;
    }

  assert (edit_serial_object == serial_object);
  obj_tmpl = NULL;

  error_code = locator_flush_instance (edit_serial_object);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  db_value_clear (&start_value);
  db_value_clear (&started_flag);

  return error_code;

error_exit:

  if (obj_tmpl != NULL)
    {
      dbt_abort_object (obj_tmpl);
    }

  db_value_clear (&start_value);
  db_value_clear (&started_flag);

  return error_code;
}


/*
 * do_change_auto_increment_serial() -
 *   return: Error code
 *   serial_obj(in/out):
 */
int
do_change_auto_increment_serial (PARSER_CONTEXT * const parser,
				 MOP serial_obj, PT_NODE * node_new_val)
{
  int error_code = NO_ERROR;
  DB_OBJECT *const serial_object = serial_obj;
  DB_OBJECT *edit_serial_object = NULL;
  DB_OTMPL *obj_tmpl = NULL;

  DB_VALUE max_val;
  DB_VALUE started;
  DB_VALUE new_val;
  DB_VALUE cmp_result;
  DB_VALUE *pval = NULL;
  DB_DATA_STATUS data_status;


  int cmp;


  /*
   * 1. obtain NUMERIC value from node_new_val
   * 2. obtain max value of the serial.
   * 3. if the new value is greater than max, throw an error
   * 4. reset the serial: started = 0, cur = min = new cur;
   */


  if (serial_object == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  db_make_null (&max_val);
  db_make_null (&new_val);
  db_make_null (&started);
  db_make_int (&cmp_result, 0);


  /* create a NUMERIC value in new_val */
  db_value_domain_init (&new_val, DB_TYPE_NUMERIC,
			DB_MAX_NUMERIC_PRECISION, 0);
  pval = pt_value_to_db (parser, node_new_val);
  if (pval == NULL)
    {
      error_code = er_errid ();
      goto error_exit;
    }

  error_code = numeric_db_value_coerce_to_num (pval, &new_val, &data_status);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }


  /* get the serial's max from db. */
  error_code = db_get (serial_object, SERIAL_ATTR_MAX_VAL, &max_val);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }


  /* The new value must be lower than the max value */
  error_code = numeric_db_value_compare (&new_val, &max_val, &cmp_result);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  cmp = DB_GET_INT (&cmp_result);
  if (cmp >= 0)
    {
      error_code = ER_AUTO_INCREMENT_NEWVAL_MUST_LT_MAXVAL;
      ERROR0 (error_code, ER_AUTO_INCREMENT_NEWVAL_MUST_LT_MAXVAL);
      goto error_exit;
    }

  /*
     RESET serial:
     min = new_val;
     cur = new_val;
     started = 0
   */

  obj_tmpl = dbt_edit_object (serial_object);
  if (obj_tmpl == NULL)
    {
      error_code = er_errid ();
      goto error_exit;
    }

  error_code = dbt_put_internal (obj_tmpl, SERIAL_ATTR_CURRENT_VAL, &new_val);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  error_code = dbt_put_internal (obj_tmpl, SERIAL_ATTR_MIN_VAL, &new_val);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  db_make_int (&started, 0);
  error_code = dbt_put_internal (obj_tmpl, SERIAL_ATTR_STARTED, &started);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  edit_serial_object = dbt_finish_object (obj_tmpl);
  if (edit_serial_object == NULL)
    {
      error_code = er_errid ();
      goto error_exit;
    }

  assert (edit_serial_object == serial_object);
  obj_tmpl = NULL;

  error_code = locator_flush_instance (edit_serial_object);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  goto normal_exit;


error_exit:

  if (obj_tmpl != NULL)
    {
      dbt_abort_object (obj_tmpl);
    }

normal_exit:

  db_value_clear (&max_val);
  db_value_clear (&new_val);
  db_value_clear (&started);
  db_value_clear (&cmp_result);

  return error_code;
}


/*
 * do_get_serial_obj_id() -
 *   return: serial object
 *   serial_obj_id(out):
 *   serial_class_mop(in):
 *   serial_name(in):
 *
 * Note:
 */
MOP
do_get_serial_obj_id (DB_IDENTIFIER * serial_obj_id,
		      DB_OBJECT * serial_class_mop, const char *serial_name)
{
  DB_OBJECT *mop;
  DB_VALUE val;
  DB_IDENTIFIER *db_id;
  char *p;
  int serial_name_size;
  int save;

  if (serial_class_mop == NULL || serial_name == NULL)
    {
      return NULL;
    }

  serial_name_size = intl_identifier_lower_string_size (serial_name);
  p = (char *) malloc (serial_name_size + 1);
  if (p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, (serial_name_size + 1));
      return NULL;
    }

  intl_identifier_lower (serial_name, p);
  db_make_string (&val, p);

  er_stack_push ();
  AU_DISABLE (save);
  mop = db_find_unique (serial_class_mop, SERIAL_ATTR_NAME, &val);
  AU_ENABLE (save);
  er_stack_pop ();

  if (mop)
    {
      db_id = ws_identifier (mop);

      if (db_id != NULL)
	{
	  *serial_obj_id = *db_id;
	}
      else
	{
	  mop = NULL;
	}
    }

  pr_clear_value (&val);
  free_and_init (p);

  return mop;
}

/*
 * do_get_serial_cached_num() -
 *   return: Error code
 *   cached_num(out) :
 *   serial_obj(in)  :
 *
 * Note:
 */
int
do_get_serial_cached_num (int *cached_num, MOP serial_obj)
{
  DB_VALUE cached_num_val;
  int error;

  error = db_get (serial_obj, SERIAL_ATTR_CACHED_NUM, &cached_num_val);
  if (error != NO_ERROR)
    {
      return error;
    }

  assert (DB_VALUE_TYPE (&cached_num_val) == DB_TYPE_INTEGER);

  *cached_num = DB_GET_INT (&cached_num_val);

  return NO_ERROR;
}

/*
 * do_create_serial() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in):
 *
 * Note:
 */
int
do_create_serial (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  DB_OBJECT *serial_class = NULL, *serial_object = NULL;
  MOP serial_mop;
  DB_IDENTIFIER serial_obj_id;
  DB_VALUE value, *pval = NULL;

  char *name = NULL;
  PT_NODE *start_val_node;
  PT_NODE *inc_val_node;
  PT_NODE *max_val_node;
  PT_NODE *min_val_node;
  PT_NODE *cached_num_node;

  DB_VALUE zero, e37, under_e36;
  DB_VALUE start_val, inc_val, max_val, min_val, cached_num_val;
  DB_VALUE cmp_result;
  DB_VALUE result_val;
  DB_VALUE tmp_val, tmp_val2;
  DB_VALUE abs_inc_val, range_val;

  int min_val_msgid = 0;
  int max_val_msgid = 0;
  int start_val_msgid = 0;
  int inc_val_msgid = 0;
  int ret_msg_id = 0;
  SERIAL_INVARIANT invariants[MAX_SERIAL_INVARIANT];
  int ninvars = 0;


  unsigned char num[DB_NUMERIC_BUF_SIZE];

  int inc_val_flag = 0, cyclic;
  int cached_num;
  DB_DATA_STATUS data_stat;
  int error = NO_ERROR;
  int found = 0, r = 0, save;
  bool au_disable_flag = false;
  char *p = NULL;
  int name_size;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  db_make_null (&value);
  db_make_null (&zero);
  db_make_null (&e37);
  db_make_null (&under_e36);
  db_make_null (&start_val);
  db_make_null (&inc_val);
  db_make_null (&max_val);
  db_make_null (&min_val);
  db_make_null (&abs_inc_val);
  db_make_null (&range_val);

  /*
   * find db_serial_class
   */
  serial_class = sm_find_class (CT_SERIAL_NAME);
  if (serial_class == NULL)
    {
      error = ER_QPROC_DB_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  /*
   * lookup if serial object name already exists?
   */

  name = (char *) PT_NODE_SR_NAME (statement);
  name_size = intl_identifier_lower_string_size (name);
  p = (char *) malloc (name_size + 1);
  if (p == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, (name_size + 1));
      goto end;
    }
  intl_identifier_lower (name, p);

  serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class, p);
  if (serial_mop != NULL)
    {
      error = ER_QPROC_SERIAL_ALREADY_EXIST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_SERIAL_ALREADY_EXIST, name);
      goto end;
    }

  /* get all values as string */
  numeric_coerce_string_to_num ("0", 1, &zero);
  numeric_coerce_string_to_num ("10000000000000000000000000000000000000",
				DB_MAX_NUMERIC_PRECISION, &e37);
  numeric_coerce_string_to_num ("-1000000000000000000000000000000000000",
				DB_MAX_NUMERIC_PRECISION, &under_e36);
  db_make_int (&cmp_result, 0);

  start_val_node = PT_NODE_SR_START_VAL (statement);
  inc_val_node = PT_NODE_SR_INCREMENT_VAL (statement);
  min_val_node = PT_NODE_SR_MIN_VAL (statement);
  max_val_node = PT_NODE_SR_MAX_VAL (statement);

  /* increment_val */
  db_value_domain_init (&inc_val,
			DB_TYPE_NUMERIC, DB_MAX_NUMERIC_PRECISION, 0);
  if (inc_val_node != NULL)
    {
      pval = pt_value_to_db (parser, inc_val_node);
      if (pval == NULL)
	{
	  error = er_errid ();
	  goto end;
	}

      error = numeric_db_value_coerce_to_num (pval, &inc_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pval = NULL;

      /* check if increment value is 0 */
      error = numeric_db_value_compare (&inc_val, &zero, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      inc_val_flag = DB_GET_INT (&cmp_result);
      if (inc_val_flag == 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_INC_VAL_ZERO, 0);
	  goto end;
	}
      inc_val_msgid = MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID;
    }
  else
    {
      /* inc_val = 1; */
      db_make_int (&value, 1);
      error = numeric_db_value_coerce_to_num (&value, &inc_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      inc_val_flag = +1;
    }

  /* start_val 1 */
  db_value_domain_init (&start_val,
			DB_TYPE_NUMERIC, DB_MAX_NUMERIC_PRECISION, 0);
  if (start_val_node != NULL)
    {
      pval = pt_value_to_db (parser, start_val_node);
      if (pval == NULL)
	{
	  error = er_errid ();
	  goto end;
	}
      error = numeric_db_value_coerce_to_num (pval, &start_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pval = NULL;
      start_val_msgid = MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID;
    }

  db_value_domain_init (&min_val,
			DB_TYPE_NUMERIC, DB_MAX_NUMERIC_PRECISION, 0);
  /*
   * min_val comes from several sources, it can be one of them:
   * 1. user input
   * 2. start_val
   * 3. 1
   * 4. -e36
   * min_val_msgid is the proper message id. it's for error message generation
   * when min_val violates some invariants.
   * if min_val is 1 or -e36, min_val_msgid is set to 0 (default value) because
   *  constants can't be the reason which violate invariants.
   * if min_val is from user input, min_val_msgid is set to
   * MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID.
   * if min_val is from start_val, min_val_msgid is set to
   * MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID.
   */
  if (min_val_node != NULL)
    {
      pval = pt_value_to_db (parser, min_val_node);
      if (pval == NULL)
	{
	  error = er_errid ();
	  goto end;
	}

      error = numeric_db_value_coerce_to_num (pval, &min_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pval = NULL;
      min_val_msgid = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID;
    }
  else
    {
      if (inc_val_flag > 0)
	{
	  if (start_val_node != NULL)
	    {
	      db_value_clone (&start_val, &min_val);
	      min_val_msgid = MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID;
	    }
	  else
	    {
	      /* min_val = 1; */
	      db_make_int (&value, 1);
	      error = numeric_db_value_coerce_to_num (&value,
						      &min_val, &data_stat);
	      if (error != NO_ERROR)
		{
		  goto end;
		}
	    }
	}
      else
	{
	  /* min_val = - 1.0e36; */
	  db_value_clone (&under_e36, &min_val);
	}
    }

  /* max_val */
  db_value_domain_init (&max_val,
			DB_TYPE_NUMERIC, DB_MAX_NUMERIC_PRECISION, 0);

  if (max_val_node != NULL)
    {
      pval = pt_value_to_db (parser, max_val_node);
      if (pval == NULL)
	{
	  error = er_errid ();
	  goto end;
	}

      error = numeric_db_value_coerce_to_num (pval, &max_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pval = NULL;
      max_val_msgid = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID;
    }
  else
    {
      if (inc_val_flag > 0)
	{
	  /* max_val = 1.0e37; */
	  db_value_clone (&e37, &max_val);
	}
      else
	{
	  if (start_val_node != NULL)
	    {
	      /* max_val = start_val */
	      db_value_clone (&start_val, &max_val);
	      max_val_msgid = MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID;
	    }
	  else
	    {
	      /* max_val = -1; */
	      db_make_int (&value, -1);
	      error = numeric_db_value_coerce_to_num (&value,
						      &max_val, &data_stat);
	      if (error != NO_ERROR)
		{
		  goto end;
		}
	    }
	}
    }

  /* start_val 2 */
  if (start_val_node == NULL)
    {
      pr_clear_value (&start_val);
      if (inc_val_flag > 0)
	{
	  /* start_val = min_val; */
	  db_value_clone (&min_val, &start_val);
	  start_val_msgid = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID;
	}
      else
	{
	  /* start_val = max_val; */
	  db_value_clone (&max_val, &start_val);
	  start_val_msgid = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID;
	}
    }


  /* cyclic */
  cyclic = PT_NODE_SR_CYCLIC (statement);

  /*
   * check values
   * min_val    start_val     max_val
   *    |--^--^--^--o--^--^--^--^---|
   *                   <--> inc_val
   */

  /*
   * the following invariants must hold:
   * min_val >= under_e36
   * max_val <= e37
   * min_val < max_val
   * min_val <= start_val
   * max_val >= start_val
   * inc_val != zero
   * abs(inc_val) <= (max_val - min_val)
   */

  /*
   * invariant for min_val >= under_e36.
   * if min_val_msgid == MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID,
   * that means the value of min_val is from start_val, if the invariant
   * is voilated, start_val invalid error message should be displayed
   * instead of min_val underflow. the val2_msgid is 0 because under_e36
   * cannot be the reason which voilates the invariant.
   */
  initialize_serial_invariant (&invariants[ninvars++], min_val, under_e36,
			       PT_GE,
			       (min_val_msgid ==
				MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID) ?
			       MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID :
			       MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW, 0,
			       ER_QPROC_SERIAL_RANGE_OVERFLOW);

  /*
   * invariant for max_val <= e37. Like the above invariant, if
   * max_val_msgid == MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID, 
   * start_val invalid error message should be displayed if the invariant
   * is voilated.
   */
  initialize_serial_invariant (&invariants[ninvars++], max_val, e37, PT_LE,
			       (max_val_msgid ==
				MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID) ?
			       MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID :
			       MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW, 0,
			       ER_QPROC_SERIAL_RANGE_OVERFLOW);

  /* invariant for min_val < max_val. */
  initialize_serial_invariant (&invariants[ninvars++], min_val, max_val,
			       PT_LT, min_val_msgid, max_val_msgid,
			       ER_INVALID_SERIAL_VALUE);

  /* invariant for min_val <= start_val */
  initialize_serial_invariant (&invariants[ninvars++], min_val, start_val,
			       PT_LE, min_val_msgid, start_val_msgid,
			       ER_INVALID_SERIAL_VALUE);

  /* invariant for max_val >= start_val */
  initialize_serial_invariant (&invariants[ninvars++], max_val, start_val,
			       PT_GE, max_val_msgid, start_val_msgid,
			       ER_INVALID_SERIAL_VALUE);

  /* invariant for inc_val != zero */
  initialize_serial_invariant (&invariants[ninvars++], inc_val, zero, PT_NE,
			       MSGCAT_SEMANTIC_SERIAL_INC_VAL_ZERO, 0,
			       ER_INVALID_SERIAL_VALUE);

  /*
   * invariant for abs(inc_val) <= (max_val - min_val).
   * if this invariant is voilated, inc_val, min_val or max_val should be
   * responsible for it. If max_val_msgid == 0, which means max_val is 
   * initialized from a constant, not inputted by user,  in this case, we don't
   * expect max_val should be responsible for the violation.
   */
  numeric_db_value_sub (&max_val, &min_val, &range_val);
  db_abs_dbval (&abs_inc_val, &inc_val);
  initialize_serial_invariant (&invariants[ninvars++], abs_inc_val, range_val,
			       PT_LE, inc_val_msgid,
			       (max_val_msgid ==
				0) ? min_val_msgid : max_val_msgid,
			       ER_INVALID_SERIAL_VALUE);


  /* cached num */
  cached_num_node = PT_NODE_SR_CACHED_NUM_VAL (statement);
  if (cached_num_node != NULL)
    {
      assert (cached_num_node->type_enum == PT_TYPE_INTEGER);

      cached_num = cached_num_node->info.value.data_value.i;

      /* result_val = ABS(CEIL((max_val - min_val) / inc_val)) */
      error = numeric_db_value_sub (&max_val, &min_val, &tmp_val);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      error = numeric_db_value_div (&tmp_val, &inc_val, &tmp_val2);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      error = db_abs_dbval (&tmp_val, &tmp_val2);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      error = db_ceil_dbval (&result_val, &tmp_val);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      pr_clear_value (&tmp_val);
      pr_clear_value (&tmp_val2);

      numeric_coerce_int_to_num (cached_num, num);
      DB_MAKE_NUMERIC (&cached_num_val, num, DB_MAX_NUMERIC_PRECISION, 0);

      /*
       * must holds: cached_num_val <= result_val
       * invariant for cached_num_val <= ABS(CEIL((max_val - min_val) / inc_val))
       */
      initialize_serial_invariant (&invariants[ninvars++], cached_num_val,
				   result_val, PT_LE,
				   MSGCAT_SEMANTIC_SERIAL_CACHED_NUM_INVALID_RANGE,
				   0, ER_INVALID_SERIAL_VALUE);
    }
  else
    {
      cached_num = 0;
    }

  assert (ninvars <= MAX_SERIAL_INVARIANT);
  error = check_serial_invariants (invariants, ninvars, &ret_msg_id);

  if (error != NO_ERROR)
    {
      if (error == ER_QPROC_SERIAL_RANGE_OVERFLOW
	  || error == ER_INVALID_SERIAL_VALUE)
	{
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      ret_msg_id, 0);
	}
      goto end;
    }
  /* now create serial object which is insert into db_serial */
  AU_DISABLE (save);
  au_disable_flag = true;

  error = do_create_serial_internal (&serial_object, p, &start_val, &inc_val,
				     &min_val, &max_val, cyclic, cached_num,
				     0, NULL, NULL);

  AU_ENABLE (save);
  au_disable_flag = false;

  if (error < 0)
    {
      goto end;
    }

  pr_clear_value (&zero);
  pr_clear_value (&e37);
  pr_clear_value (&under_e36);
  pr_clear_value (&start_val);
  pr_clear_value (&inc_val);
  pr_clear_value (&max_val);
  pr_clear_value (&min_val);
  pr_clear_value (&result_val);

  free_and_init (p);

  return NO_ERROR;

end:
  pr_clear_value (&value);
  pr_clear_value (&zero);
  pr_clear_value (&e37);
  pr_clear_value (&under_e36);
  pr_clear_value (&start_val);
  pr_clear_value (&inc_val);
  pr_clear_value (&max_val);
  pr_clear_value (&min_val);
  pr_clear_value (&result_val);

  if (au_disable_flag == true)
    AU_ENABLE (save);

  if (p)
    {
      free_and_init (p);
    }
  return error;
}

/*
 * do_create_auto_increment_serial() -
 *   return: Error code
 *   parser(in): Parser context
 *   serial_object(out):
 *   class_name(in):
 *   att(in):
 *
 * Note:
 */
int
do_create_auto_increment_serial (PARSER_CONTEXT * parser, MOP * serial_object,
				 const char *class_name, PT_NODE * att)
{
  MOP serial_class = NULL, serial_mop;
  DB_IDENTIFIER serial_obj_id;
  DB_DATA_STATUS data_stat;
  int error = NO_ERROR;
  PT_NODE *auto_increment_node, *start_val_node, *inc_val_node;
  PT_NODE *dtyp;
  char *att_name = NULL, *serial_name = NULL;
  DB_VALUE start_val, inc_val, max_val, min_val;
  DB_VALUE zero, value, *pval = NULL;
  DB_VALUE cmp_result;
  int found = 0, r = 0, i;
  DB_VALUE e38;
  char *p, num[DB_MAX_NUMERIC_PRECISION + 1];
  char att_downcase_name[SM_MAX_IDENTIFIER_LENGTH];
  size_t name_len;

  db_make_null (&e38);
  db_make_null (&value);
  db_make_null (&zero);
  db_make_null (&cmp_result);
  db_make_null (&start_val);
  db_make_null (&inc_val);
  db_make_null (&max_val);
  db_make_null (&min_val);

  numeric_coerce_string_to_num ("0", 1, &zero);
  numeric_coerce_string_to_num ("99999999999999999999999999999999999999",
				DB_MAX_NUMERIC_PRECISION, &e38);

  assert_release (att->info.attr_def.auto_increment != NULL);
  auto_increment_node = att->info.attr_def.auto_increment;
  if (auto_increment_node == NULL)
    {
      goto end;
    }

  /*
   * find db_serial
   */
  serial_class = sm_find_class (CT_SERIAL_NAME);
  if (serial_class == NULL)
    {
      error = ER_QPROC_DB_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  att_name = (char *) (att->info.attr_def.attr_name->alias_print
		       ? att->info.attr_def.attr_name->alias_print
		       : att->info.attr_def.attr_name->info.name.original);

  sm_downcase_name (att_name, att_downcase_name, SM_MAX_IDENTIFIER_LENGTH);
  att_name = att_downcase_name;

  /* serial_name : <class_name>_ai_<att_name> */
  name_len = (strlen (class_name) + strlen (att_name)
	      + AUTO_INCREMENT_SERIAL_NAME_EXTRA_LENGTH + 1);
  serial_name = (char *) malloc (name_len);
  if (serial_name == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name_len);
      goto end;
    }

  SET_AUTO_INCREMENT_SERIAL_NAME (serial_name, class_name, att_name);

  serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class,
				     serial_name);
  if (serial_mop != NULL)
    {
      error = ER_AUTO_INCREMENT_SERIAL_ALREADY_EXIST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  start_val_node = auto_increment_node->info.auto_increment.start_val;
  inc_val_node = auto_increment_node->info.auto_increment.increment_val;

  /* increment_val */
  db_value_domain_init (&inc_val,
			DB_TYPE_NUMERIC, DB_MAX_NUMERIC_PRECISION, 0);

  if (inc_val_node != NULL)
    {
      pval = pt_value_to_db (parser, inc_val_node);
      if (pval == NULL)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  goto end;
	}

      error = numeric_db_value_coerce_to_num (pval, &inc_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      pval = NULL;

      /* check increment value */
      error = numeric_db_value_compare (&inc_val, &zero, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      if (DB_GET_INT (&cmp_result) <= 0)
	{
	  error = ER_INCREMENT_VALUE_CANNOT_BE_ZERO;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto end;
	}
    }
  else
    {
      /* set to 1 */
      db_make_int (&value, 1);
      error = numeric_db_value_coerce_to_num (&value, &inc_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  /* start_val */
  db_value_domain_init (&start_val,
			DB_TYPE_NUMERIC, DB_MAX_NUMERIC_PRECISION, 0);
  if (start_val_node != NULL)
    {
      pval = pt_value_to_db (parser, start_val_node);
      if (pval == NULL)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  goto end;
	}
      error = numeric_db_value_coerce_to_num (pval, &start_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pval = NULL;
    }
  else
    {
      /* set to 1 */
      db_make_int (&value, 1);
      error = numeric_db_value_coerce_to_num (&value, &start_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

  /* min value = start_val */
  db_value_clone (&start_val, &min_val);

  /* max value - depends on att's domain */
  db_value_domain_init (&max_val,
			DB_TYPE_NUMERIC, DB_MAX_NUMERIC_PRECISION, 0);

  dtyp = att->data_type;
  switch (att->type_enum)
    {
    case PT_TYPE_INTEGER:
      db_make_int (&value, DB_INT32_MAX);
      break;
    case PT_TYPE_BIGINT:
      db_make_bigint (&value, DB_BIGINT_MAX);
      break;
    case PT_TYPE_SMALLINT:
      db_make_int (&value, DB_INT16_MAX);
      break;
    case PT_TYPE_NUMERIC:
      memset (num, '\0', DB_MAX_NUMERIC_PRECISION + 1);
      for (i = 0, p = num; i < dtyp->info.data_type.precision; i++, p++)
	{
	  *p = '9';
	}

      *p = '\0';

      (void) numeric_coerce_string_to_num (num,
					   dtyp->info.data_type.precision,
					   &value);
      break;
    default:
      /* max numeric */
      db_value_clone (&e38, &value);
    }

  error = numeric_db_value_coerce_to_num (&value, &max_val, &data_stat);
  if (error != NO_ERROR)
    {
      goto end;
    }

  /* check (start_val < max_val) */
  if (tp_value_compare (&start_val, &max_val, 1, 0) != DB_LT)
    {
      error = ER_AUTO_INCREMENT_STARTVAL_MUST_LT_MAXVAL;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  /* create auto increment serial object */
  error = do_create_serial_internal (serial_object, serial_name, &start_val,
				     &inc_val, &min_val, &max_val, 0, 0, 0,
				     class_name, att_name);
  if (error < 0)
    {
      goto end;
    }

  pr_clear_value (&e38);
  pr_clear_value (&value);
  pr_clear_value (&zero);
  pr_clear_value (&cmp_result);
  pr_clear_value (&start_val);
  pr_clear_value (&inc_val);
  pr_clear_value (&max_val);
  pr_clear_value (&min_val);

  free_and_init (serial_name);

  return NO_ERROR;

end:
  pr_clear_value (&e38);
  pr_clear_value (&value);
  pr_clear_value (&zero);
  pr_clear_value (&cmp_result);
  pr_clear_value (&start_val);
  pr_clear_value (&inc_val);
  pr_clear_value (&max_val);
  pr_clear_value (&min_val);

  if (serial_name)
    free_and_init (serial_name);

  return error;
}

/*
 * do_alter_serial() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in):
 *
 * Note:
 */
int
do_alter_serial (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  DB_OBJECT *serial_class = NULL, *serial_object = NULL;
  DB_IDENTIFIER serial_obj_id;
  DB_OTMPL *obj_tmpl = NULL;
  DB_VALUE value, *pval;

  char *name = NULL;
  PT_NODE *start_val_node;
  PT_NODE *inc_val_node;
  PT_NODE *max_val_node;
  PT_NODE *min_val_node;
  PT_NODE *cached_num_node;

  DB_VALUE zero, e37, under_e36;
  DB_DATA_STATUS data_stat;
  DB_VALUE old_inc_val, old_max_val, old_min_val, old_cached_num;
  DB_VALUE current_val, start_val, cached_num_val;
  DB_VALUE new_inc_val, new_max_val, new_min_val;
  DB_VALUE cmp_result;
  DB_VALUE result_val;
  DB_VALUE class_name_val;
  DB_VALUE tmp_val, tmp_val2;
  DB_VALUE abs_inc_val, range_val;
  int cached_num;
  int ret_msg_id = 0;

  unsigned char num[DB_NUMERIC_BUF_SIZE];

  int new_inc_val_flag = 0, new_cyclic;
  int cur_val_change, inc_val_change, max_val_change, min_val_change,
    cyclic_change, cached_num_change;

  int error = NO_ERROR;
  int found = 0, r = 0, save;
  bool au_disable_flag = false;

  SERIAL_INVARIANT invariants[MAX_SERIAL_INVARIANT];
  int ninvars = 0;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  db_make_null (&value);
  db_make_null (&zero);
  db_make_null (&e37);
  db_make_null (&under_e36);
  db_make_null (&old_inc_val);
  db_make_null (&old_max_val);
  db_make_null (&old_min_val);
  db_make_null (&new_inc_val);
  db_make_null (&new_max_val);
  db_make_null (&new_min_val);
  db_make_null (&current_val);
  db_make_null (&start_val);
  db_make_null (&class_name_val);
  db_make_null (&abs_inc_val);
  db_make_null (&range_val);


  /*
   * find db_serial_class
   */
  serial_class = sm_find_class (CT_SERIAL_NAME);
  if (serial_class == NULL)
    {
      error = ER_QPROC_DB_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  /*
   * lookup if serial object name already exists?
   */

  name = (char *) PT_NODE_SR_NAME (statement);

  serial_object = do_get_serial_obj_id (&serial_obj_id, serial_class, name);
  if (serial_object == NULL)
    {
      error = ER_QPROC_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED, name);
      goto end;
    }

  error = db_get (serial_object, SERIAL_ATTR_CLASS_NAME, &class_name_val);
  if (error < 0)
    {
      goto end;
    }

  /*
   * check if user is creator or dba
   */
  error = au_check_serial_authorization (serial_object);
  if (error != NO_ERROR)
    {
      if (error == ER_QPROC_CANNOT_UPDATE_SERIAL)
	{
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_RT_SERIAL_ALTER_NOT_ALLOWED, 0);
	}
      goto end;
    }

  /* get old values */
  error = db_get (serial_object, SERIAL_ATTR_CURRENT_VAL, &current_val);
  if (error < 0)
    {
      goto end;
    }

  error = db_get (serial_object, SERIAL_ATTR_INCREMENT_VAL, &old_inc_val);
  if (error < 0)
    {
      goto end;
    }

  error = db_get (serial_object, SERIAL_ATTR_MAX_VAL, &old_max_val);
  if (error < 0)
    goto end;

  error = db_get (serial_object, SERIAL_ATTR_MIN_VAL, &old_min_val);
  if (error < 0)
    {
      goto end;
    }

  error = db_get (serial_object, SERIAL_ATTR_CACHED_NUM, &old_cached_num);
  if (error < 0)
    {
      cached_num = 0;
    }
  else
    {
      cached_num = DB_GET_INT (&old_cached_num);
    }

  /* Now, get new values from node */

  numeric_coerce_string_to_num ("0", 1, &zero);
  numeric_coerce_string_to_num ("10000000000000000000000000000000000000",
				DB_MAX_NUMERIC_PRECISION, &e37);
  numeric_coerce_string_to_num ("-1000000000000000000000000000000000000",
				DB_MAX_NUMERIC_PRECISION, &under_e36);
  db_make_int (&cmp_result, 0);

  db_value_domain_init (&new_inc_val, DB_TYPE_NUMERIC,
			DB_MAX_NUMERIC_PRECISION, 0);
  inc_val_node = PT_NODE_SR_INCREMENT_VAL (statement);
  if (inc_val_node != NULL)
    {
      inc_val_change = 1;
      pval = pt_value_to_db (parser, inc_val_node);
      if (pval == NULL)
	{
	  error = er_errid ();
	  goto end;
	}
      error = numeric_db_value_coerce_to_num (pval, &new_inc_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pval = NULL;

      error = numeric_db_value_compare (&new_inc_val, &zero, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      new_inc_val_flag = DB_GET_INT (&cmp_result);
      /* new_inc_val == 0 */
      if (new_inc_val_flag == 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_INC_VAL_ZERO, 0);
	  goto end;
	}
    }
  else
    {
      inc_val_change = 0;
      /* new_inc_val = old_inc_val; */
      db_value_clone (&old_inc_val, &new_inc_val);
      error = numeric_db_value_compare (&new_inc_val, &zero, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      new_inc_val_flag = DB_GET_INT (&cmp_result);
    }

  /* start_val */
  db_value_domain_init (&start_val, DB_TYPE_NUMERIC,
			DB_MAX_NUMERIC_PRECISION, 0);
  start_val_node = PT_NODE_SR_START_VAL (statement);
  if (start_val_node != NULL)
    {
      cur_val_change = 1;
      pval = pt_value_to_db (parser, start_val_node);
      if (pval == NULL)
	{
	  error = er_errid ();
	  goto end;
	}
      error = numeric_db_value_coerce_to_num (pval, &start_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pval = NULL;
    }
  else
    {
      cur_val_change = 0;
      db_value_clone (&current_val, &start_val);
    }

  /* max_val */
  db_value_domain_init (&new_max_val, DB_TYPE_NUMERIC,
			DB_MAX_NUMERIC_PRECISION, 0);
  max_val_node = PT_NODE_SR_MAX_VAL (statement);
  if (max_val_node != NULL)
    {
      max_val_change = 1;
      pval = pt_value_to_db (parser, max_val_node);
      if (pval == NULL)
	{
	  error = er_errid ();
	  goto end;
	}
      error = numeric_db_value_coerce_to_num (pval, &new_max_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pval = NULL;
    }
  else
    {
      if (PT_NODE_SR_NO_MAX (statement) == 1)
	{
	  max_val_change = 1;
	  if (new_inc_val_flag > 0)
	    {
	      /* new_max_val = 1.0e37; */
	      db_value_clone (&e37, &new_max_val);
	    }
	  else
	    {
	      /* new_max_val = -1; */
	      db_make_int (&value, -1);
	      error = numeric_db_value_coerce_to_num (&value,
						      &new_max_val,
						      &data_stat);
	      if (error != NO_ERROR)
		{
		  goto end;
		}
	    }
	}
      else
	{
	  max_val_change = 0;
	  /* new_max_val = old_max_val; */
	  db_value_clone (&old_max_val, &new_max_val);
	}
    }

  /* min_val */
  db_value_domain_init (&new_min_val, DB_TYPE_NUMERIC,
			DB_MAX_NUMERIC_PRECISION, 0);
  min_val_node = PT_NODE_SR_MIN_VAL (statement);
  if (min_val_node != NULL)
    {
      min_val_change = 1;
      pval = pt_value_to_db (parser, min_val_node);
      if (pval == NULL)
	{
	  error = er_errid ();
	  goto end;
	}
      error = numeric_db_value_coerce_to_num (pval, &new_min_val, &data_stat);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pval = NULL;
    }
  else
    {
      if (PT_NODE_SR_NO_MIN (statement) == 1)
	{
	  min_val_change = 1;

	  if (new_inc_val_flag > 0)
	    {
	      /* new_min_val = 1; */
	      db_make_int (&value, 1);
	      error = numeric_db_value_coerce_to_num (&value,
						      &new_min_val,
						      &data_stat);
	      if (error != NO_ERROR)
		{
		  goto end;
		}
	    }
	  else
	    {
	      /* new_min_val = - 1.0e36; */
	      db_value_clone (&under_e36, &new_min_val);
	    }
	}
      else
	{
	  min_val_change = 0;
	  /* new_min_val = old_min_val; */
	  db_value_clone (&old_min_val, &new_min_val);
	}
    }


  /* cyclic */
  new_cyclic = PT_NODE_SR_CYCLIC (statement);
  if ((new_cyclic == 1) || (PT_NODE_SR_NO_CYCLIC (statement) == 1))
    {
      cyclic_change = 1;
    }
  else
    {
      cyclic_change = 0;
    }


  /*
   * check values
   * min_val    start_val     max_val
   *    |--^--^--^--o--^--^--^--^---|
   *                   <--> inc_val
   */

  /* invariant for min_val >= under_e36. */
  initialize_serial_invariant (&invariants[ninvars++], new_min_val, under_e36,
			       PT_GE,
			       MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW, 0,
			       ER_QPROC_SERIAL_RANGE_OVERFLOW);

  /* invariant for max_val <= e37. */
  initialize_serial_invariant (&invariants[ninvars++], new_max_val, e37,
			       PT_LE, MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW,
			       0, ER_QPROC_SERIAL_RANGE_OVERFLOW);

  /* invariant for min_val < max_val. */
  initialize_serial_invariant (&invariants[ninvars++], new_min_val,
			       new_max_val, PT_LT,
			       (min_val_change) ?
			       MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID : 0,
			       (max_val_change) ?
			       MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID : 0,
			       ER_INVALID_SERIAL_VALUE);

  /* invariant for min_val <= start_val */
  initialize_serial_invariant (&invariants[ninvars++], new_min_val, start_val,
			       PT_LE,
			       (min_val_change) ?
			       MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID : 0,
			       (cur_val_change) ?
			       MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID : 0,
			       ER_INVALID_SERIAL_VALUE);

  /* invariant for max_val >= start_val */
  initialize_serial_invariant (&invariants[ninvars++], new_max_val, start_val,
			       PT_GE,
			       (max_val_change) ?
			       MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID : 0,
			       (cur_val_change) ?
			       MSGCAT_SEMANTIC_SERIAL_START_VAL_INVALID : 0,
			       ER_INVALID_SERIAL_VALUE);

  /* invariant for inc_val != zero */
  initialize_serial_invariant (&invariants[ninvars++], new_inc_val, zero,
			       PT_NE, MSGCAT_SEMANTIC_SERIAL_INC_VAL_ZERO, 0,
			       ER_INVALID_SERIAL_VALUE);

  /* invariant for abs(inc_val) <= (max_val - min_val). */
  numeric_db_value_sub (&new_max_val, &new_min_val, &range_val);
  db_abs_dbval (&abs_inc_val, &new_inc_val);
  initialize_serial_invariant (&invariants[ninvars++], abs_inc_val, range_val,
			       PT_LE,
			       (inc_val_change) ?
			       MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID : 0,
			       (max_val_change) ?
			       MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID :
			       MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID,
			       ER_INVALID_SERIAL_VALUE);


  /* cached num */
  cached_num_node = PT_NODE_SR_CACHED_NUM_VAL (statement);
  if (cached_num_node != NULL)
    {
      assert (cached_num_node->type_enum == PT_TYPE_INTEGER);

      cached_num_change = 1;
      cached_num = cached_num_node->info.value.data_value.i;

      /* result_val = ABS(CEIL((max_val - min_val) / inc_val)) */
      error = numeric_db_value_sub (&new_max_val, &new_min_val, &tmp_val);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      error = numeric_db_value_div (&tmp_val, &new_inc_val, &tmp_val2);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      error = db_abs_dbval (&tmp_val, &tmp_val2);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      error = db_ceil_dbval (&result_val, &tmp_val);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      pr_clear_value (&tmp_val);
      pr_clear_value (&tmp_val2);

      numeric_coerce_int_to_num (cached_num, num);
      DB_MAKE_NUMERIC (&cached_num_val, num, DB_MAX_NUMERIC_PRECISION, 0);

      /*
       * must holds: cached_num_val <= result_val
       * invariant for cached_num_val <= ABS(CEIL((max_val - min_val) / inc_val))
       */
      initialize_serial_invariant (&invariants[ninvars++], cached_num_val,
				   result_val, PT_LE,
				   MSGCAT_SEMANTIC_SERIAL_CACHED_NUM_INVALID_RANGE,
				   0, ER_INVALID_SERIAL_VALUE);
    }
  else
    {
      if (PT_NODE_SR_NO_CACHE (statement) == 1)
	{
	  cached_num_change = 1;
	  cached_num = 0;
	}
      else
	{
	  cached_num_change = 0;
	}
    }

  assert (ninvars <= MAX_SERIAL_INVARIANT);
  error = check_serial_invariants (invariants, ninvars, &ret_msg_id);

  if (error != NO_ERROR)
    {
      if (error == ER_QPROC_SERIAL_RANGE_OVERFLOW
	  || error == ER_INVALID_SERIAL_VALUE)
	{
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      ret_msg_id, 0);
	}
      goto end;
    }
  /* now update serial object in db_serial */
  AU_DISABLE (save);
  au_disable_flag = true;

  obj_tmpl = dbt_edit_object (serial_object);
  if (obj_tmpl == NULL)
    {
      error = er_errid ();
      goto end;
    }

  /* current_val */
  if (cur_val_change)
    {
      error =
	dbt_put_internal (obj_tmpl, SERIAL_ATTR_CURRENT_VAL, &start_val);
      if (error < 0)
	{
	  goto end;
	}
      /* reset started flag because current_val changed */
      DB_MAKE_INT (&value, 0);
      error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_STARTED, &value);
      if (error < 0)
	{
	  goto end;
	}
      pr_clear_value (&value);
    }

  /* increment_val */
  if (inc_val_change)
    {
      error =
	dbt_put_internal (obj_tmpl, SERIAL_ATTR_INCREMENT_VAL, &new_inc_val);
      if (error < 0)
	{
	  goto end;
	}
    }

  /* max_val */
  if (max_val_change)
    {
      error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_MAX_VAL, &new_max_val);
      if (error < 0)
	{
	  goto end;
	}
    }

  /* min_val */
  if (min_val_change)
    {
      error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_MIN_VAL, &new_min_val);
      if (error < 0)
	{
	  goto end;
	}
    }

  /* cyclic */
  if (cyclic_change)
    {
      db_make_int (&value, new_cyclic);
      error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_CYCLIC, &value);
      if (error < 0)
	{
	  goto end;
	}
      pr_clear_value (&value);
    }

  /* cached num */
  if (cached_num_change)
    {
      DB_MAKE_INT (&value, cached_num);
      error = dbt_put_internal (obj_tmpl, SERIAL_ATTR_CACHED_NUM, &value);
      if (error < 0)
	{
	  goto end;
	}
      pr_clear_value (&value);
    }

  serial_object = dbt_finish_object (obj_tmpl);

  AU_ENABLE (save);
  au_disable_flag = false;

  if (serial_object == NULL)
    {
      error = er_errid ();
      goto end;
    }

  (void) serial_decache ((OID *) (&serial_obj_id));

  pr_clear_value (&zero);
  pr_clear_value (&e37);
  pr_clear_value (&under_e36);
  pr_clear_value (&start_val);
  pr_clear_value (&current_val);
  pr_clear_value (&old_inc_val);
  pr_clear_value (&old_max_val);
  pr_clear_value (&old_min_val);
  pr_clear_value (&new_inc_val);
  pr_clear_value (&new_max_val);
  pr_clear_value (&new_min_val);
  pr_clear_value (&result_val);

  return NO_ERROR;

end:
  (void) serial_decache ((OID *) (&serial_obj_id));

  pr_clear_value (&value);
  pr_clear_value (&zero);
  pr_clear_value (&e37);
  pr_clear_value (&under_e36);
  pr_clear_value (&start_val);
  pr_clear_value (&current_val);
  pr_clear_value (&old_inc_val);
  pr_clear_value (&old_max_val);
  pr_clear_value (&old_min_val);
  pr_clear_value (&new_inc_val);
  pr_clear_value (&new_max_val);
  pr_clear_value (&new_min_val);
  pr_clear_value (&result_val);
  if (au_disable_flag == true)
    {
      AU_ENABLE (save);
    }
  return error;
}

/*
 * do_drop_serial() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in):
 *
 * Note:
 */
int
do_drop_serial (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  DB_OBJECT *serial_class = NULL, *serial_object = NULL;
  DB_IDENTIFIER serial_obj_id;
  DB_VALUE class_name_val;
  char *name;
  int error = NO_ERROR;
  int found = 0, r = 0, save;
  bool au_disable_flag = false;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  db_make_null (&class_name_val);

  serial_class = sm_find_class (CT_SERIAL_NAME);
  if (serial_class == NULL)
    {
      error = ER_QPROC_DB_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  name = (char *) PT_NODE_SR_NAME (statement);

  serial_object = do_get_serial_obj_id (&serial_obj_id, serial_class, name);
  if (serial_object == NULL)
    {
      error = ER_QPROC_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED, name);
      goto end;
    }

  error = db_get (serial_object, SERIAL_ATTR_CLASS_NAME, &class_name_val);
  if (error < 0)
    {
      goto end;
    }

  if (!DB_IS_NULL (&class_name_val))
    {
      error = ER_QPROC_CANNOT_UPDATE_SERIAL;
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_SERIAL_IS_AUTO_INCREMENT_OBJ, name);
      pr_clear_value (&class_name_val);
      goto end;
    }

  /*
   * check if user is creator or dba
   */
  error = au_check_serial_authorization (serial_object);
  if (error != NO_ERROR)
    {
      if (error == ER_QPROC_CANNOT_UPDATE_SERIAL)
	{
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_RT_SERIAL_ALTER_NOT_ALLOWED, 0);
	}
      goto end;
    }

  AU_DISABLE (save);
  au_disable_flag = true;

  error = db_drop (serial_object);
  if (error < 0)
    {
      goto end;
    }

  AU_ENABLE (save);
  au_disable_flag = false;

  (void) serial_decache (&serial_obj_id);

  return NO_ERROR;

end:
  (void) serial_decache (&serial_obj_id);

  if (au_disable_flag == true)
    {
      AU_ENABLE (save);
    }
  return error;
}



/*
 * Function Group:
 * Entry functions to do execute
 *
 */

#define ER_PT_UNKNOWN_STATEMENT ER_GENERIC_ERROR
#define UNIQUE_SAVEPOINT_EXTERNAL_STATEMENT "eXTERNALsTATEMENT"

bool do_Trigger_involved;

/*
 * do_statement() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *
 * Note: Side effects can exist at the statement, especially schema information
 */
int
do_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  QUERY_EXEC_MODE old_exec_mode;
  bool old_disable_update_stats;

  /* If it is an internally created statement,
     set its host variable info again to search host variables at parent parser */
  SET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);

  if (statement)
    {
      /* only SELECT query can be executed in async mode */
      old_exec_mode = parser->exec_mode;
      parser->exec_mode = (statement->node_type == PT_SELECT) ?
	old_exec_mode : SYNC_EXEC;

      /* for the subset of nodes which represent top level statements,
       * process them. For any other node, return an error.
       */

      old_disable_update_stats = sm_Disable_updating_statistics;

      /* disable data replication log for schema replication log types */
      if (is_schema_repl_log_statment (statement))
	{
	  db_set_suppress_repl_on_transaction (true);
	}

      switch (statement->node_type)
	{
	case PT_ALTER:
	  sm_Disable_updating_statistics =
	    (statement->info.alter.hint & PT_HINT_NO_STATS)
	    ? true : old_disable_update_stats;
	  error = do_check_internal_statements (parser, statement,
						/* statement->info.alter.
						   internal_stmts, */
						do_alter);
	  sm_Disable_updating_statistics = old_disable_update_stats;
	  break;

	case PT_ATTACH:
	  error = do_attach (parser, statement);
	  break;

	case PT_PREPARE_TO_COMMIT:
	  error = do_prepare_to_commit (parser, statement);
	  break;

	case PT_COMMIT_WORK:
	  error = do_commit (parser, statement);
	  break;

	case PT_CREATE_ENTITY:
	  sm_Disable_updating_statistics =
	    (statement->info.create_entity.hint & PT_HINT_NO_STATS)
	    ? true : old_disable_update_stats;
	  error = do_check_internal_statements (parser, statement,
						/* statement->info.create_entity.
						   internal_stmts, */
						do_create_entity);
	  sm_Disable_updating_statistics = old_disable_update_stats;
	  break;

	case PT_CREATE_INDEX:
	  sm_Disable_updating_statistics =
	    (statement->info.index.hint & PT_HINT_NO_STATS)
	    ? true : old_disable_update_stats;
	  error = do_create_index (parser, statement);
	  sm_Disable_updating_statistics = old_disable_update_stats;
	  break;

	case PT_EVALUATE:
	  error = do_evaluate (parser, statement);
	  break;

	case PT_SCOPE:
	  error = do_scope (parser, statement);
	  break;

	case PT_DELETE:
	  error = do_check_delete_trigger (parser, statement, do_delete);
	  break;

	case PT_DROP:
	  error = do_check_internal_statements (parser, statement,
						/* statement->info.drop.
						   internal_stmts, */
						do_drop);
	  break;

	case PT_DROP_INDEX:
	  sm_Disable_updating_statistics =
	    (statement->info.index.hint & PT_HINT_NO_STATS)
	    ? true : old_disable_update_stats;
	  error = do_drop_index (parser, statement);
	  sm_Disable_updating_statistics = old_disable_update_stats;
	  break;

	case PT_ALTER_INDEX:
	  sm_Disable_updating_statistics =
	    (statement->info.index.hint & PT_HINT_NO_STATS)
	    ? true : old_disable_update_stats;
	  error = do_alter_index (parser, statement);
	  sm_Disable_updating_statistics = old_disable_update_stats;
	  break;

	case PT_DROP_VARIABLE:
	  error = do_drop_variable (parser, statement);
	  break;

	case PT_GRANT:
	  error = do_grant (parser, statement);
	  break;

	case PT_INSERT:
	  error = do_check_insert_trigger (parser, statement, do_insert);
	  break;

	case PT_RENAME:
	  error = do_rename (parser, statement);
	  break;

	case PT_REVOKE:
	  error = do_revoke (parser, statement);
	  break;

	case PT_CREATE_USER:
	  error = do_create_user (parser, statement);
	  break;

	case PT_DROP_USER:
	  error = do_drop_user (parser, statement);
	  break;

	case PT_ALTER_USER:
	  error = do_alter_user (parser, statement);
	  break;

	case PT_SET_XACTION:
	  error = do_set_xaction (parser, statement);
	  break;

	case PT_GET_XACTION:
	  error = do_get_xaction (parser, statement);
	  break;

	case PT_ROLLBACK_WORK:
	  error = do_rollback (parser, statement);
	  break;

	case PT_SAVEPOINT:
	  error = do_savepoint (parser, statement);
	  break;

	case PT_UNION:
	case PT_DIFFERENCE:
	case PT_INTERSECTION:
	case PT_SELECT:
	  error = do_select (parser, statement);
	  break;

	case PT_TRUNCATE:
	  error = do_truncate (parser, statement);
	  break;

	case PT_DO:
	  error = do_execute_do (parser, statement);
	  break;

	case PT_UPDATE:
	  error = do_check_update_trigger (parser, statement, do_update);
	  break;

	case PT_MERGE:
	  error = do_check_merge_trigger (parser, statement, do_merge);
	  break;

	case PT_UPDATE_STATS:
	  error = do_update_stats (parser, statement);
	  break;

	case PT_GET_STATS:
	  error = do_get_stats (parser, statement);
	  break;

	case PT_METHOD_CALL:
	  error = do_call_method (parser, statement);
	  break;

	case PT_CREATE_TRIGGER:
	  error = do_create_trigger (parser, statement);
	  break;

	case PT_DROP_TRIGGER:
	  error = do_drop_trigger (parser, statement);
	  break;

	case PT_SET_TRIGGER:
	  error = do_set_trigger (parser, statement);
	  break;

	case PT_GET_TRIGGER:
	  error = do_get_trigger (parser, statement);
	  break;

	case PT_RENAME_TRIGGER:
	  error = do_rename_trigger (parser, statement);
	  break;

	case PT_ALTER_TRIGGER:
	  error = do_alter_trigger (parser, statement);
	  break;

	case PT_EXECUTE_TRIGGER:
	  error = do_execute_trigger (parser, statement);
	  break;

	case PT_REMOVE_TRIGGER:
	  error = do_remove_trigger (parser, statement);
	  break;

	case PT_CREATE_SERIAL:
	  error = do_create_serial (parser, statement);
	  break;

	case PT_ALTER_SERIAL:
	  error = do_alter_serial (parser, statement);
	  break;

	case PT_DROP_SERIAL:
	  error = do_drop_serial (parser, statement);
	  break;

	case PT_GET_OPT_LVL:
	  error = do_get_optimization_param (parser, statement);
	  break;

	case PT_SET_OPT_LVL:
	  error = do_set_optimization_param (parser, statement);
	  break;

	case PT_SET_SYS_PARAMS:
	  error = do_set_sys_params (parser, statement);
	  break;

	case PT_CREATE_STORED_PROCEDURE:
	  error = jsp_create_stored_procedure (parser, statement);
	  break;

	case PT_ALTER_STORED_PROCEDURE_OWNER:
	  error = jsp_alter_stored_procedure_owner (parser, statement);
	  break;

	case PT_DROP_STORED_PROCEDURE:
	  error = jsp_drop_stored_procedure (parser, statement);
	  break;

	case PT_SET_NAMES:
	  error = do_set_names (parser, statement);
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, __FILE__, statement->line_number,
		  ER_PT_UNKNOWN_STATEMENT, 1, statement->node_type);
	  break;
	}

      /* enable data replication log */
      if (is_schema_repl_log_statment (statement))
	{
	  db_set_suppress_repl_on_transaction (false);
	}

      /* restore execution flag */
      parser->exec_mode = old_exec_mode;

      /* write schema replication log */
      if (error == NO_ERROR)
	{
	  error = do_replicate_schema (parser, statement);
	}
    }


  /* There may be parse tree fragments that were collected during the
   * execution of the statement that should be freed now.
   */
  pt_free_orphans (parser);

  /* During query execution,
   * if current transaction was rollbacked by the system,
   * abort transaction on client side also.
   */
  if (error == ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_only_client (false);
    }

  RESET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);

  if (error == ER_FAILED)
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_GENERIC_ERROR;
	}
    }
  return error;
}

/*
 * do_prepare_statement() - Prepare a given statement for execution
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *
 * Note:
 * 	PREPARE includes query optimization and plan generation (XASL) for the SQL
 * 	statement. EXECUTE means requesting the server to execute the given XASL.
 *
 * 	Some type of statement is not necessary or not able to do PREPARE stage.
 * 	They can or must be EXECUTEd directly without PREPARE. For those types of
 * 	statements, this function will return NO_ERROR.
 */
int
do_prepare_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;

  switch (statement->node_type)
    {
    case PT_DELETE:
      err = do_prepare_delete (parser, statement);
      break;
    case PT_INSERT:
      err = do_prepare_insert (parser, statement);
      break;
    case PT_UPDATE:
      err = do_prepare_update (parser, statement);
      break;
    case PT_MERGE:
      err = do_prepare_merge (parser, statement);
      break;
    case PT_SELECT:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
      err = do_prepare_select (parser, statement);
      break;
    case PT_EXECUTE_PREPARE:
      err = do_prepare_session_statement (parser, statement);
      break;
    default:
      /* there are no actions for other types of statements */
      break;
    }

  return ((err == ER_FAILED && (err = er_errid ()) == NO_ERROR)
	  ? ER_GENERIC_ERROR : err);
}				/* do_prepare_statement() */

/*
 * do_execute_statement() - Execute a prepared statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *
 * Note:
 * 	The statement should be PREPAREd before to EXECUTE. But, some type of
 * 	statement will be EXECUTEd directly without PREPARE stage because we can
 * 	decide the fact that they should be executed using query plan (XASL)
 * 	at the time of execution stage.
 */
int
do_execute_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;
  QUERY_EXEC_MODE old_exec_mode;
  bool old_disable_update_stats;

  /* If it is an internally created statement,
     set its host variable info again to search host variables at parent parser */
  SET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);

  /* only SELECT query can be executed in async mode */
  old_exec_mode = parser->exec_mode;
  parser->exec_mode = (statement->node_type == PT_SELECT) ?
    old_exec_mode : SYNC_EXEC;

  /* for the subset of nodes which represent top level statements,
     process them; for any other node, return an error */

  old_disable_update_stats = sm_Disable_updating_statistics;

  /* disable data replication log for schema replication log types */
  if (is_schema_repl_log_statment (statement))
    {
      db_set_suppress_repl_on_transaction (true);
    }

  switch (statement->node_type)
    {
    case PT_CREATE_ENTITY:
      sm_Disable_updating_statistics =
	(statement->info.create_entity.hint & PT_HINT_NO_STATS)
	? true : old_disable_update_stats;
      /* err = do_create_entity(parser, statement); */
      /* execute internal statements before and after do_create_entity() */
      err = do_check_internal_statements (parser, statement,
					  /* statement->info.create_entity.
					     internal_stmts, */
					  do_create_entity);
      sm_Disable_updating_statistics = old_disable_update_stats;
      break;
    case PT_CREATE_INDEX:
      sm_Disable_updating_statistics =
	(statement->info.index.hint & PT_HINT_NO_STATS)
	? true : old_disable_update_stats;
      err = do_create_index (parser, statement);
      sm_Disable_updating_statistics = old_disable_update_stats;
      break;
    case PT_CREATE_SERIAL:
      err = do_create_serial (parser, statement);
      break;
    case PT_CREATE_TRIGGER:
      err = do_create_trigger (parser, statement);
      break;
    case PT_CREATE_USER:
      err = do_create_user (parser, statement);
      break;
    case PT_ALTER:
      sm_Disable_updating_statistics =
	(statement->info.alter.hint & PT_HINT_NO_STATS)
	? true : old_disable_update_stats;
      /* err = do_alter(parser, statement); */
      /* execute internal statements before and after do_alter() */
      err = do_check_internal_statements (parser, statement,
					  /* statement->info.alter.
					     internal_stmts, */ do_alter);
      sm_Disable_updating_statistics = old_disable_update_stats;
      break;
    case PT_ALTER_INDEX:
      sm_Disable_updating_statistics =
	(statement->info.index.hint & PT_HINT_NO_STATS)
	? true : old_disable_update_stats;
      err = do_alter_index (parser, statement);
      sm_Disable_updating_statistics = old_disable_update_stats;
      break;
    case PT_ALTER_SERIAL:
      err = do_alter_serial (parser, statement);
      break;
    case PT_ALTER_TRIGGER:
      err = do_alter_trigger (parser, statement);
      break;
    case PT_ALTER_USER:
      err = do_alter_user (parser, statement);
      break;
    case PT_DROP:
      /* err = do_drop(parser, statement); */
      /* execute internal statements before and after do_drop() */
      err = do_check_internal_statements (parser, statement,
					  /* statement->info.drop.internal_stmts, */
					  do_drop);
      break;
    case PT_DROP_INDEX:
      sm_Disable_updating_statistics =
	(statement->info.index.hint & PT_HINT_NO_STATS)
	? true : old_disable_update_stats;
      err = do_drop_index (parser, statement);
      sm_Disable_updating_statistics = old_disable_update_stats;
      break;
    case PT_DROP_SERIAL:
      err = do_drop_serial (parser, statement);
      break;
    case PT_DROP_TRIGGER:
      err = do_drop_trigger (parser, statement);
      break;
    case PT_DROP_USER:
      err = do_drop_user (parser, statement);
      break;
    case PT_DROP_VARIABLE:
      err = do_drop_variable (parser, statement);
      break;
    case PT_RENAME:
      err = do_rename (parser, statement);
      break;
    case PT_RENAME_TRIGGER:
      err = do_rename_trigger (parser, statement);
      break;
    case PT_SET_TRIGGER:
      err = do_set_trigger (parser, statement);
      break;
    case PT_GET_TRIGGER:
      err = do_get_trigger (parser, statement);
      break;
    case PT_EXECUTE_TRIGGER:
      err = do_execute_trigger (parser, statement);
      break;
    case PT_REMOVE_TRIGGER:
      err = do_remove_trigger (parser, statement);
      break;
    case PT_GRANT:
      err = do_grant (parser, statement);
      break;
    case PT_REVOKE:
      err = do_revoke (parser, statement);
      break;
    case PT_ATTACH:
      err = do_attach (parser, statement);
      break;
    case PT_GET_XACTION:
      err = do_get_xaction (parser, statement);
      break;
    case PT_SET_XACTION:
      err = do_set_xaction (parser, statement);
      break;
    case PT_SAVEPOINT:
      err = do_savepoint (parser, statement);
      break;
    case PT_PREPARE_TO_COMMIT:
      err = do_prepare_to_commit (parser, statement);
      break;
    case PT_COMMIT_WORK:
      err = do_commit (parser, statement);
      break;
    case PT_ROLLBACK_WORK:
      err = do_rollback (parser, statement);
      break;
    case PT_SCOPE:
      err = do_scope (parser, statement);
      break;
    case PT_DELETE:
      err = do_check_delete_trigger (parser, statement, do_execute_delete);
      break;
    case PT_INSERT:
      err = do_execute_insert (parser, statement);
      break;
    case PT_UPDATE:
      err = do_check_update_trigger (parser, statement, do_execute_update);
      break;
    case PT_MERGE:
      err = do_check_merge_trigger (parser, statement, do_execute_merge);
      break;
    case PT_SELECT:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
      err = do_execute_select (parser, statement);
      break;
    case PT_EVALUATE:
      err = do_evaluate (parser, statement);
      break;
    case PT_METHOD_CALL:
      err = do_call_method (parser, statement);
      break;
    case PT_GET_STATS:
      err = do_get_stats (parser, statement);
      break;
    case PT_UPDATE_STATS:
      err = do_update_stats (parser, statement);
      break;
    case PT_GET_OPT_LVL:
      err = do_get_optimization_param (parser, statement);
      break;
    case PT_SET_OPT_LVL:
      err = do_set_optimization_param (parser, statement);
      break;
    case PT_SET_SYS_PARAMS:
      err = do_set_sys_params (parser, statement);
      break;
    case PT_CREATE_STORED_PROCEDURE:
      err = jsp_create_stored_procedure (parser, statement);
      break;
    case PT_ALTER_STORED_PROCEDURE_OWNER:
      err = jsp_alter_stored_procedure_owner (parser, statement);
      break;
    case PT_DROP_STORED_PROCEDURE:
      err = jsp_drop_stored_procedure (parser, statement);
      break;
    case PT_TRUNCATE:
      err = do_truncate (parser, statement);
      break;
    case PT_DO:
      err = do_execute_do (parser, statement);
      break;
    case PT_EXECUTE_PREPARE:
      err = do_execute_session_statement (parser, statement);
      break;
    case PT_SET_SESSION_VARIABLES:
      err = do_set_session_variables (parser, statement);
      break;
    case PT_DROP_SESSION_VARIABLES:
      err = do_drop_session_variables (parser, statement);
      break;
    case PT_SET_NAMES:
      err = do_set_names (parser, statement);
      break;
    default:
      er_set (ER_ERROR_SEVERITY, __FILE__, statement->line_number,
	      ER_PT_UNKNOWN_STATEMENT, 1, statement->node_type);
      break;
    }

  /* enable data replication log */
  if (is_schema_repl_log_statment (statement))
    {
      db_set_suppress_repl_on_transaction (false);
    }

  /* restore execution flag */
  parser->exec_mode = old_exec_mode;

  /* write schema replication log */
  if (err == NO_ERROR)
    {
      err = do_replicate_schema (parser, statement);
    }

  /* There may be parse tree fragments that were collected during the
     execution of the statement that should be freed now. */
  pt_free_orphans (parser);

  /* During query execution,
     if current transaction was rollbacked by the system,
     abort transaction on client side also. */
  if (err == ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_only_client (false);
    }

  RESET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);

  return ((err == ER_FAILED && (err = er_errid ()) == NO_ERROR)
	  ? ER_GENERIC_ERROR : err);
}				/* do_execute_statement() */

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * do_statements() - Execute a prepared statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement_list(in): Parse tree of a statement list
 *
 * Note: Side effects can exist at the statement list
 */
int
do_statements (PARSER_CONTEXT * parser, PT_NODE * statement_list)
{
  int error = 0;
  PT_NODE *statement;

  /* for each of a list of statement nodes, process it. */
  for (statement = statement_list; statement != NULL;
       statement = statement->next)
    {
      do_Trigger_involved = false;
      error = do_statement (parser, statement);
      do_Trigger_involved = false;
      if (error)
	{
	  break;
	}
    }

  return error;
}
#endif

/*
 * do_check_internal_statements() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement_list(in): Parse tree of a statement
 *   internal_stmt_list(in):
 *   do_func(in):
 *
 * Note:
 *   Do savepoint and execute statements before and after do_func()
 *   if an error happens, rollback to the savepoint.
 */
int
do_check_internal_statements (PARSER_CONTEXT * parser, PT_NODE * statement,
			      /* PT_NODE * internal_stmt_list, */
			      PT_DO_FUNC do_func)
{
#if 0				/* to disable TEXT */
  const char *savepoint_name = UNIQUE_SAVEPOINT_EXTERNAL_STATEMENT;
  int error = NO_ERROR, num_rows = NO_ERROR;

  if (internal_stmt_list == NULL)
    {
#endif
      return do_func (parser, statement);
#if 0				/* to disable TEXT */
    }
  else
    {
      error = tran_savepoint (savepoint_name, false);
      if (error != NO_ERROR)
	return error;

      error = do_internal_statements (parser, internal_stmt_list, 0);
      if (error >= NO_ERROR)
	{
	  /* The main statement cas use out parameters from internal statements,
	     and the internal statements generate the parameters at execution time.
	     So, it need to bind the parameters again */
	  (void) parser_walk_tree (parser, statement, pt_bind_param_node,
				   NULL, NULL, NULL);
	  num_rows = error = do_func (parser, statement);
#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"do_check_internal_statements : execute %s statement, %s\n",
			"main", parser_print_tree (parser, statement));
#endif
	  if (error >= NO_ERROR)
	    {
	      error = do_internal_statements (parser, internal_stmt_list, 1);
	    }
	}
      if (error < NO_ERROR)
	{
	  (void) tran_abort_upto_savepoint (savepoint_name);
	  return error;
	}
      return num_rows;
    }
#endif
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * do_internal_statements() -
 *   return: Error code
 *   parser(in): Parser context
 *   internal_stmt_list(in):
 *   phase(in):
 *
 * Note:
 *   For input statements, find the statements to do now and
 *   using new parser, parse, check semantics and execute these.
 *
 */
int
do_internal_statements (PARSER_CONTEXT * parser, PT_NODE * internal_stmt_list,
			const int phase)
{
  PT_NODE *stmt_str;
  DB_QUERY_RESULT *query_result;
  DB_QUERY_ERROR query_error;
  PARSER_CONTEXT *save_parser;
  DB_OBJECT *save_user;
  int au_save;
  int error = NO_ERROR;

  save_user = Au_user;
  Au_user = Au_dba_user;
  AU_DISABLE (au_save);

  for (stmt_str = internal_stmt_list; stmt_str != NULL;
       stmt_str = stmt_str->next)
    {
      if ((phase == 0 && stmt_str->etc == NULL)
	  || (phase == 1 && stmt_str->etc != NULL))
	{
	  /* To get host variable info from parent parser, set the parent parser */
	  save_parser = parent_parser;
	  parent_parser = parser;
	  error =
	    db_execute (stmt_str->info.value.text, &query_result,
			&query_error);
	  /* restore the parent parser */
	  parent_parser = save_parser;
	  if (error < NO_ERROR)
	    break;
	}
    }

  Au_user = save_user;
  AU_ENABLE (au_save);

  return error;
}
#endif

/*
 * Function Group:
 * Parse tree to update statistics translation.
 *
 */

typedef enum
{
  CST_UNDEFINED,
  CST_NOBJECTS, CST_NPAGES, CST_NATTRIBUTES, CST_ATTR_MIN, CST_ATTR_MAX,
#if 0
  CST_ATTR_NINDEXES, CST_BT_NLEAFS, CST_BT_HEIGHT,
#endif
  CST_BT_NKEYS,
} CST_ITEM_ENUM;

typedef struct cst_item CST_ITEM;
struct cst_item
{
  CST_ITEM_ENUM item;
  const char *string;
  int att_id;
  int bt_idx;
};

static CST_ITEM cst_item_tbl[] = {
  {CST_NOBJECTS, "#objects", -1, -1},
  {CST_NPAGES, "#pages", -1, -1},
  {CST_NATTRIBUTES, "#attributes", -1, -1},
  {CST_ATTR_MIN, "min", 0, -1},
  {CST_ATTR_MAX, "max", 0, -1},
#if 0
  {CST_ATTR_NINDEXES, "#indexes", 0, -1},
  {CST_BT_NLEAFS, "#leaf_pages", 0, 0},
  {CST_BT_NPAGES, "#index_pages", 0, 0},
  {CST_BT_HEIGHT, "index_height", 0, 0},
#endif
  {CST_BT_NKEYS, "#keys", 0, 0},
  {CST_UNDEFINED, "", 0, 0}
};

static char *extract_att_name (const char *str);
static int extract_bt_idx (const char *str);
static int make_cst_item_value (DB_OBJECT * obj, const char *str,
				DB_VALUE * db_val);

/*
 * do_update_stats() - Updates the statistics of a list of classes
 *		       or ALL classes
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a update statistics statement
 */
int
do_update_stats (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *cls = NULL;
  int error = NO_ERROR;
  DB_OBJECT *obj;
  int is_partition = 0, i;
  MOP *sub_partitions = NULL;

  CHECK_MODIFICATION_ERROR ();

  if (statement->info.update_stats.all_classes > 0)
    {
      return sm_update_all_statistics ();
    }
  else if (statement->info.update_stats.all_classes < 0)
    {
      return sm_update_all_catalog_statistics ();
    }
  else
    {
      for (cls = statement->info.update_stats.class_list;
	   cls != NULL && error == NO_ERROR; cls = cls->next)
	{
	  obj = db_find_class (cls->info.name.original);
	  if (obj)
	    {
	      cls->info.name.db_object = obj;
	      pt_check_user_owns_class (parser, cls);
	    }
	  else
	    {
	      return er_errid ();
	    }

	  error = sm_update_statistics (obj, true);
	  error = do_is_partitioned_classobj (&is_partition, obj, NULL,
					      &sub_partitions);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  if (is_partition == PARTITIONED_CLASS
	      || is_partition == PARTITION_CLASS)
	    {
	      for (i = 0; sub_partitions[i]; i++)
		{
		  error = sm_update_statistics (sub_partitions[i], true);
		  if (error != NO_ERROR)
		    break;
		}
	      free_and_init (sub_partitions);
	    }
	}
      return error;
    }
}

/*
 * extract_att_name() -
 *   return:
 *   str(in):
 */
static char *
extract_att_name (const char *str)
{
  char *s, *t, *att = NULL;
  int size;

  s = intl_mbs_chr (str, '(');
  if (s && *(++s))
    {
      t = intl_mbs_chr (s, ':');
      if (!t)
	{
	  t = intl_mbs_chr (s, ')');
	}
      if (t && t != s)
	{
	  size = CAST_STRLEN (t - s);
	  att = (char *) malloc (size + 1);
	  if (att)
	    {
	      if (intl_mbs_ncpy (att, s, size + 1) != NULL)
		{
		  att[size] = '\0';
		}
	      else
		{
		  free_and_init (att);
		}
	    }
	}
    }
  return att;
}

/*
 * extract_bt_idx() -
 *   return:
 *   str(in):
 */
static int
extract_bt_idx (const char *str)
{
  char *s, *t;
  int idx = -1;

  t = intl_mbs_chr (str, '(');
  if (t && *(++t))
    {
      s = intl_mbs_chr (t, ':');
      if (s && s != t && *(++s))
	{
	  t = intl_mbs_chr (s, ')');
	  if (t && t != s)
	    {
	      idx = atoi (s);
	    }
	}
    }
  return idx;
}

/*
 * make_cst_item_value() -
 *   return: Error code
 *   obj(in):
 *   str(in):
 *   db_val(in):
 */
static int
make_cst_item_value (DB_OBJECT * obj, const char *str, DB_VALUE * db_val)
{
  CST_ITEM cst_item = { CST_UNDEFINED, "", -1, -1 };
  char *att_name = NULL;
  int bt_idx;
  CLASS_STATS *class_statsp = NULL;
  ATTR_STATS *attr_statsp = NULL;
  BTREE_STATS *bt_statsp = NULL;
  int i;
  int error;

  for (i = 0; i < (signed) DIM (cst_item_tbl); i++)
    {
      if (intl_mbs_ncasecmp (str, cst_item_tbl[i].string,
			     strlen (cst_item_tbl[i].string)) == 0)
	{
	  cst_item = cst_item_tbl[i];
	  if (cst_item.att_id >= 0)
	    {
	      att_name = extract_att_name (str);
	      if (att_name == NULL)
		{
		  cst_item.item = CST_UNDEFINED;
		  break;
		}
	      cst_item.att_id = sm_att_id (obj, att_name);
	      if (cst_item.att_id < 0)
		{
		  cst_item.item = CST_UNDEFINED;
		  break;
		}
	      free_and_init (att_name);
	      if (cst_item.bt_idx >= 0)
		{
		  bt_idx = extract_bt_idx (str);
		  if (bt_idx <= 0)
		    {
		      cst_item.item = CST_UNDEFINED;
		      break;
		    }
		  cst_item.bt_idx = bt_idx;
		}
	    }
	  break;
	}
    }
  if (cst_item.item == CST_UNDEFINED)
    {
      db_make_null (db_val);
      error = ER_DO_UNDEFINED_CST_ITEM;
      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return error;
    }

  class_statsp = sm_get_statistics_force (obj);
  if (class_statsp == NULL)
    {
      db_make_null (db_val);
      return er_errid ();
    }
  if (cst_item.att_id >= 0)
    {
      for (i = 0; i < class_statsp->n_attrs; i++)
	{
	  if (class_statsp->attr_stats[i].id == cst_item.att_id)
	    {
	      attr_statsp = &class_statsp->attr_stats[i];
	      break;
	    }
	}
    }
  if (attr_statsp && cst_item.bt_idx > 0
      && cst_item.bt_idx <= attr_statsp->n_btstats)
    {
      for (i = 0; i < cst_item.bt_idx; i++)
	{
	  ;
	}
      bt_statsp = &attr_statsp->bt_stats[i];
    }

  switch (cst_item.item)
    {
    case CST_NOBJECTS:
      db_make_int (db_val, class_statsp->num_objects);
      break;
    case CST_NPAGES:
      db_make_int (db_val, class_statsp->heap_size);
      break;
    case CST_NATTRIBUTES:
      db_make_int (db_val, class_statsp->n_attrs);
      break;
    case CST_ATTR_MIN:
      if (!attr_statsp)
	{
	  db_make_null (db_val);
	}
      else
	switch (attr_statsp->type)
	  {
	  case DB_TYPE_INTEGER:
	    db_make_int (db_val, attr_statsp->min_value.i);
	    break;
	  case DB_TYPE_BIGINT:
	    db_make_bigint (db_val, attr_statsp->min_value.bigint);
	    break;
	  case DB_TYPE_SHORT:
	    db_make_short (db_val, attr_statsp->min_value.i);
	    break;
	  case DB_TYPE_FLOAT:
	    db_make_float (db_val, attr_statsp->min_value.f);
	    break;
	  case DB_TYPE_DOUBLE:
	    db_make_double (db_val, attr_statsp->min_value.d);
	    break;
	  case DB_TYPE_DATE:
	    db_value_put_encoded_date (db_val, &attr_statsp->min_value.date);
	    break;
	  case DB_TYPE_TIME:
	    db_value_put_encoded_time (db_val, &attr_statsp->min_value.time);
	    break;
	  case DB_TYPE_UTIME:
	    db_make_timestamp (db_val, attr_statsp->min_value.utime);
	    break;
	  case DB_TYPE_DATETIME:
	    db_make_datetime (db_val, &attr_statsp->min_value.datetime);
	    break;
	  case DB_TYPE_MONETARY:
	    db_make_monetary (db_val,
			      attr_statsp->min_value.money.type,
			      attr_statsp->min_value.money.amount);
	    break;
	  default:
	    db_make_null (db_val);
	    break;
	  }
      break;
    case CST_ATTR_MAX:
      if (!attr_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  switch (attr_statsp->type)
	    {
	    case DB_TYPE_INTEGER:
	      db_make_int (db_val, attr_statsp->max_value.i);
	      break;
	    case DB_TYPE_BIGINT:
	      db_make_bigint (db_val, attr_statsp->max_value.bigint);
	      break;
	    case DB_TYPE_SHORT:
	      db_make_short (db_val, attr_statsp->max_value.i);
	      break;
	    case DB_TYPE_FLOAT:
	      db_make_float (db_val, attr_statsp->max_value.f);
	      break;
	    case DB_TYPE_DOUBLE:
	      db_make_double (db_val, attr_statsp->max_value.d);
	      break;
	    case DB_TYPE_DATE:
	      db_value_put_encoded_date (db_val,
					 &attr_statsp->max_value.date);
	      break;
	    case DB_TYPE_TIME:
	      db_value_put_encoded_time (db_val,
					 &attr_statsp->max_value.time);
	      break;
	    case DB_TYPE_UTIME:
	      db_make_timestamp (db_val, attr_statsp->max_value.utime);
	      break;
	    case DB_TYPE_DATETIME:
	      db_make_datetime (db_val, &attr_statsp->max_value.datetime);
	      break;
	    case DB_TYPE_MONETARY:
	      db_make_monetary (db_val,
				attr_statsp->max_value.money.type,
				attr_statsp->max_value.money.amount);
	      break;
	    default:
	      db_make_null (db_val);
	      break;
	    }
	}
      break;
#if 0
    case CST_ATTR_NINDEXES:
      if (!attr_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, attr_statsp->n_btstats);
	}
      break;
    case CST_BT_NLEAFS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->leafs);
	}
      break;
    case CST_BT_NPAGES:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->pages);
	}
      break;
    case CST_BT_HEIGHT:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->height);
	}
      break;
#endif
    case CST_BT_NKEYS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->keys);
	}
      break;
    default:
      break;
    }

  return NO_ERROR;
}				/* make_cst_item_value() */

/*
 * do_get_stats() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a get statistics statement
 */
int
do_get_stats (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *cls, *arg, *into;
  DB_OBJECT *obj;
  DB_VALUE *ret_val, db_val;
  int error;

  cls = statement->info.get_stats.class_;
  arg = statement->info.get_stats.args;
  into = statement->info.get_stats.into_var;
  if (!cls || !arg)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  obj = db_find_class (cls->info.name.original);
  if (!obj)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  cls->info.name.db_object = obj;
  (void) pt_check_user_owns_class (parser, cls);

  ret_val = db_value_create ();
  if (ret_val == NULL)
    return er_errid ();

  pt_evaluate_tree (parser, arg, &db_val, 1);
  if (pt_has_error (parser) || DB_IS_NULL (&db_val))
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  error = make_cst_item_value (obj, DB_PULL_STRING (&db_val), ret_val);
  pr_clear_value (&db_val);
  if (error != NO_ERROR)
    {
      return error;
    }

  statement->etc = (void *) ret_val;

  if (into && into->node_type == PT_NAME && into->info.name.original)
    {
      return pt_associate_label_with_value_check_reference (into->info.
							    name.original,
							    db_value_copy
							    (ret_val));
    }

  return NO_ERROR;
}


/*
 * Function Group:
 * DO functions for transaction management
 *
 */

static int map_iso_levels (PARSER_CONTEXT * parser, PT_NODE * statement,
			   DB_TRAN_ISOLATION * tran_isolation,
			   PT_NODE * node);
static int set_iso_level (PARSER_CONTEXT * parser,
			  DB_TRAN_ISOLATION * tran_isolation, bool * async_ws,
			  PT_NODE * statement, const DB_VALUE * level);
static int check_timeout_value (PARSER_CONTEXT * parser, PT_NODE * statement,
				DB_VALUE * val);
static char *get_savepoint_name_from_db_value (DB_VALUE * val);

/*
 * do_attach() - Attaches to named (distributed 2pc) transaction
 *   return: Error code if attach fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of an attach statement
 *
 * Note:
 */
int
do_attach (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  if (!parser
      || pt_has_error (parser)
      || !statement || statement->node_type != PT_ATTACH)
    {
      return ER_GENERIC_ERROR;
    }
  else
    return db_2pc_attach_transaction (statement->info.attach.trans_id);
}

/*
 * do_prepare_to_commit() - Prepare to commit local participant of i
 *			    (distributed 2pc) transaction
 *   return: Error code if prepare-to-commit fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a prepare-to-commit statement
 *
 * Note:
 */
int
do_prepare_to_commit (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  if (!parser
      || pt_has_error (parser)
      || !statement || statement->node_type != PT_PREPARE_TO_COMMIT)
    {
      return ER_GENERIC_ERROR;
    }
  else
    return db_2pc_prepare_to_commit_transaction (statement->
						 info.prepare_to_commit.
						 trans_id);
}

/*
 * do_commit() - Commit a transaction
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a commit statement
 *
 * Note:
 */
int
do_commit (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  /* Row count should be reset to -1 for explicit commits (i.e: commit
   * statements) but should not be reset in AUTO_COMMIT mode.
   * This is the best place to reset it for commit statements.
   */
  db_update_row_count_cache (-1);
  return tran_commit (statement->info.commit_work.retain_lock ? true : false);
}

/*
 * do_rollback_savepoints() - Rollback savepoints
 *   return: Error code
 *   parser(in): Parser context
 *   sp_nane(in): Savepoint name
 */
int
do_rollback_savepoints (PARSER_CONTEXT * parser, const char *sp_name)
{
  if (!sp_name)
    {
      return db_abort_transaction ();
    }

  return NO_ERROR;
}

/*
 * do_rollback() - Rollbacks a transaction
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a rollback statement (for regularity)
 *
 * Note: If a savepoint name is given, the transaction is rolled back to
 *   the savepoint, otherwise the entire transaction is rolled back.
 */
int
do_rollback (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  const char *save_name;
  PT_NODE *name;
  DB_VALUE val;

  name = statement->info.rollback_work.save_name;
  if (name == NULL)
    {
      error = tran_abort ();
    }
  else
    {
      if (name->node_type == PT_NAME
	  && name->info.name.meta_class != PT_PARAMETER)
	{
	  save_name = name->info.name.original;
	  error = db_abort_to_savepoint_internal (save_name);
	}
      else
	{
	  pt_evaluate_tree (parser, name, &val, 1);
	  if (pt_has_error (parser))
	    {
	      return ER_GENERIC_ERROR;
	    }
	  save_name = get_savepoint_name_from_db_value (&val);
	  if (!save_name)
	    {
	      return er_errid ();
	    }
	  error = db_abort_to_savepoint_internal (save_name);
	  db_value_clear (&val);
	}
    }

  return error;
}

/*
 * do_savepoint() - Creates a transaction savepoint
 *   return: Error code if savepoint fails
 *   parser(in): Parser context of a savepoint statement
 *   statement(in): Parse tree of a rollback statement (for regularity)
 *
 * Note: If a savepoint name is given, the savepoint is created
 *   with that name, if no savepoint name is given, we generate a unique one.
 */
int
do_savepoint (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  const char *save_name;
  PT_NODE *name;
  DB_VALUE val;

  name = statement->info.savepoint.save_name;
  if (name == NULL)
    {
      PT_INTERNAL_ERROR (parser, "transactions");
    }
  else
    {
      if (name->node_type == PT_NAME
	  && name->info.name.meta_class != PT_PARAMETER)
	{
	  save_name = name->info.name.original;
	  error = db_savepoint_transaction_internal (save_name);
	}
      else
	{
	  pt_evaluate_tree (parser, name, &val, 1);
	  if (pt_has_error (parser))
	    {
	      return ER_GENERIC_ERROR;
	    }
	  save_name = get_savepoint_name_from_db_value (&val);
	  if (!save_name)
	    {
	      return er_errid ();
	    }
	  error = db_savepoint_transaction_internal (save_name);
	  db_value_clear (&val);
	}
    }

  return error;
}

/*
 * do_get_xaction() - Gets the isolation level and/or timeout value for
 *      	      a transaction
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a get transaction statement
 *
 * Note:
 */
int
do_get_xaction (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int lock_timeout_in_msecs = 0;
  DB_TRAN_ISOLATION tran_isolation = TRAN_UNKNOWN_ISOLATION;
  bool async_ws;
  int tran_num;
  const char *into_label;
  DB_VALUE *ins_val;
  PT_NODE *into_var;
  int error = NO_ERROR;

  (void) tran_get_tran_settings (&lock_timeout_in_msecs, &tran_isolation,
				 &async_ws);

  /* create a DB_VALUE to hold the result */
  ins_val = db_value_create ();
  if (ins_val == NULL)
    {
      return er_errid ();
    }

  db_make_int (ins_val, 0);

  switch (statement->info.get_xaction.option)
    {
    case PT_ISOLATION_LEVEL:
      tran_num = (int) tran_isolation;
      if (async_ws)
	{
	  tran_num |= TRAN_ASYNC_WS_BIT;
	}
      db_make_int (ins_val, tran_num);
      break;

    case PT_LOCK_TIMEOUT:
      if (lock_timeout_in_msecs > 0)
	{
	  db_make_float (ins_val, (float) lock_timeout_in_msecs / 1000);
	}
      else
	{
	  db_make_float (ins_val, (float) lock_timeout_in_msecs);
	}
      break;

    default:
      break;
    }

  statement->etc = (void *) ins_val;

  into_var = statement->info.get_xaction.into_var;
  if (into_var != NULL
      && into_var->node_type == PT_NAME
      && (into_label = into_var->info.name.original) != NULL)
    {
      /*
       * create another DB_VALUE of the new instance for
       * the label_table
       */
      ins_val = db_value_create ();
      if (ins_val == NULL)
	{
	  return er_errid ();
	}

      db_make_int (ins_val, 0);

      switch (statement->info.get_xaction.option)
	{
	case PT_ISOLATION_LEVEL:
	  tran_num = (int) tran_isolation;
	  if (async_ws)
	    {
	      tran_num |= TRAN_ASYNC_WS_BIT;
	    }
	  db_make_int (ins_val, tran_num);
	  break;

	case PT_LOCK_TIMEOUT:
	  if (lock_timeout_in_msecs > 0)
	    {
	      db_make_float (ins_val, (float) lock_timeout_in_msecs / 1000.0);
	    }
	  else
	    {
	      db_make_float (ins_val, (float) lock_timeout_in_msecs);
	    }
	  break;

	default:
	  break;
	}

      /* enter {label, ins_val} pair into the label_table */
      error =
	pt_associate_label_with_value_check_reference (into_label, ins_val);
    }

  return error;
}

/*
 * do_set_xaction() - Sets the isolation level and/or timeout value for
 *      	      a transaction
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a set transaction statement
 *
 * Note:
 */
int
do_set_xaction (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  DB_TRAN_ISOLATION tran_isolation;
  DB_VALUE val;
  PT_NODE *mode = statement->info.set_xaction.xaction_modes;
  int error = NO_ERROR;
  bool async_ws;
  float wait_secs;

  while ((error == NO_ERROR) && (mode != NULL))
    {
      switch (mode->node_type)
	{
	case PT_ISOLATION_LVL:
	  if (mode->info.isolation_lvl.level == NULL)
	    {
	      /* map schema/instance pair to level */
	      error = map_iso_levels (parser, statement, &tran_isolation,
				      mode);
	      async_ws = mode->info.isolation_lvl.async_ws ? true : false;
	    }
	  else
	    {
	      pt_evaluate_tree (parser, mode->info.isolation_lvl.level, &val,
				1);

	      if (pt_has_error (parser))
		{
		  return ER_GENERIC_ERROR;
		}

	      error = set_iso_level (parser, &tran_isolation, &async_ws,
				     statement, &val);
	    }

	  if (error == NO_ERROR)
	    {
	      error = tran_reset_isolation (tran_isolation, async_ws);
	    }
	  break;
	case PT_TIMEOUT:
	  pt_evaluate_tree (parser, mode->info.timeout.val, &val, 1);
	  if (pt_has_error (parser))
	    {
	      return ER_GENERIC_ERROR;
	    }

	  if (check_timeout_value (parser, statement, &val) != NO_ERROR)
	    {
	      return ER_GENERIC_ERROR;
	    }
	  else
	    {
	      wait_secs = DB_GET_FLOAT (&val);
	      if (wait_secs > 0)
		{
		  wait_secs *= 1000;
		}
	      (void) tran_reset_wait_times ((int) wait_secs);
	    }
	  break;
	default:
	  return ER_GENERIC_ERROR;
	}

      mode = mode->next;
    }

  return error;
}

/*
 * do_get_optimization_level() - Determine the current optimization and
 *				 return it through the statement parameter.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a get transaction statement
 *
 * Note:
 */
int
do_get_optimization_param (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  DB_VALUE *val;
  PT_NODE *into_var;
  const char *into_name;
  char cost[2];
  int error = NO_ERROR;

  val = db_value_create ();
  if (val == NULL)
    {
      return er_errid ();
    }

  switch (statement->info.get_opt_lvl.option)
    {
    case PT_OPT_LVL:
      {
	int i;
	qo_get_optimization_param (&i, QO_PARAM_LEVEL);
	db_make_int (val, i);
	break;
      }
    case PT_OPT_COST:
      {
	DB_VALUE plan;
	pt_evaluate_tree (parser, statement->info.get_opt_lvl.args, &plan, 1);
	if (pt_has_error (parser))
	  {
	    return ER_OBJ_INVALID_ARGUMENTS;
	  }
	qo_get_optimization_param (cost, QO_PARAM_COST,
				   DB_GET_STRING (&plan));
	pr_clear_value (&plan);
	db_make_string (val, cost);
      }
    default:
      /*
       * Default ok; nothing else can get in here.
       */
      break;
    }

  statement->etc = (void *) val;

  into_var = statement->info.get_opt_lvl.into_var;
  if (into_var != NULL
      && into_var->node_type == PT_NAME
      && (into_name = into_var->info.name.original) != NULL)
    {
      error =
	pt_associate_label_with_value_check_reference (into_name,
						       db_value_copy (val));
    }

  return error;
}

/*
 * do_set_optimization_param() - Set the optimization level to the indicated
 *				 value and return the old value through the
 *				 statement paramter.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a set transaction statement
 *
 * Note:
 */
int
do_set_optimization_param (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *p1, *p2;
  DB_VALUE val1, val2;
  char *plan, *cost;

  db_make_null (&val1);
  db_make_null (&val2);

  p1 = statement->info.set_opt_lvl.val;

  if (p1 == NULL)
    {
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
	      ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  pt_evaluate_tree (parser, p1, &val1, 1);
  if (pt_has_error (parser))
    {
      pr_clear_value (&val1);
      return NO_ERROR;
    }

  switch (statement->info.set_opt_lvl.option)
    {
    case PT_OPT_LVL:
      qo_set_optimization_param (NULL, QO_PARAM_LEVEL,
				 (int) DB_GET_INTEGER (&val1));
      break;
    case PT_OPT_COST:
      plan = DB_GET_STRING (&val1);
      p2 = p1->next;
      pt_evaluate_tree (parser, p2, &val2, 1);
      if (pt_has_error (parser))
	{
	  pr_clear_value (&val1);
	  pr_clear_value (&val2);
	  return ER_OBJ_INVALID_ARGUMENTS;
	}
      switch (DB_VALUE_TYPE (&val2))
	{
	case DB_TYPE_INTEGER:
	  qo_set_optimization_param (NULL, QO_PARAM_COST, plan,
				     DB_GET_INT (&val2));
	  break;
	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  cost = DB_PULL_STRING (&val2);
	  qo_set_optimization_param (NULL, QO_PARAM_COST, plan,
				     (int) cost[0]);
	  break;
	default:
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		  ER_OBJ_INVALID_ARGUMENTS, 0);
	  pr_clear_value (&val1);
	  pr_clear_value (&val2);
	  return ER_OBJ_INVALID_ARGUMENTS;
	}
      break;
    default:
      /*
       * Default ok; no other options available.
       */
      break;
    }

  pr_clear_value (&val1);
  pr_clear_value (&val2);
  return NO_ERROR;
}

/*
 * do_set_sys_params() - Set the system parameters defined in 'cubrid.conf'.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a set transaction statement
 *
 * Note:
 */
int
do_set_sys_params (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *val;
  DB_VALUE db_val;
  int error = NO_ERROR;

  val = statement->info.set_sys_params.val;
  if (val == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  db_make_null (&db_val);
  while (val && error == NO_ERROR)
    {
      pt_evaluate_tree (parser, val, &db_val, 1);

      if (pt_has_error (parser))
	{
	  error = ER_GENERIC_ERROR;
	}
      else
	{
	  error = db_set_system_parameters (DB_GET_STRING (&db_val));
	}

      pr_clear_value (&db_val);
      val = val->next;
    }

  return error;
}

/*
 * map_iso_levels() - Maps the schema/instance isolation level to the
 *      	      DB_TRAN_ISOLATION enumerated type.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   tran_isolation(out):
 *   node(in): Parse tree of a set transaction statement
 *
 * Note: Initializes isolation_levels array
 */
static int
map_iso_levels (PARSER_CONTEXT * parser, PT_NODE * statement,
		DB_TRAN_ISOLATION * tran_isolation, PT_NODE * node)
{
  PT_MISC_TYPE instances = node->info.isolation_lvl.instances;
  PT_MISC_TYPE schema = node->info.isolation_lvl.schema;

  switch (schema)
    {
    case PT_SERIALIZABLE:
      if (instances == PT_SERIALIZABLE)
	{
	  *tran_isolation = TRAN_SERIALIZABLE;
	}
      else
	{
	  PT_ERRORmf2 (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		       MSGCAT_RUNTIME_XACT_INVALID_ISO_LVL_MSG,
		       pt_show_misc_type (schema),
		       pt_show_misc_type (instances));
	  return ER_GENERIC_ERROR;
	}
      break;
    case PT_REPEATABLE_READ:
      if (instances == PT_READ_UNCOMMITTED)
	{
	  *tran_isolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
	}
      else if (instances == PT_READ_COMMITTED)
	{
	  *tran_isolation = TRAN_REP_CLASS_COMMIT_INSTANCE;
	}
      else if (instances == PT_REPEATABLE_READ)
	{
	  *tran_isolation = TRAN_REP_CLASS_REP_INSTANCE;
	}
      else
	{
	  PT_ERRORmf2 (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		       MSGCAT_RUNTIME_XACT_INVALID_ISO_LVL_MSG,
		       pt_show_misc_type (schema),
		       pt_show_misc_type (instances));
	  return ER_GENERIC_ERROR;
	}
      break;
    case PT_READ_COMMITTED:
      if (instances == PT_READ_UNCOMMITTED)
	{
	  *tran_isolation = TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE;
	}
      else if (instances == PT_READ_COMMITTED)
	{
	  *tran_isolation = TRAN_COMMIT_CLASS_COMMIT_INSTANCE;
	}
      else
	{
	  PT_ERRORmf2 (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		       MSGCAT_RUNTIME_XACT_INVALID_ISO_LVL_MSG,
		       pt_show_misc_type (schema),
		       pt_show_misc_type (instances));
	  return ER_GENERIC_ERROR;
	}
      break;
    case PT_READ_UNCOMMITTED:
      PT_ERRORmf2 (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		   MSGCAT_RUNTIME_XACT_INVALID_ISO_LVL_MSG,
		   pt_show_misc_type (schema), pt_show_misc_type (instances));
      return ER_GENERIC_ERROR;
    default:
      return ER_GENERIC_ERROR;
    }

  return NO_ERROR;
}

/*
 * set_iso_level() -
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   tran_isolation(out): Isolation level set as a side effect
 *   async_ws(out):
 *   statement(in): Parse tree of a set transaction statement
 *   level(in):
 *
 * Note: Translates the user entered isolation level (1,2,3,4,5) into
 *       the enumerated type.
 */
static int
set_iso_level (PARSER_CONTEXT * parser,
	       DB_TRAN_ISOLATION * tran_isolation, bool * async_ws,
	       PT_NODE * statement, const DB_VALUE * level)
{
  int error = NO_ERROR;
  int isolvl = DB_GET_INTEGER (level) & 0x0F;
  *async_ws = (DB_GET_INTEGER (level) & 0xF0) ? true : false;

  /* translate to the enumerated type */
  switch (isolvl)
    {
    case 1:
      *tran_isolation = TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_READCOM_S_READUNC_I));
      break;
    case 2:
      *tran_isolation = TRAN_COMMIT_CLASS_COMMIT_INSTANCE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_READCOM_S_READCOM_I));
      break;
    case 3:
      *tran_isolation = TRAN_REP_CLASS_UNCOMMIT_INSTANCE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_REPREAD_S_READUNC_I));
      break;
    case 4:
      *tran_isolation = TRAN_REP_CLASS_COMMIT_INSTANCE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_REPREAD_S_READCOM_I));
      break;
    case 5:
      *tran_isolation = TRAN_REP_CLASS_REP_INSTANCE;
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_PARSER_RUNTIME,
			       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout,
	       msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_PARSER_RUNTIME,
			       MSGCAT_RUNTIME_REPREAD_S_REPREAD_I));
      break;
    case 6:
      *tran_isolation = TRAN_SERIALIZABLE;
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_ISO_LVL_SET_TO_MSG));
      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_PARSER_RUNTIME,
				       MSGCAT_RUNTIME_SERIAL_S_SERIAL_I));
      break;
    case 0:
      if (*async_ws == true)
	{			/* only async workspace is given */
	  int dummy_lktimeout;
	  bool dummy_aws;
	  tran_get_tran_settings (&dummy_lktimeout, tran_isolation,
				  &dummy_aws);
	  break;
	}
      /* fall through */
    default:
      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		 MSGCAT_RUNTIME_XACT_ISO_LVL_MSG);
      error = ER_GENERIC_ERROR;
    }

  return error;
}

/*
 * check_timeout_value() -
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in):
 *   val(in): DB_VALUE with the value to set
 *
 * Note: Checks the user entered isolation level. Valid values are:
 *                    -1 : Infinite
 *                     0 : Don't wait
 *                    >0 : Wait this number of seconds
 */
static int
check_timeout_value (PARSER_CONTEXT * parser, PT_NODE * statement,
		     DB_VALUE * val)
{
  float timeout;

  if (db_value_coerce (val, val, &tp_Float_domain) == DOMAIN_COMPATIBLE)
    {
      timeout = DB_GET_FLOAT (val);
      if ((timeout == -1) || (timeout >= 0))
	{
	  return NO_ERROR;
	}
    }
  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
	     MSGCAT_RUNTIME_TIMEOUT_VALUE_MSG);
  return ER_GENERIC_ERROR;
}

/*
 * get_savepoint_name_from_db_value() -
 *   return: a NULL if the value doesn't properly describe the name
 *           of a savepoint.
 *   val(in):
 *
 * Note: Mutates the contents of val to hold a NULL terminated string
 *       holding a valid savepoint name.  If the value is already of
 *       type string, a NULL termination will be assumed since the
 *       name came from a parse tree.
 */
static char *
get_savepoint_name_from_db_value (DB_VALUE * val)
{
  if (DB_VALUE_TYPE (val) != DB_TYPE_CHAR
      && DB_VALUE_TYPE (val) != DB_TYPE_VARCHAR
      && DB_VALUE_TYPE (val) != DB_TYPE_NCHAR
      && DB_VALUE_TYPE (val) != DB_TYPE_VARNCHAR)
    {
      if (tp_value_cast (val, val,
			 tp_domain_resolve_default (DB_TYPE_VARCHAR), false)
	  != DOMAIN_COMPATIBLE)
	{
	  return (char *) NULL;
	}
    }

  return db_get_string (val);
}





/*
 * Function Group:
 * DO functions for trigger management
 *
 */


/* Value supplied in statement has an invalid type */
#define ER_TR_INVALID_VALUE_TYPE ER_GENERIC_ERROR

#define MAX_DOMAIN_NAME_SIZE 150

/*
 * PARSE TREE MACROS
 *
 * arguments:
 *	statement: parser node
 *
 * returns/side-effects: non-zero
 *
 * description:
 *    These are used as shorthand for parse tree access.
 *    Given a statement node, they test for certain characteristics
 *    and return a boolean.
 */

#define IS_REJECT_ACTION_STATEMENT(statement) \
  ((statement)->node_type == PT_TRIGGER_ACTION \
   && (statement)->info.trigger_action.action_type == PT_REJECT)

#define IS_INVALIDATE_ACTION_STATEMENT(statement) \
  ((statement)->node_type == PT_TRIGGER_ACTION \
   && (statement)->info.trigger_action.action_type == PT_INVALIDATE_XACTION)

#define IS_PRINT_ACTION_STATEMENT(statement) \
  ((statement)->node_type == PT_TRIGGER_ACTION \
   && (statement)->info.trigger_action.action_type == PT_PRINT)

#define PT_NODE_TR_NAME(node) \
 ((node)->info.create_trigger.trigger_name->info.name.original)
#define PT_NODE_TR_STATUS(node) \
 (convert_misc_to_tr_status((node)->info.create_trigger.trigger_status))
#define PT_NODE_TR_PRI(node) \
  ((node)->info.create_trigger.trigger_priority)
#define PT_NODE_TR_EVENT_TYPE(node) \
  (convert_event_to_tr_event((node)->info.create_trigger.trigger_event->info.event_spec.event_type))

#define PT_NODE_TR_TARGET(node) \
  ((node)->info.create_trigger.trigger_event->info.event_spec.event_target)
#define PT_TR_TARGET_CLASS(target) \
  ((target)->info.event_target.class_name->info.name.original)
#define PT_TR_TARGET_ATTR(target) \
  ((target)->info.event_target.attribute)
#define PT_TR_ATTR_NAME(attr) \
  ((attr)->info.name.original)

#define PT_NODE_COND(node) \
  ((node)->info.create_trigger.trigger_condition)
#define PT_NODE_COND_TIME(node) \
  (convert_misc_to_tr_time((node)->info.create_trigger.condition_time))

#define PT_NODE_ACTION(node) \
  ((node)->info.create_trigger.trigger_action)
#define PT_NODE_ACTION_TIME(node) \
  (convert_misc_to_tr_time((node)->info.create_trigger.action_time))

#define PT_NODE_TR_REF(node) \
  ((node)->info.create_trigger.trigger_reference)
#define PT_TR_REF_REFERENCE(ref) \
  (&(ref)->info.event_object)

static int tr_savepoint_number = 0;

static int merge_mop_list_extension (DB_OBJLIST * new_objlist,
				     DB_OBJLIST ** list);
static DB_TRIGGER_EVENT convert_event_to_tr_event (const PT_EVENT_TYPE ev);
static DB_TRIGGER_TIME convert_misc_to_tr_time (const PT_MISC_TYPE pt_time);
static DB_TRIGGER_STATUS convert_misc_to_tr_status (const PT_MISC_TYPE
						    pt_status);
static int convert_speclist_to_objlist (DB_OBJLIST ** triglist,
					PT_NODE * specnode);
static int check_trigger (DB_TRIGGER_EVENT event, PT_DO_FUNC * do_func,
			  PARSER_CONTEXT * parser, PT_NODE * statement);
static int check_merge_trigger (PT_DO_FUNC * do_func, PARSER_CONTEXT * parser,
				PT_NODE * statement);
static char **find_update_columns (int *count_ptr, PT_NODE * statement);
static void get_activity_info (PARSER_CONTEXT * parser,
			       DB_TRIGGER_ACTION * type, const char **source,
			       PT_NODE * statement);

/*
 * merge_mop_list_extension() -
 *   return: Number of MOPs to be added
 *   new(in):
 *   list(in):
 *
 * Note:
 */
static int
merge_mop_list_extension (DB_OBJLIST * new_objlist, DB_OBJLIST ** list)
{
  DB_OBJLIST *obj, *next;
  int added = 0;

  for (obj = new_objlist, next = NULL; obj != NULL; obj = next)
    {
      next = obj->next;
      if (ml_find (*list, obj->op))
	{
	  obj->next = NULL;
	  ml_ext_free (obj);
	}
      else
	{
	  obj->next = *list;
	  *list = obj;
	  added++;
	}
    }

  return added;
}

/*
 * These translate parse tree things into corresponding trigger things.
 */

/*
 * convert_event_to_tr_event() - Converts a PT_EV type into the corresponding
 *				 TR_EVENT_ type.
 *   return: DB_TRIGER_EVENT
 *   ev(in): One of PT_EVENT_TYPE
 *
 * Note:
 */
static DB_TRIGGER_EVENT
convert_event_to_tr_event (const PT_EVENT_TYPE ev)
{
  DB_TRIGGER_EVENT event = TR_EVENT_NULL;

  switch (ev)
    {
    case PT_EV_INSERT:
      event = TR_EVENT_INSERT;
      break;
    case PT_EV_STMT_INSERT:
      event = TR_EVENT_STATEMENT_INSERT;
      break;
    case PT_EV_DELETE:
      event = TR_EVENT_DELETE;
      break;
    case PT_EV_STMT_DELETE:
      event = TR_EVENT_STATEMENT_DELETE;
      break;
    case PT_EV_UPDATE:
      event = TR_EVENT_UPDATE;
      break;
    case PT_EV_STMT_UPDATE:
      event = TR_EVENT_STATEMENT_UPDATE;
      break;
    case PT_EV_ALTER:
      event = TR_EVENT_ALTER;
      break;
    case PT_EV_DROP:
      event = TR_EVENT_DROP;
      break;
    case PT_EV_COMMIT:
      event = TR_EVENT_COMMIT;
      break;
    case PT_EV_ROLLBACK:
      event = TR_EVENT_ROLLBACK;
      break;
    case PT_EV_ABORT:
      event = TR_EVENT_ABORT;
      break;
    case PT_EV_TIMEOUT:
      event = TR_EVENT_TIMEOUT;
      break;
    default:
      break;
    }

  return event;
}

/*
 * convert_misc_to_tr_time() - Converts a PT_MISC_TYPE into a corresponding
 *    			       TR_TYPE_TYPE constant.
 *   return: DB_TRIGGER_TIME
 *   pt_time(in): One of PT_MISC_TYPE
 *
 * Note:
 */
static DB_TRIGGER_TIME
convert_misc_to_tr_time (const PT_MISC_TYPE pt_time)
{
  DB_TRIGGER_TIME time;

  switch (pt_time)
    {
    case PT_AFTER:
      time = TR_TIME_AFTER;
      break;
    case PT_BEFORE:
      time = TR_TIME_BEFORE;
      break;
    case PT_DEFERRED:
      time = TR_TIME_DEFERRED;
      break;
    default:
      time = TR_TIME_NULL;
      break;
    }

  return time;
}

/*
 * convert_misc_to_tr_status() - Converts a PT_MISC_TYPE into the corresponding
 *				 TR_STATUE_TYPE.
 *   return: DB_TRIGGER_STATUS
 *   pt_status(in): One of PT_MISC_TYPE
 *
 * Note:
 */
static DB_TRIGGER_STATUS
convert_misc_to_tr_status (const PT_MISC_TYPE pt_status)
{
  DB_TRIGGER_STATUS status;

  switch (pt_status)
    {
    case PT_ACTIVE:
      status = TR_STATUS_ACTIVE;
      break;
    case PT_INACTIVE:
      status = TR_STATUS_INACTIVE;
      break;
    default:			/* if we get bogus input, should it be inactive ? */
      status = TR_STATUS_ACTIVE;
      break;
    }

  return status;
}

/*
 * convert_speclist_to_objlist() - Converts a PT_MISC_TYPE into the
 *				   corresponding TR_STATUE_TYPE.
 *   return: Error code
 *   triglist(out): Returned trigger object list
 *   specnode(in): Node with PT_TRIGGER_SPEC_LIST_INFO
 *
 * Note:
 *    This function converts a trigger specification list in PT format
 *    into a list of the corresponding trigger objects.
 *    This is used by a variety of the functions that accept trigger
 *    specifications.
 *    The list is an external MOP list and must be freed with ml_ext_free()
 *    or db_objlist_free.
 *    The alter flag is set for operations that alter triggers based
 *    on the WITH EVENT and ALL TRIGGERS specification.  In these cases
 *    we need to automatically filter out the triggers in the list for
 *    which we don't have authorization.
 */
static int
convert_speclist_to_objlist (DB_OBJLIST ** triglist, PT_NODE * specnode)
{
  int error = NO_ERROR;
  DB_OBJLIST *triggers, *etrigs;
  PT_NODE *names, *n, *events, *e;
  PT_EVENT_SPEC_INFO *espec;
  PT_EVENT_TARGET_INFO *target;
  const char *str, *attribute;
  DB_TRIGGER_EVENT tr_event;
  DB_OBJECT *trigger, *class_;

  triggers = NULL;

  if (specnode != NULL)
    {
      if (specnode->info.trigger_spec_list.all_triggers)
	{
	  error = tr_find_all_triggers (&triggers);
	}

      else if ((names = specnode->info.trigger_spec_list.trigger_name_list)
	       != NULL)
	{
	  /* since this is an explicitly specified list, if we do not have
	     alter authorization for any of the specified triggers, we need
	     to make sure the statement is not executed (no triggers are dropped).
	     Use tr_check_authorization to find out.
	   */
	  for (n = names; n != NULL && error == NO_ERROR; n = n->next)
	    {
	      str = n->info.name.original;
	      trigger = tr_find_trigger (str);
	      if (trigger == NULL)
		{
		  error = er_errid ();
		}
	      else
		{
		  error = ml_ext_add (&triggers, trigger, NULL);
		}
	    }
	}
      else if ((events = specnode->info.trigger_spec_list.event_list) != NULL)
	{
	  for (e = events; e != NULL && error == NO_ERROR; e = e->next)
	    {
	      class_ = NULL;
	      attribute = NULL;
	      espec = &(e->info.event_spec);
	      tr_event = convert_event_to_tr_event (espec->event_type);

	      if (espec->event_target != NULL)
		{
		  target = &(espec->event_target->info.event_target);
		  class_ =
		    db_find_class (target->class_name->info.name.original);
		  if (class_ == NULL)
		    {
		      error = er_errid ();
		    }
		  else
		    {
		      if (target->attribute != NULL)
			{
			  attribute = target->attribute->info.name.original;
			}

		      error =
			tr_find_event_triggers (tr_event, class_, attribute,
						true, &etrigs);
		      if (error == NO_ERROR)
			{
			  merge_mop_list_extension (etrigs, &triggers);
			}
		    }
		}
	    }
	}
    }

  if (error)
    {
      ml_ext_free (triggers);
    }
  else
    {
      *triglist = triggers;
    }

  return error;
}

/*
 * get_priority() -
 *   return: Double value
 *   parser(in): Parser context
 *   node(in): Priority value node
 *
 * Note:
 *    Shorthand function for getting the priority value out of the parse
 *    tree.  Formerly, we just assumed that this would be represented
 *    with a double value.  Now we use coersion.
 */
static double
get_priority (PARSER_CONTEXT * parser, PT_NODE * node)
{
  DB_VALUE *src, value;
  double priority;

  priority = TR_LOWEST_PRIORITY;

  src = pt_value_to_db (parser, node);
  if (src != NULL
      && (tp_value_coerce (src, &value, &tp_Double_domain) ==
	  DOMAIN_COMPATIBLE))
    {
      priority = DB_GET_DOUBLE (&value);
    }
  /* else, should be setting some kind of error */

  return priority;
}

/*
 * INSERT, UPDATE, & DELETE STATEMENTS
 */

/*
 * check_trigger() -
 *   return: Error code
 *   event(in): Trigger event type
 *   do_func(in): Function to do
 *   parser(in): Parser context used by do_func
 *   statement(in): Parse tree of a statement used by do_func
 *
 * Note: The function checks if there is any active trigger defined on
 *       the targets. If there is one, raise the trigger. Otherwise,
 *       perform the given do_ function.
 */
static int
check_trigger (DB_TRIGGER_EVENT event, PT_DO_FUNC * do_func,
	       PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err, result = NO_ERROR;
  TR_STATE *state;
  const char *savepoint_name = NULL;
  PT_NODE *node = NULL, *flat = NULL;
  DB_OBJECT *class_ = NULL;

  /* Prepare a trigger state for any triggers that must be raised in
     this statement */

  state = NULL;

  switch (event)
    {
    case TR_EVENT_STATEMENT_DELETE:
      node = statement->info.delete_.spec;
      while (node != NULL)
	{
	  if (node->info.spec.flag & PT_SPEC_FLAG_DELETE)
	    {
	      flat = node->info.spec.flat_entity_list;
	      class_ = (flat ? flat->info.name.db_object : NULL);
	      if (class_ == NULL)
		{
		  PT_INTERNAL_ERROR (parser, "invalid spec id");
		  result = ER_FAILED;
		  goto exit;
		}
	      result = tr_prepare_statement (&state, event, class_, 0, NULL);
	      if (result != NO_ERROR)
		{
		  goto exit;
		}
	    }
	  node = node->next;
	}
      break;

    case TR_EVENT_STATEMENT_INSERT:
      flat =
	(statement->info.insert.spec) ?
	statement->info.insert.spec->info.spec.flat_entity_list : NULL;
      class_ = (flat) ? flat->info.name.db_object : NULL;
      result = tr_prepare_statement (&state, event, class_, 0, NULL);
      break;

    case TR_EVENT_STATEMENT_UPDATE:
      {
	/* If this is an "update object" statement, we may not have a spec
	   list yet. This may have been fixed due to the recent changes in
	   pt_exec_trigger_stmt to do name resolution each time. */
	char **columns = NULL;
	int count =
	  pt_count_assignments (parser, statement->info.update.assignment);
	int idx;
	PT_ASSIGNMENTS_HELPER ea;
	PT_NODE *assign = NULL;

	columns = (char **) (malloc (count * sizeof (char *)));
	if (columns == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		    ER_OUT_OF_VIRTUAL_MEMORY, 1, count * sizeof (char *));
	    result = ER_FAILED;
	    goto exit;
	  }

	/* prepare trigger state structures */
	node = statement->info.update.spec;
	do
	  {
	    if (node != NULL)
	      {
		flat = node->info.spec.flat_entity_list;
		node = node->next;
	      }
	    else
	      {
		flat = statement->info.update.object_parameter;
	      }

	    if ((node == NULL || (node->info.spec.flag & PT_SPEC_FLAG_UPDATE))
		&& flat)
	      {
		idx = 0;
		pt_init_assignments_helper (parser, &ea,
					    statement->info.update.
					    assignment);
		while ((assign = pt_get_next_assignment (&ea)) != NULL)
		  {
		    if (assign->info.name.spec_id == flat->info.name.spec_id)
		      {
			columns[idx++] = (char *) assign->info.name.original;
		      }
		  }

		class_ = flat ? flat->info.name.db_object : NULL;
		if (class_ == NULL)
		  {
		    PT_INTERNAL_ERROR (parser, "invalid spec id");
		    result = ER_FAILED;
		    goto exit;
		  }

		result =
		  tr_prepare_statement (&state, event, class_, idx, columns);
	      }
	  }
	while (node);
	if (columns)
	  {
	    free_and_init (columns);
	  }
	break;
      }
    default:
      break;
    }

  if (result == NO_ERROR)
    {
      if (state == NULL)
	{
	  /* no triggers, just do it */
	  /* result = do_func(parser, statement); */
	  /* execute internal statements before and after do_func() */
	  result = do_check_internal_statements (parser, statement, do_func);
	}
      else
	{
	  /* the operations performed in 'tr_before',
	   * 'do_check_internal_statements' and 'tr_after' should be all
	   * contained in one transaction */
	  if (tr_Current_depth <= 1)
	    {
	      savepoint_name =
		mq_generate_name (parser, "UtrP", &tr_savepoint_number);
	      if (savepoint_name == NULL)
		{
		  result = ER_GENERIC_ERROR;
		  goto exit;
		}
	      result = tran_savepoint (savepoint_name, false);
	      if (result != NO_ERROR)
		{
		  goto exit;
		}
	    }

	  /* fire BEFORE STATEMENT triggers */
	  result = tr_before (state);
	  if (result == NO_ERROR)
	    {
	      /* note, do_insert, do_update, & do_delete don't return just errors,
	         they can also return positive result counts.  Need to specifically
	         check for result < 0
	       */
	      /* result = do_func(parser, statement); */
	      /* execute internal statements before and after do_func() */
	      result = do_check_internal_statements (parser, statement,
						     do_func);
	      if (result < NO_ERROR)
		{
		  tr_abort (state);
		  state = NULL;	/* state was freed */
		}
	      else
		{
		  /* try to preserve the usual result value */
		  err = tr_after (state);
		  if (err)
		    {
		      result = err;
		    }
		  if (tr_get_execution_state ())
		    {
		      state = NULL;	/* state was freed */
		    }

		}
	    }
	  else
	    {
	      state = NULL;
	    }
	}
    }

exit:
  if (state)
    {
      /* We need to free state and decrease the tr_Current_depth. */
      tr_abort (state);
    }

  if (result < NO_ERROR && savepoint_name != NULL
      && (result != ER_LK_UNILATERALLY_ABORTED))
    {
      /* savepoint from tran_savepoint() */
      (void) db_abort_to_savepoint (savepoint_name);
    }
  return result;
}

/*
 * do_check_delete_trigger() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *   do_func(in): Function to do
 *
 * Note: The function checks if there is any active trigger with event
 *   TR_EVENT_STATEMENT_DELETE defined on the target.
 *   If there is one, raise the trigger. Otherwise, perform the
 *   given do_ function.
 */
int
do_check_delete_trigger (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_DO_FUNC * do_func)
{
  if (prm_get_bool_value (PRM_ID_BLOCK_NOWHERE_STATEMENT)
      && statement->info.delete_.search_cond == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  return check_trigger (TR_EVENT_STATEMENT_DELETE,
			do_func, parser, statement);
}

/*
 * do_check_insert_trigger() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *   do_func(in): Function to do
 *
 * Note: The function checks if there is any active trigger with event
 *   TR_EVENT_STATEMENT_INSERT defined on the target.
 *   If there is one, raise the trigger. Otherwise, perform the
 *   given do_ function.
 */
int
do_check_insert_trigger (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_DO_FUNC * do_func)
{
  return check_trigger (TR_EVENT_STATEMENT_INSERT,
			do_func, parser, statement);
}

/*
 * find_update_columns() -
 *   return: Attribute (column) name array
 *   count_ptr(out): Returned name count
 *   statement(in): Parse tree of a statement to examine
 *
 * Note:
 *    This is used to to find the attribute/column names referenced in
 *    the statement.  It builds a array of strings and returns the length of
 *    the array.
 */
static char **
find_update_columns (int *count_ptr, PT_NODE * statement)
{
  PT_NODE *assign;
  char **columns;
  int count, size, i;
  PT_NODE *lhs, *att;

  assign = statement->info.update.assignment;
  for (count = 0; assign; assign = assign->next)
    {
      lhs = assign->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  /* multicolumn update */
	  count += pt_length_of_list (lhs->info.expr.arg1);
	}
      else
	{
	  count++;
	}
    }
  size = sizeof (char *) * count;

  columns = (char **) (malloc (size));
  if (columns == NULL)
    {
      return NULL;
    }

  assign = statement->info.update.assignment;
  for (i = 0; i < count; assign = assign->next)
    {
      lhs = assign->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  for (att = lhs->info.expr.arg1; att; att = att->next)
	    {
	      columns[i++] = (char *) att->info.name.original;
	    }
	}
      else
	{
	  columns[i++] = (char *) lhs->info.name.original;
	}
    }

  *count_ptr = count;
  return columns;
}

/*
 * do_check_update_trigger() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *
 * Note: The function checks if there is any active trigger with event
 *   TR_EVENT_STATEMENT_UPDATE defined on the target.
 *   If there is one, raise the trigger. Otherwise, perform the
 *   given do_ function.
 */
int
do_check_update_trigger (PARSER_CONTEXT * parser, PT_NODE * statement,
			 PT_DO_FUNC * do_func)
{
  int err;

  if (prm_get_bool_value (PRM_ID_BLOCK_NOWHERE_STATEMENT)
      && statement->info.update.search_cond == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  err = check_trigger (TR_EVENT_STATEMENT_UPDATE, do_func, parser, statement);
  return err;
}

/*
 * CREATE TRIGGER STATEMENT
 */

/*
 * get_activity_info() - Works for do_create_trigger
 *   return: None
 *   parser(in): Parse context for the create trigger statement
 *   type(out): Returned type of the activity
 *   source(out) : Returned source of the activity (sometimes NULL)
 *   statement(in): Sub-tree for the condition or action expression
 *
 * Note:
 *    This is used to convert a parser sub-tree into the corresponding
 *    pair of DB_TRIGGER_ACTIVITY and source string suitable for use
 *    with tr_create_trigger.
 *    Since we can't use this parsed representation of the expressions
 *    anyway (they aren't inside the proper scope), we just convert
 *    them back into strings with parser_print_tree and let the trigger manager
 *    call pt_compile_trigger_stmt when necessary.
 */
static void
get_activity_info (PARSER_CONTEXT * parser, DB_TRIGGER_ACTION * type,
		   const char **source, PT_NODE * statement)
{
  PT_NODE *str;

  *type = TR_ACT_NULL;
  *source = NULL;

  if (statement != NULL)
    {
      if (IS_REJECT_ACTION_STATEMENT (statement))
	{
	  *type = TR_ACT_REJECT;
	}
      else if (IS_INVALIDATE_ACTION_STATEMENT (statement))
	{
	  *type = TR_ACT_INVALIDATE;
	}
      else if (IS_PRINT_ACTION_STATEMENT (statement))
	{
	  *type = TR_ACT_PRINT;

	  /* extract the print string from the parser node,
	     not sure if I should be looking at the "data_value.s" field
	     or the "text" field, they seem to be the same always. */
	  str = statement->info.trigger_action.string;
	  if (str->node_type == PT_VALUE)
	    {
	      *source = (char *) str->info.value.data_value.str->bytes;
	    }
	}
      else
	{
	  /* complex expression */
	  *type = TR_ACT_EXPRESSION;
	  *source = parser_print_tree_with_quotes (parser, statement);
	}
    }
}

/*
 * do_create_trigger() -
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a statement
 *
 * Note: The function creates a trigger object by calling the trigger
 *   create function.
 */
int
do_create_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *cond, *action, *target, *attr, *pri;
  const char *name;
  DB_TRIGGER_STATUS status;
  double priority;
  DB_TRIGGER_EVENT event;
  DB_OBJECT *class_;
  const char *attribute;
  DB_TRIGGER_ACTION cond_type, action_type;
  DB_TRIGGER_TIME cond_time, action_time;
  const char *cond_source, *action_source;
  DB_OBJECT *trigger;
  SM_CLASS *smclass = NULL;
  int error = NO_ERROR;
  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  name = PT_NODE_TR_NAME (statement);
  status = PT_NODE_TR_STATUS (statement);

  pri = PT_NODE_TR_PRI (statement);
  if (pri != NULL)
    {
      priority = get_priority (parser, pri);
    }
  else
    {
      priority = TR_LOWEST_PRIORITY;
    }

  event = PT_NODE_TR_EVENT_TYPE (statement);
  class_ = NULL;
  attribute = NULL;
  target = PT_NODE_TR_TARGET (statement);
  if (target)
    {
      class_ = db_find_class (PT_TR_TARGET_CLASS (target));
      if (class_ == NULL)
	{
	  return er_errid ();
	}
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
      if (sm_has_text_domain (db_get_attributes (class_), 1))
	{
	  /* prevent to create a trigger at the class to contain TEXT */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_REGU_NOT_IMPLEMENTED,
		  1, rel_major_release_string ());
	  return er_errid ();
	}
#endif /* ENABLE_UNUSED_FUNCTION */
      attr = PT_TR_TARGET_ATTR (target);
      if (attr)
	{
	  attribute = PT_TR_ATTR_NAME (attr);
	}
      error = au_fetch_class (class_, &smclass, AU_FETCH_READ, AU_SELECT);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (smclass->partition_of != NULL && smclass->users == NULL)
	{
	  /* Triggers must be created on the partitioned table, not on a
	   * specific partition
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_INVALID_PARTITION_REQUEST, 0);
	  return ER_INVALID_PARTITION_REQUEST;
	}
    }
  cond = PT_NODE_COND (statement);
  cond_time = PT_NODE_COND_TIME (statement);
  /* note that cond_type can only be TR_ACT_EXPRESSION, if there is
     no conditino node, cond_source will be left NULL */
  get_activity_info (parser, &cond_type, &cond_source, cond);

  action = PT_NODE_ACTION (statement);
  action_time = PT_NODE_ACTION_TIME (statement);
  get_activity_info (parser, &action_type, &action_source, action);

  trigger =
    tr_create_trigger (name, status, priority, event, class_, attribute,
		       cond_time, cond_source, action_time, action_type,
		       action_source);

  if (trigger == NULL)
    {
      return er_errid ();
    }

  /* Save the new trigger object in the parse tree.
     Actually, we probably should also allow INTO variable sub-clause to
     be compatible with INSERT statement. In that case, the portion of
     code in do_insert() for saving the new object and creating a label
     table entry needs to be made a extern function.
   */

  /* This should be treated like a "create class"
     statement not like an "insert" statement.  The trigger object that
     gets created can't be assigned with an INTO clause so there's no need
     to return it. Assuming this doesn't host anything, delete the
     commented out lines below.
   */
#if 0
  if ((value = db_value_create ()) == NULL)
    return er_errid ();
  db_make_object (value, trigger);
  statement->etc = (void *) value;
#endif

  if (smclass != NULL && smclass->users != NULL)
    {
      /* We have to flush the newly created trigger if the class it belongs to
       * has subclasses. This is because the same trigger is assigned to the
       * whole hierarchy and we have to make sure it does not remain a
       * temporary object when it is first compiled.
       * Since the class that this trigger belongs to might also be a
       * temporary object, we actually have to flush the whole workspace
       */
      error = locator_all_flush ();
    }

  return error;
}

/*
 *  MISC TRIGGER OPERATIONS
 */

/*
 * do_drop_trigger() - Drop one or more triggers based on a trigger spec list.
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a statement
 *
 * Note:
 */
int
do_drop_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *speclist;
  DB_OBJLIST *triggers, *t;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  /* The grammar has beem define such that DROP TRIGGER can only
     be used with an explicit list of named triggers.  Although
     convert_speclist_to_objlist will handle the WITH EVENT and ALL TRIGGERS
     cases we shouldn't see those here.  If for some reason they
     do sneak in, we may get errors when we call tr_drop_triggger()
     on triggers we don't own.
   */

  speclist = statement->info.drop_trigger.trigger_spec_list;
  if (convert_speclist_to_objlist (&triggers, speclist))
    {
      return er_errid ();
    }

  if (triggers != NULL)
    {
      /* make sure we have ALTER authorization on all the triggers
         before proceeding */

      for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	{
	  error = tr_check_authorization (t->op, true);
	}

      if (error == NO_ERROR)
	{
	  /* shouldn't encounter errors in this loop, if we do,
	     may have to abort the transaction */
	  for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	    {
	      error = tr_drop_trigger (t->op, false);
	    }
	}

      /* always free this */
      ml_ext_free (triggers);
    }

  return error;
}

/*
 * do_alter_trigger() - Alter the priority or status of one or more triggers.
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree with alter trigger node
 *
 * Note:
 */
int
do_alter_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *speclist, *p_node;
  DB_OBJLIST *triggers, *t;
  double priority = TR_LOWEST_PRIORITY;
  DB_TRIGGER_STATUS status;
  PT_NODE *trigger_owner, *trigger_name;
  const char *trigger_owner_name;
  DB_VALUE returnval, trigger_name_val, user_val;

  CHECK_MODIFICATION_ERROR ();

  triggers = NULL;
  p_node = statement->info.alter_trigger.trigger_priority;
  trigger_owner = statement->info.alter_trigger.trigger_owner;
  speclist = statement->info.alter_trigger.trigger_spec_list;
  if (convert_speclist_to_objlist (&triggers, speclist))
    {
      return er_errid ();
    }

  /* currently, we can' set the status and priority at the same time.
     The existance of p_node determines which type of alter statement this is.
   */
  status = TR_STATUS_INVALID;

  if (trigger_owner != NULL)
    {
      trigger_owner_name = trigger_owner->info.name.original;
      trigger_name = speclist->info.trigger_spec_list.trigger_name_list;
    }
  else if (p_node != NULL)
    {
      priority = get_priority (parser, p_node);
    }
  else
    {
      status =
	convert_misc_to_tr_status (statement->info.
				   alter_trigger.trigger_status);
    }

  if (error == NO_ERROR)
    {
      /* make sure we have ALTER authorization on all the triggers
         before proceeding */
      for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	{
	  error = tr_check_authorization (t->op, true);
	}

      if (error == NO_ERROR)
	{
	  for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	    {
	      if (status != TR_STATUS_INVALID)
		{
		  error = tr_set_status (t->op, status, false);
		}

	      if (error == NO_ERROR && p_node != NULL)
		{
		  error = tr_set_priority (t->op, priority, false);
		}

	      if (error == NO_ERROR && trigger_owner != NULL)
		{
		  assert (trigger_name != NULL);

		  db_make_null (&returnval);

		  db_make_string (&trigger_name_val,
				  trigger_name->info.name.original);
		  db_make_string (&user_val, trigger_owner_name);

		  au_change_trigger_owner_method (t->op, &returnval,
						  &trigger_name_val,
						  &user_val);

		  if (DB_VALUE_TYPE (&returnval) == DB_TYPE_ERROR)
		    {
		      error = DB_GET_ERROR (&returnval);
		      break;
		    }

		  trigger_name = trigger_name->next;
		}
	    }
	}
    }

  if (triggers != NULL)
    {
      ml_ext_free (triggers);
    }

  return error;
}

/*
 * do_execute_trigger() - Execute the deferred activities for one or more
 *			  triggers.
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a execute trigger statement
 *
 * Note:
 */
int
do_execute_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *speclist;
  DB_OBJLIST *triggers, *t;

  CHECK_MODIFICATION_ERROR ();

  speclist = statement->info.execute_trigger.trigger_spec_list;
  error = convert_speclist_to_objlist (&triggers, speclist);

  if (error == NO_ERROR && triggers != NULL)
    {
      for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	{
	  error = tr_execute_deferred_activities (t->op, NULL);
	}
      ml_ext_free (triggers);
    }

  return error;
}

/*
 * do_remove_trigger() - Remove the deferred activities for one or more triggers
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a remove trigger statement
 *
 * Note:
 */
int
do_remove_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *speclist;
  DB_OBJLIST *triggers, *t;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  speclist = statement->info.remove_trigger.trigger_spec_list;
  error = convert_speclist_to_objlist (&triggers, speclist);

  if (error == NO_ERROR && triggers != NULL)
    {
      for (t = triggers; t != NULL && error == NO_ERROR; t = t->next)
	{
	  error = tr_drop_deferred_activities (t->op, NULL);
	}

      ml_ext_free (triggers);
    }

  return error;
}

/*
 * do_rename_trigger() - Rename a trigger
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a rename trigger statement
 *
 * Note:
 */
int
do_rename_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  const char *old_name, *new_name;
  DB_OBJECT *trigger;

  CHECK_MODIFICATION_ERROR ();

  if (prm_get_bool_value (PRM_ID_BLOCK_DDL_STATEMENT))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  old_name = statement->info.rename_trigger.old_name->info.name.original;
  new_name = statement->info.rename_trigger.new_name->info.name.original;

  trigger = tr_find_trigger (old_name);
  if (trigger == NULL)
    {
      error = er_errid ();
    }
  else
    {
      error = tr_rename_trigger (trigger, new_name, false);
    }

  return error;
}

/*
 * do_set_trigger() - Set one of the trigger options
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in): Parse tree of a set trigger statement
 *
 * Note:
 */
int
do_set_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_VALUE src, dst;

  pt_evaluate_tree (parser, statement->info.set_trigger.val, &src, 1);
  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      error = er_errid ();
    }
  else
    {
      switch (tp_value_coerce (&src, &dst, &tp_Integer_domain))
	{
	case DOMAIN_INCOMPATIBLE:
	  error = ER_TP_CANT_COERCE;
	  break;
	case DOMAIN_OVERFLOW:
	  error = ER_TP_CANT_COERCE_OVERFLOW;
	  break;
	case DOMAIN_ERROR:
	  /*
	   * tp_value_coerce() *appears* to set er_errid whenever it
	   * returns DOMAIN_ERROR (which probably really means malloc
	   * failure or something else).
	   */
	  error = er_errid ();
	  break;
	default:
	  break;
	}
    }

  if (error == ER_TP_CANT_COERCE || error == ER_TP_CANT_COERCE_OVERFLOW)
    {
      char buf1[MAX_DOMAIN_NAME_SIZE];
      char buf2[MAX_DOMAIN_NAME_SIZE];
      (void) tp_value_domain_name (&src, buf1, sizeof (buf1));
      (void) tp_domain_name (&tp_Integer_domain, buf2, sizeof (buf2));
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, buf1, buf2);
    }
  else if (error == NO_ERROR)
    {
      PT_MISC_TYPE option;
      int v;

      option = statement->info.set_trigger.option;
      v = DB_GET_INT (&dst);

      if (option == PT_TRIGGER_TRACE)
	{
	  error = tr_set_trace ((bool) v);
	}
      else if (option == PT_TRIGGER_DEPTH)
	{
	  error = tr_set_depth (v);
	}
    }

  /*
   * No need to clear dst, because it's either NULL or an integer at
   * this point.  src could be arbitrarily complex, and it was created
   * by pt_evaluate_tree, so we need to clear it before we leave.
   */
  db_value_clear (&src);

  return error;
}

/*
 * do_get_trigger() - Get one of the trigger option values.
 *   return: Error code
 *   parser(in): Parse context
 *   statement(in/out): Parse tree of a get trigger statement
 *
 * Note:
 */
int
do_get_trigger (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  const char *into_label;
  DB_VALUE *ins_val;
  PT_NODE *into;
  PT_MISC_TYPE option;

  /* create a value to hold the result */
  ins_val = db_value_create ();
  if (ins_val == NULL)
    {
      return er_errid ();
    }

  option = statement->info.set_trigger.option;
  switch (option)
    {
    case PT_TRIGGER_DEPTH:
      db_make_int (ins_val, tr_get_depth ());
      break;
    case PT_TRIGGER_TRACE:
      db_make_int (ins_val, tr_get_trace ());
      break;
    default:
      db_make_null (ins_val);	/* can't happen */
      break;
    }

  statement->etc = (void *) ins_val;

  into = statement->info.get_trigger.into_var;
  if (into != NULL
      && into->node_type == PT_NAME
      && (into_label = into->info.name.original) != NULL)
    {
      /* create another DB_VALUE for the label table */
      ins_val = db_value_copy (ins_val);

      /* enter the value into the table */
      error = pt_associate_label_with_value_check_reference (into_label,
							     ins_val);
    }

  return error;
}







/*
 * Function Group:
 * DO functions for update statements
 *
 */

typedef enum
{ NORMAL_UPDATE, UPDATE_OBJECT, ON_DUPLICATE_KEY_UPDATE } UPDATE_TYPE;

#define DB_VALUE_STACK_MAX 40

/* It is used to generate unique savepoint names */
static int update_savepoint_number = 0;

static void unlink_list (PT_NODE * list);

static QFILE_LIST_ID *get_select_list_to_update (PARSER_CONTEXT * parser,
						 PT_NODE * from,
						 PT_NODE * column_names,
						 PT_NODE * column_values,
						 PT_NODE * where,
						 PT_NODE * order_by,
						 PT_NODE * orderby_for,
						 PT_NODE * using_index,
						 PT_NODE * class_specs);
static int update_object_attribute (PARSER_CONTEXT * parser,
				    DB_OTMPL * otemplate, PT_NODE * name,
				    DB_ATTDESC * attr_desc, DB_VALUE * value);
static int update_object_tuple (PARSER_CONTEXT * parser,
				CLIENT_UPDATE_INFO * assigns,
				int assigns_count,
				CLIENT_UPDATE_CLASS_INFO * upd_classes_info,
				int classes_cnt,
				const int turn_off_unique_check,
				UPDATE_TYPE update_type);
static int update_object_by_oid (PARSER_CONTEXT * parser,
				 PT_NODE * statement,
				 UPDATE_TYPE update_type);
static int init_update_data (PARSER_CONTEXT * parser, PT_NODE * statement,
			     CLIENT_UPDATE_INFO ** assigns_data,
			     int *assigns_count,
			     CLIENT_UPDATE_CLASS_INFO ** cls_data,
			     int *cls_count, DB_VALUE ** values,
			     int *values_cnt);
static int update_objs_for_list_file (PARSER_CONTEXT * parser,
				      QFILE_LIST_ID * list_id,
				      PT_NODE * statement);
static int update_class_attributes (PARSER_CONTEXT * parser,
				    PT_NODE * statement);
static int update_at_server (PARSER_CONTEXT * parser, PT_NODE * from,
			     PT_NODE * statement, PT_NODE ** non_null_attrs,
			     int has_uniques);
static int update_check_for_constraints (PARSER_CONTEXT * parser,
					 int *has_unique,
					 PT_NODE ** not_nulls,
					 const PT_NODE * statement);
static int update_check_for_fk_cache_attr (PARSER_CONTEXT * parser,
					   const PT_NODE * statement);
static bool update_check_having_meta_attr (PARSER_CONTEXT * parser,
					   PT_NODE * assignment);
static int update_real_class (PARSER_CONTEXT * parser, PT_NODE * statement);
static XASL_NODE *statement_to_update_xasl (PARSER_CONTEXT * parser,
					    PT_NODE * statement,
					    PT_NODE ** non_null_attrs);
static int is_server_update_allowed (PARSER_CONTEXT * parser,
				     PT_NODE ** non_null_attrs,
				     int *has_uniques,
				     int *const server_allowed,
				     const PT_NODE * statement);

/*
 * unlink_list - Unlinks next pointer shortcut of lhs, rhs assignments
 *   return: None
 *   list(in): Node list to cut
 *
 * Note:
 */
static void
unlink_list (PT_NODE * list)
{
  PT_NODE *next;

  while (list)
    {
      next = list->next;
      list->next = NULL;
      list = next;
    }
}

/*
 * get_select_list_to_update -
 *   return: List file if success, otherwise NULL
 *   parser(in): Parser context
 *   from(in): Parse tree of an FROM class
 *   column_values(in): Column list in SELECT clause
 *   where(in): WHERE clause
 *   order_by(in): ORDER BY clause
 *   orderby_num(in): converted from ORDER BY with LIMIT
 *   using_index(in): USING INDEX clause
 *   class_specs(in): Another class specs in FROM clause
 *
 * Note:
 */
static QFILE_LIST_ID *
get_select_list_to_update (PARSER_CONTEXT * parser, PT_NODE * from,
			   PT_NODE * column_names, PT_NODE * column_values,
			   PT_NODE * where, PT_NODE * order_by,
			   PT_NODE * orderby_for, PT_NODE * using_index,
			   PT_NODE * class_specs)
{
  PT_NODE *statement = NULL;
  QFILE_LIST_ID *result = NULL;

  if (from && (from->node_type == PT_SPEC) && from->info.spec.range_var
      &&
      ((statement =
	pt_to_upd_del_query (parser, column_names, column_values, from,
			     class_specs, where, using_index, order_by,
			     orderby_for, 0 /* not server update */ ,
			     PT_COMPOSITE_LOCKING_UPDATE)) != NULL))
    {
      /* If we are updating a proxy, the select is not yet fully translated.
       * If we are updating anything else, this is a no-op.
       */
      statement = mq_translate (parser, statement);

      if (statement)
	{
	  /* This enables authorization checking during methods in queries */
	  AU_ENABLE (parser->au_save);
	  if (do_select (parser, statement) < NO_ERROR)
	    {
	      /* query failed, an error has already been set */
	      statement = NULL;
	    }
	  AU_DISABLE (parser->au_save);
	}
    }

  if (statement)
    {
      result = (QFILE_LIST_ID *) statement->etc;
      parser_free_tree (parser, statement);
    }
  return result;
}

/*
 * update_object_attribute -
 *   return: Error code
 *   parser(in): Parser context
 *   otemplate(in/out): Class template to be edited
 *   name(in): Parse tree of a attribute name
 *   attr_desc(in): Descriptor of attribute to update
 *   value(in): New attribute value
 *
 * Note: If db_put fails, return an error
 */
static int
update_object_attribute (PARSER_CONTEXT * parser, DB_OTMPL * otemplate,
			 PT_NODE * name, DB_ATTDESC * attr_desc,
			 DB_VALUE * value)
{
  int error;

  if (name->info.name.db_object && db_is_vclass (name->info.name.db_object))
    {
      /* this is a shared attribute of a view.
       * this means this cannot be updated in the template for
       * this real class. Its simply done separately by a db_put.
       */
      error = obj_set_shared (name->info.name.db_object,
			      name->info.name.original, value);
    }
  else
    {
      /* the normal case */
      error = dbt_dput_internal (otemplate, attr_desc, value);
    }

  return error;
}

/*
 * update_object_tuple - Updates object attributes with db_values
 *   return: Error code
 *   assigns(in): array of assignments
 *   assigns_count(in): no of assignments
 *   upd_classes_info(in): array of classes info
 *   classes_cnt(in): no of classes
 *   turn_off_unique_check(in):
 *   update_type(in):
 *
 * Note:
 */
static int
update_object_tuple (PARSER_CONTEXT * parser,
		     CLIENT_UPDATE_INFO * assigns, int assigns_count,
		     CLIENT_UPDATE_CLASS_INFO * upd_classes_info,
		     int classes_cnt, const int turn_off_unique_check,
		     UPDATE_TYPE update_type)
{
  int error = NO_ERROR;
  DB_OTMPL *otemplate = NULL, *otmpl = NULL;
  int idx = 0, upd_tpl_cnt = 0;
  DB_OBJECT *real_object = NULL, *object = NULL;
  SM_CLASS *smclass = NULL;
  SM_ATTRIBUTE *att = NULL;
  char flag_att = 0;
  int exist_active_triggers = false;
  CLIENT_UPDATE_INFO *assign = NULL;
  CLIENT_UPDATE_CLASS_INFO *cls_info = NULL;

  for (idx = 0; idx < classes_cnt && error == NO_ERROR; idx++)
    {
      cls_info = &upd_classes_info[idx];

      if (DB_IS_NULL (cls_info->oid))
	{
	  continue;
	}

      object = DB_GET_OBJECT (cls_info->oid);
      if (db_is_deleted (object))
	{
	  continue;
	}

      real_object = db_real_instance (object);
      if (real_object == NULL)
	{			/* real_object's fail */
	  error = er_errid ();
	  if (error == NO_ERROR)
	    {
	      error = ER_GENERIC_ERROR;
	    }
	  return error;
	}

      /* if this is the first tuple or the class has changed to a new subclass
       * then fetch new class */
      if (cls_info->class_mop == NULL
	  || (object->class_mop != NULL
	      && ws_mop_compare (object->class_mop,
				 cls_info->class_mop) != 0))
	{
	  cls_info->class_mop = object->class_mop;

	  if (object->class_mop != NULL)
	    {
	      error = au_fetch_class (object->class_mop, &smclass,
				      AU_FETCH_READ, AU_SELECT);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	      cls_info->smclass = smclass;
	    }
	}
      else
	{
	  /* otherwise use old class */
	  smclass = cls_info->smclass;
	}

      otemplate = dbt_edit_object (real_object);
      if (otemplate == NULL)
	{
	  return er_errid ();
	}

      if (turn_off_unique_check)
	{
	  obt_disable_unique_checking (otemplate);
	}

      /* this is an update - force NOT NULL constraint check */
      otemplate->force_check_not_null = 1;

      /* If this update came from INSERT ON DUPLICATE KEY UPDATE,
       * flush the object on updating it.
       */
      if (update_type == ON_DUPLICATE_KEY_UPDATE
	  || smclass->partition_of != NULL)
	{
	  obt_set_force_flush (otemplate);
	}

      /* iterate through class assignments and update template with new
       * values */
      for (assign = cls_info->first_assign;
	   assign != NULL && error == NO_ERROR; assign = assign->next)
	{
	  /* if this is the first update, get the attribute descriptor */
	  if (assign->attr_desc == NULL
	      /* don't get descriptors for shared attrs of views */
	      && (assign->upd_col_name->info.name.db_object == NULL
		  || !db_is_vclass (assign->upd_col_name->info.
				    name.db_object)))
	    {
	      error = db_get_attribute_descriptor (real_object,
						   assign->upd_col_name->info.
						   name.original, 0, 1,
						   &assign->attr_desc);
	    }

	  if (error == NO_ERROR)
	    {
	      /* update tuple's template */
	      error =
		update_object_attribute (parser, otemplate,
					 assign->upd_col_name,
					 assign->attr_desc, assign->db_val);

	      /* clear not constant values */
	      if (!assign->is_const)
		{
		  db_value_clear (assign->db_val);
		}
	    }
	}

      if (error != NO_ERROR)
	{
	  /* abort if an error has occurred */
	  (void) dbt_abort_object (otemplate);
	}
      else
	{
	  /* update tuple with new values */
	  object = dbt_finish_object (otemplate);
	  if (object == NULL)
	    {
	      error = er_errid ();
	      (void) dbt_abort_object (otemplate);
	      return error;
	    }
	  else
	    {
	      /* check condition for 'with check option' */
	      error =
		mq_evaluate_check_option (parser,
					  cls_info->check_where !=
					  NULL ? cls_info->check_where->info.
					  check_option.expr : NULL, object,
					  cls_info->spec->info.
					  spec.flat_entity_list);
	    }
	}
      upd_tpl_cnt++;
    }

  return error == NO_ERROR ? upd_tpl_cnt : error;
}

/*
 * update_object_by_oid - Updates attributes of object by oid
 *   return: 1 if success, otherwise returns error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a update statement
 *   update_type(in): denote whether the update comes from normal update stmt,
 *		      update object stmt or insert on duplicate key update stmt.
 *
 * Note:
 */
static int
update_object_by_oid (PARSER_CONTEXT * parser, PT_NODE * statement,
		      UPDATE_TYPE update_type)
{
  int error = NO_ERROR;
  DB_OBJECT *oid = statement->info.update.object;
  int i = 0;
  PT_NODE *node = NULL;
  int vals_cnt = 0;
  PT_NODE *class_;
  PT_NODE *lhs;

  if (!statement->info.update.spec
      || !(class_ = statement->info.update.spec->info.spec.flat_entity_list)
      || !(class_->info.name.db_object)
      || statement->info.update.spec->next != NULL)
    {
      PT_INTERNAL_ERROR (parser, "update");
      return ER_GENERIC_ERROR;
    }

  /* fetch classes that will be updated */
  node = statement->info.update.spec;
  while (node)
    {
      if (node->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	{
	  if (!locator_fetch_class
	      (node->info.spec.flat_entity_list->info.name.db_object,
	       DB_FETCH_CLREAD_INSTWRITE))
	    {
	      return er_errid ();
	    }
	}

      node = node->next;
    }

  /* get first argument of first assignment */
  lhs = statement->info.update.assignment->info.expr.arg1;
  if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
    {
      lhs = lhs->info.expr.arg1;
    }

  if (lhs->info.name.meta_class == PT_META_ATTR)
    {
      /* if left argument of first assignment is an attribute then all other
       * assignments are to class attributes */
      error = update_class_attributes (parser, statement);
    }
  else
    {
      /* update object */
      int assigns_count = 0, upd_cls_cnt = 0, multi_assign_cnt = 0;
      CLIENT_UPDATE_INFO *assigns = NULL;
      CLIENT_UPDATE_CLASS_INFO *cls_info = NULL;
      DB_VALUE *dbvals = NULL;
      PT_ASSIGNMENTS_HELPER ea;
      PT_NODE *rhs = NULL, *lhs = NULL;

      /* load structures for update */
      error = init_update_data (parser, statement, &assigns, &assigns_count,
				&cls_info, &upd_cls_cnt, &dbvals, &vals_cnt);

      DB_MAKE_OBJECT (&dbvals[0], oid);

      if (error == NO_ERROR)
	{
	  /* iterate through assignments and evaluate right side of each
	   * assignment */
	  i = 0;
	  pt_init_assignments_helper (parser, &ea,
				      statement->info.update.assignment);
	  while (pt_get_next_assignment (&ea) && error == NO_ERROR)
	    {
	      rhs = ea.rhs;
	      lhs = ea.lhs;
	      multi_assign_cnt = 1;
	      /* for multi-column assignments with common right side count
	       * number of attributes to assign to */
	      if (ea.is_n_column)
		{
		  while (pt_get_next_assignment (&ea) && rhs == ea.rhs)
		    {
		      multi_assign_cnt++;
		    }
		}

	      error = mq_evaluate_expression_having_serial (parser, rhs,
							    assigns[i].db_val,
							    multi_assign_cnt,
							    oid,
							    lhs->info.name.
							    spec_id);
	      i += multi_assign_cnt;
	    }

	  /* update tuple */
	  if (error >= NO_ERROR)
	    {
	      error =
		update_object_tuple (parser, assigns, assigns_count,
				     cls_info, upd_cls_cnt, 0, update_type);
	    }

	}

      /* free assignments array */
      if (assigns != NULL)
	{
	  /* free attribute descriptors */
	  for (i = assigns_count - 1; i >= 0; i--)
	    {
	      if (assigns[i].attr_desc)
		{
		  db_free_attribute_descriptor (assigns[i].attr_desc);
		}
	    }
	  db_private_free (NULL, assigns);
	}

      /* free classes information */
      if (cls_info != NULL)
	{
	  db_private_free (NULL, cls_info);
	}

      /* free dbvals array */
      if (dbvals != NULL)
	{
	  db_private_free (NULL, dbvals);
	}
    }

  if (error < NO_ERROR)
    return error;
  else
    return 1;			/* we successfully updated 1 object */
}

/*
 * init_update_data () - init update data structures
 *   return: NO_ERROR or error code
 *   parser(in): Parser context
 *   assigns_data(out): address of a pointer variable that will receive the
 *			   array for assignments info. This array must be
 *			   released by the caller
 *   assigns_no(out): address of a int variable that will receive number of
 *		      assignments
 *   cls_data(out): address of a pointer that will receive information about
 *		    classes that will be updated
 *   cls_count(out): address of a integer variable that will receive number of
 *		     classes that will be updated
 *   values(out): address of a pointer that will receive an array of DB_VALUE
 *		  that represents runtime computed values and constants.
 *		  This array is referenced by elements of assigns_data.
 *    values_cnt(out): number of classes OIDs + values computed at
 *		       runtime for assignments.
 *
 * Note:
 */
static int
init_update_data (PARSER_CONTEXT * parser, PT_NODE * statement,
		  CLIENT_UPDATE_INFO ** assigns_data, int *assigns_count,
		  CLIENT_UPDATE_CLASS_INFO ** cls_data, int *cls_count,
		  DB_VALUE ** values, int *values_cnt)
{
  int error = NO_ERROR;
  int assign_cnt = 0, upd_cls_cnt = 0, vals_cnt = 0, idx, idx2, idx3;
  PT_ASSIGNMENTS_HELPER ea;
  PT_NODE *node = NULL, *assignments, *spec, *class_spec, *check_where;
  DB_VALUE *dbvals = NULL;
  CLIENT_UPDATE_CLASS_INFO *cls_info = NULL, *cls_info_tmp = NULL;
  CLIENT_UPDATE_INFO *assigns = NULL, *assign = NULL, *assign2 = NULL;

  assign_cnt = vals_cnt = 0;
  assignments = statement->node_type == PT_MERGE
    ? statement->info.merge.update.assignment
    : statement->info.update.assignment;
  spec = statement->node_type == PT_MERGE
    ? statement->info.merge.into : statement->info.update.spec;
  class_spec = statement->node_type == PT_MERGE ? NULL
    : statement->info.update.class_specs;
  check_where = statement->node_type == PT_MERGE
    ? statement->info.merge.check_where : statement->info.update.check_where;

  pt_init_assignments_helper (parser, &ea, assignments);
  while (pt_get_next_assignment (&ea))
    {
      if (!ea.is_rhs_const)
	{
	  /* count values that are not constants */
	  vals_cnt++;
	}
      /* count number of assignments */
      assign_cnt++;
    }

  /* allocate memory for assignment structures */
  assigns =
    (CLIENT_UPDATE_INFO *) db_private_alloc (NULL, assign_cnt *
					     sizeof (CLIENT_UPDATE_INFO));
  if (assigns == NULL)
    {
      error = ER_REGU_NO_SPACE;
      goto error_return;
    }

  node = spec;
  while (node)
    {
      if (node->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	{
	  /* count classes that will be updated */
	  upd_cls_cnt++;
	}

      node = node->next;
    }

  node = class_spec;
  while (node)
    {
      if (node->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	{
	  /* count classes that will be updated */
	  upd_cls_cnt++;
	}
      node = node->next;
    }

  /* allocate array of classes information structures */
  cls_info =
    (CLIENT_UPDATE_CLASS_INFO *) db_private_alloc (NULL,
						   upd_cls_cnt *
						   sizeof
						   (CLIENT_UPDATE_CLASS_INFO));
  if (cls_info == NULL)
    {
      error = ER_REGU_NO_SPACE;
      goto error_return;
    }

  /* add number of class oid's */
  vals_cnt += upd_cls_cnt;
  /* allocate array of DB_VALUE's. The length of the array must be equal to
   * that of select statement's list */
  dbvals =
    (DB_VALUE *) db_private_alloc (NULL,
				   (assign_cnt +
				    upd_cls_cnt) * sizeof (DB_VALUE));
  if (dbvals == NULL)
    {
      error = ER_REGU_NO_SPACE;
      goto error_return;
    }

  /* initialize classes info array */
  idx = 0;
  node = spec;
  while (node)
    {
      if (node->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	{
	  PT_NODE *save = check_where;

	  cls_info_tmp = &cls_info[idx++];
	  cls_info_tmp->spec = node;
	  cls_info_tmp->first_assign = NULL;
	  cls_info_tmp->class_mop = NULL;

	  /* condition to check for 'with check option' option */
	  while (check_where != NULL
		 && check_where->info.check_option.spec_id !=
		 node->info.spec.id)
	    {
	      check_where = check_where->next;
	    }
	  cls_info_tmp->check_where = check_where;

	  check_where = save;
	}

      node = node->next;
    }

  /* initialize classes info array */
  node = class_spec;
  while (node)
    {
      if (node->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	{
	  PT_NODE *save = check_where;

	  cls_info_tmp = &cls_info[idx++];
	  cls_info_tmp->spec = node;
	  cls_info_tmp->first_assign = NULL;
	  cls_info_tmp->class_mop = NULL;

	  /* condition to check for 'with check option' option */
	  while (check_where != NULL
		 && check_where->info.check_option.spec_id !=
		 node->info.spec.id)
	    {
	      check_where = check_where->next;
	    }
	  cls_info_tmp->check_where = check_where;

	  check_where = save;
	}

      node = node->next;
    }

  /* Fill assignment structures */
  idx = 0;
  pt_init_assignments_helper (parser, &ea, assignments);
  for (idx3 = 1, assign = assigns; pt_get_next_assignment (&ea); assign++)
    {
      assign->attr_desc = NULL;
      assign->upd_col_name = ea.lhs;
      /* Distribution of dbvals array. The array must match the select list
       * of the generated select statement: first upd_cls_cnt elements must
       * be associated with OID representing tuple from a class, followed
       * by values that must be calculated at runtime for assignment and then
       * by constants */
      if (ea.is_rhs_const)
	{
	  /* constants */
	  assign->db_val = &dbvals[assign_cnt + upd_cls_cnt - idx3++];
	  *assign->db_val = *pt_value_to_db (parser, ea.rhs);
	  assign->is_const = true;
	}
      else
	{
	  /* not constants */
	  assign->db_val = &dbvals[upd_cls_cnt + idx++];
	  assign->is_const = false;
	}


      for (idx2 = 0; idx2 < upd_cls_cnt; idx2++)
	{
	  if (cls_info[idx2].spec->info.spec.id == ea.lhs->info.name.spec_id)
	    {
	      /* OIDs are in reverse order */
	      cls_info[idx2].oid = &dbvals[upd_cls_cnt - idx2 - 1];
	      /* attach class information to assignment */
	      assign->cls_info = &cls_info[idx2];
	      /* link assignment to its class info */
	      if (cls_info[idx2].first_assign)
		{
		  assign2 = cls_info[idx2].first_assign;
		  while (assign2->next)
		    {
		      assign2 = assign2->next;
		    }
		  assign2->next = assign;
		}
	      else
		{
		  cls_info[idx2].first_assign = assign;
		}
	      assign->next = NULL;
	      break;
	    }
	}
    }

  /* output computed data */
  *assigns_data = assigns;
  *assigns_count = assign_cnt;
  *cls_data = cls_info;
  *cls_count = upd_cls_cnt;
  *values = dbvals;
  *values_cnt = vals_cnt;

  return error;

error_return:
  /* free class information array */
  if (cls_info)
    {
      db_private_free (NULL, cls_info);
    }

  /* free assignments information */
  if (assigns != NULL)
    {
      /* free attribute descriptors */
      for (idx = 0; idx < assign_cnt; idx++)
	{
	  assign = &assigns[idx];
	  if (assign->attr_desc)
	    {
	      db_free_attribute_descriptor (assign->attr_desc);
	    }
	}
      db_private_free (NULL, assigns);
    }

  /* free dbvals array */
  if (dbvals != NULL)
    {
      db_private_free (NULL, dbvals);
    }

  return error;
}

/*
 * update_objs_for_list_file - Updates oid attributes for every oid
 *				in a list file
 *   return: Number of affected objects if success, otherwise an error code
 *   parser(in): Parser context
 *   list_id(in): A list file of oid's and values
 *   statement(in): update statement
 *
 * Note:
 */
static int
update_objs_for_list_file (PARSER_CONTEXT * parser,
			   QFILE_LIST_ID * list_id, PT_NODE * statement)
{
  int error = NO_ERROR;
  int idx = 0, count = 0, assign_count = 0;
  int upd_cls_cnt = 0, vals_cnt = 0;
  CLIENT_UPDATE_INFO *assigns = NULL, *assign = NULL;
  CLIENT_UPDATE_CLASS_INFO *cls_info = NULL;
  int turn_off_unique_check;
  CURSOR_ID cursor_id;
  DB_VALUE *dbvals = NULL;
  const char *savepoint_name = NULL;
  int cursor_status;
  PT_NODE *check_where;
  bool has_unique;

  if (list_id == NULL || statement == NULL)
    {
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__, ER_REGU_SYSTEM, 0);
      error = ER_REGU_SYSTEM;
      goto done;
    }

  check_where = statement->node_type == PT_MERGE
    ? statement->info.merge.check_where : statement->info.update.check_where;
  has_unique = statement->node_type == PT_MERGE
    ? statement->info.merge.has_unique : statement->info.update.has_unique;

  /* load data in update structures */
  error = init_update_data (parser, statement, &assigns, &assign_count,
			    &cls_info, &upd_cls_cnt, &dbvals, &vals_cnt);
  if (error != NO_ERROR)
    {
      goto done;
    }

  /* if the list file contains more than 1 object we need to savepoint
   * the statement to guarantee statement atomicity.
   */
  if (list_id->tuple_cnt > 1 || check_where || has_unique)
    {
      savepoint_name =
	mq_generate_name (parser, "UusP", &update_savepoint_number);
      error = tran_savepoint (savepoint_name, false);
      if (error != NO_ERROR)
	{
	  goto done;
	}
    }

  /* 'turn_off_unique_check' is used when call update_object_tuple(). */
  if (list_id->tuple_cnt == 1 && upd_cls_cnt == 1)
    {
      /* Instance level uniqueness checking is performed on the server
       * when a new single row is inserted.
       */
      turn_off_unique_check = 0;
    }
  else
    {
      /* list_id->tuple_cnt > 1 : multiple row update
       * Statement level uniqueness checking is performed on the client
       */
      turn_off_unique_check = 1;
    }

  /* open cursor */
  if (!cursor_open (&cursor_id, list_id, false, false))
    {
      error = ER_GENERIC_ERROR;
      if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	{
	  (void) tran_abort_upto_savepoint (savepoint_name);
	}
      goto done;
    }
  cursor_id.query_id = parser->query_id;

  /* set prefetching lock mode to WRITE access since we'll be
   * updating all the objects in the list file.
   */
  (void) cursor_set_prefetch_lock_mode (&cursor_id, DB_FETCH_WRITE);

  cursor_status = cursor_next_tuple (&cursor_id);

  while (cursor_status == DB_CURSOR_SUCCESS)
    {
      /* read OIDs and runtime computed values */
      if (cursor_get_tuple_value_list
	  (&cursor_id, vals_cnt, dbvals) != NO_ERROR)
	{
	  error = er_errid ();
	  cursor_close (&cursor_id);
	  if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	  goto done;
	}

      /* perform update for current tuples */
      error = update_object_tuple (parser, assigns, assign_count,
				   cls_info, upd_cls_cnt,
				   turn_off_unique_check, NORMAL_UPDATE);

      /* close cursor and restore to savepoint in case of error */
      if (error < NO_ERROR)
	{
	  error = er_errid ();
	  cursor_close (&cursor_id);
	  if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	  goto done;
	}

      count += error;		/* number of objects affected. Incorrect for multi-table update !!! */
      cursor_status = cursor_next_tuple (&cursor_id);
    }

  /* close cursor and restore to savepoint in case of error */
  if (cursor_status != DB_CURSOR_END)
    {
      error = ER_GENERIC_ERROR;
      cursor_close (&cursor_id);
      if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	{
	  (void) tran_abort_upto_savepoint (savepoint_name);
	}
      goto done;
    }
  cursor_close (&cursor_id);

  /* check uniques */
  if (has_unique)
    {

      for (idx = upd_cls_cnt - 1; idx >= 0; idx--)
	{
	  if (!(cls_info[idx].spec->info.spec.flag & PT_SPEC_FLAG_HAS_UNIQUE))
	    {
	      continue;
	    }
	  error =
	    sm_flush_for_multi_update (cls_info[idx].spec->info.spec.
				       flat_entity_list->info.name.db_object);
	  /* if error and a savepoint was created, rollback to savepoint.
	   * No need to rollback if the TM aborted the transaction itself.
	   */
	  if ((error < NO_ERROR) && savepoint_name
	      && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	      break;
	    }
	}
    }

done:
  /* free classes information array */
  if (cls_info)
    {
      db_private_free (NULL, cls_info);
    }

  /* free assignments information */
  if (assigns != NULL)
    {
      /* free attributes descriptors */
      for (idx = 0; idx < assign_count; idx++)
	{
	  assign = &assigns[idx];
	  if (assign->attr_desc)
	    {
	      db_free_attribute_descriptor (assign->attr_desc);
	    }
	}
      db_private_free (NULL, assigns);
    }

  /* free values array */
  if (dbvals != NULL)
    {
      db_private_free (NULL, dbvals);
    }

  if (error >= NO_ERROR)
    {
      return count;
    }
  else
    {
      return error;
    }
}

/*
 * update_class_attributes - Returns corresponding lists of names and expressions
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): update statement
 *
 * Note:
 */
static int
update_class_attributes (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_OTMPL *otemplate = NULL;
  PT_NODE *spec = NULL, *rhs = NULL, *assignments = NULL;
  PT_ASSIGNMENTS_HELPER ea;
  int idx = 0, count = 0, assigns_count = 0;
  int upd_cls_cnt = 0, vals_cnt = 0, multi_assign_cnt = 0;
  CLIENT_UPDATE_INFO *assigns = NULL, *assign = NULL;
  CLIENT_UPDATE_CLASS_INFO *cls_info = NULL, *cls = NULL;
  DB_VALUE *dbvals = NULL;

  assignments = statement->node_type == PT_MERGE
    ? statement->info.merge.update.assignment
    : statement->info.update.assignment;

  /* load data for update */
  error =
    init_update_data (parser, statement, &assigns, &assigns_count, &cls_info,
		      &upd_cls_cnt, &dbvals, &vals_cnt);
  if (error == NO_ERROR)
    {
      /* evaluate values for assignment */
      pt_init_assignments_helper (parser, &ea, assignments);
      for (idx = 0; idx < assigns_count && error == NO_ERROR;
	   idx += multi_assign_cnt)
	{
	  assign = &assigns[idx];
	  cls = assign->cls_info;

	  pt_get_next_assignment (&ea);
	  rhs = ea.rhs;
	  multi_assign_cnt = 1;
	  if (ea.is_n_column)
	    {
	      while (pt_get_next_assignment (&ea) && rhs == ea.rhs)
		{
		  multi_assign_cnt++;
		}
	    }

	  pt_evaluate_tree (parser, rhs, assign->db_val, multi_assign_cnt);
	  if (pt_has_error (parser))
	    {
	      error = ER_GENERIC_ERROR;
	    }
	}
    }

  /* execute assignments */
  if (error == NO_ERROR)
    {
      for (idx = 0; idx < upd_cls_cnt && error == NO_ERROR; idx++)
	{
	  cls = &cls_info[idx];

	  otemplate =
	    dbt_edit_object (cls->spec->info.spec.flat_entity_list->info.
			     name.db_object);
	  for (assign = cls->first_assign;
	       assign != NULL && error == NO_ERROR; assign = assign->next)
	    {
	      error =
		dbt_put_internal (otemplate,
				  assign->upd_col_name->info.name.original,
				  assign->db_val);
	    }
	  if (error == NO_ERROR && dbt_finish_object (otemplate) == NULL)
	    {
	      error = er_errid ();
	      (void) dbt_abort_object (otemplate);
	    }
	}
    }

  /* free assignments array */
  if (assigns != NULL)
    {
      /* free attributes descriptors */
      for (idx = assigns_count - 1; idx >= 0; idx--)
	{
	  if (assigns[idx].attr_desc)
	    {
	      db_free_attribute_descriptor (assigns[idx].attr_desc);
	    }
	}
      db_private_free (NULL, assigns);
    }

  /* free classes info array */
  if (cls_info != NULL)
    {
      db_private_free (NULL, cls_info);
    }

  /* free values array */
  if (dbvals != NULL)
    {
      db_private_free (NULL, dbvals);
    }

  return error;
}

/*
 * statement_to_update_xasl - Converts an update parse tree to
 * 			      an XASL graph for an update
 *   parser(in): Parser context
 *   statement(in): Parse tree of a update statement
 *   non_null_attrs(in):
 *
 * Note:
 */
static XASL_NODE *
statement_to_update_xasl (PARSER_CONTEXT * parser, PT_NODE * statement,
			  PT_NODE ** non_null_attrs)
{
  return pt_to_update_xasl (parser, statement, non_null_attrs);
}

/*
 * update_at_server - Build an xasl tree for a server update and execute it
 *   return: Tuple count if success, otherwise an error code
 *   parser(in): Parser context
 *   from(in): Class spec to update
 *   statement(in): Parse tree of a update statement
 *   non_null_attrs(in):
 *   has_uniques(in):
 *
 * Note:
 *  The xasl tree has an UPDATE_PROC node as the top node and
 *  a BUILDLIST_PROC as its aptr.  The BUILDLIST_PROC selects the
 *  instance OID and any update attribute expression values.
 *  The UPDATE_PROC node scans the BUILDLIST_PROC results.
 *  The UPDATE_PROC node contains the attribute ID's and values
 *  for update constants.  The server executes the aptr and then
 *  for each instance selected, updates it with the attribute expression
 *  values and constants.  The result information is sent back to the
 *  client as a list file without any pages.  The list file tuple count
 *  is used as the return value from this routine.
 *
 *  The instances for the class are flushed from the client before the
 *  update is executed.  If any instances are updated, the instances are
 *  decached from the client after the update is executed.
 *
 *  It is assumed that class attributes and regular attributes
 *  are not mixed in the same update statement.
 */
static int
update_at_server (PARSER_CONTEXT * parser, PT_NODE * from,
		  PT_NODE * statement, PT_NODE ** non_null_attrs,
		  int has_uniques)
{
  int error = NO_ERROR;
  int i;
  XASL_NODE *xasl = NULL;
  int size, count = 0;
  char *stream = NULL;
  QUERY_ID query_id = NULL_QUERY_ID;
  QFILE_LIST_ID *list_id = NULL;
  PT_NODE *cl_name_node = NULL, *spec = NULL;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();

  xasl = statement_to_update_xasl (parser, statement, non_null_attrs);
  if (xasl)
    {
      UPDATE_PROC_NODE *update = &xasl->proc.update;

      error = xts_map_xasl_to_stream (xasl, &stream, &size);
      if (error != NO_ERROR)
	{
	  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	}

      for (i = 0; i < update->no_assigns; i++)
	{
	  if (update->assigns[i].constant)
	    {
	      pr_clear_value (update->assigns[i].constant);
	    }
	}
    }
  else
    {
      error = er_errid ();
    }

  if (error == NO_ERROR)
    {
      int au_save;

      AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
      error = query_prepare_and_execute (stream, size, &query_id,
					 parser->host_var_count +
					 parser->auto_param_count,
					 parser->host_variables, &list_id,
					 parser->exec_mode |
					 ASYNC_UNEXECUTABLE);
      AU_RESTORE (au_save);
    }
  parser->query_id = query_id;

  /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
  if (stream)
    {
      free_and_init (stream);
    }

  if (list_id)
    {
      count = list_id->tuple_cnt;
      if (count > 0)
	{
	  spec = statement->info.update.spec;
	  while (spec)
	    {
	      for (cl_name_node = spec->info.spec.flat_entity_list;
		   cl_name_node && error == NO_ERROR;
		   cl_name_node = cl_name_node->next)
		{
		  error =
		    sm_flush_and_decache_objects (cl_name_node->info.
						  name.db_object, true);
		}
	      spec = spec->next;
	    }
	}
      regu_free_listid (list_id);
    }
  pt_end_query (parser);

  /* mark the end of another level of xasl packing */
  pt_exit_packing_buf ();

  if (error >= NO_ERROR)
    {
      return count;
    }
  else
    {
      return error;
    }
}

/*
 * update_check_for_constraints - Determine whether attributes of the target
 *				  classes have UNIQUE and/or NOT NULL
 *				  constraints, and return a list of NOT NULL
 *				  attributes if exist
 *   return: Error code
 *   parser(in): Parser context
 *   has_unique(out): Indicator representing there is UNIQUE constraint, 1 or 0
 *   not_nulls(out): A list of pointers to NOT NULL attributes, or NULL
 *   statement(in):  Parse tree of an UPDATE or MERGE statement
 *
 * Note:
 */
static int
update_check_for_constraints (PARSER_CONTEXT * parser, int *has_unique,
			      PT_NODE ** not_nulls, const PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *lhs = NULL, *att = NULL, *pointer = NULL, *spec = NULL;
  PT_NODE *assignment;
  DB_OBJECT *class_obj = NULL;

  assignment = statement->node_type == PT_MERGE
    ? statement->info.merge.update.assignment
    : statement->info.update.assignment;

  *has_unique = 0;
  *not_nulls = NULL;

  for (; assignment; assignment = assignment->next)
    {
      lhs = assignment->info.expr.arg1;
      if (lhs->node_type == PT_NAME)
	{
	  att = lhs;
	}
      else if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  att = lhs->info.expr.arg1;
	}
      else
	{
	  /* bullet proofing, should not get here */
#if defined(CUBRID_DEBUG)
	  fprintf (stdout, "system error detected in %s, line %d.\n",
		   __FILE__, __LINE__);
#endif
	  error = ER_GENERIC_ERROR;
	  goto exit_on_error;
	}

      for (; att; att = att->next)
	{
	  if (att->node_type != PT_NAME)
	    {
	      /* bullet proofing, should not get here */
#if defined(CUBRID_DEBUG)
	      fprintf (stdout, "system error detected in %s, line %d.\n",
		       __FILE__, __LINE__);
#endif
	      error = ER_GENERIC_ERROR;
	      goto exit_on_error;
	    }

	  spec = pt_find_spec_in_statement (parser, statement, att);
	  if (spec == NULL
	      || (class_obj =
		  spec->info.spec.flat_entity_list->info.name.db_object) ==
	      NULL)
	    {
	      error = ER_GENERIC_ERROR;
	      goto exit_on_error;
	    }

	  if (*has_unique == 0 && sm_att_unique_constrained (class_obj,
							     att->info.
							     name.original))
	    {
	      *has_unique = 1;
	      spec->info.spec.flag |= PT_SPEC_FLAG_HAS_UNIQUE;
	    }
	  if (*has_unique == 0 &&
	      sm_att_in_unique_filter_constraint_predicate (class_obj,
							    att->info.name.
							    original))
	    {
	      *has_unique = 1;
	      spec->info.spec.flag |= PT_SPEC_FLAG_HAS_UNIQUE;
	    }
	  if (sm_att_constrained (class_obj, att->info.name.original,
				  SM_ATTFLAG_NON_NULL))
	    {
	      pointer = pt_point (parser, att);
	      if (pointer == NULL)
		{
		  PT_ERRORm (parser, att, MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		  error = MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
		  goto exit_on_error;
		}
	      *not_nulls = parser_append_node (pointer, *not_nulls);
	    }
	}			/* for ( ; attr; ...) */
    }				/* for ( ; assignment; ...) */

  return NO_ERROR;

exit_on_error:
  if (*not_nulls)
    {
      parser_free_tree (parser, *not_nulls);
      *not_nulls = NULL;
    }
  return error;
}

/*
 * update_check_for_fk_cache_attr() -
 *   return: Error code if update fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of an UPDATE or MERGE statement
 *
 */
static int
update_check_for_fk_cache_attr (PARSER_CONTEXT * parser,
				const PT_NODE * statement)
{
  PT_NODE *lhs = NULL, *att = NULL, *spec = NULL;
  PT_NODE *assignment;
  DB_OBJECT *class_obj = NULL;

  assignment = statement->node_type == PT_MERGE
    ? statement->info.merge.update.assignment
    : statement->info.update.assignment;

  for (; assignment; assignment = assignment->next)
    {
      lhs = assignment->info.expr.arg1;
      if (lhs->node_type == PT_NAME)
	{
	  att = lhs;
	}
      else if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  att = lhs->info.expr.arg1;
	}
      else
	{
	  return ER_GENERIC_ERROR;
	}

      for (; att; att = att->next)
	{
	  if (att->node_type != PT_NAME)
	    {
	      return ER_GENERIC_ERROR;
	    }

	  spec = pt_find_spec_in_statement (parser, statement, att);
	  if (spec == NULL
	      || (class_obj =
		  spec->info.spec.flat_entity_list->info.name.db_object) ==
	      NULL)
	    {
	      return ER_GENERIC_ERROR;
	    }

	  if (sm_is_att_fk_cache (class_obj, att->info.name.original))
	    {
	      PT_ERRORmf (parser, att, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_CANT_ASSIGN_FK_CACHE_ATTR,
			  att->info.name.original);
	      return ER_PT_SEMANTIC;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * update_check_for_meta_attr () -
 *   return: true if update assignment clause has class or shared attribute
 *   parser(in): Parser context
 *   assignment(in): Parse tree of an assignment clause
 *
 * Note:
 */
static bool
update_check_having_meta_attr (PARSER_CONTEXT * parser, PT_NODE * assignment)
{
  PT_NODE *lhs, *att;

  for (; assignment; assignment = assignment->next)
    {
      lhs = assignment->info.expr.arg1;
      if (lhs->node_type == PT_NAME)
	{
	  att = lhs;
	}
      else if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  att = lhs->info.expr.arg1;
	}
      else
	{
	  att = NULL;
	}

      for (; att; att = att->next)
	{
	  if (att->node_type == PT_NAME
	      && att->info.name.meta_class != PT_NORMAL)
	    {
	      return true;
	    }
	}
    }

  return false;
}

/*
 * update_real_class() -
 *   return: Error code if update fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a update statement
 *
 * Note: If the statement is of type "update class foo ...", this
 *   routine updates class attributes of foo.  If the statement is of
 *   type "update foo ...", this routine updates objects or rows in foo.
 *   It is assumed that class attributes and regular attributes
 *   are not mixed in the same update statement.
 */
static int
update_real_class (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *non_null_attrs = NULL, *spec = statement->info.update.spec;
  DB_OBJECT *class_obj = NULL;
  int server_allowed = 0;
  int has_uniques = 0;
  PT_NODE **links = NULL;

  /* update a "real" class in this database */

  while (spec)
    {
      if (spec->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	{
	  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;

	  /* The IX lock on the class is sufficient.
	   * DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE
	   */
	  if (!locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE))
	    {
	      goto exit_on_error;
	    }
	}
      spec = spec->next;
    }

  if (is_server_update_allowed (parser, &non_null_attrs, &has_uniques,
				&server_allowed, statement) != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (server_allowed)
    {
      /* do update on server */
      error = update_at_server (parser, spec, statement, &non_null_attrs,
				has_uniques);
    }
  else
    {
      PT_NODE *lhs = NULL;
      PT_NODE *select_names = NULL;
      PT_NODE *select_values = NULL;
      PT_NODE *const_names = NULL;
      PT_NODE *const_values = NULL;
      QFILE_LIST_ID *oid_list = NULL;
      int no_vals = 0;
      int no_consts = 0;
      int wait_msecs = -2;
      int old_wait_msecs = -2;
      float hint_waitsecs;

      /* do update on client */
      lhs = statement->info.update.assignment->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  lhs = lhs->info.expr.arg1;
	}
      if (lhs->info.name.meta_class != PT_META_ATTR)
	{
	  PT_NODE *hint_arg = statement->info.update.waitsecs_hint;

	  if (statement->info.update.hint & PT_HINT_LK_TIMEOUT
	      && PT_IS_HINT_NODE (hint_arg))
	    {
	      hint_waitsecs = (float) atof (hint_arg->info.name.original);
	      if (hint_waitsecs > 0)
		{
		  wait_msecs = (int) (hint_waitsecs * 1000);
		}
	      else
		{
		  wait_msecs = (int) hint_waitsecs;
		}
	      if (wait_msecs >= -1)
		{
		  old_wait_msecs = TM_TRAN_WAIT_MSECS ();
		  (void) tran_reset_wait_times (wait_msecs);
		}
	    }
	  if (error == NO_ERROR)
	    {
	      error =
		pt_get_assignment_lists (parser, &select_names,
					 &select_values, &const_names,
					 &const_values, &no_vals, &no_consts,
					 statement->info.update.assignment,
					 &links);
	      if (error != NO_ERROR)
		{
		  goto exit_on_error;
		}
	      /* get the oid's and new values */
	      oid_list =
		get_select_list_to_update (parser,
					   statement->info.update.spec,
					   select_names, select_values,
					   statement->info.update.search_cond,
					   statement->info.update.order_by,
					   statement->info.update.orderby_for,
					   statement->info.update.using_index,
					   statement->info.update.
					   class_specs);

	      /* restore tree structure */
	      pt_restore_assignment_links (statement->info.update.assignment,
					   links, -1);
	    }
	  if (old_wait_msecs >= -1)
	    {
	      (void) tran_reset_wait_times (old_wait_msecs);
	    }

	  if (!oid_list)
	    {
	      /* an error should be set already, don't lose it */
	      error = ER_GENERIC_ERROR;
	      goto exit_on_error;
	    }

	  /* update each oid */
	  error = update_objs_for_list_file (parser, oid_list, statement);

	  regu_free_listid (oid_list);
	  pt_end_query (parser);
	}
      else
	{
	  /* we are updating class attributes */
	  error = update_class_attributes (parser, statement);
	}
    }

  if (non_null_attrs != NULL)
    {
      parser_free_tree (parser, non_null_attrs);
      non_null_attrs = NULL;
    }

  return error;

exit_on_error:

  if (error == NO_ERROR)
    {
      error = er_errid ();
    }
  if (non_null_attrs != NULL)
    {
      parser_free_tree (parser, non_null_attrs);
      non_null_attrs = NULL;
    }
  return error;
}

/*
 * is_server_update_allowed() - Checks to see if a server-side update is
 *                              allowed
 *   return: NO_ERROR or error code on failure
 *   parser(in): Parser context
 *   non_null_attrs(in/out): Parse tree for attributes with the NOT NULL
 *                           constraint
 *   has_uniques(in/out): whether unique indexes are affected by the update
 *   server_allowed(in/out): whether the update can be executed on the server
 *   statement(in): Parse tree of an update statement
 */
static int
is_server_update_allowed (PARSER_CONTEXT * parser, PT_NODE ** non_null_attrs,
			  int *has_uniques, int *const server_allowed,
			  const PT_NODE * statement)
{
  int error = NO_ERROR;
  int is_partition = 0;
  int trigger_involved = 0, ti = 0, is_virt = 0;
  PT_NODE *spec = statement->info.update.spec;
  DB_OBJECT *class_obj = NULL;

  assert (non_null_attrs != NULL && has_uniques != NULL
	  && server_allowed != NULL);
  assert (*non_null_attrs == NULL);
  *has_uniques = 0;
  *server_allowed = 0;

  /* check if at least one spec that will be updated is virtual or
   * has triggers */
  while (spec && !trigger_involved && !is_virt)
    {
      if (!(spec->info.spec.flag & PT_SPEC_FLAG_UPDATE))
	{
	  spec = spec->next;
	  continue;
	}
      class_obj = spec->info.spec.flat_entity_list->info.name.db_object;
      error =
	do_is_partitioned_classobj (&is_partition, class_obj, NULL, NULL);
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      /* If the class is partitioned and has any type of trigger, the update must
         be executed on the client. */
      error = sm_class_has_triggers (class_obj, &ti,
				     (is_partition ? TR_EVENT_ALL :
				      TR_EVENT_UPDATE));
      if (error != NO_ERROR)
	{
	  goto error_exit;
	}

      if (ti)
	{
	  trigger_involved = ti;
	}

      is_virt = (spec->info.spec.flat_entity_list->info.name.virt_object
		 != NULL);

      spec = spec->next;
    }

  error = update_check_for_constraints (parser, has_uniques, non_null_attrs,
					statement);
  if (error < NO_ERROR)
    {
      goto error_exit;
    }

  error = update_check_for_fk_cache_attr (parser, statement);
  if (error != NO_ERROR)
    {
      goto error_exit;
    }

  /* Check to see if the update can be done on the server */
  *server_allowed = ((!trigger_involved && !is_virt)
		     && !update_check_having_meta_attr (parser,
							statement->info.
							update.assignment));

  return error;

error_exit:
  if (non_null_attrs != NULL && *non_null_attrs != NULL)
    {
      parser_free_tree (parser, *non_null_attrs);
      *non_null_attrs = NULL;
    }
  return error;
}

/*
 * do_update() - Updates objects or rows
 *   return: Error code if update fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a update statement
 *
 * Note:
 */
int
do_update (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  int result = NO_ERROR;
  int rollbacktosp = 0;
  const char *savepoint_name = NULL;

  CHECK_MODIFICATION_ERROR ();

  /* DON'T REMOVE this, correct authorization validation of views
   * depends on this.
   *
   * DON'T return from the body of this function. Break out of the loop
   * if necessary.
   */
  AU_DISABLE (parser->au_save);

  /* if the update statement contains more than one update component,
   * we savepoint the update components to try to guarantee update
   * statement atomicity.
   */

  while (statement && (error >= 0))
    {
      if (statement->node_type != PT_UPDATE
	  || statement->info.update.assignment == NULL)
	{
	  /* bullet proofing, should not get here */
	  PT_INTERNAL_ERROR (parser, "update");
	  error = ER_GENERIC_ERROR;
	  break;
	}

      if (pt_false_where (parser, statement))
	{
	  /* nothing to update, where part is false */
	}
      else if (statement->info.update.object != NULL)
	{
	  /* this is a update object if it has an object */
	  error = update_object_by_oid (parser, statement, UPDATE_OBJECT);
	}
      else
	{
	  /* the following is the "normal" sql type execution */
	  error = update_real_class (parser, statement);
	}

      if (error < NO_ERROR && er_errid () != NO_ERROR)
	{
	  pt_record_error (parser, parser->statement_number,
			   statement->line_number, statement->column_number,
			   er_msg (), NULL);
	}

      result += error;
      statement = statement->next;
    }

  /* if error and a savepoint was created, rollback to savepoint.
   * No need to rollback if the TM aborted the transaction.
   */
  if ((error < NO_ERROR) && rollbacktosp
      && (error != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  if (error < 0)
    {
      result = error;
    }

  /* DON'T REMOVE this, correct authorization validation of views
   * depends on this.
   */
  AU_ENABLE (parser->au_save);

  return result;
}

/*
 * do_prepare_update() - Prepare the UPDATE statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a update statement
 *
 * Note:
 */
int
do_prepare_update (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err;
  PT_NODE *flat, *not_nulls, *lhs, *spec = NULL;
  DB_OBJECT *class_obj;
  int has_trigger, has_unique, au_save, has_virt = 0;
  bool server_update;
  XASL_ID *xasl_id;
  const char *qstr = NULL;

  if (parser == NULL || statement == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  for (err = NO_ERROR; statement && (err >= NO_ERROR); statement
       = statement->next)
    {
      /* there can be no results, this is a compile time false where clause */
      if (pt_false_where (parser, statement))
	{
	  /* tell to the execute routine that there's no XASL to execute */
	  statement->xasl_id = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next UPDATE statement */
	}

      /*
       * Update object case:
       *   this is a update object if it has an object
       */
      if (statement->info.update.object)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next UPDATE statement */
	}

      /* if already prepared */
      if (statement->xasl_id)
	{
	  continue;		/* continue to next UPDATE statement */
	}

      AU_SAVE_AND_DISABLE (au_save);	/* because sm_class_has_trigger() calls
					   au_fetch_class() */

      /* check if at least one spec to be updated has triggers. If none has
       * triggers then see if at least one is virtual */
      spec = statement->info.update.spec;
      has_trigger = 0;
      while (spec && !has_trigger && err == NO_ERROR)
	{
	  if (spec->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	    {
	      flat = spec->info.spec.flat_entity_list;
	      class_obj = (flat) ? flat->info.name.db_object : NULL;
	      /* the presence of a proxy trigger should force the update
	         to be performed through the workspace  */
	      err =
		sm_class_has_triggers (class_obj, &has_trigger,
				       TR_EVENT_UPDATE);

	      if (!has_virt)
		{
		  has_virt = (flat->info.name.virt_object != NULL);
		}
	    }

	  spec = spec->next;
	}
      AU_RESTORE (au_save);

      /* err = has_proxy_trigger(flat, &has_trigger); */
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}
      /* sm_class_has_triggers() checked if the class has active triggers */
      statement->info.update.has_trigger = (bool) has_trigger;

      err = update_check_for_fk_cache_attr (parser, statement);
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}

      /* check if the target class has UNIQUE constraint and
         get attributes that has NOT NULL constraint */
      err = update_check_for_constraints (parser, &has_unique, &not_nulls,
					  statement);
      if (err < NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}

      statement->info.update.has_unique = (bool) has_unique;

      /* determine whether it can be server-side or OID list update */
      server_update = (!has_trigger && !has_virt
		       && !update_check_having_meta_attr (parser,
							  statement->info.
							  update.assignment));

      lhs = statement->info.update.assignment->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  lhs = lhs->info.expr.arg1;
	}
      statement->info.update.server_update = server_update;

      /* if we are updating class attributes, not need to prepare */
      if (lhs->info.name.meta_class == PT_META_ATTR)
	{
	  statement->info.update.do_class_attrs = true;
	  continue;		/* continue to next UPDATE statement */
	}

      if (server_update)
	{
	  /* make query string */
	  parser->dont_prt_long_string = 1;
	  parser->long_string_skipped = 0;
	  parser->print_type_ambiguity = 0;
	  PT_NODE_PRINT_TO_ALIAS (parser, statement,
				  (PT_CONVERT_RANGE | PT_PRINT_QUOTES));
	  qstr = statement->alias_print;
	  parser->dont_prt_long_string = 0;
	  if (parser->long_string_skipped || parser->print_type_ambiguity)
	    {
	      statement->cannot_prepare = 1;
	      return NO_ERROR;
	    }
	}

      xasl_id = NULL;
      if (server_update)
	{
	  /*
	   * Server-side update case: (by requesting server to execute XASL)
	   *  build UPDATE_PROC XASL
	   */
	  XASL_NODE *xasl;
	  char *stream;
	  int size;

	  /* look up server's XASL cache for this query string
	     and get XASL file id (XASL_ID) returned if found */
	  if (statement->recompile == 0)
	    {
	      err = query_prepare (qstr, NULL, 0, &xasl_id);
	      if (err != NO_ERROR)
		{
		  err = er_errid ();
		}
	    }
	  else
	    {
	      err = qmgr_drop_query_plan (qstr,
					  ws_identifier (db_get_user ()),
					  NULL, true);
	    }
	  if (!xasl_id && err == NO_ERROR)
	    {
	      /* cache not found;
	         make XASL from the parse tree including query optimization
	         and plan generation */

	      /* mark the beginning of another level of xasl packing */
	      pt_enter_packing_buf ();

	      /* this prevents authorization checking during generating XASL */
	      AU_SAVE_AND_DISABLE (au_save);

	      /* pt_to_update_xasl() will build XASL tree from parse tree */
	      xasl = pt_to_update_xasl (parser, statement, &not_nulls);
	      AU_RESTORE (au_save);
	      stream = NULL;
	      if (xasl && (err >= NO_ERROR))
		{
		  int i;
		  UPDATE_PROC_NODE *update = &xasl->proc.update;

		  /* convert the created XASL tree to the byte stream for
		     transmission to the server */
		  err = xts_map_xasl_to_stream (xasl, &stream, &size);
		  if (err != NO_ERROR)
		    {
		      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
				 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		    }
		  for (i = update->no_assigns - 1; i >= 0; i--)
		    {
		      if (update->assigns[i].constant)
			{
			  pr_clear_value (update->assigns[i].constant);
			}
		    }
		}
	      else
		{
		  err = er_errid ();
		  pt_record_error (parser, parser->statement_number,
				   statement->line_number,
				   statement->column_number, er_msg (), NULL);
		}

	      /* mark the end of another level of xasl packing */
	      pt_exit_packing_buf ();

	      /* request the server to prepare the query;
	         give XASL stream generated from the parse tree
	         and get XASL file id returned */
	      if (stream && (err >= NO_ERROR))
		{
		  err = query_prepare (qstr, stream, size, &xasl_id);
		  if (err != NO_ERROR)
		    {
		      err = er_errid ();
		    }
		}
	      /* As a result of query preparation of the server,
	         the XASL cache for this query will be created or updated. */

	      /* free 'stream' that is allocated inside of
	         xts_map_xasl_to_stream() */
	      if (stream)
		{
		  free_and_init (stream);
		}
	      statement->use_plan_cache = 0;
	    }
	  else
	    {			/* if (!xasl_id) */
	      spec = statement->info.update.spec;
	      while (spec && err == NO_ERROR)
		{
		  flat = spec->info.spec.flat_entity_list;
		  while (flat)
		    {
		      if (locator_flush_class (flat->info.name.db_object)
			  != NO_ERROR)
			{
			  xasl_id = NULL;
			  err = er_errid ();
			  break;
			}
		      flat = flat->next;
		    }
		  spec = spec->next;
		}
	      if (err == NO_ERROR)
		{
		  statement->use_plan_cache = 1;
		}
	      else
		{
		  statement->use_plan_cache = 0;
		}
	    }
	}
      else
	{			/* if (server_update) */
	  /*
	   * OID list update case: (by selecting OIDs to update)
	   *  make SELECT statement for this UPDATE statement
	   */
	  PT_NODE *select_statement = NULL;
	  PT_NODE *select_names = NULL, *select_values = NULL;
	  PT_NODE *const_names = NULL, *const_values = NULL;
	  PT_NODE **links = NULL;
	  int no_vals = 0, no_consts = 0;

	  err =
	    pt_get_assignment_lists (parser, &select_names, &select_values,
				     &const_names, &const_values, &no_vals,
				     &no_consts,
				     statement->info.update.assignment,
				     &links);

	  if (err != NO_ERROR)
	    {
	      PT_INTERNAL_ERROR (parser, "update");
	      break;		/* stop while loop if error */
	    }

	  /* make sure that lhs->info.name.meta_class != PT_META_ATTR */
	  select_statement =
	    pt_to_upd_del_query (parser, select_names, select_values,
				 statement->info.update.spec,
				 statement->info.update.class_specs,
				 statement->info.update.search_cond,
				 statement->info.update.using_index,
				 statement->info.update.order_by,
				 statement->info.update.orderby_for, 0,
				 PT_COMPOSITE_LOCKING_UPDATE);

	  /* restore tree structure; pt_get_assignment_lists() */
	  pt_restore_assignment_links (statement->info.update.assignment,
				       links, -1);

	  /* translate views or virtual classes into base classes;
	     If we are updating a proxy, the SELECT is not yet fully
	     translated. If we are updating anything else, this is a no-op. */

	  /* this prevents authorization checking during view transformation */
	  AU_SAVE_AND_DISABLE (au_save);

	  select_statement = mq_translate (parser, select_statement);
	  AU_RESTORE (au_save);
	  if (select_statement)
	    {
	      /* get XASL_ID by calling do_prepare_select() */
	      err = do_prepare_select (parser, select_statement);
	      xasl_id = select_statement->xasl_id;
	      parser_free_tree (parser, select_statement);
	    }
	  else
	    {
	      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	      err = er_errid ();
	    }

	}			/* else (server_update) */

      /* save the XASL_ID that is allocated and returned by
         query_prepare() into 'statement->xasl_id'
         to be used by do_execute_update() */
      statement->xasl_id = xasl_id;

      if (not_nulls)
	{
	  parser_free_tree (parser, not_nulls);
	}
    }

  return err;
}

/*
 * do_execute_update() - Execute the prepared UPDATE statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a update statement
 *
 * Note:
 */
int
do_execute_update (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err, result = 0;
  PT_NODE *flat, *spec = NULL;
  const char *savepoint_name;
  DB_OBJECT *class_obj;
  QFILE_LIST_ID *list_id;
  int au_save;
  int wait_msecs = -2, old_wait_msecs = -2;
  float hint_waitsecs;
  PT_NODE *hint_arg;

  CHECK_MODIFICATION_ERROR ();

  /* If the UPDATE statement contains more than one update component,
     we savepoint the update components to try to guarantee UPDATE statement
     atomicity. */
  savepoint_name = NULL;

  for (err = NO_ERROR, result = 0; statement && (err >= NO_ERROR);
       statement = statement->next)
    {
      /*
       * Update object case:
       *   update object by OID
       */
      if (statement->info.update.object)
	{
	  err = update_object_by_oid (parser, statement, UPDATE_OBJECT);
	  continue;		/* continue to next UPDATE statement */
	}

      /* check if it is not necessary to execute this statement,
         e.g. false where or not prepared correctly */
      if (!statement->info.update.do_class_attrs && !statement->xasl_id)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next UPDATE statement */
	}

      spec = statement->info.update.spec;
      while (spec)
	{
	  if (spec->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	    {
	      flat = spec->info.spec.flat_entity_list;
	      class_obj = (flat) ? flat->info.name.db_object : NULL;
	      /* The IX lock on the class is sufficient.
	         DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
	      if (locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE)
		  == NULL)
		{
		  err = er_errid ();
		  break;	/* stop while loop if error */
		}
	    }

	  spec = spec->next;
	}
      if (err != NO_ERROR)
	{
	  break;		/* stop while loop if error */
	}

      /*
       * Server-side update or OID list update case:
       *  execute the prepared(stored) XASL (UPDATE_PROC or SELECT statement)
       */
      if (statement->info.update.server_update
	  || !statement->info.update.do_class_attrs)
	{
	  /* Request that the server executes the stored XASL, which is
	     the execution plan of the prepared query, with the host variables
	     given by users as parameter values for the query.
	     As a result, query id and result file id (QFILE_LIST_ID) will be
	     returned.
	     do_prepare_update() has saved the XASL file id (XASL_ID) in
	     'statement->xasl_id' */

	  int query_flag = parser->exec_mode | ASYNC_UNEXECUTABLE;

	  /* flush necessary objects before execute */
	  spec = statement->info.update.spec;
	  while (spec)
	    {
	      if (spec->info.spec.flag & PT_SPEC_FLAG_UPDATE)
		{
		  err =
		    sm_flush_objects (spec->info.spec.flat_entity_list->info.
				      name.db_object);
		  if (err != NO_ERROR)
		    {
		      break;	/* stop while loop if error */
		    }
		}
	      spec = spec->next;
	    }
	  if (err != NO_ERROR)
	    {
	      break;
	    }

	  if (statement->do_not_keep == 0)
	    {
	      query_flag |= KEEP_PLAN_CACHE;
	    }
	  query_flag |= NOT_FROM_RESULT_CACHE;
	  query_flag |= RESULT_CACHE_INHIBITED;

	  AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
	  list_id = NULL;
	  parser->query_id = -1;

	  /* check host variables are filled :
	     values from orderby_for may not have been auto-parameterized
	     as host variables; if that is the case, do it now */
	  if (statement->info.update.orderby_for != NULL &&
	      parser->auto_param_count == 0)
	    {
	      assert ((parser->host_variables == NULL &&
		       parser->host_var_count == 0) ||
		      (parser->host_variables != NULL &&
		       parser->host_var_count > 0));

	      qo_do_auto_parameterize (parser,
				       statement->info.update.orderby_for);
	    }
	  err = query_execute (statement->xasl_id, &parser->query_id,
			       parser->host_var_count +
			       parser->auto_param_count,
			       parser->host_variables, &list_id, query_flag,
			       NULL, NULL);
	  AU_RESTORE (au_save);
	  if (err != NO_ERROR)
	    {
	      break;		/* stop while loop if error */
	    }
	}

      if (!statement->info.update.server_update)
	{
	  hint_arg = statement->info.update.waitsecs_hint;
	  if (statement->info.update.hint & PT_HINT_LK_TIMEOUT
	      && PT_IS_HINT_NODE (hint_arg))
	    {
	      hint_waitsecs = (float) atof (hint_arg->info.name.original);
	      if (hint_waitsecs > 0)
		{
		  wait_msecs = (int) (hint_waitsecs * 1000);
		}
	      else
		{
		  wait_msecs = (int) hint_waitsecs;
		}
	      if (wait_msecs >= -1)
		{
		  old_wait_msecs = TM_TRAN_WAIT_MSECS ();
		  (void) tran_reset_wait_times (wait_msecs);
		}
	    }
	  AU_SAVE_AND_DISABLE (au_save);	/* this prevents authorization
						   checking during execution */
	  if (statement->info.update.do_class_attrs)
	    {
	      /* in case of update class attributes, */
	      err = update_class_attributes (parser, statement);
	    }
	  else
	    {
	      /* in the case of OID list update, now update the selected OIDs */
	      err = update_objs_for_list_file (parser, list_id, statement);
	    }

	  AU_RESTORE (au_save);
	  if (old_wait_msecs >= -1)
	    {
	      (void) tran_reset_wait_times (old_wait_msecs);
	    }
	}

      if (statement->info.update.server_update
	  || !statement->info.update.do_class_attrs)
	{
	  /* free returned QFILE_LIST_ID */
	  if (list_id)
	    {
	      if (list_id->tuple_cnt > 0
		  && statement->info.update.server_update)
		{
		  spec = statement->info.update.spec;
		  while (spec && err == NO_ERROR)
		    {
		      if (spec->info.spec.flag & PT_SPEC_FLAG_UPDATE)
			{
			  flat = spec->info.spec.flat_entity_list;
			  class_obj =
			    (flat) ? flat->info.name.db_object : NULL;
			  err =
			    sm_flush_and_decache_objects (class_obj, true);
			}

		      spec = spec->next;
		    }
		}
	      if (err >= NO_ERROR && statement->info.update.server_update)
		{
		  err = list_id->tuple_cnt;	/* as a result */
		}
	      regu_free_listid (list_id);
	    }
	  /* end the query; reset query_id and call qmgr_end_query() */
	  pt_end_query (parser);
	}

      /* accumulate intermediate results */
      if (err >= NO_ERROR)
	{
	  result += err;
	}

      spec = statement->info.update.spec;
      while (spec && err == NO_ERROR)
	{
	  if (spec->info.spec.flag & PT_SPEC_FLAG_UPDATE)
	    {
	      flat = spec->info.spec.flat_entity_list;
	      class_obj = (flat) ? flat->info.name.db_object : NULL;

	      if (class_obj && db_is_vclass (class_obj))
		{
		  err = sm_flush_objects (class_obj);
		}

	    }

	  spec = spec->next;
	}

      if ((err < NO_ERROR) && er_errid () != NO_ERROR)
	{
	  pt_record_error (parser, parser->statement_number,
			   statement->line_number, statement->column_number,
			   er_msg (), NULL);
	}
    }

  /* If error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction. */
  if ((err < NO_ERROR) && savepoint_name
      && (err != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  return (err < NO_ERROR) ? err : result;
}


/*
 * Function Group:
 * DO functions for delete statements
 *
 */

/* used to generate unique savepoint names */
static int delete_savepoint_number = 0;

static int select_delete_list (PARSER_CONTEXT * parser,
			       QFILE_LIST_ID ** result_p,
			       PT_NODE * delete_stmt);
static int delete_object_tuple (const PARSER_CONTEXT * parser,
				const DB_VALUE * values);
#if defined(ENABLE_UNUSED_FUNCTION)
static int delete_object_by_oid (const PARSER_CONTEXT * parser,
				 const PT_NODE * statement);
#endif /* ENABLE_UNUSED_FUNCTION */
static int delete_list_by_oids (PARSER_CONTEXT * parser,
				QFILE_LIST_ID * list_id);
static int build_xasl_for_server_delete (PARSER_CONTEXT * parser,
					 PT_NODE * statement);
static int has_unique_constraint (DB_OBJECT * mop);
static int delete_real_class (PARSER_CONTEXT * parser, PT_NODE * statement);

/*
 * select_delete_list() -
 *   return: Error code
 *   parser(in/out): Parser context
 *   result(out): QFILE_LIST_ID for query result
 *   delete_stmt(in): delete statement
 *
 * Note : The list_id is allocated during query execution
 */
static int
select_delete_list (PARSER_CONTEXT * parser, QFILE_LIST_ID ** result_p,
		    PT_NODE * delete_stmt)
{
  PT_NODE *statement = NULL;
  QFILE_LIST_ID *result = NULL;
  statement =
    pt_to_upd_del_query (parser, NULL, NULL, delete_stmt->info.delete_.spec,
			 delete_stmt->info.delete_.class_specs,
			 delete_stmt->info.delete_.search_cond,
			 delete_stmt->info.delete_.using_index, NULL, NULL,
			 0 /* not server update */ ,
			 PT_COMPOSITE_LOCKING_DELETE);
  if (statement != NULL)
    {
      /* If we are updating a proxy, the select is not yet fully translated.
         if we are updating anything else, this is a no-op. */
      statement = mq_translate (parser, statement);

      if (statement)
	{
	  /* This enables authorization checking during methods in queries */
	  AU_ENABLE (parser->au_save);
	  if (do_select (parser, statement) < NO_ERROR)
	    {
	      /* query failed, an error has already been set */
	      statement = NULL;
	    }
	  AU_DISABLE (parser->au_save);
	}
    }

  if (statement)
    {
      result = (QFILE_LIST_ID *) statement->etc;
      parser_free_tree (parser, statement);
    }

  *result_p = result;
  return (er_errid () < 0) ? er_errid () : NO_ERROR;
}

/*
 * delete_object_tuple() - Deletes object attributes with db_values
 *   return: Error code if db_put fails
 *   parser(in): Parser context
 *   values(in): Array of db_value pointers. The first element in the
 *  		 array is the OID of the object to delete.
 */
static int
delete_object_tuple (const PARSER_CONTEXT * parser, const DB_VALUE * values)
{
  int error = NO_ERROR;
  DB_OBJECT *object;
  DB_OBJECT *class_obj;
  DB_ATTRIBUTE *attr;

  if (values && DB_VALUE_TYPE (values) == DB_TYPE_OBJECT)
    {
      object = DB_GET_OBJECT (values);

      /* authorizations checked in compiler--turn off but remember in
         parser so we can re-enable in case we run out of memory and
         longjmp to the cleanup routine. */

      /* delete blob or clob data files if exist */
      class_obj = db_get_class (object);
      attr = db_get_attributes (class_obj);
      while (attr)
	{
	  if (attr->type->id == DB_TYPE_BLOB ||
	      attr->type->id == DB_TYPE_CLOB)
	    {
	      DB_VALUE dbvalue;
	      error = db_get (object, attr->header.name, &dbvalue);
	      if (error == NO_ERROR)
		{
		  DB_ELO *elo;

		  assert (db_value_type (&dbvalue) == DB_TYPE_BLOB ||
			  db_value_type (&dbvalue) == DB_TYPE_CLOB);
		  elo = db_get_elo (&dbvalue);
		  if (elo)
		    {
		      error = db_elo_delete (elo);
		    }
		  db_value_clear (&dbvalue);
		  error = (error >= 0 ? NO_ERROR : error);
		}
	      if (error != NO_ERROR)
		{
		  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
		  return error;
		}
	    }
	  attr = db_attribute_next (attr);
	}
      /* TODO: to delete blob or clob at db api call */
      error = db_drop (object);
    }
  else
    {
      error = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * delete_object_by_oid() - Deletes db object by oid.
 *   return: Error code if delete fails
 *   parser(in): Parser context
 *   statement(in): Parse tree representing object to delete
 */
static int
delete_object_by_oid (const PARSER_CONTEXT * parser,
		      const PT_NODE * statement)
{
  int error = NO_ERROR;

  error = ER_GENERIC_ERROR;	/* unimplemented */

  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * delete_list_by_oids() - Deletes every oid in a list file
 *   return: Error code if delete fails
 *   parser(in): Parser context
 *   list_id(in): A list file of oid's
 */
static int
delete_list_by_oids (PARSER_CONTEXT * parser, QFILE_LIST_ID * list_id)
{
  int error = NO_ERROR;
  int cursor_status;
  DB_VALUE *oids = NULL;
  CURSOR_ID cursor_id;
  int count = 0, attrs_cnt = 0, idx;	/* how many objects were deleted? */
  const char *savepoint_name = NULL;
  int *flush_to_server = NULL;
  DB_OBJECT *mop = NULL;
  bool has_savepoint = false, is_cursor_open = false;

  if (list_id == NULL)
    {
      return NO_ERROR;
    }

  /* if the list file contains more than 1 object we need to savepoint
     the statement to guarantee statement atomicity. */
  if (list_id->tuple_cnt >= 1)
    {
      savepoint_name =
	mq_generate_name (parser, "UdsP", &delete_savepoint_number);

      error = tran_savepoint (savepoint_name, false);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
      has_savepoint = true;
    }

  if (!cursor_open (&cursor_id, list_id, false, false))
    {
      error = ER_GENERIC_ERROR;
      goto cleanup;
    }
  is_cursor_open = true;
  attrs_cnt = list_id->type_list.type_cnt;

  oids = (DB_VALUE *) db_private_alloc (NULL, attrs_cnt * sizeof (DB_VALUE));
  if (oids == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto cleanup;
    }
  flush_to_server = (int *) db_private_alloc (NULL, attrs_cnt * sizeof (int));
  if (flush_to_server == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto cleanup;
    }
  for (idx = 0; idx < attrs_cnt; idx++)
    {
      flush_to_server[idx] = -1;
    }

  cursor_id.query_id = parser->query_id;
  cursor_status = cursor_next_tuple (&cursor_id);

  while ((error >= NO_ERROR) && cursor_status == DB_CURSOR_SUCCESS)
    {
      error = cursor_get_tuple_value_list (&cursor_id, attrs_cnt, oids);
      /* The select list contains instance OID - class OID pairs */
      for (idx = 0; idx < attrs_cnt && error >= NO_ERROR; idx++)
	{
	  if (DB_IS_NULL (&oids[idx]))
	    {
	      continue;
	    }

	  mop = DB_GET_OBJECT (&oids[idx]);

	  if (db_is_deleted (mop))
	    {
	      /* if the object is an invalid object (was already deleted) then
	         skip the delete, instance flush and count steps */
	      continue;
	    }

	  if (flush_to_server[idx] == -1)
	    {
	      flush_to_server[idx] = has_unique_constraint (mop);
	    }
	  error = delete_object_tuple (parser, &oids[idx]);
	  if (error == ER_HEAP_UNKNOWN_OBJECT && do_Trigger_involved)
	    {
	      er_clear ();
	      error = NO_ERROR;
	      continue;
	    }

	  if ((error >= NO_ERROR) && flush_to_server[idx])
	    {
	      error = (locator_flush_instance (mop) == NO_ERROR) ?
		0 : (er_errid () != NO_ERROR ? er_errid () : -1);
	    }

	  if (error >= NO_ERROR)
	    {
	      count++;		/* another object has been deleted */
	    }
	}

      if (error >= NO_ERROR)
	{
	  cursor_status = cursor_next_tuple (&cursor_id);
	}
    }

  if ((error >= NO_ERROR) && cursor_status != DB_CURSOR_END)
    {
      error = ER_GENERIC_ERROR;
    }

cleanup:
  if (is_cursor_open)
    {
      cursor_close (&cursor_id);
    }

  /* if error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction
     itself.
   */
  if (has_savepoint && (error < NO_ERROR) && savepoint_name
      && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_savepoint (savepoint_name);
    }

  if (oids != NULL)
    {
      db_private_free (NULL, oids);
    }

  if (flush_to_server != NULL)
    {
      db_private_free (NULL, flush_to_server);
    }

  if (error >= NO_ERROR)
    {
      return count;
    }
  else
    {
      return error;
    }
}

/*
 * build_xasl_for_server_delete() - Build an xasl tree for a server delete
 *                                     and execute it.
 *   return: Error code if delete fails
 *   parser(in/out): Parser context
 *   statement(in): Parse tree of a delete statement.
 *
 * Note:
 *  The xasl tree has an DELETE_PROC node as the top node and
 *  a BUILDLIST_PROC as its aptr.  The BUILDLIST_PROC selects the
 *  instance OID.  The DELETE_PROC node scans the BUILDLIST_PROC results.
 *  The server executes the aptr and then for each instance selected,
 *  deletes it.  The result information is sent back to the
 *  client as a list file without any pages.  The list file tuple count
 *  is used as the return value from this routine.
 *
 *  The instances for the class are flushed from the client before the
 *  delete is executed.  If any instances are deleted, the instances are
 *  decached from the client after the delete is executed.
 */
static int
build_xasl_for_server_delete (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  XASL_NODE *xasl = NULL;
  DB_OBJECT *class_obj;
  int size, count = 0;
  char *stream = NULL;
  QUERY_ID query_id = NULL_QUERY_ID;
  QFILE_LIST_ID *list_id = NULL;
  const PT_NODE *node;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();

  xasl = pt_to_delete_xasl (parser, statement);

  if (xasl)
    {
      error = xts_map_xasl_to_stream (xasl, &stream, &size);
      if (error != NO_ERROR)
	{
	  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	}
    }
  else
    {
      error = er_errid ();
    }

  if (error == NO_ERROR)
    {
      int au_save;

      AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
      error = query_prepare_and_execute (stream,
					 size,
					 &query_id,
					 parser->host_var_count +
					 parser->auto_param_count,
					 parser->host_variables,
					 &list_id,
					 parser->exec_mode |
					 ASYNC_UNEXECUTABLE);
      AU_RESTORE (au_save);
    }
  parser->query_id = query_id;

  /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
  if (stream)
    {
      free_and_init (stream);
    }

  if (list_id)
    {
      count = list_id->tuple_cnt;
      if (count > 0)
	{
	  node = statement->info.delete_.spec;
	  while (node && error == NO_ERROR)
	    {
	      if (node->info.spec.flag & PT_SPEC_FLAG_DELETE)
		{
		  class_obj =
		    node->info.spec.flat_entity_list->info.name.db_object;
		  error = sm_flush_and_decache_objects (class_obj, true);
		}

	      node = node->next;
	    }
	}
      regu_free_listid (list_id);
    }

  pt_end_query (parser);

  /* mark the end of another level of xasl packing */
  pt_exit_packing_buf ();

  if (error >= NO_ERROR)
    {
      return count;
    }
  else
    {
      return error;
    }
}

/*
 * has_unique_constraint() - Check if class has an unique constraint
 *   return: 1 if the class has an unique constraint, otherwise 0
 *   mop(in/out): Class object to be checked
 */
static int
has_unique_constraint (DB_OBJECT * mop)
{
  DB_CONSTRAINT *constraint_list, *c;
  SM_CONSTRAINT_TYPE ctype;

  if (mop == NULL)
    {
      return 0;
    }

  constraint_list = db_get_constraints (mop);
  for (c = constraint_list; c; c = c->next)
    {
      ctype = c->type;
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (ctype))
	{
	  return 1;
	}
    }

  return 0;
}

/*
 * delete_real_class() - Deletes objects or rows
 *   return: Error code if delete fails
 *   parser(in/out): Parser context
 *   statement(in): Delete statement
 */
static int
delete_real_class (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  QFILE_LIST_ID *oid_list = NULL;
  int trigger_involved = 0;
  MOBJ class_;
  DB_OBJECT *class_obj;
  int wait_msecs = -2, old_wait_msecs = -2;
  float hint_waitsecs;
  PT_NODE *hint_arg = NULL, *node = NULL, *spec = NULL;
  bool has_virt_object = false;

  /* delete a "real" class in this database */

  spec = statement->info.delete_.spec;
  while (spec)
    {
      if (spec->info.spec.flag & PT_SPEC_FLAG_DELETE)
	{
	  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;

	  if (spec->info.spec.flat_entity_list->info.name.virt_object)
	    {
	      has_virt_object = true;
	    }
	  /* The IX lock on the class is sufficient.
	     DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
	  class_ = locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE);
	  if (!class_)
	    {
	      return er_errid ();
	    }

	  if (!trigger_involved)
	    {
	      error =
		sm_class_has_triggers (class_obj, &trigger_involved,
				       TR_EVENT_DELETE);
	      if (error != NO_ERROR)
		{
		  return error;
		}
	    }
	}

      spec = spec->next;
    }

  /* do delete on server if there is no trigger involved and the
     class is a real class */
  if (!trigger_involved && !has_virt_object)
    {
      error = build_xasl_for_server_delete (parser, statement);
    }
  else
    {
      hint_arg = statement->info.delete_.waitsecs_hint;
      if (statement->info.delete_.hint & PT_HINT_LK_TIMEOUT
	  && PT_IS_HINT_NODE (hint_arg))
	{
	  hint_waitsecs = (float) atof (hint_arg->info.name.original);
	  if (hint_waitsecs > 0)
	    {
	      wait_msecs = (int) (hint_waitsecs * 1000);
	    }
	  else
	    {
	      wait_msecs = (int) hint_waitsecs;
	    }
	  if (wait_msecs >= -1)
	    {
	      old_wait_msecs = TM_TRAN_WAIT_MSECS ();
	      (void) tran_reset_wait_times (wait_msecs);
	    }
	}
      if (error >= NO_ERROR)
	{
	  /* get the oid's and new values */
	  error = select_delete_list (parser, &oid_list, statement);
	}
      if (old_wait_msecs >= -1)
	{
	  (void) tran_reset_wait_times (old_wait_msecs);
	}

      if (!oid_list)
	{
	  /* an error should be set already, don't lose it */
	  return error;
	}

      /* delete each oid */
      error = delete_list_by_oids (parser, oid_list);
      regu_free_listid (oid_list);
      pt_end_query (parser);
    }
  return error;
}

/*
 * do_delete() - Deletes objects or rows
 *   return: Error code if delete fails
 *   parser(in/out): Parser context
 *   statement(in): Delete statement
 *
 * Note: Returning the number of deleted object on success is a bug fix!
 *       uci_static expects do_execute_statement to return the number of
 *       affected objects for a successful DELETE, UPDATE, INSERT, SELECT.
 */
int
do_delete (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  int result = NO_ERROR;
  PT_NODE *spec;
  int rollbacktosp = 0;
  const char *savepoint_name = NULL;

  CHECK_MODIFICATION_ERROR ();

  /* DON'T REMOVE this, correct authorization validation of views
     depends on this.

     DON'T return from the body of this function. Break out of the loop
     if necessary. */

  AU_DISABLE (parser->au_save);

  while (statement && error >= 0)
    {

      if (statement->node_type != PT_DELETE)
	{
	  /* bullet proofing, should not get here */
	  PT_INTERNAL_ERROR (parser, "delete");
	  error = ER_GENERIC_ERROR;
	  break;
	}

      spec = statement->info.delete_.spec;

      if (pt_false_where (parser, statement))
	{
	  /* nothing to delete, where part is false */
	}
#if defined(ENABLE_UNUSED_FUNCTION)
      else if (!spec)
	{
	  /* this is an delete object if it has no spec */
	  error = delete_object_by_oid (parser, statement);
	}
#endif /* ENABLE_UNUSED_FUNCTION */
      else
	{
	  /* the following is the "normal" sql type execution */
	  error = delete_real_class (parser, statement);
	}

      result += error;
      statement = statement->next;
    }

  /* if error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction. */

  if ((error < NO_ERROR) && rollbacktosp
      && (error != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  if (error < 0)
    {
      result = error;
    }

  /* DON'T REMOVE this, correct authorization validation of views
     depends on this. */

  AU_ENABLE (parser->au_save);

  return result;
}

/*
 * do_prepare_delete() - Prepare the DELETE statement
 *   return: Error code
 *   parser(in/out): Parser context
 *   statement(in/out): Delete statement
 */
int
do_prepare_delete (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err;
  PT_NODE *flat;
  DB_OBJECT *class_obj;
  int has_trigger, au_save;
  bool server_delete, has_virt_obj;
  XASL_ID *xasl_id;
  PT_NODE *node = NULL;

  if (parser == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  for (err = NO_ERROR; statement && (err >= NO_ERROR);
       statement = statement->next)
    {
      /* there can be no results, this is a compile time false where clause */
      if (pt_false_where (parser, statement))
	{
	  /* tell to the execute routine that there's no XASL to execute */
	  statement->xasl_id = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next DELETE statement */
	}

      /* Delete object case:
         this is an delete object if it has no spec */
      if (!statement->info.delete_.spec)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next DELETE statement */
	}

      /* if already prepared */
      if (statement->xasl_id)
	{
	  continue;		/* continue to next DELETE statement */
	}

      /* the presence of a proxy trigger should force the delete
         to be performed through the workspace  */
      AU_SAVE_AND_DISABLE (au_save);	/* because sm_class_has_trigger() calls
					   au_fetch_class() */
      has_virt_obj = false;
      has_trigger = 0;
      node = (PT_NODE *) statement->info.delete_.spec;
      while (node && err == NO_ERROR && !has_trigger)
	{
	  if (node->info.spec.flag & PT_SPEC_FLAG_DELETE)
	    {
	      flat = node->info.spec.flat_entity_list;
	      if (flat)
		{
		  if (flat->info.name.virt_object)
		    {
		      has_virt_obj = true;
		    }
		  class_obj = flat->info.name.db_object;
		}
	      else
		{
		  class_obj = NULL;
		}
	      err =
		sm_class_has_triggers (class_obj, &has_trigger,
				       TR_EVENT_DELETE);
	    }

	  node = node->next;
	}

      AU_RESTORE (au_save);
      /* err = has_proxy_trigger(flat, &has_trigger); */
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "delete");
	  break;		/* stop while loop if error */
	}
      /* sm_class_has_triggers() checked if the class has active triggers */
      statement->info.delete_.has_trigger = has_trigger;

      /* determine whether it can be server-side or OID list deletion */
      server_delete = (!has_trigger && !has_virt_obj);

      statement->info.delete_.server_delete = server_delete;

      xasl_id = NULL;
      if (server_delete)
	{
	  /* Server-side deletion case: (by requesting server to execute XASL)
	     build DELETE_PROC XASL */

	  const char *qstr;
	  XASL_NODE *xasl;
	  char *stream;
	  int size;

	  /* make query string */
	  parser->dont_prt_long_string = 1;
	  parser->long_string_skipped = 0;
	  parser->print_type_ambiguity = 0;
	  PT_NODE_PRINT_TO_ALIAS (parser, statement,
				  (PT_CONVERT_RANGE | PT_PRINT_QUOTES));
	  qstr = statement->alias_print;
	  parser->dont_prt_long_string = 0;
	  if (parser->long_string_skipped || parser->print_type_ambiguity)
	    {
	      statement->cannot_prepare = 1;
	      return NO_ERROR;
	    }

	  /* look up server's XASL cache for this query string
	     and get XASL file id (XASL_ID) returned if found */
	  if (statement->recompile == 0)
	    {
	      err = query_prepare (qstr, NULL, 0, &xasl_id);
	      if (err != NO_ERROR)
		{
		  err = er_errid ();
		}
	    }
	  else
	    {
	      err = qmgr_drop_query_plan (qstr,
					  ws_identifier (db_get_user ()),
					  NULL, true);
	    }
	  if (!xasl_id && err == NO_ERROR)
	    {
	      /* cache not found;
	         make XASL from the parse tree including query optimization
	         and plan generation */

	      /* mark the beginning of another level of xasl packing */
	      pt_enter_packing_buf ();

	      /* this prevents authorization checking during generating XASL */
	      AU_SAVE_AND_DISABLE (au_save);

	      /* pt_to_delete_xasl() will build XASL tree from parse tree */
	      xasl = pt_to_delete_xasl (parser, statement);
	      AU_RESTORE (au_save);
	      stream = NULL;
	      if (xasl && (err >= NO_ERROR))
		{
		  /* convert the created XASL tree to the byte stream for
		     transmission to the server */
		  err = xts_map_xasl_to_stream (xasl, &stream, &size);
		  if (err != NO_ERROR)
		    {
		      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
				 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		    }
		}
	      else
		{
		  err = er_errid ();
		  pt_record_error (parser, parser->statement_number,
				   statement->line_number,
				   statement->column_number, er_msg (), NULL);
		}

	      /* mark the end of another level of xasl packing */
	      pt_exit_packing_buf ();

	      /* request the server to prepare the query;
	         give XASL stream generated from the parse tree
	         and get XASL file id returned */
	      if (stream && (err >= NO_ERROR))
		{
		  err = query_prepare (qstr, stream, size, &xasl_id);
		  if (err != NO_ERROR)
		    {
		      err = er_errid ();
		    }
		}
	      /* As a result of query preparation of the server,
	         the XASL cache for this query will be created or updated. */

	      /* free 'stream' that is allocated inside of
	         xts_map_xasl_to_stream() */
	      if (stream)
		{
		  free_and_init (stream);
		}
	      statement->use_plan_cache = 0;
	    }
	  else
	    {
	      if (err == NO_ERROR)
		{
		  statement->use_plan_cache = 1;
		}
	      else
		{
		  statement->use_plan_cache = 0;
		}
	    }
	}
      else
	{
	  /* OID list deletion case: (by selecting OIDs to delete)
	     make SELECT statement for this DELETE statement */

	  PT_NODE *select_statement;
	  PT_DELETE_INFO *delete_info;

	  delete_info = &statement->info.delete_;
	  select_statement = pt_to_upd_del_query (parser, NULL, NULL,
						  delete_info->spec,
						  delete_info->class_specs,
						  delete_info->search_cond,
						  delete_info->using_index,
						  NULL, NULL, 0,
						  PT_COMPOSITE_LOCKING_DELETE);
	  /* translate views or virtual classes into base classes;
	     If we are updating a proxy, the SELECT is not yet fully
	     translated. If we are updating anything else, this is a no-op. */

	  /* this prevents authorization checking during view transformation */
	  AU_SAVE_AND_DISABLE (au_save);

	  select_statement = mq_translate (parser, select_statement);
	  AU_RESTORE (au_save);
	  if (select_statement)
	    {
	      /* get XASL_ID by calling do_prepare_select() */
	      err = do_prepare_select (parser, select_statement);
	      xasl_id = select_statement->xasl_id;
	      parser_free_tree (parser, select_statement);
	    }
	  else
	    {
	      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	      err = er_errid ();
	    }

	}

      /* save the XASL_ID that is allocated and returned by
         query_prepare() into 'statement->xasl_id'
         to be used by do_execute_delete() */
      statement->xasl_id = xasl_id;

    }

  return err;
}

/*
 * do_execute_delete() - Execute the prepared DELETE statement
 *   return: Tuple count if success, otherwise an error code
 *   parser(in): Parser context
 *   statement(in): Delete statement
 */
int
do_execute_delete (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err, result = 0;
  PT_NODE *flat, *node;
  const char *savepoint_name;
  DB_OBJECT *class_obj;
  QFILE_LIST_ID *list_id;
  int au_save, isvirt = 0;
  int wait_msecs = -2, old_wait_msecs = -2;
  float hint_waitsecs;
  PT_NODE *hint_arg;
  int query_flag;

  CHECK_MODIFICATION_ERROR ();

  /* If the DELETE statement contains more than one delete component,
     we savepoint the delete components to try to guarantee DELETE statement
     atomicity. */
  savepoint_name = NULL;

  for (err = NO_ERROR, result = 0; statement && (err >= NO_ERROR);
       statement = statement->next)
    {
#if defined(ENABLE_UNUSED_FUNCTION)
      /* Delete object case:
         delete object by OID */

      if (!statement->info.delete_.spec)
	{
	  err = delete_object_by_oid (parser, statement);
	  continue;		/* continue to next DELETE statement */
	}
#endif /* ENABLE_UNUSED_FUNCTION */

      /* check if it is not necessary to execute this statement,
         e.g. false where or not prepared correctly */
      if (!statement->xasl_id)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next DELETE statement */
	}

      /* Server-side deletion or OID list deletion case:
         execute the prepared(stored) XASL (DELETE_PROC or SELECT statement) */

      node = statement->info.delete_.spec;

      while (node && err == NO_ERROR)
	{
	  flat = node->info.spec.flat_entity_list;
	  class_obj = (flat) ? flat->info.name.db_object : NULL;

	  if (node->info.spec.flag & PT_SPEC_FLAG_DELETE)
	    {
	      /* The IX lock on the class is sufficient.
	         DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
	      if (locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE)
		  == NULL)
		{
		  err = er_errid ();
		  break;	/* stop while loop if error */
		}
	    }

	  /* flush necessary objects before execute */
	  err = sm_flush_objects (class_obj);

	  node = node->next;
	}

      if (err != NO_ERROR)
	{
	  break;		/* stop while loop if error */
	}

      /* Request that the server executes the stored XASL, which is
         the execution plan of the prepared query, with the host variables
         given by users as parameter values for the query.
         As a result, query id and result file id (QFILE_LIST_ID) will be
         returned.
         do_prepare_delete() has saved the XASL file id (XASL_ID) in
         'statement->xasl_id' */
      query_flag = parser->exec_mode | ASYNC_UNEXECUTABLE;
      if (statement->do_not_keep == 0)
	{
	  query_flag |= KEEP_PLAN_CACHE;
	}
      query_flag |= NOT_FROM_RESULT_CACHE;
      query_flag |= RESULT_CACHE_INHIBITED;

      AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
      parser->query_id = -1;
      list_id = NULL;
      err = query_execute (statement->xasl_id,
			   &parser->query_id,
			   parser->host_var_count +
			   parser->auto_param_count,
			   parser->host_variables, &list_id, query_flag,
			   NULL, NULL);
      AU_RESTORE (au_save);

      /* in the case of OID list deletion, now delete the selected OIDs */
      if ((err >= NO_ERROR) && list_id)
	{
	  if (statement->info.delete_.server_delete)
	    {
	      err = list_id->tuple_cnt;
	    }
	  else
	    {
	      hint_arg = statement->info.delete_.waitsecs_hint;
	      if (statement->info.delete_.hint & PT_HINT_LK_TIMEOUT
		  && PT_IS_HINT_NODE (hint_arg))
		{
		  hint_waitsecs = (float) atof (hint_arg->info.name.original);
		  if (hint_waitsecs > 0)
		    {
		      wait_msecs = (int) (hint_waitsecs * 1000);
		    }
		  else
		    {
		      wait_msecs = (int) hint_waitsecs;
		    }
		  if (wait_msecs >= -1)
		    {
		      old_wait_msecs = TM_TRAN_WAIT_MSECS ();
		      (void) tran_reset_wait_times (wait_msecs);
		    }
		}
	      AU_SAVE_AND_DISABLE (au_save);	/* this prevents authorization
						   checking during execution */
	      /* delete each oid */
	      err = delete_list_by_oids (parser, list_id);
	      AU_RESTORE (au_save);
	      if (old_wait_msecs >= -1)
		{
		  (void) tran_reset_wait_times (old_wait_msecs);
		}
	    }
	}

      /* free returned QFILE_LIST_ID */
      if (list_id)
	{
	  if (list_id->tuple_cnt > 0 && statement->info.delete_.server_delete)
	    {
	      int err2 = NO_ERROR;
	      node = statement->info.delete_.spec;

	      while (node && err2 >= NO_ERROR)
		{
		  if (node->info.spec.flag & PT_SPEC_FLAG_DELETE)
		    {
		      flat = node->info.spec.flat_entity_list;
		      class_obj = (flat) ? flat->info.name.db_object : NULL;

		      err2 = sm_flush_and_decache_objects (class_obj, true);
		    }

		  node = node->next;
		}
	      if (err2 != NO_ERROR)
		{
		  err = err2;
		}
	    }
	  regu_free_listid (list_id);
	}
      /* end the query; reset query_id and call qmgr_end_query() */
      if (parser->query_id > 0)
	{
	  if (er_errid () != ER_LK_UNILATERALLY_ABORTED)
	    {
	      qmgr_end_query (parser->query_id);
	    }
	  parser->query_id = -1;
	}

      /* accumulate intermediate results */
      if (err >= NO_ERROR)
	{
	  result += err;
	}

      node = statement->info.delete_.spec;

      while (node && err >= NO_ERROR)
	{
	  if (node->info.spec.flag & PT_SPEC_FLAG_DELETE)
	    {
	      flat = node->info.spec.flat_entity_list;
	      class_obj = (flat) ? flat->info.name.db_object : NULL;

	      if (class_obj && db_is_vclass (class_obj))
		{
		  err = sm_flush_objects (class_obj);
		}
	    }

	  node = node->next;
	}
    }

  /* If error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction. */
  if ((err < NO_ERROR) && savepoint_name
      && (err != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  return (err < NO_ERROR) ? err : result;
}


/*
 * Function Group:
 * Implements the evaluate statement.
 *
 */


/*
 * do_evaluate() - Evaluates an expression
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a insert statement
 */
int
do_evaluate (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_VALUE expr_value, *into_val;
  PT_NODE *expr, *into_var;
  const char *into_label;

  db_make_null (&expr_value);

  if (!statement || !((expr = statement->info.evaluate.expression)))
    {
      return ER_GENERIC_ERROR;
    }

  pt_evaluate_tree (parser, expr, &expr_value, 1);
  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      return ER_PT_SEMANTIC;
    }

  statement->etc = (void *) db_value_copy (&expr_value);
  into_var = statement->info.evaluate.into_var;

  if (into_var
      && into_var->node_type == PT_NAME
      && (into_label = into_var->info.name.original) != NULL)
    {
      /* create another DB_VALUE of the new instance for
         the label_table */
      into_val = db_value_copy (&expr_value);

      /* enter {label, ins_val} pair into the label_table */
      error = pt_associate_label_with_value_check_reference (into_label,
							     into_val);
    }

  pr_clear_value (&expr_value);
  return error;
}





/*
 * Function Group:
 * DO functions for insert statements
 *
 */


typedef enum
{ INSERT_SELECT = 1,
  INSERT_VALUES = 2,
  INSERT_DEFAULT = 4,
  INSERT_REPLACE = 8,
  INSERT_ON_DUP_KEY_UPDATE = 16
} SERVER_PREFERENCE;

/* used to generate unique savepoint names */
static int insert_savepoint_number = 0;

static int insert_object_attr (const PARSER_CONTEXT * parser,
			       DB_OTMPL * otemplate, DB_VALUE * value,
			       PT_NODE * name, DB_ATTDESC * attr_desc);
static int check_for_cons (PARSER_CONTEXT * parser,
			   int *has_unique,
			   PT_NODE ** non_null_attrs,
			   const PT_NODE * attr_list, DB_OBJECT * class_obj);
static int check_for_default_expr (PARSER_CONTEXT * parser,
				   PT_NODE * specified_attrs,
				   PT_NODE ** default_expr_attrs,
				   DB_OBJECT * class_obj);
static int insert_check_for_fk_cache_attr (PARSER_CONTEXT * parser,
					   const PT_NODE * attr_list,
					   DB_OBJECT * class_obj);
static int is_server_insert_allowed (PARSER_CONTEXT * parser,
				     PT_NODE ** non_null_attrs,
				     int *has_uniques, int *server_allowed,
				     const PT_NODE * statement,
				     const PT_NODE * values_list,
				     const PT_NODE * class_);
static int do_insert_at_server (PARSER_CONTEXT * parser,
				PT_NODE * statement, PT_NODE * values_list,
				PT_NODE * non_null_attrs,
				PT_NODE ** upd_non_null_attrs,
				const int has_uniques,
				const int upd_has_uniques,
				PT_NODE * default_expr_attrs,
				bool is_first_value);
static int insert_subquery_results (PARSER_CONTEXT * parser,
				    PT_NODE * statement,
				    PT_NODE * values_list, PT_NODE * class_,
				    const char **savepoint_name);
static int is_attr_not_in_insert_list (const PARSER_CONTEXT * parser,
				       PT_NODE * name_list, const char *name);
static int check_missing_non_null_attrs (const PARSER_CONTEXT * parser,
					 const PT_NODE * spec,
					 PT_NODE * attr_list,
					 const bool has_default_values_list);
static PT_NODE *make_vmops (PARSER_CONTEXT * parser, PT_NODE * node,
			    void *arg, int *continue_walk);
static PT_NODE *test_check_option (PARSER_CONTEXT * parser, PT_NODE * node,
				   void *arg, int *continue_walk);
static int insert_local (PARSER_CONTEXT * parser, PT_NODE * statement);
static int insert_predefined_values_into_partition (const PARSER_CONTEXT *
						    parser,
						    DB_OTMPL ** rettemp,
						    const MOP classobj,
						    const DB_VALUE * partcol,
						    PARTITION_SELECT_INFO *
						    psi,
						    PARTITION_INSERT_CACHE *
						    pic);
static PT_NODE *build_expression_from_assignment (PARSER_CONTEXT * parser,
						  PT_NODE * spec,
						  OBJ_TEMPASSIGN *
						  assignment);
static int build_select_statement (PARSER_CONTEXT * parser, DB_OTMPL * tmpl,
				   PT_NODE * spec, PT_NODE * class_specs,
				   PT_NODE ** statement, bool for_update);
static int build_predicate_for_unique_constraint (PARSER_CONTEXT * parser,
						  PT_NODE * spec,
						  DB_OTMPL * tmpl,
						  SM_CLASS_CONSTRAINT *
						  constraint,
						  PT_NODE ** predicate);
static int do_on_duplicate_key_update (PARSER_CONTEXT * parser,
				       DB_OTMPL * tpl, PT_NODE * spec,
				       PT_NODE * class_specs,
				       PT_NODE * update_stmt);
static int do_replace_into (PARSER_CONTEXT * parser, DB_OTMPL * tmpl,
			    PT_NODE * spec, PT_NODE * class_specs);
static int is_replace_or_odku_allowed (DB_OBJECT * obj, int *allowed);
/*
 * insert_object_attr()
 *   return: Error code if db_put fails
 *   parser(in): Short description of the param1
 *   otemplate(in/out): Short description of the param2
 *   value(in/out): New attr value
 *   name(in): Name to update
 *   attr_desc(in): Attr descriptor of attribute to update
 */
static int
insert_object_attr (const PARSER_CONTEXT * parser,
		    DB_OTMPL * otemplate, DB_VALUE * value,
		    PT_NODE * name, DB_ATTDESC * attr_desc)
{
  int error;

  if (DB_VALUE_TYPE (value) == DB_TYPE_OBJECT && !DB_IS_NULL (value))
    {
      /* we may need to check for value coming in from a parameter
         or host variable as a not-yet-translated-to-real-class
         value. This must be done at run time in general. */
      db_make_object (value, db_real_instance (db_get_object (value)));
    }

  if (name->info.name.db_object && db_is_vclass (name->info.name.db_object))
    {
      /* this is a shared attribute of a view.
         this means this cannot be updated in the template for
         this real class. Its simply done separately by a db_put. */
      error = obj_set_shared (name->info.name.db_object,
			      name->info.name.original, value);
    }
  else
    {
      /* the normal case */
      SM_ATTRIBUTE *att;

      att = db_get_attribute (otemplate->classobj, name->info.name.original);
      error = dbt_dput_internal (otemplate, attr_desc, value);
    }

  return error;
}


static int
do_prepare_insert_internal (PARSER_CONTEXT * parser,
			    PT_NODE * statement, PT_NODE * values_list,
			    PT_NODE * non_null_attrs,
			    PT_NODE ** upd_non_null_attrs,
			    const int has_uniques, const int upd_has_uniques)
{
  int error = NO_ERROR;
  XASL_NODE *xasl = NULL;
  int size = 0;
  char *stream = NULL;
  int i = 0;
  int j = 0;
  XASL_ID *xasl_id = NULL;
  const char *qstr;
  PT_NODE *val, *head = NULL, *prev = NULL;

  if (!parser || !statement || !values_list
      || statement->node_type != PT_INSERT)
    {
      assert (false);
      return ER_GENERIC_ERROR;
    }

  assert (statement->info.insert.do_replace == false);
  assert (statement->info.insert.on_dup_key_update == NULL);

  /* insert value auto parameterize */
  val = statement->info.insert.value_clauses->info.node_list.list;
  for (; val != NULL; val = val->next)
    {
      if (pt_is_const_not_hostvar (val) && !PT_IS_NULL_NODE (val))
	{
	  val = pt_rewrite_to_auto_param (parser, val);
	  if (prev != NULL)
	    {
	      prev->next = val;
	    }

	  if (val == NULL)
	    {
	      break;
	    }
	}

      if (head == NULL)
	{
	  head = val;
	}

      prev = val;
    }

  statement->info.insert.value_clauses->info.node_list.list = head;

  /* make query string */
  parser->dont_prt_long_string = 1;
  parser->long_string_skipped = 0;
  parser->print_type_ambiguity = 0;
  PT_NODE_PRINT_TO_ALIAS (parser, statement,
			  (PT_CONVERT_RANGE | PT_PRINT_QUOTES));
  qstr = statement->alias_print;
  parser->dont_prt_long_string = 0;
  if (parser->long_string_skipped || parser->print_type_ambiguity)
    {
      statement->cannot_prepare = 1;
      return NO_ERROR;
    }

  /* look up server's XASL cache for this query string
     and get XASL file id (XASL_ID) returned if found */
  if (statement->recompile == 0)
    {
      error = query_prepare (qstr, NULL, 0, &xasl_id);
      if (error != NO_ERROR)
	{
	  error = er_errid ();
	}
    }
  else
    {
      error = qmgr_drop_query_plan (qstr,
				    ws_identifier (db_get_user ()), NULL,
				    true);
    }

  if (!xasl_id && error == NO_ERROR)
    {
      PT_NODE *default_expr_attrs = NULL;
      PT_NODE *class_;

      class_ = statement->info.insert.spec->info.spec.flat_entity_list;
      error =
	check_for_default_expr (parser, statement->info.insert.attr_list,
				&default_expr_attrs,
				class_->info.name.db_object);
      if (error != NO_ERROR)
	{
	  statement->use_plan_cache = 0;
	  statement->xasl_id = NULL;
	  return error;
	}

      /* mark the beginning of another level of xasl packing */
      pt_enter_packing_buf ();
      xasl = pt_to_insert_xasl (parser, statement, values_list, has_uniques,
				non_null_attrs, default_expr_attrs, true);

      if (xasl)
	{
	  INSERT_PROC_NODE *insert = &xasl->proc.insert;

	  assert (xasl->dptr_list == NULL);

	  if (error == NO_ERROR)
	    {
	      error = xts_map_xasl_to_stream (xasl, &stream, &size);
	      if (error != NO_ERROR)
		{
		  PT_ERRORm (parser, statement,
			     MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		}
	    }
	}
      else
	{
	  error = er_errid ();
	}

      /* mark the end of another level of xasl packing */
      pt_exit_packing_buf ();

      if (stream && (error >= NO_ERROR))
	{
	  error = query_prepare (qstr, stream, size, &xasl_id);
	  if (error != NO_ERROR)
	    {
	      error = er_errid ();
	    }
	}

      /* As a result of query preparation of the server,
         the XASL cache for this query will be created or updated. */

      /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
      if (stream)
	{
	  free_and_init (stream);
	}

      statement->use_plan_cache = 0;
    }
  else
    {
      if (error == NO_ERROR)
	{
	  statement->use_plan_cache = 1;
	}
      else
	{
	  statement->use_plan_cache = 0;
	}
    }

  /* save the XASL_ID that is allocated and returned by
     query_prepare() into 'statement->xasl_id'
     to be used by do_execute_update() */
  statement->xasl_id = xasl_id;

  return NO_ERROR;
}



/*
 * do_insert_at_server() - Brief description of this function
 *   return: Error code if insert fails
 *   parser(in/out): Parser context
 *   statement(in): Parse tree of a insert statement.
 *   values_list(in): The list of values to insert.
 *   non_null_attrs(in):
 *   has_uniques(in):
 *   is_first_value(in): True if the first value of VALUE clause.
 *
 * Note: Build an xasl tree for a server insert and execute it.
 *
 *  The xasl tree has an INSERT_PROC node as the top node and
 *  a BUILDLIST_PROC as its aptr.  The BUILDLIST_PROC selects the
 *  insert values.  The INSERT_PROC node scans the BUILDLIST_PROC results.
 *  The server executes the aptr and then for each instance selected,
 *  inserts it.  The result information is sent back to the
 *  client as a list file without any pages.  The list file tuple count
 *  is used as the return value from this routine.

 *  The instances for the class are flushed from the client before the
 *  insert is executed.
 */
static int
do_insert_at_server (PARSER_CONTEXT * parser,
		     PT_NODE * statement, PT_NODE * values_list,
		     PT_NODE * non_null_attrs, PT_NODE ** upd_non_null_attrs,
		     const int has_uniques, const int upd_has_uniques,
		     PT_NODE * default_expr_attrs, bool is_first_value)
{
  int error = NO_ERROR;
  XASL_NODE *xasl = NULL;
  int size = 0, count = 0;
  char *stream = NULL;
  QUERY_ID query_id = NULL_QUERY_ID;
  QFILE_LIST_ID *list_id = NULL;
  int i = 0;
  int j = 0, k = 0;
  PT_NODE *update_statement = NULL;

  if (parser == NULL || statement == NULL || values_list == NULL
      || statement->node_type != PT_INSERT)
    {
      return ER_GENERIC_ERROR;
    }

  update_statement = statement->info.insert.on_dup_key_update;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();
  xasl = pt_to_insert_xasl (parser, statement, values_list, has_uniques,
			    non_null_attrs, default_expr_attrs,
			    is_first_value);

  if (xasl)
    {
      INSERT_PROC_NODE *insert = &xasl->proc.insert;

      assert (xasl->dptr_list == NULL);
      if (update_statement != NULL)
	{
	  XASL_NODE *update_xasl =
	    statement_to_update_xasl (parser, update_statement,
				      upd_non_null_attrs);

	  if (update_xasl != NULL)
	    {
	      int dup_key_oid_var_index =
		pt_get_dup_key_oid_var_index (parser, update_statement);

	      if (dup_key_oid_var_index < 0)
		{
		  error = ER_FAILED;
		  PT_ERRORmf2 (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			       MSGCAT_RUNTIME_HOSTVAR_INDEX_ERROR,
			       dup_key_oid_var_index,
			       parser->auto_param_count);
		}
	      else
		{
		  xasl->dptr_list = update_xasl;
		  insert->dup_key_oid_var_index = dup_key_oid_var_index;
		}
	    }
	  else
	    {
	      error = er_errid ();
	      assert (error != NO_ERROR);
	    }
	}

      if (error == NO_ERROR)
	{
	  error = xts_map_xasl_to_stream (xasl, &stream, &size);
	  if (error != NO_ERROR)
	    {
	      PT_ERRORm (parser, statement,
			 MSGCAT_SET_PARSER_RUNTIME,
			 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	    }
	}

      if (xasl->dptr_list != NULL)
	{
	  UPDATE_PROC_NODE *update = &xasl->dptr_list->proc.update;

	  for (i = update->no_classes - 1; i >= 0; i--)
	    {
	      if (update->assigns[i].constant)
		{
		  pr_clear_value (update->assigns[i].constant);
		}
	    }
	}
    }
  else
    {
      error = er_errid ();
    }

  if (error == NO_ERROR && stream != NULL)
    {
      int au_save;

      assert (size > 0);

      AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
      error = query_prepare_and_execute (stream,
					 size,
					 &query_id,
					 (parser->host_var_count +
					  parser->auto_param_count),
					 parser->host_variables,
					 &list_id,
					 (parser->exec_mode |
					  ASYNC_UNEXECUTABLE));
      AU_RESTORE (au_save);
    }

  parser->query_id = query_id;

  /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
  if (stream)
    {
      free_and_init (stream);
    }

  if (list_id)
    {
      PT_NODE *cl_name_node = NULL;

      count = list_id->tuple_cnt;
      if (count > 0 && update_statement != NULL)
	{
	  for (cl_name_node =
	       update_statement->info.update.spec->info.spec.flat_entity_list;
	       cl_name_node != NULL && error == NO_ERROR;
	       cl_name_node = cl_name_node->next)
	    {
	      error =
		sm_flush_and_decache_objects (cl_name_node->info.
					      name.db_object, true);
	    }
	}

      if (count > 0 && statement->info.insert.do_replace)
	{
	  MOP class_obj =
	    statement->info.insert.spec->info.spec.flat_entity_list->info.
	    name.db_object;

	  error = sm_flush_and_decache_objects (class_obj, true);
	}

      regu_free_listid (list_id);
    }

  pt_end_query (parser);

  /* mark the end of another level of xasl packing */
  pt_exit_packing_buf ();

  if (error >= NO_ERROR)
    {
      return count;
    }
  else
    {
      return error;
    }
}

/*
 * check_for_default_expr() - Builds a list of attributes that have a default
 *			      expression and are not found in the specified
 *			      attributes list
 *   return: Error code
 *   parser(in/out): Parser context
 *   specified_attrs(in): the list of attributes that are not to be considered
 *   default_expr_attrs(out):
 *   class_obj(in):
 */
static int
check_for_default_expr (PARSER_CONTEXT * parser, PT_NODE * specified_attrs,
			PT_NODE ** default_expr_attrs, DB_OBJECT * class_obj)
{
  SM_CLASS *cls;
  SM_ATTRIBUTE *att;
  int error = NO_ERROR;

  error = au_fetch_class_force (class_obj, &cls, AU_FETCH_READ);
  if (error != NO_ERROR)
    {
      return error;
    }
  for (att = cls->attributes; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->default_value.default_expr != DB_DEFAULT_NONE)
	{
	  PT_NODE *node = NULL;
	  /* if a value has already been specified for this attribute,
	     go to the next one */
	  for (node = specified_attrs; node != NULL; node = node->next)
	    {
	      if (!pt_str_compare (pt_get_name (node),
				   att->header.name, CASE_INSENSITIVE))
		{
		  break;
		}
	    }
	  if (node != NULL)
	    {
	      continue;
	    }

	  if (*default_expr_attrs == NULL)
	    {
	      *default_expr_attrs = parser_new_node (parser, PT_NAME);
	      if (*default_expr_attrs)
		{
		  (*default_expr_attrs)->info.name.original =
		    att->header.name;
		}
	      else
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return ER_FAILED;
		}
	    }
	  else
	    {
	      PT_NODE *new_ = parser_new_node (parser, PT_NAME);
	      if (new_)
		{
		  new_->info.name.original = att->header.name;
		  *default_expr_attrs =
		    parser_append_node (*default_expr_attrs, new_);
		}
	      else
		{
		  PT_INTERNAL_ERROR (parser, "allocate new node");
		  return ER_FAILED;
		}
	    }
	}
    }
  return NO_ERROR;
}

/*
 * check_for_cons() - Determines whether an attribute has the constaint
 *   return: Error code
 *   parser(in): Parser context
 *   has_unique(in/out):
 *   non_null_attrs(in/out): all the "NOT NULL" attributes except for the
 *                           AUTO_INCREMENT ones
 *   attr_list(in): Parse tree of an insert statement attribute list
 *   class_obj(in): Class object

 */
static int
check_for_cons (PARSER_CONTEXT * parser, int *has_unique,
		PT_NODE ** non_null_attrs, const PT_NODE * attr_list,
		DB_OBJECT * class_obj)
{
  PT_NODE *pointer;

  assert (*non_null_attrs == NULL);
  *has_unique = 0;

  while (attr_list)
    {
      if (attr_list->node_type != PT_NAME)
	{
	  /* bullet proofing, should not get here */
	  return ER_GENERIC_ERROR;
	}

      if (*has_unique == 0
	  && sm_att_unique_constrained (class_obj,
					attr_list->info.name.original))
	{
	  *has_unique = 1;
	}

      if (sm_att_constrained (class_obj, attr_list->info.name.original,
			      SM_ATTFLAG_NON_NULL))
	{
	  /* NULL values are allowed for auto_increment columns.
	   * It means that the next auto_increment value will be inserted.
	   */
	  if (sm_att_auto_increment (class_obj,
				     attr_list->info.name.original) == false)
	    {
	      pointer = pt_point (parser, attr_list);
	      if (pointer == NULL)
		{
		  PT_ERRORm (parser, attr_list,
			     MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);

		  if (*non_null_attrs)
		    {
		      parser_free_tree (parser, *non_null_attrs);
		    }
		  *non_null_attrs = NULL;

		  return MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
		}
	      *non_null_attrs = parser_append_node (pointer, *non_null_attrs);
	    }
	}

      attr_list = attr_list->next;
    }

  if (*has_unique == 0)
    {
      /* check if the class has an auto-increment attribute which has unique
         constraint */

      SM_CLASS *class_;
      SM_ATTRIBUTE *att;

      if (au_fetch_class_force (class_obj, &class_, AU_FETCH_READ) ==
	  NO_ERROR)
	{
	  for (att = class_->ordered_attributes; att; att = att->order_link)
	    {
	      if ((att->flags & SM_ATTFLAG_AUTO_INCREMENT)
		  && classobj_has_unique_constraint (att->constraints))
		{
		  *has_unique = 1;
		  break;
		}
	    }
	}
    }

  return NO_ERROR;
}

/*
 * insert_check_for_fk_cache_attr() - Brief description of this function
 *   return: Error code
 *   parser(in): Parser context
 *   attr_list(in):
 *   class_obj(in):
 */
static int
insert_check_for_fk_cache_attr (PARSER_CONTEXT * parser,
				const PT_NODE * attr_list,
				DB_OBJECT * class_obj)
{
  while (attr_list)
    {
      if (attr_list->node_type != PT_NAME)
	{
	  return ER_GENERIC_ERROR;
	}

      if (sm_is_att_fk_cache (class_obj, attr_list->info.name.original))
	{
	  PT_ERRORmf (parser, attr_list, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_CANT_ASSIGN_FK_CACHE_ATTR,
		      attr_list->info.name.original);
	  return ER_PT_SEMANTIC;
	}
      attr_list = attr_list->next;
    }

  return NO_ERROR;
}

/*
 * is_server_insert_allowed() - Checks to see if a server-side insert is
 *                              allowed
 *   return: Returns an error if any input nodes are
 *           misformed.
 *   parser(in): Parser context
 *   non_null_attrs(in/out): Parse tree for attributes with the NOT NULL
 *                           constraint
 *   has_uniques(in/out):
 *   server_allowed(in/out): Boolean flag
 *   statement(in): Parse tree of a insert statement
 *   values_list(in): The list of values to insert.
 *   class(in): Parse tree of the target class
 *
 *
 */
static int
is_server_insert_allowed (PARSER_CONTEXT * parser,
			  PT_NODE ** non_null_attrs,
			  int *has_uniques, int *server_allowed,
			  const PT_NODE * statement,
			  const PT_NODE * values_list, const PT_NODE * class_)
{
  int error = NO_ERROR;
  int trigger_involved;
  PT_NODE *attrs, *attr;
  PT_NODE *vals, *val;
  /* set lock timeout hint if specified */
  int server_preference;

  *server_allowed = 0;

  attrs = statement->info.insert.attr_list;
  vals = NULL;
  /* server insert cannot handle insert into a shared attribute */
  attr = attrs;
  while (attr)
    {
      if (attr->node_type != PT_NAME)
	{
	  /* this can occur when inserting into a view with default values.
	   * The name list may not be inverted, and may contain expressions,
	   * such as (x+2).
	   */
	  return error;
	}
      if (attr->info.name.meta_class != PT_NORMAL)
	{
	  /* We found a shared attribute, bail out */
	  return error;
	}
      attr = attr->next;
    }

  error = insert_check_for_fk_cache_attr (parser, attrs,
					  class_->info.name.db_object);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = check_for_cons (parser,
			  has_uniques, non_null_attrs,
			  attrs, class_->info.name.db_object);

  if (error != NO_ERROR)
    {
      return error;
    }

  server_preference = prm_get_integer_value (PRM_ID_INSERT_MODE);

  if (statement->info.insert.hint & PT_HINT_INSERT_MODE)
    {
      PT_NODE *mode = statement->info.insert.insert_mode;
      if (mode && mode->node_type == PT_NAME)
	{
	  server_preference = atoi (mode->info.name.original);
	}
    }

  /* check the insert form against the preference */
  if (statement->info.insert.do_replace)
    {
      if (!(server_preference & INSERT_REPLACE))
	{
	  return error;
	}
    }
  if (statement->info.insert.on_dup_key_update)
    {
      if (!(server_preference & INSERT_ON_DUP_KEY_UPDATE))
	{
	  return error;
	}
    }
  if (values_list->info.node_list.list_type == PT_IS_SUBQUERY)
    {
      if (!(server_preference & INSERT_SELECT))
	{
	  return error;
	}
    }
  else if (values_list->info.node_list.list_type == PT_IS_VALUE)
    {
      vals = values_list->info.node_list.list;
      if (!(server_preference & INSERT_VALUES))
	{
	  return error;
	}
    }
  else if (values_list->info.node_list.list_type == PT_IS_DEFAULT_VALUE)
    {
      if (!(server_preference & INSERT_DEFAULT))
	{
	  return error;
	}
    }
  else
    {
      assert (false);
    }

  val = vals;
  while (val)
    {
      /* pt_to_regu in parser_generate_xasl can't handle insert
         because it has not been taught to. Its feasible
         for this to call pt_to_insert_xasl with a bit of work.
       */
      if (val->node_type == PT_INSERT)
	{
	  return error;
	}
      /* pt_to_regu in parser_generate_xasl can't handle subquery
         because the context isn't set up. Again its feasible
         with some work.
       */
      if (PT_IS_QUERY_NODE_TYPE (val->node_type))
	{
	  return error;
	}
      val = val->next;
    }

  if (statement->info.insert.into_var != NULL)
    {
      return error;
    }

  /* check option could be done on the server by adding another predicate
     to the insert_info block. However, one must also take care that
     subqueries in this predicate have their xasl blocks properly
     attached to the insert xasl block. Currently, pt_to_pred
     will do that only if being called from parser_generate_xasl.
     This may mean that do_server_insert should call parser_generate_xasl,
     and have a portion of its code put.
   */
  if (statement->info.insert.where != NULL)
    {
      return error;
    }

  if (statement->info.insert.do_replace && *has_uniques)
    {
      error = sm_class_has_triggers (class_->info.name.db_object,
				     &trigger_involved, TR_EVENT_DELETE);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (trigger_involved)
	{
	  return error;
	}
    }

  if (statement->info.insert.on_dup_key_update != NULL && *has_uniques)
    {
      error = sm_class_has_triggers (class_->info.name.db_object,
				     &trigger_involved, TR_EVENT_UPDATE);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (trigger_involved)
	{
	  return error;
	}
    }

  error = sm_class_has_triggers (class_->info.name.db_object,
				 &trigger_involved, TR_EVENT_INSERT);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* Even if unique indexes are defined on the class,
     the operation could be performed on server.
   */
  if (!trigger_involved)
    {
      *server_allowed = 1;
    }

  return error;
}

/*
 * build_expression_from_assignment() -
 *
 *
 *   return: expression or NULL
 *
 *   parser(in)	   : parser context
 *   spec (in)	   : the spec for this template
 *   assignment(in): template assignment
 *
 * Notes: This function creates a PT_NODE corresponding to the predicate
 *	  argument = value. This is used as part of the WHERE clause of a
 *	  SELECT statement that checks for unique constraint violations.
 */

static PT_NODE *
build_expression_from_assignment (PARSER_CONTEXT * parser, PT_NODE * spec,
				  OBJ_TEMPASSIGN * assignment)
{
  PT_NODE *expr = NULL;
  PT_NODE *arg = NULL;
  PT_NODE *val = NULL;

  assert (assignment != NULL);
  assert (assignment->att != NULL);
  assert (assignment->variable != NULL);

  arg = pt_name (parser, assignment->att->header.name);
  if (arg == NULL)
    {
      goto error_return;
    }

  arg->info.name.spec_id = spec->info.spec.id;
  arg->info.name.meta_class = PT_NORMAL;
  arg->info.name.resolved = spec->info.spec.range_var->info.name.original;

  arg->data_type = pt_domain_to_data_type (parser, assignment->att->domain);
  if (arg->data_type == NULL)
    {
      goto error_return;
    }

  arg->type_enum = arg->data_type->type_enum;

  if (assignment->variable != NULL)
    {
      val = pt_dbval_to_value (parser, assignment->variable);
    }

  if (val == NULL)
    {
      goto error_return;
    }

  expr = pt_expression_2 (parser, PT_EQ, arg, val);
  if (expr == NULL)
    {
      goto error_return;
    }

  return expr;

error_return:
  if (arg != NULL)
    {
      parser_free_tree (parser, arg);
      arg = NULL;
    }
  if (val != NULL)
    {
      parser_free_tree (parser, val);
      val = NULL;
    }
  if (expr != NULL)
    {
      parser_free_tree (parser, expr);
      expr = NULL;
    }
  return NULL;
}

/*
 * build_predicate_for_unique_constraint()
 *			      - Builds a predicate that checks if an inserted
 *				object would violate an unique constraint
 *
 *   return: NO_ERROR if successful, error otherwise
 *
 *   parser(in)	: Parser context
 *   spec (in)	: the spec into which the insert statement will insert
 *   templ (in)	: the template to be inserted
 *   constraint(in) : an unique constraint for which to generate the predicate
 *   predicate (in/out) : a predicate of the form attr1=val1 AND attr2=val2...
 *
 * Notes: The object template is already populated. If there are unique
 *	  constraints for which an argument value is not supplied
 *	  it cannot be violated by the template insert because those values
 *	  will be NULL, and NULL values don't generate unique key violations.
 */
static int
build_predicate_for_unique_constraint (PARSER_CONTEXT * parser,
				       PT_NODE * spec, DB_OTMPL * templ,
				       SM_CLASS_CONSTRAINT * constraint,
				       PT_NODE ** predicate)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE **db_attribute = NULL;
  PT_NODE *pred = NULL;
  PT_NODE *expr = NULL;

  assert (*predicate == NULL);
  assert (SM_IS_CONSTRAINT_UNIQUE_FAMILY (constraint->type));

  for (db_attribute = constraint->attributes; *db_attribute; db_attribute++)
    {
      char found = 0;
      int i = 0;
      OBJ_TEMPASSIGN *assignment = templ->assignments[(*db_attribute)->order];

      if (assignment == NULL)
	{
	  /* this constraint will not be violated */
	  error = NO_ERROR;
	  goto cleanup;
	}

      if (assignment->variable == NULL)
	{
	  /* this constraint cannot be violated */
	  error = NO_ERROR;
	  goto cleanup;
	}

      expr = build_expression_from_assignment (parser, spec, assignment);
      if (expr == NULL)
	{
	  parser_free_tree (parser, pred);
	  *predicate = NULL;
	  error = er_errid ();
	  goto cleanup;
	}
      if (pred == NULL)
	{
	  pred = expr;
	}
      else
	{
	  pred = pt_expression_2 (parser, PT_AND, pred, expr);
	}
    }

  *predicate = pred;
  return error;

cleanup:
  if (pred)
    {
      parser_free_tree (parser, pred);
    }
  *predicate = NULL;
  return error;
}

/*
 * build_select_statement() - Builds a select statement used for checking
 *			      unique constraint violations
 *   return: error code
 *
 *   parser(in)	s     : Parser context
 *   spec(in)	      : the spec used for insert
 *   class_specs(in)  :
 *   attributes(in)   : the list of inserted attributes
 *   values(in)	      : the list of values assigned to attributes
 *   statement(in/out):	the select statement, will be set to NULL if unique
 *                      key violations are not possible
 *   for_update(in): true if query is used in update operation
 *
 * Notes: This function builds the parse tree for a SELECT statement that
 *	  returns a list of OIDs that would cause a unique constraint
 *	  violation. This function is used for the broker-side execution of
 *	  "insert .. on duplicate key update" and "replace" statements.
 *	  Example:
 *	  for table t(i int , str string, d date) with unique
 *	  indexes t(i) and t(str,d),
 *	  INSERT INTO t(i,str,d) VALUES(1,'1','01-01-01') ON DUPLICATE KEY
 *	      UPDATE i = 2;
 *	  builds the SELECT statement:
 *	  SELECT t FROM t WHERE i = 1 UNION 
 *        SELECT t FROM t WHERE (str = '1' AND d = '01-01-01');
 */
static int
build_select_statement (PARSER_CONTEXT * parser, DB_OTMPL * tmpl,
			PT_NODE * spec, PT_NODE * class_specs,
			PT_NODE ** statement, bool for_update)
{
  PT_NODE *where = NULL, *stmt = NULL;
  PT_NODE *union_stmt = NULL, *arg1 = NULL, *arg2 = NULL;
  SM_CLASS_CONSTRAINT *constraint = NULL;
  SM_CLASS *class_ = NULL;
  int error = NO_ERROR;

  assert (parser != NULL);

  class_ = tmpl->class_;
  assert (class_ != NULL);

  assert (*statement == NULL);
  *statement = NULL;

  /* Populate the defaults and auto_increment values here because we need them
   * when building the WHERE clause for the SELECT statement. These values
   * will not be reassigned if the template will eventually be inserted as
   * they are already populated.
   */
  error = obt_populate_known_arguments (tmpl);
  if (error != NO_ERROR)
    {
      goto error_cleanup;
    }

  /* build the WHERE predicate:
   * The WHERE predicate checks to see if any constraints would be violated
   * by the insert statement by checking new values against the values already
   * stored.
   */
  for (constraint = class_->constraints; constraint != NULL;
       constraint = constraint->next)
    {
      if (!SM_IS_CONSTRAINT_UNIQUE_FAMILY (constraint->type))
	{
	  continue;
	}

      where = NULL;
      error = build_predicate_for_unique_constraint (parser, spec, tmpl,
						     constraint, &where);
      if (error != NO_ERROR)
	{
	  goto error_cleanup;
	}

      if (where == NULL)
	{
	  continue;
	}

      /* create a select statement */
      stmt = parser_new_node (parser, PT_SELECT);
      if (stmt != NULL)
	{
	  stmt->info.query.q.select.from = parser_copy_tree_list (parser,
								  spec);
	  /* add in the class specs to the spec list */
	  stmt->info.query.q.select.from =
	    parser_append_node (parser_copy_tree_list (parser, class_specs),
				stmt->info.query.q.select.from);

	  stmt->info.query.q.select.where = where;

	  /* will add the class and instance OIDs later */

	  if (arg1 == NULL)
	    {
	      arg1 = stmt;
	    }
	  else
	    {
	      arg2 = stmt;
	    }
	}
      else
	{
	  error = er_errid ();
	  goto error_cleanup;
	}

      if (arg1 && arg2)
	{
	  stmt = arg1 = pt_union (parser, arg1, arg2);
	  if (stmt == NULL)
	    {
	      error = er_errid ();
	      goto error_cleanup;
	    }
	  stmt->info.query.all_distinct = PT_DISTINCT;

	  arg2 = NULL;
	}
    }

  if (stmt == NULL)
    {
      /* we didn't find any constraint that could be violated */
      return error;
    }

  /* add the class and instance OIDs to the select list */
  stmt = pt_add_row_classoid_name (parser, stmt, 0);
  stmt = pt_add_row_oid_name (parser, stmt);

  stmt = mq_reset_ids_in_statement (parser, stmt);

  if (for_update)
    {
      stmt->info.query.composite_locking = PT_COMPOSITE_LOCKING_UPDATE;
    }
  else
    {
      stmt->info.query.composite_locking = PT_COMPOSITE_LOCKING_DELETE;
    }

  *statement = stmt;

  return error;

error_cleanup:
  if (stmt != NULL)
    {
      parser_free_tree (parser, stmt);
    }

  return error;
}

/*
 * do_on_duplicate_key_update() - runs an update statement instead of an
 *                                insert statement for the cases in which
 *                                the insert would cause a unique constraint
 *                                violation
 *   return: The number of affected rows if successful, error code otherwise
 *
 *   parser(in)	      : Parser context
 *   tmpl(in)	      : object template to be inserted
 *   spec(in)	      : the spec used for insert
 *   class_specs(in)  :
 *   update_stmt(in)  : the update statement to run if there are unique
 *			constraint violations
 *
 * Notes: This function creates a SELECT statement based on the values
 *        supplied in the template object and runs update_stmt for one
 *        of the OIDs with which tmpl would generate unique key conflicts.
 *	  If this function returns 0 then no rows were updated and the caller
 *	  should proceed with the insert.
 */
static int
do_on_duplicate_key_update (PARSER_CONTEXT * parser, DB_OTMPL * tmpl,
			    PT_NODE * spec, PT_NODE * class_specs,
			    PT_NODE * update_stmt)
{
  int retval = NO_ERROR;
  int ret_code = 0;
  PT_NODE *select = NULL;
  CURSOR_ID cursor_id;
  DB_VALUE oid;
  int clear_cursor = 0;

  assert (update_stmt->node_type == PT_UPDATE);
  assert (update_stmt->info.update.object == NULL);

  DB_MAKE_NULL (&oid);

  retval = build_select_statement (parser, tmpl, spec, class_specs, &select,
				   true);
  if (retval != NO_ERROR)
    {
      goto error_cleanup;
    }
  if (select == NULL)
    {
      /* we didn't find any constraint that would be violated */
      retval = 0;
      goto error_cleanup;
    }

  retval = do_select (parser, select);
  if (retval < NO_ERROR || select->etc == NULL)
    {
      goto error_cleanup;
    }

  if (!cursor_open (&cursor_id, (QFILE_LIST_ID *) select->etc, false, true))
    {
      retval = ER_GENERIC_ERROR;
      goto error_cleanup;
    }

  cursor_id.query_id = parser->query_id;
  retval = 0;
  ret_code = cursor_next_tuple (&cursor_id);

  if (ret_code != DB_CURSOR_END && ret_code != DB_CURSOR_SUCCESS)
    {
      retval = ER_GENERIC_ERROR;
      clear_cursor = 1;
      goto error_cleanup;
    }

  if (ret_code == DB_CURSOR_SUCCESS)
    {
      /* create a backup of the search condition */
      PT_NODE *search_cond = NULL;

      ret_code = cursor_get_current_oid (&cursor_id, &oid);
      if (ret_code != DB_CURSOR_SUCCESS)
	{
	  clear_cursor = 1;
	  retval = ER_GENERIC_ERROR;
	}

      /* ignore the search condition because we use an instance update that
         does not expect a where clause to be present */
      search_cond = update_stmt->info.update.search_cond;
      update_stmt->info.update.search_cond = NULL;
      update_stmt->info.update.object = oid.data.op;

      ret_code = update_object_by_oid (parser, update_stmt,
				       ON_DUPLICATE_KEY_UPDATE);

      /* restore initial update parse tree */
      update_stmt->info.update.object = NULL;
      update_stmt->info.update.search_cond = search_cond;
      search_cond = NULL;

      if (ret_code < NO_ERROR)
	{
	  clear_cursor = 1;
	  retval = ret_code;
	  goto error_cleanup;
	}

      /* one updated row */
      retval = 1;
    }

  /* clear modifications to update_stmt */
  update_stmt->info.update.object = NULL;

  cursor_close (&cursor_id);
  regu_free_listid ((QFILE_LIST_ID *) select->etc);
  parser_free_tree (parser, select);
  db_value_clear (&oid);

  pt_end_query (parser);

  return retval;

error_cleanup:
  /* reset object information */
  update_stmt->info.update.object = NULL;
  if (clear_cursor)
    {
      cursor_close (&cursor_id);
      regu_free_listid ((QFILE_LIST_ID *) select->etc);
    }
  if (!DB_IS_NULL (&oid))
    {
      db_value_clear (&oid);
    }
  if (select != NULL)
    {
      parser_free_tree (parser, select);
    }

  pt_end_query (parser);

  return retval;
}

/*
 * do_replace_into() - runs an delete statement for the cases in
 *                     which INSERT would cause a unique constraint violation
 *
 *   return: The number of affected rows if successful, error code otherwise
 *
 *   parser(in)	      : Parser context
 *   tmpl(in)	      : object template to be inserted
 *   spec(in)	      : the spec used for insert
 *   class_specs(in)  :
 *
 * Notes: This function creates a SELECT statement based on the values
 *        supplied in the template object and runs delete for the
 *        OIDs with which tmpl would generate unique key violations
 */

static int
do_replace_into (PARSER_CONTEXT * parser, DB_OTMPL * tmpl,
		 PT_NODE * spec, PT_NODE * class_specs)
{
  int retval = NO_ERROR;
  PT_NODE *select = NULL;
  int au_save = 0;

  retval = build_select_statement (parser, tmpl, spec, class_specs, &select,
				   false);
  if (retval != NO_ERROR)
    {
      goto cleanup;
    }

  if (select == NULL)
    {
      /* we didn't find any constraint that could be violated */
      retval = NO_ERROR;
      goto cleanup;
    }

  retval = do_select (parser, select);
  if (retval < NO_ERROR)
    {
      goto cleanup;
    }

  if (select->etc)
    {
      AU_SAVE_AND_DISABLE (au_save);
      retval = delete_list_by_oids (parser, (QFILE_LIST_ID *) select->etc);
      AU_RESTORE (au_save);
      regu_free_listid ((QFILE_LIST_ID *) select->etc);
    }

  pt_end_query (parser);

cleanup:
  if (select != NULL)
    {
      parser_free_tree (parser, select);
    }
  pt_end_query (parser);

  return retval;
}

/*
 * is_replace_or_odku_allowed() - checks to see if the class is partitioned or
 *				  part of an inheritance chain
 *
 *   return: error code if unsuccessful, NO_ERROR otherwise
 *
 *   obj(in)	      : object to be checked
 *   allowed(out)     : 0 if not allowed, 1 if allowed
 *
 */

static int
is_replace_or_odku_allowed (DB_OBJECT * obj, int *allowed)
{
  int error = NO_ERROR;
  SM_CLASS *smclass = NULL;

  /* TODO This check should be modified when the partition code is fixed.
   *      We cannot currently distinguish between a partitioned class
   *      and a class in an inheritance chain (i.e. with *real* subclasses)
   */

  *allowed = 1;
  error = au_fetch_class (obj, &smclass, AU_FETCH_READ, AU_SELECT);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (smclass->inheritance != NULL || smclass->users != NULL)
    {
      *allowed = 0;
    }

  return error;
}

/*
 * do_insert_template() - Inserts an object or row into an object template
 *   return: Error code
 *   parser(in): Short description of the param1
 *   otemplate(in/out): class template to be inserted
 *   statement(in): Parse tree of an insert statement
 *   values_list(in): The list of values to insert.
 *   savepoint_name(in):
 *   is_first_value(in): True if the first value of VALUE clause. This will be
 *                       used for server-side insertion.
 */
int
do_insert_template (PARSER_CONTEXT * parser, DB_OTMPL ** otemplate,
		    PT_NODE * statement, PT_NODE * values_list,
		    const char **savepoint_name, int *row_count_ptr,
		    bool is_first_value)
{
  const char *into_label;
  DB_VALUE *ins_val, *val, db_value;
  int error = NO_ERROR;
  PT_NODE *attr, *vc;
  PT_NODE *into;
  PT_NODE *class_;
  PT_NODE *non_null_attrs = NULL;
  PT_NODE *upd_non_null_attrs = NULL;
  PT_NODE *default_expr_attrs = NULL;
  DB_ATTDESC **attr_descs = NULL;
  int i, degree, row_count = 0;
  int server_allowed = 0;
  int has_uniques = 0;
  int upd_has_uniques = 0;
  int wait_msecs = -2, old_wait_msecs = -2;
  float hint_waitsecs;
  PT_NODE *hint_arg;
  SM_CLASS *smclass = NULL;

  db_make_null (&db_value);

  if (row_count_ptr != NULL)
    {
      *row_count_ptr = 0;
    }

  degree = 0;
  class_ = statement->info.insert.spec->info.spec.flat_entity_list;

  /* clear any previous error indicator because the
     rest of do_insert is sensitive to er_errid(). */
  er_clear ();

  if (!locator_fetch_class (class_->info.name.db_object,
			    DB_FETCH_CLREAD_INSTWRITE))
    {
      return er_errid ();
    }

  if (statement->info.insert.do_replace
      || statement->info.insert.on_dup_key_update != NULL)
    {
      /* Check to see if the class into which we are inserting is part of
         an inheritance chain. We do not allow these statements to be executed
         in these cases as we might have undefined behavior, such as trying
         to update a column that belongs to a child for a duplicate key in the
         parent table that does not have that column.
       */
      /* TODO This check should be modified when the partition code is fixed.
       *      We cannot currently distinguish between a partitioned class
       *      and a class in an inheritance chain (i.e. with *real*
       *      subclasses)
       */
      int allowed = 0;
      error =
	is_replace_or_odku_allowed (class_->info.name.db_object, &allowed);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (!allowed)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_REPLACE_ODKU_NOT_ALLOWED, 0);
	  return er_errid ();
	}
    }

  error = is_server_insert_allowed (parser, &non_null_attrs,
				    &has_uniques, &server_allowed,
				    statement, values_list, class_);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (statement->info.insert.on_dup_key_update != NULL && server_allowed)
    {
      PT_NODE *upd_statement = statement->info.insert.on_dup_key_update;

      server_allowed = 0;
      error = is_server_update_allowed (parser, &upd_non_null_attrs,
					&upd_has_uniques, &server_allowed,
					upd_statement);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (prm_get_bool_value (PRM_ID_HOSTVAR_LATE_BINDING) &&
	  ((prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES) <= 0)
	   || statement->cannot_prepare))
	{
	  server_allowed = 0;
	}
    }

  if (server_allowed)
    {
      error =
	check_for_default_expr (parser, statement->info.insert.attr_list,
				&default_expr_attrs,
				class_->info.name.db_object);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
      error = do_insert_at_server (parser, statement, values_list,
				   non_null_attrs, &upd_non_null_attrs,
				   has_uniques, upd_has_uniques,
				   default_expr_attrs, is_first_value);
      if (error >= 0)
	{
	  row_count = error;
	}
    }
  else if (values_list->info.node_list.list_type == PT_IS_SUBQUERY
	   && (vc = values_list->info.node_list.list) != NULL)
    {
      /* execute subquery & insert its results into target class */
      row_count =
	insert_subquery_results (parser, statement, values_list, class_,
				 savepoint_name);
      error = (row_count < 0) ? row_count : NO_ERROR;
    }
  else if (values_list->info.node_list.list_type == PT_IS_VALUE
	   || values_list->info.node_list.list_type == PT_IS_DEFAULT_VALUE)
    {
      /* there is one value to insert into target class
         If we are doing PT_IS_DEFAULT_VALUE value_clause
         will be NULL and we will only make a template with
         no values put in. */
      row_count = 1;


      /* now create the object using templates, and then dbt_put
         each value for each corresponding attribute.
         Of course, it is presumed that
         the order in which attributes are defined in the class as
         well as in the actual insert statement is preserved. */
      *otemplate = dbt_create_object_internal (class_->info.name.db_object);
      if (*otemplate == NULL)
	{
	  error = er_errid ();
	  goto cleanup;
	}

      if (sm_is_partitioned_class (class_->info.name.db_object))
	{
	  /* The reason we're forcing a flush here is to throw an error
	   * if the object does belong to any partition. If we don't do it
	   * here, the error will be thrown when the object is flushed either
	   * by the next statement or by a commit/rollback call. However,
	   * there cases when we don't need to do this. Hash partitioning 
	   * algorithm guarantees that there always is a partition for each
	   * record and range partitioning using maxvalue/minvalue does the
	   * same. This flushing should be refined
	   */
	  obt_set_force_flush (*otemplate);
	}
      vc = values_list->info.node_list.list;
      attr = statement->info.insert.attr_list;
      degree = pt_length_of_list (attr);

      /* allocate attribute descriptors */
      if (attr)
	{
	  attr_descs = (DB_ATTDESC **) calloc (degree, sizeof (DB_ATTDESC *));
	  if (attr_descs == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      (degree * sizeof (DB_ATTDESC *)));
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto cleanup;
	    }
	}

      hint_arg = statement->info.insert.waitsecs_hint;
      if (statement->info.insert.hint & PT_HINT_LK_TIMEOUT
	  && PT_IS_HINT_NODE (hint_arg))
	{
	  hint_waitsecs = (float) atof (hint_arg->info.name.original);
	  if (hint_waitsecs > 0)
	    {
	      wait_msecs = (int) (hint_waitsecs * 1000);
	    }
	  else
	    {
	      wait_msecs = (int) hint_waitsecs;
	    }
	  if (wait_msecs >= -1)
	    {
	      old_wait_msecs = TM_TRAN_WAIT_MSECS ();

	      (void) tran_reset_wait_times (wait_msecs);
	    }
	}
      i = 0;
      while (attr && vc)
	{
	  if (vc->node_type == PT_INSERT && *savepoint_name == NULL)
	    {
	      /* this is a nested insert.  recurse to create object
	         template for the nested insert. */
	      DB_OTMPL *temp = NULL;

	      assert (vc->info.insert.value_clauses != NULL);
	      if (vc->info.insert.value_clauses->next != NULL)
		{
		  error = ER_DO_INSERT_TOO_MANY;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  break;
		}
	      error = do_insert_template (parser, &temp, vc,
					  vc->info.insert.value_clauses,
					  savepoint_name, NULL, true);
	      if (error >= NO_ERROR)
		{
		  if (!vc->info.insert.spec)
		    {
		      /* guard against seg fault for bad parse tree */
		      PT_INTERNAL_ERROR (parser, "insert");
		    }
		}
	      if (error < NO_ERROR)
		{
		  break;
		}

	      db_make_pointer (&db_value, temp);
	    }
	  else
	    {
	      pt_evaluate_tree_having_serial (parser, vc, &db_value, 1);
	      if (pt_has_error (parser))
		{
		  (void) pt_report_to_ersys (parser, PT_EXECUTION);
		  error = er_errid ();
		  db_value_clear (&db_value);
		  break;
		}
	    }

	  /* don't get descriptors for shared attrs of views */
	  if (!attr->info.name.db_object
	      || !db_is_vclass (attr->info.name.db_object))
	    {
	      error =
		db_get_attribute_descriptor (class_->info.name.db_object,
					     attr->info.name.original, 0,
					     1, &attr_descs[i]);
	    }
	  if (error >= NO_ERROR)
	    {
	      error =
		insert_object_attr (parser, *otemplate, &db_value,
				    attr, attr_descs[i]);
	    }
	  /* else vc is a SELECT query whose result is empty */

	  /* pt_evaluate_tree() always clones the db_value.
	     Thus we must clear it.
	   */
	  db_value_clear (&db_value);

	  if (!pt_has_error (parser))
	    {
	      if (error < NO_ERROR)
		{
		  PT_ERRORmf3 (parser, vc, MSGCAT_SET_PARSER_RUNTIME,
			       MSGCAT_RUNTIME_DBT_PUT_ERROR,
			       pt_short_print (parser, vc),
			       attr->info.name.original,
			       pt_chop_trailing_dots (parser,
						      db_error_string (3)));
		}
	    }

	  attr = attr->next;
	  vc = vc->next;
	  i++;
	}
      if (old_wait_msecs >= -1)
	{
	  (void) tran_reset_wait_times (old_wait_msecs);
	}
    }

  if (non_null_attrs != NULL)
    {
      parser_free_tree (parser, non_null_attrs);
      non_null_attrs = NULL;
    }

  if (upd_non_null_attrs != NULL)
    {
      parser_free_tree (parser, upd_non_null_attrs);
      upd_non_null_attrs = NULL;
    }

  if ((error >= NO_ERROR)
      && (into = statement->info.insert.into_var) != NULL
      && into->node_type == PT_NAME
      && (into_label = into->info.name.original) != NULL)
    {
      /* check to see if more than one instance was inserted */
      if (row_count > 1)
	{
	  error = ER_DO_INSERT_TOO_MANY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else
	{
	  /* create another DB_VALUE of the new instance for
	     the label_table
	   */
	  ins_val = db_value_create ();
	  if (ins_val == NULL)
	    {
	      error = er_errid ();
	      goto cleanup;
	    }
	  db_make_object (ins_val, (DB_OBJECT *) NULL);
	  if (values_list->info.node_list.list_type == PT_IS_VALUE
	      || values_list->info.node_list.list_type == PT_IS_DEFAULT_VALUE)
	    {

	      error = dbt_set_label (*otemplate, ins_val);
	    }
	  else if ((val = (DB_VALUE *) (statement->etc)) != NULL)
	    {
	      db_make_object (ins_val, DB_GET_OBJECT (val));
	    }

	  if (error == NO_ERROR)
	    {
	      /* enter {label, ins_val} pair into the label_table */
	      error =
		pt_associate_label_with_value_check_reference (into_label,
							       ins_val);
	    }
	}
    }

  if (error < NO_ERROR)
    {
      goto cleanup;
    }

  if ((*otemplate) != NULL && statement->info.insert.on_dup_key_update)
    {
      error =
	do_on_duplicate_key_update (parser, *otemplate,
				    statement->info.insert.spec,
				    statement->info.insert.class_specs,
				    statement->info.insert.on_dup_key_update);
      if (error < 0)
	{
	  goto cleanup;
	}
      else if (error > 0)
	{
	  /* a successful update, go to finish */
	  row_count += error;
	  dbt_abort_object (*otemplate);
	  *otemplate = NULL;
	  error = NO_ERROR;
	}
      else
	{			/* error == 0 */
	  int level;
	  qo_get_optimization_param (&level, QO_PARAM_LEVEL);
	  if (level & 0x02)
	    {
	      /* do not execute, go to finish */
	      row_count = 0;
	      dbt_abort_object (*otemplate);
	      *otemplate = NULL;
	      error = NO_ERROR;
	    }
	}
    }
  if ((*otemplate) != NULL && statement->info.insert.do_replace)
    {
      error = do_replace_into (parser, *otemplate,
			       statement->info.insert.spec,
			       statement->info.insert.class_specs);
      if (error > 0)
	{
	  row_count += error;
	}
    }

cleanup:
  /* free attribute descriptors */
  if (attr_descs)
    {
      for (i = 0; i < degree; i++)
	{
	  if (attr_descs[i])
	    {
	      db_free_attribute_descriptor (attr_descs[i]);
	    }
	}
      free_and_init (attr_descs);
    }

  if (row_count_ptr != NULL)
    {
      *row_count_ptr = row_count;
    }
  return error;
}

/*
 * insert_subquery_results() - Execute subquery & insert its results into
 *                                a target class
 *   return: Error code
 *   parser(in): Handle to the parser used to process & derive subquery qry
 *   statemet(in/out):
 *   values_list(in): The list of values to insert.
 *   class(in):
 *   savepoint_name(in):
 *
 * Note:
 *   The function requires parser is the handle to the parser used to derive qry
 *   qry is an error-free abstract syntax tree derived from a CUBRID
 *   nested SELECT, UNION, DIFFERENCE, INTERSECTION subquery.
 *   qry's select_list attributes have been expanded & type-checked.
 *   qry's select_list and attrs have the same number of elements.
 *   It modifies database state of target class and
 *   effects that executes subquery and inserts its results as new instances of
 *   target class.
 */
static int
insert_subquery_results (PARSER_CONTEXT * parser,
			 PT_NODE * statement, PT_NODE * values_list,
			 PT_NODE * class_, const char **savepoint_name)
{
  int error = NO_ERROR;
  CURSOR_ID cursor_id;
  DB_OTMPL *otemplate = NULL;
  DB_OBJECT *obj = NULL;
  PT_NODE *attr, *qry, *attrs;
  DB_VALUE *vals = NULL, *val = NULL, *ins_val = NULL;
  int degree, k, cnt, i;
  DB_ATTDESC **attr_descs = NULL;

  if (values_list == NULL || values_list->node_type != PT_NODE_LIST
      || values_list->info.node_list.list_type != PT_IS_SUBQUERY
      || (qry = values_list->info.node_list.list) == NULL
      || (statement->node_type != PT_INSERT
	  && statement->node_type != PT_MERGE))
    {
      return ER_GENERIC_ERROR;
    }
  attrs = statement->node_type == PT_MERGE
    ? statement->info.merge.insert.attr_list
    : statement->info.insert.attr_list;
  if (attrs == NULL)
    {
      return ER_GENERIC_ERROR;
    }

  cnt = 0;

  switch (qry->node_type)
    {
    default:			/* preconditions not met */
      return ER_GENERIC_ERROR;	/* so, nothing doing.    */

    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* execute the subquery */
      error = do_select (parser, qry);
      if (error < NO_ERROR || qry->etc == NULL)
	{
	  return error;
	}

      /* insert subquery results into target class */
      if (cursor_open (&cursor_id, (QFILE_LIST_ID *) qry->etc, false, false))
	{
	  cursor_id.query_id = parser->query_id;

	  /* allocate space for attribute values */
	  degree =
	    pt_length_of_select_list (pt_get_select_list (parser, qry),
				      EXCLUDE_HIDDEN_COLUMNS);
	  vals = (DB_VALUE *) malloc (degree * sizeof (DB_VALUE));
	  if (vals == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1,
		      degree * sizeof (DB_VALUE));
	      cnt = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto cleanup;
	    }

	  /* allocate attribute descriptor array */
	  if (degree)
	    {
	      attr_descs = (DB_ATTDESC **)
		malloc ((degree) * sizeof (DB_ATTDESC *));
	      if (attr_descs == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_OUT_OF_VIRTUAL_MEMORY, 1,
			  degree * sizeof (DB_ATTDESC *));
		  cnt = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto cleanup;
		}
	      for (i = 0; i < degree; i++)
		{
		  attr_descs[i] = NULL;
		}
	    }

	  /* if the list file contains more than 1 object we need to savepoint
	     the statement to guarantee statement atomicity. */
	  if (((QFILE_LIST_ID *) qry->etc)->tuple_cnt > 1 && !*savepoint_name)
	    {
	      *savepoint_name = mq_generate_name (parser, "UisP",
						  &insert_savepoint_number);
	      error = tran_savepoint (*savepoint_name, false);
	    }

	  if (error >= NO_ERROR)
	    {
	      /* for each tuple in subquery result do */
	      while (cursor_next_tuple (&cursor_id) == DB_CURSOR_SUCCESS)
		{
		  /* get current tuple of subquery result */
		  if (cursor_get_tuple_value_list (&cursor_id, degree, vals)
		      != NO_ERROR)
		    {
		      break;
		    }

		  /* create an instance of the target class using templates */
		  otemplate =
		    dbt_create_object_internal (class_->info.name.db_object);
		  if (otemplate == NULL)
		    {
		      break;
		    }
		  if (otemplate->class_->partition_of != NULL &&
		      otemplate->class_->users != NULL)
		    {
		      obt_set_force_flush (otemplate);
		    }
		  /* update new instance with current tuple of subquery result */
		  for (attr = attrs, val = vals, k = 0;
		       attr != NULL && k < degree;
		       attr = attr->next, val++, k++)
		    {
		      /* if this is the first tuple, get the attr descriptor */
		      if (attr_descs != NULL)
			{
			  if (attr_descs[k] == NULL)
			    {
			      /* don't get descriptors for shared attrs of views */
			      if (!attr->info.name.db_object
				  || !db_is_vclass (attr->info.
						    name.db_object))
				{
				  error = db_get_attribute_descriptor
				    (class_->info.name.db_object,
				     attr->info.name.original, 0, 1,
				     &attr_descs[k]);
				}
			    }
			}

		      if (error >= NO_ERROR)
			{
			  error = insert_object_attr (parser, otemplate,
						      val, attr,
						      attr_descs[k]);
			}

		      if (error < NO_ERROR)
			{
			  dbt_abort_object (otemplate);
			  cursor_close (&cursor_id);
			  cnt = er_errid ();
			  goto cleanup;
			}
		    }

		  if (statement->node_type == PT_INSERT
		      && statement->info.insert.on_dup_key_update)
		    {
		      error =
			do_on_duplicate_key_update (parser, otemplate,
						    statement->info.insert.
						    spec,
						    statement->info.insert.
						    class_specs,
						    statement->info.insert.
						    on_dup_key_update);
		      if (error < 0)
			{
			  /* there was an error, cleanup and return */
			  cursor_close (&cursor_id);
			  if (obj == NULL)
			    {
			      dbt_abort_object (otemplate);
			    }
			  cnt = error;
			  goto cleanup;
			}
		      if (error > 0)
			{
			  /* a successful update, go to finish */
			  cnt += error;
			  dbt_abort_object (otemplate);
			  otemplate = NULL;
			  error = NO_ERROR;
			}
		    }

		  if (statement->node_type == PT_INSERT
		      && statement->info.insert.do_replace)
		    {
		      error = do_replace_into (parser, otemplate,
					       statement->info.insert.spec,
					       statement->info.insert.
					       class_specs);
		      if (error < 0)
			{
			  cursor_close (&cursor_id);
			  if (obj == NULL)
			    {
			      dbt_abort_object (otemplate);
			    }
			  cnt = error;
			  goto cleanup;
			}

		      cnt += error;
		    }

		  if (otemplate != NULL)
		    {
		      /* apply the object template */
		      obj = dbt_finish_object (otemplate);

		      if (obj && error >= NO_ERROR)
			{
			  if (statement->node_type == PT_INSERT)
			    {
			      error =
				mq_evaluate_check_option (parser,
							  statement->
							  info.insert.where,
							  obj, class_);
			    }
			  else if (statement->node_type == PT_MERGE
				   && statement->info.merge.check_where)
			    {
			      error =
				mq_evaluate_check_option (parser,
							  statement->info.
							  merge.check_where->
							  info.check_option.
							  expr, obj, class_);
			    }
			}

		      if (obj == NULL || error < NO_ERROR)
			{
			  cursor_close (&cursor_id);
			  if (obj == NULL)
			    {
			      dbt_abort_object (otemplate);
			    }
			  cnt = er_errid ();
			  goto cleanup;
			}

		    }

		  /* keep track of how many we have inserted */
		  cnt++;
		}
	    }

	  cursor_close (&cursor_id);
	}
    }

cleanup:

  if (vals != NULL)
    {
      for (val = vals, k = 0; k < degree; val++, k++)
	{
	  db_value_clear (val);
	}
      free_and_init (vals);
    }

  if (attr_descs != NULL)
    {
      for (i = 0; i < degree; i++)
	{
	  if (attr_descs[i] != NULL)
	    {
	      db_free_attribute_descriptor (attr_descs[i]);
	    }
	}
      free_and_init (attr_descs);
    }

  regu_free_listid ((QFILE_LIST_ID *) qry->etc);
  pt_end_query (parser);

  return cnt;
}

/*
 * is_attr_not_in_insert_list() - Returns 1 if the name is not on the name_list,
 *              0 otherwise. name_list is assumed to be a list of PT_NAME nodes.
 *   return: Error code
 *   param1(out): Short description of the param1
 *   param2(in/out): Short description of the param2
 *   param2(in): Short description of the param3
 *
 * Note: If you feel the need
 */
static int
is_attr_not_in_insert_list (const PARSER_CONTEXT * parser,
			    PT_NODE * name_list, const char *name)
{
  PT_NODE *tmp;
  int not_on_list = 1;

  for (tmp = name_list; tmp != NULL; tmp = tmp->next)
    {
      if (intl_identifier_casecmp (tmp->info.name.original, name) == 0)
	{
	  not_on_list = 0;
	  break;
	}
    }

  return not_on_list;

}				/* is_attr_not_in_insert_list */

/*
 * check_missing_non_null_attrs() - Check to see that all attributes of
 *              the class that have a NOT NULL constraint AND have no default
 *              value are present in the inserts assign list.
 *   return: Error code
 *   parser(in):
 *   spec(in):
 *   attr_list(in):
 *   has_default_values_list(in): whether this statement is used to insert
 *                                default values
 */
static int
check_missing_non_null_attrs (const PARSER_CONTEXT * parser,
			      const PT_NODE * spec, PT_NODE * attr_list,
			      const bool has_default_values_list)
{
  DB_ATTRIBUTE *attr;
  DB_OBJECT *class_;
  int error = NO_ERROR;

  if (!spec || !spec->info.spec.entity_name
      || !(class_ = spec->info.spec.entity_name->info.name.db_object))
    {
      return ER_GENERIC_ERROR;
    }

  attr = db_get_attributes (class_);
  while (attr)
    {
      if (db_attribute_is_non_null (attr)
	  && db_value_is_null (db_attribute_default (attr))
	  && attr->default_value.default_expr == DB_DEFAULT_NONE
	  && (is_attr_not_in_insert_list (parser, attr_list,
					  db_attribute_name (attr))
	      || has_default_values_list)
	  && !(attr->flags & SM_ATTFLAG_AUTO_INCREMENT))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_OBJ_MISSING_NON_NULL_ASSIGN, 1,
		  db_attribute_name (attr));
	  error = ER_OBJ_MISSING_NON_NULL_ASSIGN;
	}
      attr = db_attribute_next (attr);
    }

  return error;
}

/*
 * make_vmops() -
 *   return: Error code
 *   parser(in): Short description of the param1
 *   node(in):
 *   arg(in/out):
 *   continue_walk(in/out):
 */
static PT_NODE *
make_vmops (PARSER_CONTEXT * parser, PT_NODE * node,
	    void *arg, int *continue_walk)
{
  DB_OBJECT **vobj = ((DB_OBJECT **) arg);
  DB_OBJECT *vclass_mop, *obj;
  const char *into_label;
  DB_VALUE *val;

  if (node->node_type != PT_INSERT)
    {
      return node;
    }

  /* make a virtual obj if it is a virtual class and has an into label */
  if (node->info.insert.into_var
      &&
      ((vclass_mop =
	node->info.insert.spec->info.spec.flat_entity_list->info.
	name.virt_object) != NULL))
    {
      into_label = node->info.insert.into_var->info.name.original;
      val = pt_find_value_of_label (into_label);
      if (val != NULL)
	{
	  obj = DB_GET_OBJECT (val);
	  *vobj = vid_build_virtual_mop (obj, vclass_mop);
	  /* change the label to point to the newly created vmop, we don't need
	     to call pt_associate_label_with_value here because we've directly
	     modified the value that has been installed in the table.
	   */
	  db_make_object (val, *vobj);
	}
    }
  else
    {
      *vobj = NULL;
    }

  return node;

}

/*
 * test_check_option() - Tests if we are inserting to a class through a view
 *                         with a check option.
 *   return: Error code
 *   parser(in): Parser context
 *   node(in): The PT_NAME node of a potential insert
 *   arg(in/out): Nonzero iff insert statement has a check option
 *   continue_walk(in/out):
 */
static PT_NODE *
test_check_option (PARSER_CONTEXT * parser, PT_NODE * node,
		   void *arg, int *continue_walk)
{
  int *found = (int *) arg;
  PT_NODE *class_;
  DB_OBJECT *view;

  if (node->node_type != PT_INSERT || !node->info.insert.spec)
    {
      return node;
    }

  /* make a virtual obj if it is a virtual class and has an into label */
  class_ = node->info.insert.spec->info.spec.flat_entity_list;
  view = class_->info.name.virt_object;
  if (view)
    {
      if (sm_get_class_flag (view, SM_CLASSFLAG_WITHCHECKOPTION)
	  || sm_get_class_flag (view, SM_CLASSFLAG_LOCALCHECKOPTION))
	{
	  *found = 1;
	  *continue_walk = PT_STOP_WALK;
	}
    }

  return node;

}

/*
 * insert_local() - Inserts an object or row
 *   return: Error code if insert fails
 *   parser(in): Parser context
 *   statement(in): The parse tree of a insert statement
 *
 * Note:
 *   The function requires flat is the PT_NAME node of an
 *   insert/update/delete spec's flat_entity_list.
 *   It modifies has_trigger and effects that determine if flat's
 *   target class is a proxy that has an active trigger
 */
static int
insert_local (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  DB_OBJECT *obj, *vobj;
  int error = NO_ERROR;
  int row_count_total = 0;
  PT_NODE *class_, *vc;
  DB_VALUE *ins_val;
  int save;
  int has_check_option = 0;
  const char *savepoint_name = NULL;
  PT_NODE *crt_list = NULL;
  bool has_default_values_list = false;
  bool is_multiple_tuples_insert = false;
  bool need_savepoint = false;
  int has_trigger = 0;
  bool is_trigger_involved = false;

  if (!statement
      || statement->node_type != PT_INSERT
      || !statement->info.insert.spec
      || !statement->info.insert.spec->info.spec.flat_entity_list)
    {
      return ER_GENERIC_ERROR;
    }

  class_ = statement->info.insert.spec->info.spec.flat_entity_list;

  statement->etc = NULL;

  for (crt_list = statement->info.insert.value_clauses,
       has_default_values_list = false;
       crt_list != NULL; crt_list = crt_list->next)
    {
      if (crt_list->info.node_list.list_type == PT_IS_DEFAULT_VALUE)
	{
	  has_default_values_list = true;
	  break;
	}
    }

  error = do_evaluate_default_expr (parser, class_);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = check_missing_non_null_attrs (parser, statement->info.insert.spec,
					statement->info.insert.attr_list,
					has_default_values_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  crt_list = statement->info.insert.value_clauses;
  if (crt_list->next != NULL)
    {
      is_multiple_tuples_insert = true;
    }

  if (crt_list->info.node_list.list_type == PT_IS_SUBQUERY
      && (vc = crt_list->info.node_list.list) && pt_false_where (parser, vc))
    {
      /* 0 tuples inserted. */
      return 0;
    }

  /*
   * It is necessary to add savepoint in the cases as below.
   *
   * 1. when multiple tuples were inserted (ex: insert into ... values(), (), ();)
   * 2. the REPLACE statement (ex: replace into ... values ..;)
   * 3. view having 'with check option'
   * 4. class/view having trigger
   */

  if (is_multiple_tuples_insert == true
      || statement->info.insert.do_replace == true)
    {
      need_savepoint = true;
    }

  if (need_savepoint == false)
    {
      statement = parser_walk_tree (parser, statement, NULL, NULL,
				    test_check_option, &has_check_option);
      if (has_check_option)
	{
	  need_savepoint = true;
	}
    }

  /* DO NOT RETURN UNTIL AFTER AU_ENABLE! */
  AU_DISABLE (save);
  parser->au_save = save;

  if (need_savepoint == false
      && statement->info.insert.on_dup_key_update != NULL)
    {
      has_trigger = 0;
      error = sm_class_has_triggers (class_->info.name.db_object,
				     &has_trigger, TR_EVENT_UPDATE);
      if (error != NO_ERROR)
	{
	  AU_ENABLE (save);
	  return error;
	}
      if (has_trigger != 0)
	{
	  need_savepoint = true;
	}
    }

  if (need_savepoint == false)
    {
      has_trigger = 0;
      error = sm_class_has_triggers (class_->info.name.db_object,
				     &has_trigger, TR_EVENT_INSERT);
      if (error != NO_ERROR)
	{
	  AU_ENABLE (save);
	  return error;
	}
      if (has_trigger != 0)
	{
	  need_savepoint = true;
	}
    }

  /*
   *  if the insert statement contains more than one insert component,
   *  we savepoint the insert components to try to guarantee insert
   *  statement atomicity.
   */
  if (need_savepoint == true)
    {
      savepoint_name =
	mq_generate_name (parser, "UisP", &insert_savepoint_number);
      if (savepoint_name == NULL)
	{
	  AU_ENABLE (save);
	  return ER_GENERIC_ERROR;
	}
      error = tran_savepoint (savepoint_name, false);
      if (error != NO_ERROR)
	{
	  AU_ENABLE (save);
	  return error;
	}
    }

  /* the do_Trigger_involved will be set as true when execute trigger
   * statement. it will not be set back. we need to keep its value
   * to update last insert id.
   */
  is_trigger_involved = do_Trigger_involved;
  if (!do_Trigger_involved)
    {
      obt_begin_insert_values ();
    }

  for (row_count_total = 0; crt_list != NULL; crt_list = crt_list->next)
    {
      DB_OTMPL *otemplate = NULL;
      int row_count = 0;

      error = do_insert_template (parser, &otemplate, statement, crt_list,
				  &savepoint_name, &row_count,
				  (row_count_total == 0 ? true : false));

      if (error < NO_ERROR)
	{
	  if (otemplate != NULL)
	    {
	      dbt_abort_object (otemplate);
	    }
	}
      else if (otemplate != NULL)
	{
	  obj = dbt_finish_object (otemplate);
	  if (obj == NULL)
	    {
	      error = er_errid ();
	      /* On error, the template must be freed.
	       */
	      dbt_abort_object (otemplate);
	    }

	  if (error >= NO_ERROR)
	    {
	      error = mq_evaluate_check_option (parser,
						statement->info.insert.where,
						obj, class_);
	    }

	  if (error >= NO_ERROR && !is_multiple_tuples_insert)
	    {
	      /* If any of the (nested) inserts were view objects we
	         need to find them and create VMOPS for them.  Use a
	         post walk so that vobj will point to the vmop for the
	         outer insert if one is needed.
	       */
	      vobj = NULL;
	      statement = parser_walk_tree (parser, statement, NULL, NULL,
					    make_vmops, &vobj);
	      /* create a DB_VALUE to hold the newly inserted instance */
	      ins_val = db_value_create ();
	      if (ins_val == NULL)
		{
		  error = er_errid ();
		}
	      else
		{
		  if (vobj != NULL)
		    {
		      /* use the virtual mop */
		      db_make_object (ins_val, vobj);
		    }
		  else
		    {
		      db_make_object (ins_val, obj);
		    }
		  statement->etc = (void *) ins_val;
		}

	    }
	}

      if (parser->abort)
	{
	  error = er_errid ();
	}
      if (error < 0)
	{
	  break;
	}
      row_count_total += row_count;
    }

  AU_ENABLE (save);

  /* restore the obt_Last_insert_id_generated flag after insert. */
  if (!is_trigger_involved && obt_Last_insert_id_generated)
    {
      obt_Last_insert_id_generated = false;
      if (error != NO_ERROR)
	{
	  (void) csession_reset_cur_insert_id ();
	}
    }

  /* if error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction.
   */
  if (error < NO_ERROR && savepoint_name
      && (error != ER_LK_UNILATERALLY_ABORTED))
    {
      /* savepoint from tran_savepoint() */
      (void) tran_internal_abort_upto_savepoint (savepoint_name, true);
      /* Use a special version of rollback which will not clobber
         cached views. We can do this because we know insert can not
         have created any views.
         This is instead of the extern function:
         db_abort_to_savepoint(savepoint_name);
       */
    }

  return error < 0 ? error : row_count_total;
}

/*
 * do_insert() - Inserts an object or row
 *   return: Error code if insert fails
 *   parser(in/out): Parser context
 *   statement(in/out): Parse tree of a insert statement
 */
int
do_insert (PARSER_CONTEXT * parser, PT_NODE * root_statement)
{
  PT_NODE *statement = root_statement;
  int error;

  CHECK_MODIFICATION_ERROR ();

  error = insert_local (parser, statement);

  while (error < NO_ERROR && statement->next)
    {
      if (pt_has_error (parser))
	{
	  pt_report_to_ersys (parser, PT_EXECUTION);
	}
      /* assume error was from mismatch of multiple possible translated
         inserts. Try the next statement in the list.
         Only report the last error.
       */
      parser_free_tree (parser, parser->error_msgs);
      parser->error_msgs = NULL;
      statement = statement->next;
      error = insert_local (parser, statement);

      /* check whether this transaction is a victim of deadlock during */
      /* request to the driver */
      if (parser->abort)
	{
	  return (er_errid ());
	}
      /* This is to allow the row "counting" to be done
         in db_execute_and_keep_statement, and also correctly
         returns the "result" of the last insert statement.
         Only the first insert statement in the list is examined
         for results.
       */
      root_statement->etc = statement->etc;
      statement->etc = NULL;
    }

  return error;
}

/*
 * do_prepare_insert () - Prepare the INSERT statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in):
 */
int
do_prepare_insert (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *class_;
  int has_check_option = 0;
  PT_NODE *values = NULL;
  PT_NODE *non_null_attrs = NULL;
  PT_NODE *upd_non_null_attrs = NULL;
  int server_allowed = 0;
  int has_uniques = 0;
  int has_trigger = 0;
  int upd_has_uniques = 0;
  bool has_default_values_list = false;
  PT_NODE *attr_list;

  if (statement == NULL ||
      statement->node_type != PT_INSERT ||
      statement->info.insert.spec == NULL ||
      statement->info.insert.spec->info.spec.flat_entity_list == NULL)
    {
      assert (false);
      return ER_GENERIC_ERROR;
    }

  statement->etc = NULL;
  class_ = statement->info.insert.spec->info.spec.flat_entity_list;
  values = statement->info.insert.value_clauses;

  /* prepare only when simple insert clause are used */
  if (values->info.node_list.list_type != PT_IS_VALUE)
    {
      return NO_ERROR;
    }

  /* prevent multi statements */
  /* prevent multi values insert */
  /* prevent do replace */
  /* prevent dup key update */
  if (pt_length_of_list (statement) > 1 ||
      pt_length_of_list (values) > 1 ||
      statement->info.insert.do_replace ||
      statement->info.insert.on_dup_key_update != NULL)
    {
      return NO_ERROR;
    }

  /* prevent blob, clob plan cache */
  for (attr_list = statement->info.insert.attr_list; attr_list != NULL;
       attr_list = attr_list->next)
    {
      if (attr_list->type_enum == PT_TYPE_BLOB ||
	  attr_list->type_enum == PT_TYPE_CLOB)

	return NO_ERROR;
    }

  /* check non null attrs */
  if (values->info.node_list.list_type == PT_IS_DEFAULT_VALUE)
    {
      has_default_values_list = true;
    }

  error = check_missing_non_null_attrs (parser, statement->info.insert.spec,
					statement->info.insert.attr_list,
					has_default_values_list);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = is_server_insert_allowed (parser, &non_null_attrs,
				    &has_uniques, &server_allowed,
				    statement, values, class_);

  if (error != NO_ERROR || !server_allowed)
    {
      return error;
    }

  error = do_prepare_insert_internal (parser, statement, values,
				      non_null_attrs,
				      &upd_non_null_attrs, has_uniques,
				      upd_has_uniques);

  if (non_null_attrs != NULL)
    {
      parser_free_tree (parser, non_null_attrs);
      non_null_attrs = NULL;
    }

  if (upd_non_null_attrs != NULL)
    {
      parser_free_tree (parser, upd_non_null_attrs);
      upd_non_null_attrs = NULL;
    }

  return error;
}

/*
 * do_execute_insert () - Execute the prepared INSERT statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in):
 */
int
do_execute_insert (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err;
  PT_NODE *flat;
  DB_OBJECT *class_obj;
  QFILE_LIST_ID *list_id;
  QUERY_FLAG query_flag;

  CHECK_MODIFICATION_ERROR ();

  if (statement->xasl_id == NULL)
    {
      return do_check_insert_trigger (parser, statement, do_insert);
    }

  flat = statement->info.insert.spec->info.spec.flat_entity_list;
  class_obj = (flat) ? flat->info.name.db_object : NULL;

  query_flag = parser->exec_mode | ASYNC_UNEXECUTABLE;

  if (statement->do_not_keep == 0)
    {
      query_flag |= KEEP_PLAN_CACHE;
    }
  query_flag |= NOT_FROM_RESULT_CACHE;
  query_flag |= RESULT_CACHE_INHIBITED;

  list_id = NULL;
  parser->query_id = -1;

  err = query_execute (statement->xasl_id, &parser->query_id,
		       parser->host_var_count +
		       parser->auto_param_count,
		       parser->host_variables, &list_id, query_flag,
		       NULL, NULL);

  /* free returned QFILE_LIST_ID */
  if (list_id)
    {
      /* set as result */
      err = list_id->tuple_cnt;
      regu_free_listid (list_id);
    }

  /* end the query; reset query_id and call qmgr_end_query() */
  pt_end_query (parser);

  if ((err < NO_ERROR) && er_errid () != NO_ERROR)
    {
      pt_record_error (parser, parser->statement_number,
		       statement->line_number, statement->column_number,
		       er_msg (), NULL);
    }

  return err;
}

/*
 * insert_predefined_values_into_partition () - Execute the prepared INSERT
 *                                                 statement
 *   return: Error code
 *   parser(in): Parser context
 *   rettemp(out): Partitioned new instance template
 *   classobj(in): Class's db_object
 *   partcol(in): Partition's key column name
 *   psi(in): Partition selection information
 *   pic(in): Partition insert cache
 *
 * Note: attribute's auto increment value or default value
 *       is fetched, partition selection is performed,
 *       instance created, cached values are inserted.
 *       and auto increment or default value are inserted.
 */
static int
insert_predefined_values_into_partition (const PARSER_CONTEXT * parser,
					 DB_OTMPL ** rettemp,
					 const MOP classobj,
					 const DB_VALUE * partcol,
					 PARTITION_SELECT_INFO * psi,
					 PARTITION_INSERT_CACHE * pic)
{
  DB_ATTRIBUTE *dbattr;
  DB_VALUE *ins_val = NULL;
  MOP retobj;
  int error;
  PARTITION_INSERT_CACHE *picwork;
  DB_VALUE next_val, auto_inc_val;
  int r = 0;
  char auto_increment_name[AUTO_INCREMENT_SERIAL_NAME_MAX_LENGTH];
  MOP serial_class_mop = NULL, serial_mop;
  DB_IDENTIFIER serial_obj_id;
  DB_DATA_STATUS data_status;
  const char *class_name;

  for (dbattr = db_get_attributes (classobj);
       dbattr; dbattr = db_attribute_next (dbattr))
    {
      const char *mbs2 = DB_GET_STRING (partcol);
      if (mbs2 == NULL)
	{
	  return -1;
	}

      if (intl_identifier_casecmp (db_attribute_name (dbattr), mbs2) == 0)
	{
	  /* partition column */

	  /* 1. check auto increment */
	  if (dbattr->flags & SM_ATTFLAG_AUTO_INCREMENT)
	    {
	      /* set auto increment value */
	      if (dbattr->auto_increment == NULL)
		{
		  if (serial_class_mop == NULL)
		    {
		      serial_class_mop = sm_find_class (CT_SERIAL_NAME);
		    }

		  class_name = sm_class_name (dbattr->class_mop);
		  SET_AUTO_INCREMENT_SERIAL_NAME (auto_increment_name,
						  class_name,
						  dbattr->header.name);
		  serial_mop = do_get_serial_obj_id (&serial_obj_id,
						     serial_class_mop,
						     auto_increment_name);

		  if (serial_mop == NULL)
		    {
		      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			      ER_OBJ_INVALID_ATTRIBUTE, 1,
			      auto_increment_name);
		      return ER_OBJ_INVALID_ATTRIBUTE;
		    }

		  dbattr->auto_increment = serial_mop;

		}

	      if (dbattr->auto_increment != NULL)
		{
		  db_make_null (&next_val);

		  /* Do not update LAST_INSERT_ID during executing a trigger. */
		  if (do_Trigger_involved == true
		      || obt_Last_insert_id_generated == true)
		    {
		      error = serial_get_next_value (&next_val, &dbattr->auto_increment->oid_info.oid, 0,	/* no cache */
						     1,	/* generate one */
						     GENERATE_SERIAL);
		    }
		  else
		    {
		      error = serial_get_next_value (&next_val, &dbattr->auto_increment->oid_info.oid, 0,	/* no cache */
						     1,	/* generate one */
						     GENERATE_AUTO_INCREMENT);
		      if (error == NO_ERROR)
			{
			  obt_Last_insert_id_generated = true;
			}
		    }

		  if (error == NO_ERROR)
		    {
		      db_value_domain_init (&auto_inc_val, dbattr->type->id,
					    dbattr->domain->precision,
					    dbattr->domain->scale);

		      (void) numeric_db_value_coerce_from_num (&next_val,
							       &auto_inc_val,
							       &data_status);

		      if (data_status == NO_ERROR)
			{
			  ins_val = &auto_inc_val;
			}
		    }
		}
	    }

	  /* 2. check default value */
	  if (ins_val == NULL)
	    {
	      ins_val = db_attribute_default (dbattr);
	    }

	  break;
	}
    }

  if (ins_val == NULL)
    {
      return -1;
    }

  error = do_select_partition (psi, ins_val, &retobj);
  if (!error)
    {
      *rettemp = dbt_create_object_internal (retobj);
      if (*rettemp != NULL)
	{
	  /* cache back & insert */
	  for (picwork = pic; picwork; picwork = picwork->next)
	    {
	      error = insert_object_attr (parser, *rettemp,
					  picwork->val,
					  picwork->attr, picwork->desc);
	      if (error)
		{
		  break;
		}
	    }

	  error = obt_assign (*rettemp, dbattr, 0, ins_val, NULL);
	  return (error == NO_ERROR) ? NO_ERROR : -1;
	}
      error = -1;
    }

  return error;
}






/*
 * Function Group:
 * Implement method calls
 *
 */

static int call_method (PARSER_CONTEXT * parser, PT_NODE * statement);

/*
 * call_method() -
 *   return: Value returned by method if success, otherwise an error code
 *   parser(in): Parser context
 *   node(in): Parse tree of a call statement
 *
 * Note:
 */
static int
call_method (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  const char *into_label, *proc;
  int error = NO_ERROR;
  DB_OBJECT *obj = NULL;
  DB_VALUE target_value, *ins_val, ret_val, db_value;
  DB_VALUE_LIST *val_list = 0, *vl, **next_val_list;
  PT_NODE *vc, *into, *target, *method;

  db_make_null (&ret_val);
  db_make_null (&target_value);

  /*
   * The method name and ON name are required.
   */
  if (!statement
      || !(method = statement->info.method_call.method_name)
      || method->node_type != PT_NAME || !(proc = method->info.name.original)
      || !(target = statement->info.method_call.on_call_target))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return er_errid ();
    }

  /*
   * Determine whether the object is a class or instance.
   */

  pt_evaluate_tree (parser, target, &target_value, 1);
  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_SEMANTIC);
      return er_errid ();
    }

  if (DB_VALUE_TYPE (&target_value) == DB_TYPE_NULL)
    {
      /*
       * Don't understand the rationale behind this case.  What's the
       * point here?  MRS 4/30/96
       */
      error = NO_ERROR;
    }
  else
    {
      if (DB_VALUE_TYPE (&target_value) == DB_TYPE_OBJECT)
	{
	  obj = DB_GET_OBJECT ((&target_value));
	}

      if (obj == NULL || pt_has_error (parser))
	{
	  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		     MSGCAT_SEMANTIC_METH_TARGET_NOT_OBJ);
	  return er_errid ();
	}

      /*
       * Build an argument list.
       */
      next_val_list = &val_list;
      vc = statement->info.method_call.arg_list;
      for (; vc != NULL; vc = vc->next)
	{
	  DB_VALUE *db_val;
	  bool to_break = false;

	  *next_val_list =
	    (DB_VALUE_LIST *) calloc (1, sizeof (DB_VALUE_LIST));
	  if (*next_val_list == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE_LIST));
	      return er_errid ();
	    }
	  (*next_val_list)->next = (DB_VALUE_LIST *) 0;

	  /*
	   * Don't clone host vars; they may actually be acting as output
	   * variables (e.g., a character array that is intended to receive
	   * bytes from the method), and cloning will ensure that the
	   * results never make it to the expected area.  Since
	   * pt_evaluate_tree() always clones its db_values we must not
	   * use pt_evaluate_tree() to extract the db_value from a host
	   * variable;  instead extract it ourselves.
	   */
	  if (PT_IS_CONST (vc))
	    {
	      db_val = pt_value_to_db (parser, vc);
	    }
	  else
	    {
	      pt_evaluate_tree (parser, vc, &db_value, 1);
	      if (pt_has_error (parser))
		{
		  /* to maintain the list to free all the allocated */
		  to_break = true;
		}
	      db_val = &db_value;
	    }

	  if (db_val != NULL)
	    {
	      (*next_val_list)->val = *db_val;

	      next_val_list = &(*next_val_list)->next;
	    }

	  if (to_break)
	    {
	      break;
	    }
	}

      /*
       * Call the method.
       */
      if (pt_has_error (parser))
	{
	  pt_report_to_ersys (parser, PT_SEMANTIC);
	  error = er_errid ();
	}
      else
	{
	  error = db_send_arglist (obj, proc, &ret_val, val_list);
	}

      /*
       * Free the argument list.  Again, it is important to be careful
       * with host variables.  Since we didn't clone them, we shouldn't
       * free or clear them.
       */
      vc = statement->info.method_call.arg_list;
      for (; val_list && vc; vc = vc->next)
	{
	  vl = val_list->next;
	  if (!PT_IS_CONST (vc))
	    {
	      db_value_clear (&val_list->val);
	    }
	  free_and_init (val_list);
	  val_list = vl;
	}

      if (error == NO_ERROR)
	{
	  /*
	   * Save the method result.
	   */
	  statement->etc = (void *) db_value_copy (&ret_val);

	  if ((into = statement->info.method_call.to_return_var) != NULL
	      && into->node_type == PT_NAME
	      && (into_label = into->info.name.original) != NULL)
	    {
	      /* create another DB_VALUE of the new instance for the label_table */
	      ins_val = db_value_copy (&ret_val);

	      /* enter {label, ins_val} pair into the label_table */
	      error =
		pt_associate_label_with_value_check_reference (into_label,
							       ins_val);
	    }
	}
    }

  db_value_clear (&ret_val);
  return error;
}

/*
 * do_call_method() -
 *   return: Value returned by method if success, otherwise an error code
 *   parser(in): Parser context
 *   node(in): Parse tree of a call statement
 *
 * Note:
 */
int
do_call_method (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *method;

  if (!statement
      || !(method = statement->info.method_call.method_name)
      || method->node_type != PT_NAME || !(method->info.name.original))
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return er_errid ();
    }

  if (statement->info.method_call.on_call_target)
    {
      return call_method (parser, statement);
    }
  else
    {
      return jsp_call_stored_procedure (parser, statement);
    }
}

/*
 * These functions are provided just so we have some builtin gadgets that we can
 * use for quick and dirty method testing.  To get at them, alter your
 * favorite class like this:
 *
 * 	alter class foo
 * 		add method pickaname() string
 * 		function dbmeth_class_name;
 *
 * or
 *
 * 	alter class foo
 * 		add method pickaname(string) string
 * 		function dbmeth_print;
 *
 * After that you should be able to invoke "pickaname" on "foo" instances
 * to your heart's content.  dbmeth_class_name() will retrieve the class
 * name of the target instance and return it as a string; dbmeth_print()
 * will print the supplied value on stdout every time it is invoked.
 */

/*
 * TODO: The following function names need to be fixed.
 * Renaming can affects user interface.
 */

/*
 * dbmeth_class_name() -
 *   return: None
 *   self(in): Class object
 *   result(out): DB_VALUE for a class name
 *
 * Note: Position of function arguments must be kept
 *   for pre-defined function pointers(au_static_links)
 */
void
dbmeth_class_name (DB_OBJECT * self, DB_VALUE * result)
{
  const char *cname;
  DB_VALUE tmp;

  cname = db_get_class_name (self);

  /*
   * Make a string and clone it so that it won't become invalid if the
   * underlying class object that gave us the string goes away.  Of
   * course, this gives the responsibility for freeing the cloned
   * string to someone else; is anybody accepting it?
   */
  db_make_string (&tmp, cname);
  db_value_clone (&tmp, result);
}

/*
 * TODO: The functin name need to be fixed.
 * it is known system method so must fix corresponding qa first
 */

/*
 * dbmeth_print() -
 *   return: None
 *   self(in): Class object
 *   result(out): NULL value
 *   msg(in): DB_VALUE for a message
 *
 * Note: Position of function arguments must be kept
 *   for pre-defined function pointers(au_static_links)
 */
void
dbmeth_print (DB_OBJECT * self, DB_VALUE * result, DB_VALUE * msg)
{
  db_value_print (msg);
  printf ("\n");
  db_make_null (result);
}






/*
 * Function Group:
 * Functions for the implementation of virtual queries.
 *
 */


/*
 * do_select() -
 *   return: Error code
 *   parser(in/out): Parser context
 *   statement(in/out): A statement to do
 *
 * Note: Side effects can exist at returned result through application extern
 */
int
do_select (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error;
  XASL_NODE *xasl = NULL;
  QFILE_LIST_ID *list_id = NULL;
  int into_cnt, i;
  PT_NODE *into;
  const char *into_label;
  DB_VALUE *vals, *v;
  int save;
  int size;
  char *stream = NULL;
  QUERY_ID query_id = NULL_QUERY_ID;
  QUERY_FLAG query_flag;
  int async_flag;

  error = NO_ERROR;

  /* click counter check */
  if (statement->is_click_counter)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  AU_DISABLE (save);
  parser->au_save = save;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();

  async_flag = pt_statement_have_methods (parser, statement) ?
    ASYNC_UNEXECUTABLE : ASYNC_EXECUTABLE;

  query_flag = parser->exec_mode | async_flag;

  if (query_flag == (ASYNC_EXEC | ASYNC_UNEXECUTABLE))
    {
      query_flag = SYNC_EXEC | ASYNC_UNEXECUTABLE;
    }

  if (parser->dont_collect_exec_stats == true)
    {
      query_flag |= DONT_COLLECT_EXEC_STATS;
    }

#if defined(CUBRID_DEBUG)
  PT_NODE_PRINT_TO_ALIAS (parser, statement, PT_CONVERT_RANGE);
#endif

  pt_null_etc (statement);

  xasl = parser_generate_xasl (parser, statement);

  if (xasl && !pt_has_error (parser))
    {
      if (statement->node_type == PT_SELECT
	  && PT_SELECT_INFO_IS_FLAGED (statement,
				       PT_SELECT_INFO_MULTI_UPATE_AGG))
	{
	  XASL_SET_FLAG (xasl, XASL_MULTI_UPDATE_AGG);
	}

      if (pt_false_where (parser, statement))
	{
	  /* there is no results, this is a compile time false where clause */
	}
      else
	{
	  if (error >= NO_ERROR)
	    {
	      error = xts_map_xasl_to_stream (xasl, &stream, &size);
	      if (error != NO_ERROR)
		{
		  PT_ERRORm (parser, statement,
			     MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		}
	    }

	  if (error >= NO_ERROR)
	    {
	      error = query_prepare_and_execute (stream,
						 size,
						 &query_id,
						 parser->host_var_count +
						 parser->auto_param_count,
						 parser->host_variables,
						 &list_id, query_flag);
	    }
	  parser->query_id = query_id;
	  statement->etc = list_id;

	  /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
	  if (stream)
	    {
	      free_and_init (stream);
	    }

	  if (error >= NO_ERROR)
	    {
	      /* if select ... into label ... has some result val
	         then enter {label,val} pair into the label_table */
	      into = statement->info.query.into_list;

	      into_cnt = pt_length_of_list (into);
	      if (into_cnt > 0
		  && (vals = (DB_VALUE *)
		      malloc (into_cnt * sizeof (DB_VALUE))) != NULL)
		{
		  if (pt_get_one_tuple_from_list_id
		      (parser, statement, vals, into_cnt))
		    {
		      for (i = 0, v = vals;
			   i < into_cnt && into; i++, v++, into = into->next)
			{
			  if (into->node_type == PT_NAME
			      && (into_label =
				  into->info.name.original) != NULL)
			    {
			      error =
				pt_associate_label_with_value_check_reference
				(into_label, db_value_copy (v));
			    }
			  db_value_clear (v);
			}
		    }
		  else if (into->node_type == PT_NAME)
		    {
		      PT_ERRORmf (parser, statement,
				  MSGCAT_SET_PARSER_RUNTIME,
				  MSGCAT_RUNTIME_PARM_IS_NOT_SET,
				  into->info.name.original);
		    }
		  free_and_init (vals);
		}
	    }
	  else
	    {
	      error = er_errid ();
	      if (error == NO_ERROR)
		{
		  error = ER_REGU_SYSTEM;
		}
	    }
	}			/* else */
    }
  else
    {
      error = er_errid ();
      if (error == NO_ERROR)
	{
	  error = ER_FAILED;
	}
    }

  /* mark the end of another level of xasl packing */
  pt_exit_packing_buf ();

  AU_ENABLE (save);
  return error;
}

/*
 * do_prepare_select() - Prepare the SELECT statement including optimization and
 *                       plan generation, and creating XASL as the result
 *   return: Error code
 *   parser(in/out): Parser context
 *   statement(in/out): A statement to do
 *
 * Note:
 */
int
do_prepare_select (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;
  const char *qstr;
  XASL_ID *xasl_id;
  XASL_NODE *xasl;
  char *stream;
  int size;
  int au_save;

  if (parser == NULL || statement == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* click counter check */
  if (statement->is_click_counter)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  /* there can be no results, this is a compile time false where clause */
  if (pt_false_where (parser, statement))
    {
      /* tell to the execute routine that there's no XASL to execute */
      statement->xasl_id = NULL;
      return NO_ERROR;
    }

  /* if already prepared */
  if (statement->xasl_id)
    {
      return NO_ERROR;
    }

  /* make query string */
  parser->dont_prt_long_string = 1;
  parser->long_string_skipped = 0;
  parser->print_type_ambiguity = 0;
  PT_NODE_PRINT_TO_ALIAS (parser, statement,
			  (PT_CONVERT_RANGE | PT_PRINT_QUOTES
			   | PT_PRINT_DIFFERENT_SESSION_PRMS));
  qstr = statement->alias_print;
  parser->dont_prt_long_string = 0;
  if (parser->long_string_skipped || parser->print_type_ambiguity)
    {
      statement->cannot_prepare = 1;
      return NO_ERROR;
    }

  /* look up server's XASL cache for this query string
     and get XASL file id (XASL_ID) returned if found */
  xasl_id = NULL;
  if (statement->recompile == 0)
    {
      err = query_prepare (qstr, NULL, 0, &xasl_id);
      if (err != NO_ERROR)
	{
	  err = er_errid ();
	}
    }
  else
    {
      err = qmgr_drop_query_plan (qstr, ws_identifier (db_get_user ()),
				  NULL, true);
    }
  if (!xasl_id && err == NO_ERROR)
    {
      /* cache not found;
         make XASL from the parse tree including query optimization
         and plan generation */

      /* mark the beginning of another level of xasl packing */
      pt_enter_packing_buf ();

      AU_SAVE_AND_DISABLE (au_save);	/* this prevents authorization
					   checking during generating XASL */
      /* parser_generate_xasl() will build XASL tree from parse tree */
      xasl = parser_generate_xasl (parser, statement);
      AU_RESTORE (au_save);
      stream = NULL;

      if (xasl && (err == NO_ERROR) && !pt_has_error (parser))
	{
	  if (PT_SELECT_INFO_IS_FLAGED (statement,
					PT_SELECT_INFO_MULTI_UPATE_AGG))
	    {
	      XASL_SET_FLAG (xasl, XASL_MULTI_UPDATE_AGG);
	    }

	  /* convert the created XASL tree to the byte stream for transmission
	     to the server */
	  err = xts_map_xasl_to_stream (xasl, &stream, &size);
	  if (err != NO_ERROR)
	    {
	      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	    }
	}
      else
	{
	  err = er_errid ();
	  if (err == NO_ERROR)
	    {
	      err = ER_FAILED;
	    }
	}

      /* mark the end of another level of xasl packing */
      pt_exit_packing_buf ();

      /* request the server to prepare the query;
         give XASL stream generated from the parse tree
         and get XASL file id returned */
      if (stream && (err == NO_ERROR))
	{
	  err = query_prepare (qstr, stream, size, &xasl_id);
	  if (err != NO_ERROR)
	    {
	      err = er_errid ();
	    }
	}
      /* As a result of query preparation of the server,
         the XASL cache for this query will be created or updated. */

      /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
      if (stream)
	{
	  free_and_init (stream);
	}
      statement->use_plan_cache = 0;
    }
  else
    {
      if (err == NO_ERROR)
	{
	  statement->use_plan_cache = 1;
	}
      else
	{
	  statement->use_plan_cache = 0;
	}
    }

  /* save the XASL_ID that is allocated and returned by query_prepare()
     into 'statement->xasl_id' to be used by do_execute_select() */
  statement->xasl_id = xasl_id;

  return err;
}				/* do_prepare_select() */

/*
 * do_prepare_session_statement () - prepare step for a prepared session
 *				     statement
 * return : error code or NO_ERROR
 * parser (in)	  : parser context
 * statement (in) : prepared statement
 */
int
do_prepare_session_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  assert (statement->node_type == PT_EXECUTE_PREPARE);
  if (statement->xasl_id != NULL)
    {
      /* already "prepared" */
      return NO_ERROR;
    }
  statement->xasl_id = (XASL_ID *) malloc (sizeof (XASL_ID));
  if (statement->xasl_id == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (XASL_ID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  XASL_ID_COPY (statement->xasl_id, &statement->info.execute.xasl_id);
  return NO_ERROR;
}

/*
 * do_execute_session_statement () - execute a prepared session statement
 * return :
 * parser (in)	  : parser context
 * statement (in) : statement to execute
 */
int
do_execute_session_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err;
  QFILE_LIST_ID *list_id;
  int query_flag, into_cnt, i, au_save;
  PT_NODE *into;
  const char *into_label;
  DB_VALUE *vals, *v;
  CACHE_TIME clt_cache_time;
  CURSOR_ID cursor_id;

  assert (pt_node_to_cmd_type (statement) == CUBRID_STMT_EXECUTE_PREPARE);

  if (parser->exec_mode == ASYNC_EXEC)
    {
      query_flag = ASYNC_EXEC | ASYNC_EXECUTABLE;
    }
  else
    {
      query_flag = SYNC_EXEC | ASYNC_UNEXECUTABLE;
    }

  if (parser->is_holdable)
    {
      query_flag |= RESULT_HOLDABLE;
    }

  /* flush necessary objects before execute */
  if (ws_has_updated ())
    {
      (void) parser_walk_tree (parser, statement, pt_flush_classes, NULL,
			       NULL, NULL);
    }

  if (parser->abort)
    {
      return er_errid ();
    }

  /* Request that the server executes the stored XASL, which is the execution
   * plan of the prepared query, with the host variables given by users as
   * parameter values for the query.
   * As a result, query id and result file id (QFILE_LIST_ID) will be
   * returned.
   */

  AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
				   checking for method */
  parser->query_id = -1;
  list_id = NULL;

  CACHE_TIME_RESET (&clt_cache_time);
  if (statement->clt_cache_check)
    {
      clt_cache_time = statement->cache_time;
      statement->clt_cache_check = 0;
    }
  CACHE_TIME_RESET (&statement->cache_time);
  statement->clt_cache_reusable = 0;

  err = query_execute (statement->xasl_id,
		       &parser->query_id,
		       parser->host_var_count + parser->auto_param_count,
		       parser->host_variables,
		       &list_id,
		       query_flag, &clt_cache_time, &statement->cache_time);

  AU_RESTORE (au_save);

  if (CACHE_TIME_EQ (&clt_cache_time, &statement->cache_time))
    {
      statement->clt_cache_reusable = 1;
    }

  /* save the returned QFILE_LIST_ID into 'statement->etc' */
  statement->etc = (void *) list_id;

  if (err < NO_ERROR)
    {
      return er_errid ();
    }
  /* if select ... into label ... has some result val
     then enter {label,val} pair into the label_table */
  into = statement->info.execute.into_list;
  into_cnt = pt_length_of_list (into);
  if (into_cnt == 0)
    {
      return err;
    }

  vals = (DB_VALUE *) malloc (into_cnt * sizeof (DB_VALUE));
  if (vals == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      into_cnt * sizeof (DB_VALUE));
      return ER_FAILED;
    }
  if (!cursor_open (&cursor_id, list_id, false,
		    statement->info.execute.oids_included))
    {
      return err;
    }
  cursor_id.query_id = parser->query_id;
  if (cursor_next_tuple (&cursor_id) != DB_CURSOR_SUCCESS
      || cursor_get_tuple_value_list (&cursor_id, into_cnt, vals) != NO_ERROR)
    {
      free_and_init (vals);
      return err;
    }
  cursor_close (&cursor_id);

  for (i = 0, v = vals; i < into_cnt && into; i++, v++, into = into->next)
    {
      if (into->node_type == PT_NAME
	  && (into_label = into->info.name.original) != NULL)
	{
	  err =
	    pt_associate_label_with_value_check_reference (into_label,
							   db_value_copy (v));
	}
      db_value_clear (v);
    }

  free_and_init (vals);

  return err;
}

/*
 * do_execute_select() - Execute the prepared SELECT statement
 *   return: Error code
 *   parser(in/out): Parser context
 *   statement(in/out): A statement to do
 *
 * Note:
 */
int
do_execute_select (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err;
  QFILE_LIST_ID *list_id;
  int query_flag, into_cnt, i, au_save;
  PT_NODE *into;
  const char *into_label;
  DB_VALUE *vals, *v;
  CACHE_TIME clt_cache_time;

  /* check if it is not necessary to execute this statement,
     e.g. false where or not prepared correctly */
  if (!statement->xasl_id)
    {
      statement->etc = NULL;
      return NO_ERROR;
    }

  /* adjust query flag */
  if (parser->exec_mode == ASYNC_EXEC)
    {
      if (pt_statement_have_methods (parser, statement))
	{
	  query_flag = SYNC_EXEC | ASYNC_UNEXECUTABLE;
	}
      else
	{
	  query_flag = ASYNC_EXEC | ASYNC_EXECUTABLE;
	}
    }
  else
    {
      query_flag = SYNC_EXEC | ASYNC_UNEXECUTABLE;
    }

  if (statement->do_not_keep == 0)
    {
      query_flag |= KEEP_PLAN_CACHE;
    }

  if (statement->si_datetime == 1 || statement->si_tran_id == 1)
    {
      statement->info.query.reexecute = 1;
      statement->info.query.do_not_cache = 1;
    }

  if (statement->info.query.reexecute == 1)
    {
      query_flag |= NOT_FROM_RESULT_CACHE;
    }

  if (statement->info.query.do_cache == 1)
    {
      query_flag |= RESULT_CACHE_REQUIRED;
    }

  if (statement->info.query.do_not_cache == 1)
    {
      query_flag |= RESULT_CACHE_INHIBITED;
    }
  if (parser->is_holdable)
    {
      query_flag |= RESULT_HOLDABLE;
    }

  if (parser->dont_collect_exec_stats == true)
    {
      query_flag |= DONT_COLLECT_EXEC_STATS;
    }

  /* flush necessary objects before execute */
  if (ws_has_updated ())
    {
      (void) parser_walk_tree (parser, statement, pt_flush_classes, NULL,
			       NULL, NULL);
    }

  if (parser->abort)
    {
      return er_errid ();
    }

  /* Request that the server executes the stored XASL, which is the execution
     plan of the prepared query, with the host variables given by users as
     parameter values for the query.
     As a result, query id and result file id (QFILE_LIST_ID) will be returned.
     do_prepare_select() has saved the XASL file id (XASL_ID) in
     'statement->xasl_id' */

  AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
				   checking for method */
  parser->query_id = -1;
  list_id = NULL;

  CACHE_TIME_RESET (&clt_cache_time);
  if (statement->clt_cache_check)
    {
      clt_cache_time = statement->cache_time;
      statement->clt_cache_check = 0;
    }
  CACHE_TIME_RESET (&statement->cache_time);
  statement->clt_cache_reusable = 0;

  err = query_execute (statement->xasl_id,
		       &parser->query_id,
		       parser->host_var_count + parser->auto_param_count,
		       parser->host_variables,
		       &list_id,
		       query_flag, &clt_cache_time, &statement->cache_time);

  AU_RESTORE (au_save);

  if (CACHE_TIME_EQ (&clt_cache_time, &statement->cache_time))
    {
      statement->clt_cache_reusable = 1;
    }

  /* save the returned QFILE_LIST_ID into 'statement->etc' */
  statement->etc = (void *) list_id;

  if (err < NO_ERROR)
    {
      return er_errid ();
    }

  /* if SELECT ... INTO label ... has some result val, then enter {label,val}
     pair into the label_table */
  into = statement->info.query.into_list;
  if ((into_cnt = pt_length_of_list (into)) > 0
      && (vals = (DB_VALUE *) malloc (into_cnt * sizeof (DB_VALUE))) != NULL)
    {
      if (pt_get_one_tuple_from_list_id (parser, statement, vals, into_cnt))
	{
	  for (i = 0, v = vals; i < into_cnt && into;
	       i++, v++, into = into->next)
	    {
	      if (into->node_type == PT_NAME
		  && (into_label = into->info.name.original) != NULL)
		{
		  err =
		    pt_associate_label_with_value_check_reference (into_label,
								   db_value_copy
								   (v));
		}
	      db_value_clear (v);
	    }
	}
      else if (into->node_type == PT_NAME)
	{
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_PARM_IS_NOT_SET,
		      into->info.name.original);
	}
      free_and_init (vals);
    }

  return err;
}				/* do_execute_select() */





/*
 * Function Group:
 * DO Functions for replication management
 *
 */


/*
 * do_replicate_schema() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): The parse tree of a DDL statement
 *
 * Note:
 */
int
do_replicate_schema (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  REPL_INFO repl_info;
  REPL_INFO_SCHEMA repl_schema;
  PARSER_VARCHAR *name = NULL;
  static const char *unknown_schema_name = "-";

  if (db_Enable_replications > 0)
    {
      return NO_ERROR;
    }

  switch (statement->node_type)
    {
    case PT_CREATE_ENTITY:
      name =
	pt_print_bytes (parser, statement->info.create_entity.entity_name);
      repl_schema.statement_type = CUBRID_STMT_CREATE_CLASS;
      break;

    case PT_ALTER:
      name = pt_print_bytes (parser, statement->info.alter.entity_name);
      repl_schema.statement_type = CUBRID_STMT_ALTER_CLASS;
      break;

    case PT_RENAME:
      name = pt_print_bytes (parser, statement->info.rename.old_name);
      repl_schema.statement_type = CUBRID_STMT_RENAME_CLASS;
      break;

    case PT_DROP:
      repl_schema.statement_type = CUBRID_STMT_DROP_CLASS;
      break;

    case PT_CREATE_INDEX:
      name = pt_print_bytes (parser, statement->info.index.indexed_class);
      repl_schema.statement_type = CUBRID_STMT_CREATE_INDEX;
      break;

    case PT_ALTER_INDEX:
      name = pt_print_bytes (parser, statement->info.index.indexed_class);
      repl_schema.statement_type = CUBRID_STMT_ALTER_INDEX;
      break;

    case PT_DROP_INDEX:
      name = pt_print_bytes (parser, statement->info.index.indexed_class);
      repl_schema.statement_type = CUBRID_STMT_DROP_INDEX;
      break;

    case PT_CREATE_SERIAL:
      repl_schema.statement_type = CUBRID_STMT_CREATE_SERIAL;
      break;

    case PT_ALTER_SERIAL:
      repl_schema.statement_type = CUBRID_STMT_ALTER_SERIAL;
      break;

    case PT_DROP_SERIAL:
      repl_schema.statement_type = CUBRID_STMT_DROP_SERIAL;
      break;

    case PT_DROP_VARIABLE:
      repl_schema.statement_type = CUBRID_STMT_DROP_LABEL;
      break;

    case PT_CREATE_STORED_PROCEDURE:
      repl_schema.statement_type = CUBRID_STMT_CREATE_STORED_PROCEDURE;
      break;

    case PT_ALTER_STORED_PROCEDURE_OWNER:
      repl_schema.statement_type = CUBRID_STMT_ALTER_STORED_PROCEDURE_OWNER;
      break;

    case PT_DROP_STORED_PROCEDURE:
      repl_schema.statement_type = CUBRID_STMT_DROP_STORED_PROCEDURE;
      break;

    case PT_CREATE_USER:
      repl_schema.statement_type = CUBRID_STMT_CREATE_USER;
      break;

    case PT_ALTER_USER:
      repl_schema.statement_type = CUBRID_STMT_ALTER_USER;
      break;

    case PT_DROP_USER:
      repl_schema.statement_type = CUBRID_STMT_DROP_USER;
      break;

    case PT_GRANT:
      repl_schema.statement_type = CUBRID_STMT_GRANT;
      break;

    case PT_REVOKE:
      repl_schema.statement_type = CUBRID_STMT_REVOKE;
      break;

    case PT_CREATE_TRIGGER:
      repl_schema.statement_type = CUBRID_STMT_CREATE_TRIGGER;
      break;

    case PT_RENAME_TRIGGER:
      repl_schema.statement_type = CUBRID_STMT_RENAME_TRIGGER;
      break;

    case PT_DROP_TRIGGER:
      repl_schema.statement_type = CUBRID_STMT_DROP_TRIGGER;
      break;

    case PT_REMOVE_TRIGGER:
      repl_schema.statement_type = CUBRID_STMT_REMOVE_TRIGGER;
      break;

    case PT_ALTER_TRIGGER:
      repl_schema.statement_type = CUBRID_STMT_SET_TRIGGER;
      break;

    case PT_TRUNCATE:
      repl_schema.statement_type = CUBRID_STMT_TRUNCATE;
      break;

    case PT_UPDATE_STATS:	/* UPDATE STATISTICS statements are not replicated intentionally. */
    default:
      return NO_ERROR;
    }

  repl_info.repl_info_type = REPL_INFO_TYPE_SCHEMA;
  if (name == NULL)
    {
      repl_schema.name = (char *) unknown_schema_name;
    }
  else
    {
      repl_schema.name = (char *) pt_get_varchar_bytes (name);
    }
  repl_schema.ddl = parser_print_tree_with_quotes (parser, statement);

  repl_info.info = (char *) &repl_schema;

  error = locator_flush_replication_info (&repl_info);

  return error;
}





/*
 * Function Group:
 * Implements the scope statement.
 *
 */

/*
 * do_scope() - scopes a statement
 *   return: Error code if scope fails
 *   parser(in/out): Parser context
 *   statement(in): The parse tree of a scope statement
 *
 * Note:
 */
int
do_scope (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *stmt;

  if (!statement
      || (statement->node_type != PT_SCOPE)
      || !((stmt = statement->info.scope.stmt))
      || (stmt->node_type != PT_TRIGGER_ACTION))
    {
      return ER_GENERIC_ERROR;
    }
  else
    {
      switch (stmt->info.trigger_action.action_type)
	{
	case PT_REJECT:
	case PT_INVALIDATE_XACTION:
	case PT_PRINT:
	  break;

	case PT_EXPRESSION:
	  do_Trigger_involved = true;
#if 0
	  if (prm_get_integer_value (PRM_ID_XASL_MAX_PLAN_CACHE_ENTRIES) > 0)
	    {

	      /* prepare a statement to execute */
	      error =
		do_prepare_statement (parser,
				      stmt->info.trigger_action.expression);
	      if (error >= NO_ERROR)
		{
		  /* execute the prepared statement */
		  error =
		    do_execute_statement (parser,
					  stmt->info.
					  trigger_action.expression);
		}
	    }
	  else
	    {
	      error =
		do_statement (parser, stmt->info.trigger_action.expression);
	    }
#else
	  error = do_statement (parser, stmt->info.trigger_action.expression);
#endif
	  /* Do not reset do_Trigger_involved here. This is intention. */
	  break;

	default:
	  break;
	}

      return error;
    }
}





/*
 * Function Group:
 * Implements the DO statement.
 *
 */

/*
 * do_execute_do() - execute the DO statement
 *   return: Error code if scope fails
 *   parser(in/out): Parser context
 *   statement(in): The parse tree of the DO statement
 *
 * Note:
 */
int
do_execute_do (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  XASL_NODE *xasl = NULL;
  QFILE_LIST_ID *list_id = NULL;
  int save;
  int size;
  char *stream = NULL;
  QUERY_ID query_id = NULL_QUERY_ID;
  QUERY_FLAG query_flag;

  AU_DISABLE (save);
  parser->au_save = save;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();

  /* only sync executable because we're after the side effects */
  query_flag = SYNC_EXEC | ASYNC_UNEXECUTABLE;
  /* don't cache anything */
  query_flag |= NOT_FROM_RESULT_CACHE;
  query_flag |= RESULT_CACHE_INHIBITED;

  pt_null_etc (statement);

  /* generate statement's XASL */
  xasl = parser_generate_do_stmt_xasl (parser, statement);

  if (pt_has_error (parser))
    {
      pt_report_to_ersys (parser, PT_EXECUTION);
      error = er_errid ();
      goto end;
    }
  else if (xasl == NULL)
    {
      error = er_errid ();
      goto end;
    }

  /* map XASL to stream */
  error = xts_map_xasl_to_stream (xasl, &stream, &size);
  if (error != NO_ERROR)
    {
      goto end;
    }

  /* execute statement */
  error = query_prepare_and_execute (stream,
				     size,
				     &query_id,
				     parser->host_var_count +
				     parser->auto_param_count,
				     parser->host_variables,
				     &list_id, query_flag);

  if (error != NO_ERROR)
    {
      goto end;
    }

  parser->query_id = query_id;
  statement->etc = list_id;

end:
  /* free 'stream' that is allocated inside of xts_map_xasl_to_stream() */
  if (stream)
    {
      free_and_init (stream);
    }

  /* mark the end of another level of xasl packing */
  pt_exit_packing_buf ();
  AU_ENABLE (save);

  return error;
}

/*
 * do_set_session_variables () - execute a set session variables statement
 * return : error code or no error
 * parser (in)	  : parser
 * statement (in) : statement
 */
int
do_set_session_variables (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_VALUE *variables = NULL;
  int count = 0, i = 0;
  PT_NODE *assignment = NULL;

  assert (statement != NULL);
  assert (statement->node_type == PT_SET_SESSION_VARIABLES);

  count = 0;
  /* count assignments */
  assignment = statement->info.set_variables.assignments;
  while (assignment)
    {
      count++;
      assignment = assignment->next;
    }
  /* we will store assignments in an array containing
     name1, value1, name2, value2... */
  variables = (DB_VALUE *) malloc (count * 2 * sizeof (DB_VALUE));
  if (variables == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      count * 2 * sizeof (DB_VALUE));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto cleanup;
    }

  /* initialize variables in case we need to exit with error */
  for (i = 0; i < count * 2; i++)
    {
      DB_MAKE_NULL (&variables[i]);
    }

  for (i = 0, assignment = statement->info.set_variables.assignments;
       assignment; i += 2, assignment = assignment->next)
    {
      pr_clone_value (pt_value_to_db (parser, assignment->info.expr.arg1),
		      &variables[i]);
      pt_evaluate_tree_having_serial (parser, assignment->info.expr.arg2,
				      &variables[i + 1], 1);

      if (pt_has_error (parser))
	{
	  /* if error occurred, don't send junk to server */
	  pt_report_to_ersys (parser, PT_EXECUTION);
	  error = er_errid ();
	  goto cleanup;
	}
    }

  error = csession_set_session_variables (variables, count * 2);

cleanup:
  if (variables != NULL)
    {
      for (i = 0; i < count * 2; i++)
	{
	  pr_clear_value (&variables[i]);
	}
      free_and_init (variables);
    }
  return error;
}

/*
 * do_drop_session_variables () - execute a drop session variables statement
 * return : error code or no error
 * parser (in)	  : parser
 * statement (in) : statement
 */
int
do_drop_session_variables (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = NO_ERROR;
  DB_VALUE *values = NULL;
  int count = 0, i = 0;
  PT_NODE *variables = NULL;

  assert (statement != NULL);
  assert (statement->node_type = PT_DROP_SESSION_VARIABLES);

  count = 0;
  /* count assignments */
  variables = statement->info.drop_session_var.variables;
  while (variables)
    {
      count++;
      variables = variables->next;
    }
  /* we will store assignments in an array containing
     name1, value1, name2, value2... */
  values = (DB_VALUE *) malloc (count * sizeof (DB_VALUE));
  if (values == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      count * sizeof (DB_VALUE));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto cleanup;
    }

  for (i = 0, variables = statement->info.drop_session_var.variables;
       variables; i++, variables = variables->next)
    {
      pr_clone_value (pt_value_to_db (parser, variables), &values[i]);
    }

  error = csession_drop_session_variables (values, count);

cleanup:
  if (values != NULL)
    {
      for (i = 0; i < count; i++)
	{
	  pr_clear_value (&values[i]);
	}
      free_and_init (values);
    }
  return error;
}

/*
 * MERGE STATEMENT
 */

/*
 * do_check_merge_trigger() -
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a statement
 *
 * Note: The function checks if there is any active trigger with event
 *   TR_EVENT_STATEMENT_INSERT/UPDATE/DELETE defined on the target.
 *   If there is one, raise the trigger. Otherwise, perform the
 *   given do_ function.
 */
int
do_check_merge_trigger (PARSER_CONTEXT * parser, PT_NODE * statement,
			PT_DO_FUNC * do_func)
{
  int err;

  if (prm_get_bool_value (PRM_ID_BLOCK_NOWHERE_STATEMENT)
      && statement->info.merge.search_cond == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  err = check_merge_trigger (do_func, parser, statement);
  return err;
}

/*
 * check_merge_trigger() -
 *   return: Error code
 *   do_func(in): Function to do
 *   parser(in): Parser context used by do_func
 *   statement(in): Parse tree of a statement used by do_func
 *
 * Note: The function checks if there is any active trigger for UPDATE,
 *       INSERT, or DELETE statements of a MERGE statement.
 */
static int
check_merge_trigger (PT_DO_FUNC * do_func, PARSER_CONTEXT * parser,
		     PT_NODE * statement)
{
  int err, result = NO_ERROR;
  TR_STATE *state;
  const char *savepoint_name = NULL;
  PT_NODE *node = NULL, *flat = NULL;
  DB_OBJECT *class_ = NULL;

  /* Prepare a trigger state for any triggers that must be raised in
     this statement */

  state = NULL;

  flat = (statement->info.merge.into) ?
    statement->info.merge.into->info.spec.flat_entity_list : NULL;
  class_ = (flat) ? flat->info.name.db_object : NULL;
  if (class_ == NULL)
    {
      PT_INTERNAL_ERROR (parser, "invalid spec id");
      result = ER_FAILED;
      goto exit;
    }

  if (statement->info.merge.update.assignment)
    {
      /* UPDATE statement triggers */
      result = tr_prepare_statement (&state, TR_EVENT_STATEMENT_UPDATE,
				     class_, 0, NULL);
      if (result != NO_ERROR)
	{
	  goto exit;
	}
      /* DELETE statement triggers */
      if (statement->info.merge.update.has_delete)
	{
	  result = tr_prepare_statement (&state, TR_EVENT_STATEMENT_DELETE,
					 class_, 0, NULL);
	  if (result != NO_ERROR)
	    {
	      goto exit;
	    }
	}
    }
  if (statement->info.merge.insert.value_clauses)
    {
      /* INSERT statement triggers */
      result = tr_prepare_statement (&state, TR_EVENT_STATEMENT_INSERT,
				     class_, 0, NULL);
      if (result != NO_ERROR)
	{
	  goto exit;
	}
    }

  if (state == NULL)
    {
      /* no triggers */
      result = do_check_internal_statements (parser, statement, do_func);
    }
  else
    {
      /* the operations performed in 'tr_before',
       * 'do_check_internal_statements' and 'tr_after' should be all
       * contained in one transaction */
      if (tr_Current_depth <= 1)
	{
	  savepoint_name =
	    mq_generate_name (parser, "UtrP", &tr_savepoint_number);
	  if (savepoint_name == NULL)
	    {
	      result = ER_GENERIC_ERROR;
	      goto exit;
	    }
	  result = tran_savepoint (savepoint_name, false);
	  if (result != NO_ERROR)
	    {
	      goto exit;
	    }
	}

      /* fire BEFORE STATEMENT triggers */
      result = tr_before (state);
      if (result == NO_ERROR)
	{
	  result = do_check_internal_statements (parser, statement, do_func);
	  if (result < NO_ERROR)
	    {
	      tr_abort (state);
	      state = NULL;	/* state was freed */
	    }
	  else
	    {
	      /* fire AFTER STATEMENT triggers */
	      /* try to preserve the usual result value */
	      err = tr_after (state);
	      if (err != NO_ERROR)
		{
		  result = err;
		}
	      if (tr_get_execution_state ())
		{
		  state = NULL;	/* state was freed */
		}
	    }
	}
      else
	{
	  /* state was freed */
	  state = NULL;
	}
    }

exit:
  if (state)
    {
      /* We need to free state and decrease the tr_Current_depth. */
      tr_abort (state);
    }

  if (result < NO_ERROR && savepoint_name != NULL
      && (result != ER_LK_UNILATERALLY_ABORTED))
    {
      /* savepoint from tran_savepoint() */
      (void) db_abort_to_savepoint (savepoint_name);
    }
  return result;
}

/*
 * do_merge () - MERGE statement
 *   return:
 *   parser(in):
 *   statement(in):
 *
 */
int
do_merge (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;
  PT_NODE *not_nulls = NULL, *lhs, *spec = NULL;
  int has_unique;
  const char *savepoint_name = NULL;
  DB_OBJECT *class_obj;
  QFILE_LIST_ID *list_id = NULL;
  PT_NODE *upd_select_stmt = NULL;
  PT_NODE *ins_select_stmt = NULL;
  PT_NODE *del_select_stmt = NULL;
  PT_NODE *select_names = NULL, *select_values = NULL;
  PT_NODE *const_names = NULL, *const_values = NULL;
  PT_NODE *flat, *values_list = NULL;
  PT_NODE **links = NULL;
  PT_NODE *hint_arg;
  int no_vals, no_consts;
  int wait_msecs = -2, old_wait_msecs = -2;
  float hint_waitsecs;
  int result = 0;

  CHECK_MODIFICATION_ERROR ();

  AU_DISABLE (parser->au_save);

  if (pt_false_where (parser, statement))
    {
      /* nothing to merge */
      goto exit;
    }

  spec = statement->info.merge.into;
  flat = spec->info.spec.flat_entity_list;
  if (flat == NULL)
    {
      err = ER_GENERIC_ERROR;
      goto exit;
    }
  class_obj = flat->info.name.db_object;

  /* check update part */
  if (statement->info.merge.update.assignment)
    {
      err = update_check_for_fk_cache_attr (parser, statement);
      if (err != NO_ERROR)
	{
	  goto exit;
	}

      /* check if the target class has UNIQUE constraint */
      err = update_check_for_constraints (parser, &has_unique, &not_nulls,
					  statement);
      /* not needed */
      if (not_nulls)
	{
	  parser_free_tree (parser, not_nulls);
	  not_nulls = NULL;
	}
      if (err != NO_ERROR)
	{
	  goto exit;
	}
      if (!statement->info.merge.has_unique)
	{
	  statement->info.merge.has_unique = (bool) has_unique;
	}

      lhs = statement->info.merge.update.assignment->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  lhs = lhs->info.expr.arg1;
	}
      if (lhs->info.name.meta_class == PT_META_ATTR)
	{
	  statement->info.merge.update.do_class_attrs = true;
	}

      if (!statement->info.merge.update.do_class_attrs)
	{
	  /* make the SELECT statement for OID list to be updated */
	  no_vals = 0;
	  no_consts = 0;

	  err =
	    pt_get_assignment_lists (parser, &select_names, &select_values,
				     &const_names, &const_values, &no_vals,
				     &no_consts,
				     statement->info.merge.update.assignment,
				     &links);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }

	  upd_select_stmt =
	    pt_to_merge_upd_del_query (parser, select_values,
				       &statement->info.merge,
				       PT_COMPOSITE_LOCKING_UPDATE);

	  /* restore tree structure; pt_get_assignment_lists() */
	  pt_restore_assignment_links (statement->info.merge.update.
				       assignment, links, -1);

	  upd_select_stmt = mq_translate (parser, upd_select_stmt);
	  if (upd_select_stmt == NULL)
	    {
	      err = er_errid ();
	      if (err == NO_ERROR)
		{
		  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		  err = er_errid ();
		}
	      goto exit;
	    }
	}

      if (statement->info.merge.update.has_delete)
	{
	  /* make the SELECT statement for OID list to be deleted */
	  del_select_stmt =
	    pt_to_merge_upd_del_query (parser, NULL, &statement->info.merge,
				       PT_COMPOSITE_LOCKING_DELETE);
	  del_select_stmt = mq_translate (parser, del_select_stmt);
	  if (del_select_stmt == NULL)
	    {
	      err = er_errid ();
	      if (err == NO_ERROR)
		{
		  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		  err = er_errid ();
		}
	      goto exit;
	    }
	}
    }

  /* check insert part */
  if (statement->info.merge.insert.value_clauses)
    {
      PT_NODE *attrs = statement->info.merge.insert.attr_list;

      err = insert_check_for_fk_cache_attr (parser, attrs,
					    flat->info.name.db_object);
      if (err != NO_ERROR)
	{
	  goto exit;
	}

      err = check_for_cons (parser, &has_unique, &not_nulls, attrs,
			    flat->info.name.db_object);
      if (not_nulls)
	{
	  parser_free_tree (parser, not_nulls);
	  not_nulls = NULL;
	}
      if (err != NO_ERROR)
	{
	  goto exit;
	}
      if (!statement->info.merge.has_unique)
	{
	  statement->info.merge.has_unique = (bool) has_unique;
	}

      /* check not nulls attrs are present in attr list */
      err = check_missing_non_null_attrs (parser, spec, attrs, false);
      if (err != NO_ERROR)
	{
	  goto exit;
	}
    }

  /* IX lock on the class */
  if (locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE) == NULL)
    {
      err = er_errid ();
      if (err == NO_ERROR)
	{
	  err = ER_GENERIC_ERROR;
	}
      goto exit;
    }

  if (statement->info.merge.update.assignment)
    {
      if (!statement->info.merge.update.do_class_attrs)
	{
	  /* flush necessary objects before execute */
	  err = sm_flush_objects (class_obj);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }

	  /* enable authorization checking during methods in queries */
	  AU_ENABLE (parser->au_save);
	  err = do_select (parser, upd_select_stmt);
	  AU_DISABLE (parser->au_save);
	  if (err < NO_ERROR)
	    {
	      /* query failed, an error has already been set */
	      goto exit;
	    }

	  list_id = (QFILE_LIST_ID *) upd_select_stmt->etc;
	  parser_free_tree (parser, upd_select_stmt);
	  upd_select_stmt = NULL;
	}
    }

  hint_arg = statement->info.merge.waitsecs_hint;
  if ((statement->info.merge.hint & PT_HINT_LK_TIMEOUT)
      && PT_IS_HINT_NODE (hint_arg))
    {
      hint_waitsecs = (float) atof (hint_arg->info.name.original);
      if (hint_waitsecs > 0)
	{
	  wait_msecs = (int) (hint_waitsecs * 1000);
	}
      else
	{
	  wait_msecs = (int) hint_waitsecs;
	}
      if (wait_msecs >= -1)
	{
	  old_wait_msecs = TM_TRAN_WAIT_MSECS ();
	  (void) tran_reset_wait_times (wait_msecs);
	}
    }

  /* do update part */
  if (statement->info.merge.update.assignment)
    {
      if (statement->info.merge.update.do_class_attrs)
	{
	  /* update class attributes */
	  err = update_class_attributes (parser, statement);
	}
      else
	{
	  /* OID list update */
	  err = update_objs_for_list_file (parser, list_id, statement);
	  /* free returned QFILE_LIST_ID */
	  if (list_id)
	    {
	      if (err >= NO_ERROR && list_id->tuple_cnt > 0)
		{
		  err = sm_flush_and_decache_objects (class_obj, true);
		}
	      regu_free_listid (list_id);
	    }
	  if (err >= NO_ERROR)
	    {
	      pt_end_query (parser);
	    }
	}

      /* set result count */
      if (err >= NO_ERROR)
	{
	  result += err;
	}

      /* do delete part */
      if (statement->info.merge.update.has_delete)
	{
	  /* flush necessary objects before execute */
	  err = sm_flush_objects (class_obj);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }

	  /* enable authorization checking during methods in queries */
	  AU_ENABLE (parser->au_save);
	  err = do_select (parser, del_select_stmt);
	  AU_DISABLE (parser->au_save);

	  if (err >= NO_ERROR)
	    {
	      list_id = (QFILE_LIST_ID *) del_select_stmt->etc;
	      if (list_id)
		{
		  /* delete each oid */
		  err = delete_list_by_oids (parser, list_id);
		  /* set result count */
		  if (err >= NO_ERROR)
		    {
		      result += err;
		    }
		  if (err >= NO_ERROR && list_id->tuple_cnt > 0)
		    {
		      err = sm_flush_and_decache_objects (class_obj, true);
		    }
		  regu_free_listid (list_id);
		}
	      pt_end_query (parser);
	    }
	  parser_free_tree (parser, del_select_stmt);
	  del_select_stmt = NULL;
	}
    }

  /* do insert part */
  if (err >= NO_ERROR
      && (values_list = statement->info.merge.insert.value_clauses) != NULL)
    {
      PT_NODE *save_list;
      PT_MISC_TYPE save_type;

      ins_select_stmt =
	pt_to_merge_insert_query (parser, values_list->info.node_list.list,
				  &statement->info.merge);
      ins_select_stmt = mq_translate (parser, ins_select_stmt);
      if (ins_select_stmt == NULL)
	{
	  err = er_errid ();
	  if (err == NO_ERROR)
	    {
	      err = ER_GENERIC_ERROR;
	    }
	  if (old_wait_msecs >= -1)
	    {
	      (void) tran_reset_wait_times (old_wait_msecs);
	    }
	  goto exit;
	}

      /* save node list */
      save_type = values_list->info.node_list.list_type;
      save_list = values_list->info.node_list.list;

      values_list->info.node_list.list_type = PT_IS_SUBQUERY;
      values_list->info.node_list.list = ins_select_stmt;

      obt_begin_insert_values ();
      /* execute subquery & insert its results into target class */
      err = insert_subquery_results (parser, statement, values_list, flat,
				     &savepoint_name);
      if (parser->abort)
	{
	  err = er_errid ();
	}
      else if (err >= NO_ERROR)
	{
	  result += err;
	}

      /* restore node list */
      values_list->info.node_list.list_type = save_type;
      values_list->info.node_list.list = save_list;

      parser_free_tree (parser, ins_select_stmt);
      ins_select_stmt = NULL;
    }

  if (old_wait_msecs >= -1)
    {
      (void) tran_reset_wait_times (old_wait_msecs);
    }

  if (err >= NO_ERROR && db_is_vclass (class_obj))
    {
      err = sm_flush_objects (class_obj);
    }

exit:
  if (upd_select_stmt != NULL)
    {
      parser_free_tree (parser, upd_select_stmt);
    }
  if (ins_select_stmt != NULL)
    {
      parser_free_tree (parser, ins_select_stmt);
    }
  if (del_select_stmt != NULL)
    {
      parser_free_tree (parser, del_select_stmt);
    }

  if ((err < NO_ERROR) && er_errid () != NO_ERROR)
    {
      pt_record_error (parser, parser->statement_number,
		       statement->line_number, statement->column_number,
		       er_msg (), NULL);
    }
  /* If error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction. */
  if ((err < NO_ERROR) && savepoint_name
      && (err != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  AU_ENABLE (parser->au_save);

  return (err < NO_ERROR) ? err : result;
}

/*
 * do_prepare_merge() - Prepare the MERGE statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in/out): Parse tree of a MERGE statement
 *
 */
int
do_prepare_merge (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;
  PT_NODE *non_nulls_upd = NULL, *non_nulls_ins = NULL, *lhs, *flat, *spec;
  int has_unique = 0, has_trigger = 0, has_virt = 0, au_save;
  bool server_insert, server_update, server_op;

  PT_NODE *select_statement = NULL;
  PT_NODE *select_names = NULL, *select_values = NULL;
  PT_NODE *const_names = NULL, *const_values = NULL;
  PT_NODE **links = NULL;
  PT_NODE *default_expr_attrs = NULL;
  DB_OBJECT *class_obj;

  const char *qstr = NULL;
  int no_vals, no_consts;

  if (parser == NULL || statement == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS,
	      0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  if (pt_false_where (parser, statement))
    {
      /* nothing to merge */
      statement->xasl_id = NULL;
      goto cleanup;
    }

  if (statement->xasl_id)
    {
      /* already prepared */
      goto cleanup;
    }

  /* check into for triggers and virtual class */
  AU_SAVE_AND_DISABLE (au_save);

  spec = statement->info.merge.into;
  flat = spec->info.spec.flat_entity_list;
  class_obj = (flat) ? flat->info.name.db_object : NULL;

  if (statement->info.merge.update.assignment)
    {
      err = sm_class_has_triggers (class_obj, &has_trigger, TR_EVENT_UPDATE);
      if (err == NO_ERROR && !has_trigger)
	{
	  err = sm_class_has_triggers (class_obj, &has_trigger,
				       TR_EVENT_STATEMENT_UPDATE);
	}
      if (err == NO_ERROR && !has_trigger
	  && statement->info.merge.update.has_delete)
	{
	  err =
	    sm_class_has_triggers (class_obj, &has_trigger, TR_EVENT_DELETE);
	  if (err == NO_ERROR && !has_trigger)
	    {
	      err = sm_class_has_triggers (class_obj, &has_trigger,
					   TR_EVENT_STATEMENT_DELETE);
	    }
	}
    }
  if (err == NO_ERROR && !has_trigger
      && statement->info.merge.insert.value_clauses)
    {
      err = sm_class_has_triggers (class_obj, &has_trigger, TR_EVENT_INSERT);
      if (err == NO_ERROR && !has_trigger)
	{
	  err = sm_class_has_triggers (class_obj, &has_trigger,
				       TR_EVENT_STATEMENT_INSERT);
	}
    }

  has_virt = db_is_vclass (class_obj)
	     || ((flat) ? (flat->info.name.virt_object != NULL) : false);

  AU_RESTORE (au_save);

  if (err != NO_ERROR)
    {
      goto cleanup;
    }

  err = do_evaluate_default_expr (parser, flat);
  if (err != NO_ERROR)
    {
      goto cleanup;
    }

  /* check update part */
  if (statement->info.merge.update.assignment)
    {
      err = update_check_for_fk_cache_attr (parser, statement);
      if (err != NO_ERROR)
	{
	  goto cleanup;
	}

      /* check if the target class has UNIQUE constraint */
      err = update_check_for_constraints (parser, &has_unique, &non_nulls_upd,
					  statement);
      if (err != NO_ERROR)
	{
	  goto cleanup;
	}
      if (!statement->info.merge.has_unique)
	{
	  statement->info.merge.has_unique = (bool) has_unique;
	}

      server_update = !has_trigger && !has_virt
	&& !update_check_having_meta_attr (parser,
					   statement->info.merge.
					   update.assignment);

      lhs = statement->info.merge.update.assignment->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  lhs = lhs->info.expr.arg1;
	}

      /* if we are updating class attributes, not need to prepare */
      if (lhs->info.name.meta_class == PT_META_ATTR)
	{
	  statement->info.merge.update.do_class_attrs = true;
	  goto cleanup;
	}
    }
  else
    {
      server_update = !has_trigger && !has_virt;
    }

  /* check insert part */
  if (statement->info.merge.insert.value_clauses)
    {
      PT_NODE *value_clauses = statement->info.merge.insert.value_clauses;
      PT_NODE *attr, *attrs = statement->info.merge.insert.attr_list;

      if (prm_get_integer_value (PRM_ID_INSERT_MODE) & INSERT_SELECT)
	{
	  /* server insert cannot handle insert into a shared attribute */
	  server_insert = true;
	  attr = attrs;
	  while (attr)
	    {
	      if (attr->node_type != PT_NAME
		  || attr->info.name.meta_class != PT_NORMAL)
		{
		  server_insert = false;
		  break;
		}
	      attr = attr->next;
	    }
	}
      else
	{
	  server_insert = false;
	}

      err = insert_check_for_fk_cache_attr (parser, attrs,
					    flat->info.name.db_object);
      if (err != NO_ERROR)
	{
	  goto cleanup;
	}

      err = check_for_cons (parser, &has_unique, &non_nulls_ins, attrs,
			    flat->info.name.db_object);
      if (err != NO_ERROR)
	{
	  goto cleanup;
	}
      if (!statement->info.merge.has_unique)
	{
	  statement->info.merge.has_unique = (bool) has_unique;
	}

      /* check not nulls attrs are present in attr list */
      err = check_missing_non_null_attrs (parser, spec, attrs, false);
      if (err != NO_ERROR)
	{
	  goto cleanup;
	}
    }
  else
    {
      server_insert = !has_trigger && !has_virt;
    }

  server_op = (server_insert && server_update);
  statement->info.merge.server_op = server_op;

  if (server_op)
    {
      XASL_ID *xasl_id = NULL;
      XASL_NODE *xasl;
      char *stream;
      int size;

      /* make query string */
      parser->dont_prt_long_string = 1;
      parser->long_string_skipped = 0;
      parser->print_type_ambiguity = 0;
      PT_NODE_PRINT_TO_ALIAS (parser, statement,
			      (PT_CONVERT_RANGE | PT_PRINT_QUOTES));
      qstr = statement->alias_print;
      parser->dont_prt_long_string = 0;
      if (parser->long_string_skipped || parser->print_type_ambiguity)
	{
	  statement->cannot_prepare = 1;
	  goto cleanup;
	}

      /* lookup in XASL cache */
      if (statement->recompile == 0)
	{
	  err = query_prepare (qstr, NULL, 0, &xasl_id);
	  if (err != NO_ERROR)
	    {
	      err = er_errid ();
	    }
	}
      else
	{
	  err = qmgr_drop_query_plan (qstr, ws_identifier (db_get_user ()),
				      NULL, true);
	}

      if (!xasl_id && err == NO_ERROR)
	{
	  if (statement->info.merge.insert.value_clauses)
	    {
	      err =
		check_for_default_expr (parser,
					statement->info.merge.insert.
					attr_list, &default_expr_attrs,
					flat->info.name.db_object);
	      if (err != NO_ERROR)
		{
		  statement->use_plan_cache = 0;
		  statement->xasl_id = NULL;
		  goto cleanup;
		}
	    }

	  /* mark the beginning of another level of xasl packing */
	  pt_enter_packing_buf ();

	  AU_SAVE_AND_DISABLE (au_save);

	  /* generate MERGE XASL */
	  xasl = pt_to_merge_xasl (parser, statement, &non_nulls_upd,
				   &non_nulls_ins, default_expr_attrs);

	  AU_RESTORE (au_save);

	  stream = NULL;

	  if (xasl && (err >= NO_ERROR))
	    {
	      err = xts_map_xasl_to_stream (xasl, &stream, &size);
	      if (err != NO_ERROR)
		{
		  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		}

	      /* clear constant values */
	      if (xasl->proc.merge.update_xasl)
		{
		  int i;
		  UPDATE_PROC_NODE *update =
		    &xasl->proc.merge.update_xasl->proc.update;
		  for (i = update->no_assigns - 1; i >= 0; i--)
		    {
		      if (update->assigns[i].constant)
			{
			  pr_clear_value (update->assigns[i].constant);
			}
		    }
		}
	    }
	  else
	    {
	      err = er_errid ();
	      pt_record_error (parser, parser->statement_number,
			       statement->line_number,
			       statement->column_number, er_msg (), NULL);
	    }

	  /* mark the end of another level of xasl packing */
	  pt_exit_packing_buf ();

	  /* cache the XASL */
	  if (stream && (err >= NO_ERROR))
	    {
	      err = query_prepare (qstr, stream, size, &xasl_id);
	      if (err != NO_ERROR)
		{
		  err = er_errid ();
		}
	    }

	  /* free 'stream' that is allocated inside of
	     xts_map_xasl_to_stream() */
	  if (stream)
	    {
	      free_and_init (stream);
	    }
	  statement->use_plan_cache = 0;
	  statement->xasl_id = xasl_id;
	}
      else
	{
	  if (err == NO_ERROR)
	    {
	      statement->use_plan_cache = 1;
	      statement->xasl_id = xasl_id;
	    }
	  else
	    {
	      statement->use_plan_cache = 0;
	    }

	  goto cleanup;
	}
    }
  else
    {
      if (statement->info.merge.update.assignment)
	{
	  /* make the SELECT statement for OID list to be updated */
	  no_vals = 0;
	  no_consts = 0;

	  err =
	    pt_get_assignment_lists (parser, &select_names, &select_values,
				     &const_names, &const_values, &no_vals,
				     &no_consts,
				     statement->info.merge.update.assignment,
				     &links);
	  if (err != NO_ERROR)
	    {
	      goto cleanup;
	    }

	  select_statement =
	    pt_to_merge_upd_del_query (parser, select_values,
				       &statement->info.merge,
				       PT_COMPOSITE_LOCKING_UPDATE);

	  /* restore tree structure; pt_get_assignment_lists() */
	  pt_restore_assignment_links (statement->info.merge.update.
				       assignment, links, -1);

	  /* translate views or virtual classes into base classes;
	     If we are updating a proxy, the SELECT is not yet fully
	     translated. If we are updating anything else, this is a no-op. */

	  /* this prevents authorization checking during view transformation */
	  AU_SAVE_AND_DISABLE (au_save);
	  select_statement = mq_translate (parser, select_statement);
	  AU_RESTORE (au_save);
	  if (select_statement)
	    {
	      /* get XASL_ID by calling do_prepare_select() */
	      err = do_prepare_select (parser, select_statement);
	      /* save the XASL_ID to be used by do_execute_merge() */
	      statement->xasl_id = select_statement->xasl_id;
	      /* deallocate the SELECT statement */
	      parser_free_tree (parser, select_statement);
	    }
	  else
	    {
	      err = er_errid ();
	      if (err == NO_ERROR)
		{
		  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		  err = er_errid ();
		}
	      goto cleanup;
	    }
	}

      /* nothing to do for merge delete and insert parts */
    }

cleanup:
  if (non_nulls_upd)
    {
      parser_free_tree (parser, non_nulls_upd);
      non_nulls_upd = NULL;
    }
  if (non_nulls_ins)
    {
      parser_free_tree (parser, non_nulls_ins);
      non_nulls_ins = NULL;
    }
  return err;
}

/*
 * do_execute_merge() - Execute the prepared MERGE statement
 *   return: Error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a MERGE statement
 *
 */
int
do_execute_merge (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR, result = 0;
  PT_NODE *flat, *spec = NULL, *values_list = NULL;
  const char *savepoint_name;
  DB_OBJECT *class_obj;
  QFILE_LIST_ID *list_id = NULL;
  int au_save;
  int wait_msecs = -2, old_wait_msecs = -2;
  float hint_waitsecs;
  PT_NODE *hint_arg;

  CHECK_MODIFICATION_ERROR ();

  /* savepoint for statement atomicity */
  savepoint_name = NULL;

  if (!statement->info.merge.update.assignment
      && !statement->info.merge.insert.value_clauses && !statement->xasl_id)
    {
      /* nothing to execute */
      statement->etc = NULL;
      goto exit;
    }

  spec = statement->info.merge.into;
  flat = spec->info.spec.flat_entity_list;
  if (flat == NULL)
    {
      err = ER_GENERIC_ERROR;
      goto exit;
    }
  class_obj = flat->info.name.db_object;

  /* The IX lock on the class is sufficient.
   * DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
  if (locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE) == NULL)
    {
      err = er_errid ();
      goto exit;
    }

  if (statement->info.merge.server_op)
    {
      /* server side execution */

      int query_flag = parser->exec_mode | ASYNC_UNEXECUTABLE;

      /* flush necessary objects before execute */
      err = sm_flush_objects (class_obj);
      if (err != NO_ERROR)
	{
	  goto exit;
	}

      if (statement->do_not_keep == 0)
	{
	  query_flag |= KEEP_PLAN_CACHE;
	}
      query_flag |= NOT_FROM_RESULT_CACHE;
      query_flag |= RESULT_CACHE_INHIBITED;

      AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
      if (statement->info.merge.insert.value_clauses)
	{
	  obt_begin_insert_values ();
	}

      list_id = NULL;
      parser->query_id = -1;

      err = query_execute (statement->xasl_id, &parser->query_id,
			   parser->host_var_count + parser->auto_param_count,
			   parser->host_variables, &list_id, query_flag,
			   NULL, NULL);
      AU_RESTORE (au_save);
      if (err != NO_ERROR)
	{
	  goto exit;
	}

      /* free returned QFILE_LIST_ID */
      if (list_id)
	{
	  if (list_id->tuple_cnt > 0)
	    {
	      err = sm_flush_and_decache_objects (class_obj, true);
	    }
	  if (err >= NO_ERROR)
	    {
	      result += list_id->tuple_cnt;
	    }
	  regu_free_listid (list_id);
	}
      /* end the query; reset query_id and call qmgr_end_query() */
      pt_end_query (parser);
    }
  else
    {
      /* client side execution */

      if (statement->info.merge.update.assignment
	  && !statement->info.merge.update.do_class_attrs)
	{
	  int query_flag = parser->exec_mode | ASYNC_UNEXECUTABLE;

	  /* flush necessary objects before execute */
	  err = sm_flush_objects (class_obj);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (statement->do_not_keep == 0)
	    {
	      query_flag |= KEEP_PLAN_CACHE;
	    }
	  query_flag |= NOT_FROM_RESULT_CACHE;
	  query_flag |= RESULT_CACHE_INHIBITED;

	  AU_SAVE_AND_ENABLE (au_save);	/* this insures authorization
					   checking for method */
	  list_id = NULL;
	  parser->query_id = -1;
	  err =
	    query_execute (statement->xasl_id, &parser->query_id,
			   parser->host_var_count + parser->auto_param_count,
			   parser->host_variables, &list_id, query_flag,
			   NULL, NULL);
	  AU_RESTORE (au_save);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }
	}

      hint_arg = statement->info.merge.waitsecs_hint;
      if ((statement->info.merge.hint & PT_HINT_LK_TIMEOUT)
	  && PT_IS_HINT_NODE (hint_arg))
	{
	  hint_waitsecs = (float) atof (hint_arg->info.name.original);
	  if (hint_waitsecs > 0)
	    {
	      wait_msecs = (int) (hint_waitsecs * 1000);
	    }
	  else
	    {
	      wait_msecs = (int) hint_waitsecs;
	    }
	  if (wait_msecs >= -1)
	    {
	      old_wait_msecs = TM_TRAN_WAIT_MSECS ();
	      (void) tran_reset_wait_times (wait_msecs);
	    }
	}

      /* update part */
      if (statement->info.merge.update.assignment)
	{
	  AU_SAVE_AND_DISABLE (au_save);

	  if (statement->info.merge.update.do_class_attrs)
	    {
	      /* update class attributes */
	      err = update_class_attributes (parser, statement);
	    }
	  else
	    {
	      /* OID list update */
	      err = update_objs_for_list_file (parser, list_id, statement);
	    }

	  AU_RESTORE (au_save);

	  /* set result count */
	  if (err >= NO_ERROR)
	    {
	      result += err;
	    }

	  if (!statement->info.merge.update.do_class_attrs)
	    {
	      /* free returned QFILE_LIST_ID */
	      if (list_id)
		{
		  if (err >= NO_ERROR && list_id->tuple_cnt > 0)
		    {
		      err = sm_flush_and_decache_objects (class_obj, true);
		    }
		  regu_free_listid (list_id);
		}
	      /* end the query; reset query_id and call qmgr_end_query() */
	      pt_end_query (parser);
	    }

	  /* delete part */
	  if (err >= NO_ERROR && statement->info.merge.update.has_delete)
	    {
	      PT_NODE *select_stmt;

	      select_stmt =
		pt_to_merge_upd_del_query (parser, NULL,
					   &statement->info.merge,
					   PT_COMPOSITE_LOCKING_DELETE);
	      select_stmt = mq_translate (parser, select_stmt);
	      if (select_stmt == NULL)
		{
		  if (select_stmt == NULL)
		    {
		      err = er_errid ();
		      if (err == NO_ERROR)
			{
			  err = ER_GENERIC_ERROR;
			}
		      if (old_wait_msecs >= -1)
			{
			  (void) tran_reset_wait_times (old_wait_msecs);
			}
		      goto exit;
		    }
		}

	      AU_SAVE_AND_ENABLE (au_save);
	      err = do_select (parser, select_stmt);
	      AU_RESTORE (au_save);

	      if (err >= NO_ERROR)
		{
		  list_id = (QFILE_LIST_ID *) select_stmt->etc;
		  if (list_id)
		    {
		      /* delete each oid */
		      AU_SAVE_AND_DISABLE (au_save);
		      err = delete_list_by_oids (parser, list_id);
		      AU_RESTORE (au_save);
		      /* set result count */
		      if (err >= NO_ERROR)
			{
			  result += err;
			}
		      if (err >= NO_ERROR && list_id->tuple_cnt > 0)
			{
			  err =
			    sm_flush_and_decache_objects (class_obj, true);
			}
		      regu_free_listid (list_id);
		    }
		  pt_end_query (parser);
		}
	      parser_free_tree (parser, select_stmt);
	    }
	}

      /* insert part */
      if (err >= NO_ERROR
	  && (values_list = statement->info.merge.insert.value_clauses)
	  != NULL)
	{
	  PT_NODE *select_stmt, *save_list;
	  PT_MISC_TYPE save_type;

	  select_stmt =
	    pt_to_merge_insert_query (parser,
				      values_list->info.node_list.list,
				      &statement->info.merge);
	  select_stmt = mq_translate (parser, select_stmt);

	  if (select_stmt == NULL)
	    {
	      err = er_errid ();
	      if (err == NO_ERROR)
		{
		  err = ER_GENERIC_ERROR;
		}
	      if (old_wait_msecs >= -1)
		{
		  (void) tran_reset_wait_times (old_wait_msecs);
		}
	      goto exit;
	    }

	  /* save node list */
	  save_type = values_list->info.node_list.list_type;
	  save_list = values_list->info.node_list.list;

	  values_list->info.node_list.list_type = PT_IS_SUBQUERY;
	  values_list->info.node_list.list = select_stmt;

	  AU_SAVE_AND_DISABLE (au_save);

	  obt_begin_insert_values ();
	  /* execute subquery & insert its results into target class */
	  err = insert_subquery_results (parser, statement, values_list, flat,
					 &savepoint_name);
	  if (parser->abort)
	    {
	      err = er_errid ();
	    }
	  else if (err >= NO_ERROR)
	    {
	      result += err;
	    }

	  AU_RESTORE (au_save);

	  /* restore node list */
	  values_list->info.node_list.list_type = save_type;
	  values_list->info.node_list.list = save_list;

	  parser_free_tree (parser, select_stmt);
	}

      if (old_wait_msecs >= -1)
	{
	  (void) tran_reset_wait_times (old_wait_msecs);
	}

      if (err >= NO_ERROR && db_is_vclass (class_obj))
	{
	  err = sm_flush_objects (class_obj);
	}
    }

exit:
  if ((err < NO_ERROR) && er_errid () != NO_ERROR)
    {
      pt_record_error (parser, parser->statement_number,
		       statement->line_number, statement->column_number,
		       er_msg (), NULL);
    }
  /* If error and a savepoint was created, rollback to savepoint.
     No need to rollback if the TM aborted the transaction. */
  if ((err < NO_ERROR) && savepoint_name
      && (err != ER_LK_UNILATERALLY_ABORTED))
    {
      do_rollback_savepoints (parser, savepoint_name);
    }

  return (err < NO_ERROR) ? err : result;
}

/*
 * do_set_names() - Set the client charset and collation.
 *   return: Error code if it fails
 *   parser(in): Parser context
 *   statement(in): Parse tree of a set transaction statement
 *
 * Note:
 */
int
do_set_names (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int error = ER_GENERIC_ERROR;
  int charset_id, collation_id;

  if (statement->info.set_names.charset_node != NULL)
    {
      error = pt_check_grammar_charset_collation (parser,
						  statement->info.set_names.
						  charset_node,
						  statement->info.set_names.
						  collation_node, &charset_id,
						  &collation_id);
    }

  if (error == NO_ERROR)
    {
      lang_set_client_charset_coll ((INTL_CODESET) charset_id, collation_id);
    }

  return (error != NO_ERROR) ? ER_OBJ_INVALID_ARGUMENTS : NO_ERROR;
}
