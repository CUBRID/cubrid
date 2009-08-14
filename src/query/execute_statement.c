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
#include "dbval.h"
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

/*
 * Function Group:
 * Do create/alter/drop serial statement
 *
 */

#define SR_CLASS_NAME			CT_SERIAL_NAME

#define SR_ATT_NAME			"name"
#define SR_ATT_OWNER 			"owner"
#define SR_ATT_CURRENT_VAL		"current_val"
#define SR_ATT_INCREMENT_VAL		"increment_val"
#define SR_ATT_MAX_VAL			"max_val"
#define SR_ATT_MIN_VAL			"min_val"
#define SR_ATT_CYCLIC			"cyclic"
#define SR_ATT_STARTED			"started"
#define SR_ATT_CLASS_NAME       	"class_name"
#define SR_ATT_ATT_NAME         	"att_name"

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

  serial_class = db_find_class (SR_CLASS_NAME);
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
  error = dbt_put_internal (obj_tmpl, SR_ATT_NAME, &value);
  pr_clear_value (&value);
  if (error < 0)
    {
      goto end;
    }

  /* owner */
  db_make_object (&value, Au_user);
  error = dbt_put_internal (obj_tmpl, SR_ATT_OWNER, &value);
  pr_clear_value (&value);
  if (error < 0)
    goto end;

  /* current_val */
  error = dbt_put_internal (obj_tmpl, SR_ATT_CURRENT_VAL, current_val);
  if (error < 0)
    {
      goto end;
    }

  /* increment_val */
  error = dbt_put_internal (obj_tmpl, SR_ATT_INCREMENT_VAL, inc_val);
  if (error < 0)
    {
      goto end;
    }

  /* min_val */
  error = dbt_put_internal (obj_tmpl, SR_ATT_MIN_VAL, min_val);
  if (error < 0)
    goto end;

  /* max_val */
  error = dbt_put_internal (obj_tmpl, SR_ATT_MAX_VAL, max_val);
  if (error < 0)
    {
      goto end;
    }

  /* cyclic */
  db_make_int (&value, cyclic);	/* always false */
  error = dbt_put_internal (obj_tmpl, SR_ATT_CYCLIC, &value);
  pr_clear_value (&value);
  if (error < 0)
    {
      goto end;
    }

  /* started */
  db_make_int (&value, started);
  error = dbt_put_internal (obj_tmpl, SR_ATT_STARTED, &value);
  pr_clear_value (&value);
  if (error < 0)
    {
      goto end;
    }

  /* class name */
  if (class_name)
    {
      db_make_string (&value, class_name);
      error = dbt_put_internal (obj_tmpl, SR_ATT_CLASS_NAME, &value);
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
      error = dbt_put_internal (obj_tmpl, SR_ATT_ATT_NAME, &value);
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

  if (!serial_obj || !class_name || !att_name)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  db_make_null (&value);

  serial_object = serial_obj;
  sm_downcase_name (att_name, att_downcase_name, SM_MAX_IDENTIFIER_LENGTH);
  att_name = att_downcase_name;

  /* serial_name : <class_name>_ai_<att_name> */
  serial_name = (char *) malloc (strlen (class_name) + strlen (att_name) + 5);
  if (serial_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
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
  error = dbt_put_internal (obj_tmpl, SR_ATT_NAME, &value);
  if (error < 0)
    {
      goto update_auto_increment_error;
    }

  /* class name */
  db_make_string (&value, class_name);
  error = dbt_put_internal (obj_tmpl, SR_ATT_CLASS_NAME, &value);
  if (error < 0)
    {
      goto update_auto_increment_error;
    }

  /* att name */
  db_make_string (&value, att_name);
  error = dbt_put_internal (obj_tmpl, SR_ATT_ATT_NAME, &value);
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
 * do_get_serial_obj_id() -
 *   return: Error code
 *   serial_obj_id(out):
 *   found(out):
 *   serial_name(in):
 *
 * Note:
 */
int
do_get_serial_obj_id (DB_IDENTIFIER * serial_obj_id, int *found,
		      const char *serial_name)
{
  DB_OBJECT *class_mop, *mop;
  DB_VALUE val;
  char *p;

  *found = 0;

  er_stack_push ();
  class_mop = sm_find_class (SR_CLASS_NAME);
  er_stack_pop ();
  if (class_mop == NULL)
    {
      return ER_FAILED;
    }

  p = (char *) malloc (strlen (serial_name) + 1);
  if (p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
      return ER_FAILED;
    }

  intl_mbs_lower (serial_name, p);

  db_make_string (&val, p);

  er_stack_push ();
  mop = db_find_unique (class_mop, SR_ATT_NAME, &val);
  er_stack_pop ();
  if (mop)
    {
      DB_IDENTIFIER *db_id = db_identifier (mop);

      *found = 1;
      if (db_id != NULL)
	{
	  *serial_obj_id = *db_id;
	}
    }

  pr_clear_value (&val);
  free_and_init (p);
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
  DB_IDENTIFIER serial_obj_id;
  DB_VALUE value, *pval = NULL;

  char *name = NULL;
  PT_NODE *start_val_node;
  PT_NODE *inc_val_node;
  PT_NODE *max_val_node;
  PT_NODE *min_val_node;

  DB_VALUE zero, e37, under_e36;
  DB_VALUE start_val, inc_val, max_val, min_val;
  DB_VALUE cmp_result, cmp_result2;

  int inc_val_flag = 0, cyclic;
  DB_DATA_STATUS data_stat;
  int error = NO_ERROR;
  int found = 0, r = 0, save;
  bool au_disable_flag = false;
  char *p = NULL;

  CHECK_MODIFICATION_ERROR ();

  if (PRM_BLOCK_DDL_STATEMENT)
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
  /*
   * find db_serial_class
   */
  serial_class = db_find_class (SR_CLASS_NAME);
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
  p = (char *) malloc (strlen (name) + 1);
  if (p == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }
  intl_mbs_lower (name, p);

  r = do_get_serial_obj_id (&serial_obj_id, &found, p);
  if (r == NO_ERROR && found)
    {
      error = ER_QPROC_SERIAL_ALREADY_EXIST;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		  MSGCAT_SEMANTIC_SERIAL_ALREADY_EXIST, name);
      goto end;
    }

  /* get all values as string */
  numeric_coerce_string_to_num ("0", &zero);
  numeric_coerce_string_to_num ("10000000000000000000000000000000000000",
				&e37);
  numeric_coerce_string_to_num ("-1000000000000000000000000000000000000",
				&under_e36);
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
    }

  db_value_domain_init (&min_val,
			DB_TYPE_NUMERIC, DB_MAX_NUMERIC_PRECISION, 0);
  /* min_val */
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
    }
  else
    {
      if (inc_val_flag > 0)
	{
	  if (start_val_node != NULL)
	    {
	      db_value_clone (&start_val, &min_val);
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
	}
      else
	{
	  /* start_val = max_val; */
	  db_value_clone (&max_val, &start_val);
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
  if (inc_val_flag > 0)
    {
      error = numeric_db_value_compare (&max_val, &e37, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* max_val > 1.0e37 */
      if (DB_GET_INT (&cmp_result) > 0)
	{
	  error = ER_QPROC_SERIAL_RANGE_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW, 0);
	  goto end;
	}

      error = numeric_db_value_compare (&min_val, &under_e36, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* min_val < -1.0e36 */
      if (DB_GET_INT (&cmp_result) < 0)
	{
	  error = ER_QPROC_SERIAL_RANGE_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW, 0);
	  goto end;
	}

      error = numeric_db_value_compare (&min_val, &start_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      error = numeric_db_value_compare (&min_val, &max_val, &cmp_result2);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* min_val > start_val || min_val >= max_val */
      if (DB_GET_INT (&cmp_result) > 0 || DB_GET_INT (&cmp_result2) >= 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID, 0);
	  goto end;
	}

      error = numeric_db_value_compare (&max_val, &start_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* max_val < start_val */
      if (DB_GET_INT (&cmp_result) < 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID, 0);
	  goto end;
	}

      numeric_db_value_sub (&max_val, &min_val, &value);
      error = numeric_db_value_compare (&value, &inc_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      pr_clear_value (&value);
      /* (max_val-min_val) < inc_val */
      if (DB_GET_INT (&cmp_result) < 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID, 0);
	  goto end;
	}
    }
  else
    {
      error = numeric_db_value_compare (&max_val, &e37, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* max_val > 1.0e37 */
      if (DB_GET_INT (&cmp_result) > 0)
	{
	  error = ER_QPROC_SERIAL_RANGE_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW, 0);
	  goto end;
	}

      error = numeric_db_value_compare (&min_val, &under_e36, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* min_val < -1.0e36 */
      if (DB_GET_INT (&cmp_result) < 0)
	{
	  error = ER_QPROC_SERIAL_RANGE_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW, 0);
	  goto end;
	}

      error = numeric_db_value_compare (&max_val, &start_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      error = numeric_db_value_compare (&max_val, &min_val, &cmp_result2);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* max_val < start_val || max_val <= min_val */
      if (DB_GET_INT (&cmp_result) < 0 || DB_GET_INT (&cmp_result2) <= 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID, 0);
	  goto end;
	}

      error = numeric_db_value_compare (&min_val, &start_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* min_val > start_val */
      if (DB_GET_INT (&cmp_result) > 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID, 0);
	  goto end;
	}

      numeric_db_value_sub (&min_val, &max_val, &value);
      error = numeric_db_value_compare (&value, &inc_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      pr_clear_value (&value);
      /* (min_val-max_val) > inc_val */
      if (DB_GET_INT (&cmp_result) > 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID, 0);
	  goto end;
	}
    }

  /* now create serial object which is insert into db_serial */
  AU_DISABLE (save);
  au_disable_flag = true;

  error = do_create_serial_internal (&serial_object,
				     p,
				     &start_val,
				     &inc_val,
				     &min_val,
				     &max_val, cyclic, 0, NULL, NULL);

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
  DB_OBJECT *serial_class = NULL;
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

  db_make_null (&e38);
  db_make_null (&value);
  db_make_null (&zero);
  db_make_null (&cmp_result);
  db_make_null (&start_val);
  db_make_null (&inc_val);
  db_make_null (&max_val);
  db_make_null (&min_val);

  numeric_coerce_string_to_num ("0", &zero);
  numeric_coerce_string_to_num ("99999999999999999999999999999999999999",
				&e38);

  auto_increment_node = att->info.attr_def.auto_increment;
  if (auto_increment_node == NULL)
    {
      goto end;
    }

  /*
   * find db_serial
   */
  serial_class = db_find_class (SR_CLASS_NAME);
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
  serial_name = (char *) malloc (strlen (class_name) + strlen (att_name) + 5);
  if (serial_name == NULL)
    {
      goto end;
    }

  SET_AUTO_INCREMENT_SERIAL_NAME (serial_name, class_name, att_name);

  r = do_get_serial_obj_id (&serial_obj_id, &found, serial_name);
  if (r == 0 && found)
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
	  error = er_errid ();
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

      (void) numeric_coerce_string_to_num (num, &value);
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
  error = do_create_serial_internal (serial_object,
				     serial_name,
				     &start_val,
				     &inc_val,
				     &min_val,
				     &max_val, 0, 0, class_name, att_name);

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

  DB_VALUE zero, e37, under_e36;
  DB_DATA_STATUS data_stat;
  DB_VALUE old_inc_val, old_max_val, old_min_val, current_val, start_val;
  DB_VALUE new_inc_val, new_max_val, new_min_val;
  DB_VALUE cmp_result, cmp_result2;
  DB_VALUE class_name_val;

  int new_inc_val_flag = 0, new_cyclic;
  int cur_val_change, inc_val_change, max_val_change, min_val_change,
    cyclic_change;

  int error = NO_ERROR;
  int found = 0, r = 0, save;
  bool au_disable_flag = false;

  CHECK_MODIFICATION_ERROR ();

  if (PRM_BLOCK_DDL_STATEMENT)
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

  /*
   * find db_serial_class
   */
  serial_class = db_find_class (SR_CLASS_NAME);
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

  r = do_get_serial_obj_id (&serial_obj_id, &found, name);
  if (r == NO_ERROR && found)
    {
      serial_object = db_object (&serial_obj_id);
      if (serial_object == NULL)
	{
	  error = ER_QPROC_SERIAL_NOT_FOUND;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED, name);
	  goto end;
	}
    }
  else
    {
      error = ER_QPROC_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED, name);
      goto end;
    }

  error = db_get (serial_object, SR_ATT_CLASS_NAME, &class_name_val);
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
  error = db_get (serial_object, SR_ATT_CURRENT_VAL, &current_val);
  if (error < 0)
    {
      goto end;
    }

  error = db_get (serial_object, SR_ATT_INCREMENT_VAL, &old_inc_val);
  if (error < 0)
    {
      goto end;
    }

  error = db_get (serial_object, SR_ATT_MAX_VAL, &old_max_val);
  if (error < 0)
    goto end;

  error = db_get (serial_object, SR_ATT_MIN_VAL, &old_min_val);
  if (error < 0)
    {
      goto end;
    }

  /* Now, get new values from node */

  numeric_coerce_string_to_num ("0", &zero);
  numeric_coerce_string_to_num ("10000000000000000000000000000000000000",
				&e37);
  numeric_coerce_string_to_num ("-1000000000000000000000000000000000000",
				&under_e36);
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
     fprintf (stderr,
     "inc_val_change(%d), min_val_change(%d), max_val_change(%d), cyclic_change(%d)\n",
     inc_val_change, min_val_change, max_val_change, cyclic_change);
   */
  /*
   * check values
   * min_val    start_val     max_val
   *    |--^--^--^--o--^--^--^--^---|
   *                   <--> inc_val
   */
  if (new_inc_val_flag > 0)
    {
      error = numeric_db_value_compare (&new_max_val, &e37, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /*  new_max_val > 1.0e37 */
      if (DB_GET_INT (&cmp_result) > 0)
	{
	  error = ER_QPROC_SERIAL_RANGE_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW, 0);
	  goto end;
	}
      error =
	numeric_db_value_compare (&new_min_val, &under_e36, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* new_min_val < -1.0e36 */
      if (DB_GET_INT (&cmp_result) < 0)
	{
	  error = ER_QPROC_SERIAL_RANGE_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW, 0);
	  goto end;
	}

      error =
	numeric_db_value_compare (&new_min_val, &start_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      error =
	numeric_db_value_compare (&new_min_val, &new_max_val, &cmp_result2);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* new_min_val > current_val || new_min_val >= new_max_val */
      if (DB_GET_INT (&cmp_result) > 0 || DB_GET_INT (&cmp_result2) >= 0)
	{
	  if (min_val_change || cur_val_change)
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID, 0);
	      goto end;
	    }
	  else
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID, 0);
	      goto end;
	    }
	}

      error =
	numeric_db_value_compare (&new_max_val, &start_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      if (DB_GET_INT (&cmp_result) < 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID, 0);
	  goto end;
	}

      numeric_db_value_sub (&new_max_val, &new_min_val, &value);
      error = numeric_db_value_compare (&value, &new_inc_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      pr_clear_value (&value);
      /* (new_max_val-new_min_val) < new_inc_val */
      if (DB_GET_INT (&cmp_result) < 0)
	{
	  if (inc_val_change)
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID, 0);
	      goto end;
	    }
	  else if (max_val_change)
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID, 0);
	      goto end;
	    }
	  else
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID, 0);
	      goto end;
	    }
	}
    }
  else
    {
      error = numeric_db_value_compare (&new_max_val, &e37, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* new_max_val > 1.0e37 */
      if (DB_GET_INT (&cmp_result) > 0)
	{
	  error = ER_QPROC_SERIAL_RANGE_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW, 0);
	  goto end;
	}

      error =
	numeric_db_value_compare (&new_min_val, &under_e36, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* new_min_val < -1.0e36 */
      if (DB_GET_INT (&cmp_result) < 0)
	{
	  error = ER_QPROC_SERIAL_RANGE_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW, 0);
	  goto end;
	}

      error =
	numeric_db_value_compare (&new_max_val, &start_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      error =
	numeric_db_value_compare (&new_max_val, &new_min_val, &cmp_result2);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      /* new_max_val < current_val || new_max_val <= new_min_val */
      if (DB_GET_INT (&cmp_result) < 0 || DB_GET_INT (&cmp_result2) <= 0)
	{
	  if (max_val_change)
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID, 0);
	      goto end;
	    }
	  else
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID, 0);
	      goto end;
	    }
	}

      error =
	numeric_db_value_compare (&new_min_val, &start_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      if (DB_GET_INT (&cmp_result) > 0)
	{
	  error = ER_INVALID_SERIAL_VALUE;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
		      MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID, 0);
	  goto end;
	}

      numeric_db_value_sub (&new_min_val, &new_max_val, &value);
      error = numeric_db_value_compare (&value, &new_inc_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      pr_clear_value (&value);
      /* (new_min_val-new_max_val) > new_inc_val */
      if (DB_GET_INT (&cmp_result) > 0)
	{
	  if (inc_val_change)
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID, 0);
	      goto end;
	    }
	  else if (min_val_change)
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID, 0);
	      goto end;
	    }
	  else
	    {
	      error = ER_INVALID_SERIAL_VALUE;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID, 0);
	      goto end;
	    }
	}
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
      error = dbt_put_internal (obj_tmpl, SR_ATT_CURRENT_VAL, &start_val);
      if (error < 0)
	{
	  goto end;
	}
    }

  /* increment_val */
  if (inc_val_change)
    {
      error = dbt_put_internal (obj_tmpl, SR_ATT_INCREMENT_VAL, &new_inc_val);
      if (error < 0)
	{
	  goto end;
	}
    }

  /* max_val */
  if (max_val_change)
    {
      error = dbt_put_internal (obj_tmpl, SR_ATT_MAX_VAL, &new_max_val);
      if (error < 0)
	{
	  goto end;
	}
    }

  /* min_val */
  if (min_val_change)
    {
      error = dbt_put_internal (obj_tmpl, SR_ATT_MIN_VAL, &new_min_val);
      if (error < 0)
	{
	  goto end;
	}
    }

  /* cyclic */
  if (cyclic_change)
    {
      db_make_int (&value, new_cyclic);
      error = dbt_put_internal (obj_tmpl, SR_ATT_CYCLIC, &value);
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

  return NO_ERROR;

end:
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

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  db_make_null (&class_name_val);

  serial_class = db_find_class (SR_CLASS_NAME);
  if (serial_class == NULL)
    {
      error = ER_QPROC_DB_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto end;
    }

  name = (char *) PT_NODE_SR_NAME (statement);

  r = do_get_serial_obj_id (&serial_obj_id, &found, name);
  if (r == NO_ERROR && found)
    {
      serial_object = db_object (&serial_obj_id);
      if (serial_object == NULL)
	{
	  error = ER_QPROC_SERIAL_NOT_FOUND;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		      MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED, name);
	  goto end;
	}
    }
  else
    {
      error = ER_QPROC_SERIAL_NOT_FOUND;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name);
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		  MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED, name);
      goto end;
    }

  error = db_get (serial_object, SR_ATT_CLASS_NAME, &class_name_val);
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

  return NO_ERROR;

end:
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

  /* If it is an internally created statement,
     set it's host variable info again to search host variables at parent parser */
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

      switch (statement->node_type)
	{
	case PT_ALTER:
	  error = do_check_internal_statements (parser, statement,
						/* statement->info.alter.
						   internal_stmts, */
						do_alter);
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
	  error = do_check_internal_statements (parser, statement,
						/* statement->info.create_entity.
						   internal_stmts, */
						do_create_entity);
	  break;

	case PT_CREATE_INDEX:
	  error = do_create_index (parser, statement);
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
	  error = do_drop_index (parser, statement);
	  break;

	case PT_ALTER_INDEX:
	  error = do_alter_index (parser, statement);
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

	case PT_UPDATE:
	  error = do_check_update_trigger (parser, statement, do_update);
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

	case PT_DROP_STORED_PROCEDURE:
	  error = jsp_drop_stored_procedure (parser, statement);
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, __FILE__, statement->line_number,
		  ER_PT_UNKNOWN_STATEMENT, 1, statement->node_type);
	  break;
	}

      /* restore execution flag */
      parser->exec_mode = old_exec_mode;

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
 * 	PEPARE includes query optimization and plan generation (XASL) for the SQL
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
#if 0				/* disabled until implementation completed */
      err = do_prepare_insert (parser, statement);
#endif
      break;
    case PT_UPDATE:
      err = do_prepare_update (parser, statement);
      break;
    case PT_SELECT:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
      err = do_prepare_select (parser, statement);
      break;
    default:
      /* there're no actions for other types of statements */
      break;
    }

  if (err == ER_FAILED)
    {
      err = er_errid ();
      if (err == NO_ERROR)
	{
	  err = ER_GENERIC_ERROR;
	}
    }
  return err;
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
 * 	at the time of exeuction stage.
 */
int
do_execute_statement (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  int err = NO_ERROR;
  QUERY_EXEC_MODE old_exec_mode;

  /* If it is an internally created statement,
     set it's host variable info again to search host variables at parent parser */
  SET_HOST_VARIABLES_IF_INTERNAL_STATEMENT (parser);

  /* only SELECT query can be executed in async mode */
  old_exec_mode = parser->exec_mode;
  parser->exec_mode = (statement->node_type == PT_SELECT) ?
    old_exec_mode : SYNC_EXEC;

  /* for the subset of nodes which represent top level statements,
     process them; for any other node, return an error */
  switch (statement->node_type)
    {
    case PT_CREATE_ENTITY:
      /* err = do_create_entity(parser, statement); */
      /* execute internal statements before and after do_create_entity() */
      err = do_check_internal_statements (parser, statement,
					  /* statement->info.create_entity.
					     internal_stmts, */
					  do_create_entity);
      break;
    case PT_CREATE_INDEX:
      err = do_create_index (parser, statement);
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
      /* err = do_alter(parser, statement); */
      /* execute internal statements before and after do_alter() */
      err = do_check_internal_statements (parser, statement,
					  /* statement->info.alter.
					     internal_stmts, */ do_alter);
      break;
    case PT_ALTER_INDEX:
      err = do_alter_index (parser, statement);
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
      err = do_drop_index (parser, statement);
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
#if 0				/* disabled until implementation completed */
      err = do_check_insert_trigger (parser, statement, do_execute_insert);
#else
      err = do_check_insert_trigger (parser, statement, do_insert);
#endif
      break;
    case PT_UPDATE:
      err = do_check_update_trigger (parser, statement, do_execute_update);
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
    case PT_DROP_STORED_PROCEDURE:
      err = jsp_drop_stored_procedure (parser, statement);
      break;
    default:
      er_set (ER_ERROR_SEVERITY, __FILE__, statement->line_number,
	      ER_PT_UNKNOWN_STATEMENT, 1, statement->node_type);
      break;
    }

  /* restore execution flag */
  parser->exec_mode = old_exec_mode;

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

  if (err == ER_FAILED)
    {
      err = er_errid ();
      if (err == NO_ERROR)
	{
	  err = ER_GENERIC_ERROR;
	}
    }
  return err;
}				/* do_execute_statement() */

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
	     So, it need to bind the paramters again */
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





/*
 * Function Group:
 * Parse tree to update statistics translation.
 *
 */

typedef enum
{
  CST_UNDEFINED,
  CST_NOBJECTS, CST_NPAGES, CST_NATTRIBUTES,
  CST_ATTR_MIN, CST_ATTR_MAX, CST_ATTR_NINDEXES,
  CST_BT_NLEAFS, CST_BT_NPAGES, CST_BT_HEIGHT, CST_BT_NKEYS,
  CST_BT_NOIDS, CST_BT_NNULLS, CST_BT_NUKEYS
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
#if 0
  {CST_BT_NOIDS, "#oids", 0, 0},
  {CST_BT_NNULLS, "#nulls", 0, 0},
  {CST_BT_NUKEYS, "#unique_keys", 0, 0},
#endif
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

	  error = sm_update_statistics (obj);
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
		  error = sm_update_statistics (sub_partitions[i]);
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
      t = intl_mbs_chr (s, ')');
      if (!t)
	{
	  t = intl_mbs_chr (s, ':');
	}
      if (t && t != s)
	{
	  size = CAST_STRLEN (t - s);
	  att = (char *) malloc (size + 1);
	  if (att)
	    {
	      if (intl_mbs_ncpy (att, s, size) != NULL)
		{
		  att[size] = '\0';
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
    case CST_BT_NOIDS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->oids);
	}
      break;
    case CST_BT_NNULLS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->nulls);
	}
      break;
    case CST_BT_NUKEYS:
      if (!attr_statsp || !bt_statsp)
	{
	  db_make_null (db_val);
	}
      else
	{
	  db_make_int (db_val, bt_statsp->ukeys);
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

  pt_evaluate_tree (parser, arg, &db_val);
  if (parser->error_msgs || DB_IS_NULL (&db_val))
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
      return pt_associate_label_with_value (into->info.name.original,
					    db_value_copy (ret_val));
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
    return db_2pc_prepare_to_commit_transaction (statement->info.
						 prepare_to_commit.trans_id);
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
  return tran_commit (statement->info.commit_work.retain_lock ? true : false);
}

/*
 * do_rollback_savepoints() - Rollback savepoints of named ldbs
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
 *   The function requires ldbnames is a list of ldbs
 *   and effects doing savepoints of named ldbs
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
	  pt_evaluate_tree (parser, name, &val);
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
	  pt_evaluate_tree (parser, name, &val);
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
  float lock_timeout = 0;
  DB_TRAN_ISOLATION tran_isolation = TRAN_UNKNOWN_ISOLATION;
  bool async_ws;
  int tran_num;
  const char *into_label;
  DB_VALUE *ins_val;
  PT_NODE *into_var;
  int error = NO_ERROR;

  (void) tran_get_tran_settings (&lock_timeout, &tran_isolation, &async_ws);

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
      db_make_float (ins_val, lock_timeout);
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
	  db_make_float (ins_val, lock_timeout);
	  break;

	default:
	  break;
	}

      /* enter {label, ins_val} pair into the label_table */
      error = pt_associate_label_with_value (into_label, ins_val);
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
	      pt_evaluate_tree (parser, mode->info.isolation_lvl.level, &val);

	      if (parser->error_msgs)
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
	  pt_evaluate_tree (parser, mode->info.timeout.val, &val);
	  if (parser->error_msgs)
	    {
	      return ER_GENERIC_ERROR;
	    }

	  if (check_timeout_value (parser, statement, &val) != NO_ERROR)
	    {
	      return ER_GENERIC_ERROR;
	    }
	  else
	    {
	      (void) tran_reset_wait_times (DB_GET_FLOAT (&val));
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
	pt_evaluate_tree (parser, statement->info.get_opt_lvl.args, &plan);
	if (parser->error_msgs)
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
      error = pt_associate_label_with_value (into_name, db_value_copy (val));
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

  pt_evaluate_tree (parser, p1, &val1);
  if (parser->error_msgs)
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
      pt_evaluate_tree (parser, p2, &val2);
      if (parser->error_msgs)
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
 * do_set_sys_params() - Set the system paramters defined in 'cubrid.conf'.
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
      pt_evaluate_tree (parser, val, &db_val);

      if (parser->error_msgs)
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
	  float dummy_lktimeout;
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
      if (tp_value_cast (val, val, tp_Domains[DB_TYPE_VARCHAR], false)
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

static int merge_mop_list_extension (DB_OBJLIST * new_objlist,
				     DB_OBJLIST ** list);
static DB_TRIGGER_EVENT convert_event_to_tr_event (const PT_EVENT_TYPE ev);
static DB_TRIGGER_TIME convert_misc_to_tr_time (const PT_MISC_TYPE pt_time);
static DB_TRIGGER_STATUS convert_misc_to_tr_status (const PT_MISC_TYPE
						    pt_status);
static int convert_speclist_to_objlist (DB_OBJLIST ** triglist,
					PT_NODE * specnode);
static int check_trigger (DB_TRIGGER_EVENT event, DB_OBJECT * class_,
			  const char **attributes, int attribute_count,
			  PT_DO_FUNC * do_func, PARSER_CONTEXT * parser,
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
 * convert_misc_to_tr_status() - Converts a PT_MISC_TYPE into the correspondint
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
 *				   correspondint TR_STATUE_TYPE.
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
 *   class(in): Target class
 *   attributes(in): Target attributes
 *   attribute_count(in): Number of target attributes
 *   do_func(in): Function to do
 *   parser(in): Parser context used by do_func
 *   statement(in): Parse tree of a statement used by do_func
 *
 * Note: The function checks if there is any active trigger defined on
 *       the target. If there is one, raise the trigger. Otherwise,
 *       perform the given do_ function.
 */
static int
check_trigger (DB_TRIGGER_EVENT event,
	       DB_OBJECT * class_,
	       const char **attributes,
	       int attribute_count,
	       PT_DO_FUNC * do_func, PARSER_CONTEXT * parser,
	       PT_NODE * statement)
{
  int err, result = NO_ERROR;
  TR_STATE *state;

  /* Prepare a trigger state for any triggers that must be raised in
     this statement */

  state = NULL;

  result = tr_prepare_statement (&state, event, class_,
				 attribute_count, attributes);

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
		}
	      else
		{
		  /* try to preserve the usual result value */
		  err = tr_after (state);
		  if (err)
		    {
		      result = err;
		    }
		}
	    }
	}
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
  PT_NODE *flat;
  DB_OBJECT *class_obj;

  if (PRM_BLOCK_NOWHERE_STATEMENT
      && statement->info.delete_.search_cond == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  /* get the delete class */
  flat = (statement->info.delete_.spec) ?
    statement->info.delete_.spec->info.spec.flat_entity_list : NULL;
  class_obj = (flat) ? flat->info.name.db_object : NULL;

  if (class_obj)
    {
      return check_trigger (TR_EVENT_STATEMENT_DELETE, class_obj, NULL, 0,
			    do_func, parser, statement);
    }
  return NO_ERROR;
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
  PT_NODE *flat;
  DB_OBJECT *class_obj;

  /* get the insert class */
  flat = (statement->info.insert.spec) ?
    statement->info.insert.spec->info.spec.flat_entity_list : NULL;
  class_obj = (flat) ? flat->info.name.db_object : NULL;

  if (class_obj)
    {
      return check_trigger (TR_EVENT_STATEMENT_INSERT, class_obj, NULL, 0,
			    do_func, parser, statement);
    }
  return NO_ERROR;
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
  PT_NODE *node;
  DB_OBJECT *class_obj;
  char **columns;
  int count = 0;
  int err;

  if (PRM_BLOCK_NOWHERE_STATEMENT
      && statement->info.update.search_cond == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  /* If this is an "update object" statement, we may not have a spec list
     yet. This may have been fixed due to the recent changes in
     pt_exec_trigger_stmt to do name resolution each time. */
  node = (statement->info.update.spec) ?
    statement->info.update.spec->info.spec.flat_entity_list :
    statement->info.update.object_parameter;
  class_obj = (node) ? node->info.name.db_object : NULL;
  if (!class_obj)
    {
      return NO_ERROR;
    }

  columns = find_update_columns (&count, statement);
  err = check_trigger (TR_EVENT_STATEMENT_UPDATE, class_obj,
		       (const char **) columns, count,
		       do_func, parser, statement);
  if (columns)
    {
      free_and_init (columns);
    }
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
	      *source = str->info.value.text;
	    }
	}
      else
	{
	  /* complex expression */
	  *type = TR_ACT_EXPRESSION;
	  *source = parser_print_tree (parser, statement);
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

  CHECK_MODIFICATION_ERROR ();

  if (PRM_BLOCK_DDL_STATEMENT)
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
      if (sm_has_text_domain (db_get_attributes (class_), 1))
	{
	  /* prevent to create a trigger at the class to contain TEXT */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_REGU_NOT_IMPLEMENTED,
		  1, rel_major_release_string ());
	  return er_errid ();
	}
      attr = PT_TR_TARGET_ATTR (target);
      if (attr)
	{
	  attribute = PT_TR_ATTR_NAME (attr);
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

  return NO_ERROR;
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

  if (PRM_BLOCK_DDL_STATEMENT)
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

  CHECK_MODIFICATION_ERROR ();

  triggers = NULL;
  p_node = statement->info.alter_trigger.trigger_priority;
  speclist = statement->info.alter_trigger.trigger_spec_list;
  if (convert_speclist_to_objlist (&triggers, speclist))
    {
      return er_errid ();
    }

  /* currently, we can' set the status and priority at the same time.
     The existance of p_node determines which type of alter statement this is.
   */
  status = TR_STATUS_INVALID;

  if (p_node == NULL)
    {
      status =
	convert_misc_to_tr_status (statement->info.alter_trigger.
				   trigger_status);
    }
  else
    {
      priority = get_priority (parser, p_node);
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

  if (PRM_BLOCK_DDL_STATEMENT)
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

  if (PRM_BLOCK_DDL_STATEMENT)
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

  pt_evaluate_tree (parser, statement->info.set_trigger.val, &src);
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
      error = pt_associate_label_with_value (into_label, ins_val);
    }

  return error;
}







/*
 * Function Group:
 * DO functions for update statements
 *
 */

#define DB_VALUE_STACK_MAX 40

/* It is used to generate unique savepoint names */
static int update_savepoint_number = 0;

static void unlink_list (PT_NODE * list);

static QFILE_LIST_ID *get_select_list_to_update (PARSER_CONTEXT * parser,
						 PT_NODE * from,
						 PT_NODE * column_values,
						 PT_NODE * where,
						 PT_NODE * using_index,
						 PT_NODE * class_specs);
static int update_object_attribute (PARSER_CONTEXT * parser,
				    DB_OTMPL * otemplate, PT_NODE * name,
				    DB_ATTDESC * attr_desc, DB_VALUE * value);
static int update_object_tuple (PARSER_CONTEXT * parser, DB_OBJECT * object,
				PT_NODE * list_column_names,
				DB_VALUE * list_values,
				PT_NODE * const_column_names,
				DB_VALUE * const_values,
				DB_ATTDESC ** list_attr_descs,
				DB_ATTDESC ** const_attr_descs,
				PT_NODE * class_, PT_NODE * check_where,
				const int turn_off_unique_check);
static int update_object_by_oid (PARSER_CONTEXT * parser,
				 PT_NODE * statement);
static int update_objs_for_list_file (PARSER_CONTEXT * parser,
				      QFILE_LIST_ID * list_id,
				      PT_NODE * list_column_names,
				      PT_NODE * const_column_names,
				      PT_NODE * const_column_values,
				      PT_NODE * class_, PT_NODE * check_where,
				      const int has_uniques);
static int get_assignment_lists (PARSER_CONTEXT * parser,
				 PT_NODE ** select_names,
				 PT_NODE ** select_values,
				 PT_NODE ** const_names,
				 PT_NODE ** const_values, int *no_vals,
				 int *no_consts, PT_NODE * assign);
static int update_class_attributes (PARSER_CONTEXT * parser,
				    DB_OBJECT * class_obj,
				    PT_NODE * select_names,
				    PT_NODE * select_values,
				    PT_NODE * const_names,
				    PT_NODE * const_values);
static int update_at_server (PARSER_CONTEXT * parser, PT_NODE * from,
			     PT_NODE * statement, PT_NODE ** non_null_attrs,
			     int has_uniques);
static int check_for_constraints (PARSER_CONTEXT * parser, int *has_unique,
				  PT_NODE ** not_nulls, PT_NODE * assignment,
				  DB_OBJECT * class_obj);
static int update_check_for_fk_cache_attr (PARSER_CONTEXT * parser,
					   PT_NODE * assignment,
					   DB_OBJECT * class_obj);
static int update_real_class (PARSER_CONTEXT * parser, PT_NODE * spec,
			      PT_NODE * statement);

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
 *   using_index(in): USING INDEX clause
 *   class_specs(in): Another class specs in FROM clause
 *
 * Note:
 */
static QFILE_LIST_ID *
get_select_list_to_update (PARSER_CONTEXT * parser, PT_NODE * from,
			   PT_NODE * column_values, PT_NODE * where,
			   PT_NODE * using_index, PT_NODE * class_specs)
{
  PT_NODE *statement = NULL;
  QFILE_LIST_ID *result = NULL;

  if (from && (from->node_type == PT_SPEC) && from->info.spec.range_var
      && ((statement = pt_to_upd_del_query (parser, column_values, from,
					    class_specs, where, using_index,
					    0 /* not server update */ )) !=
	  NULL))
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
       * this real class. Its simply done seperately by a db_put.
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
 *   object(in): Object to update
 *   list_column_names(in): Name list of columns
 *   list_values(in): Value list of columns
 *   const_column_names(in):
 *   const_values(in):
 *   list_attr_descs(in): List of attribute descriptors
 *   const_attr_descs(in):
 *   class(in): Parse tree of an class spec
 *   check_where(in):
 *   turn_off_unique_check(in):
 *
 * Note:
 */
static int
update_object_tuple (PARSER_CONTEXT * parser, DB_OBJECT * object,
		     PT_NODE * list_column_names, DB_VALUE * list_values,
		     PT_NODE * const_column_names, DB_VALUE * const_values,
		     DB_ATTDESC ** list_attr_descs,
		     DB_ATTDESC ** const_attr_descs, PT_NODE * class_,
		     PT_NODE * check_where, const int turn_off_unique_check)
{
  int error = NO_ERROR;
  DB_OTMPL *otemplate, *otmpl;
  PT_NODE *name;
  int i;
  DB_OBJECT *real_object;
  MOP newobj;
  SM_CLASS *smclass;
  SM_ATTRIBUTE *att;
  DB_VALUE *valptr, retval;
  char flag_att, flag_prc;
  int exist_active_triggers;


  real_object = db_real_instance (object);
  if (real_object == NULL)
    {				/* real_object's fail */
      if ((error = er_errid ()) == NO_ERROR)
	error = ER_GENERIC_ERROR;
      return error;
    }

  newobj = NULL;
  if (object->class_mop)
    {
      error = au_fetch_class (object->class_mop, &smclass, AU_FETCH_READ,
			      AU_SELECT);
      if (error == NO_ERROR && smclass->partition_of)
	{
	  newobj = do_is_partition_changed (parser, smclass,
					    object->class_mop,
					    list_column_names, list_values,
					    const_column_names, const_values);
	}
    }

  if (newobj)
    {
      /* partition */
      exist_active_triggers = sm_active_triggers (smclass, TR_EVENT_ALL);
      if (exist_active_triggers)
	{
	  if (exist_active_triggers < 0)
	    {
	      error = er_errid ();
	      return ((error != NO_ERROR) ? error : ER_GENERIC_ERROR);
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_NOT_ALLOWED_ACCESS_TO_PARTITION, 0);
	      return ER_NOT_ALLOWED_ACCESS_TO_PARTITION;
	    }
	}
      /* partition change - new insert & old delete */
      otmpl = dbt_create_object_internal (newobj);
      if (otmpl == NULL)
	{
	  error = er_errid ();
	  return ((error != NO_ERROR) ? error : ER_GENERIC_ERROR);
	}

      for (att = smclass->attributes, flag_att = 0; att != NULL;)
	{
	  flag_prc = 0;
	  valptr = list_values;
	  for (name = list_column_names; name != NULL; name = name->next)
	    {
	      if (SM_COMPARE_NAMES
		  (att->header.name, name->info.name.original) == 0)
		{
		  error = dbt_put_internal (otmpl, name->info.name.original,
					    valptr);
		  flag_prc = 1;
		  break;
		}
	      valptr++;
	    }

	  if (!flag_prc)
	    {
	      valptr = const_values;
	      for (name = const_column_names; name != NULL; name = name->next)
		{
		  if (SM_COMPARE_NAMES (att->header.name,
					name->info.name.original) == 0)
		    {
		      error =
			dbt_put_internal (otmpl, name->info.name.original,
					  valptr);
		      flag_prc = 1;
		      break;
		    }
		  valptr++;
		}
	    }

	  if (!flag_prc)
	    {
	      error = db_get (real_object, att->header.name, &retval);
	      if (error != NO_ERROR)
		{
		  break;
		}
	      error = dbt_put_internal (otmpl, att->header.name, &retval);
	      db_value_clear (&retval);
	    }
	  att = (SM_ATTRIBUTE *) att->header.next;
	  if (att == NULL && flag_att == 0)
	    {
	      flag_att++;
	      att = smclass->shared;
	    }
	}
      for (name = list_column_names; name != NULL; name = name->next)
	{
	  db_value_clear (list_values);
	  list_values++;
	}

      if (error != NO_ERROR)
	{
	  (void) dbt_abort_object (otmpl);
	  return error;
	}
      else
	{
	  error = obj_delete (real_object);
	  object = dbt_finish_object (otmpl);
	  if (object == NULL)
	    {
	      error = er_errid ();
	      (void) dbt_abort_object (otmpl);
	      return error;
	    }
	}
    }
  else
    {
      /* noraml */
      otemplate = dbt_edit_object (real_object);
      if (otemplate == NULL)
	{
	  return er_errid ();
	}
      i = 0;
      if (turn_off_unique_check)
	{
	  obt_disable_unique_checking (otemplate);
	}

      for (name = list_column_names; list_attr_descs != NULL && name != NULL;
	   name = name->next)
	{
	  /* if this is the first update, get the attribute descriptor */
	  if (list_attr_descs[i] == NULL)
	    {
	      /* don't get descriptors for shared attrs of views */
	      if (!name->info.name.db_object
		  || !db_is_vclass (name->info.name.db_object))
		{
		  error = db_get_attribute_descriptor (real_object,
						       name->info.name.
						       original, 0, 1,
						       &list_attr_descs[i]);
		}
	      if (error != NO_ERROR)
		break;
	    }

	  if (error == NO_ERROR)
	    {
	      error = update_object_attribute (parser, otemplate, name,
					       list_attr_descs[i],
					       list_values);
	    }
	  db_value_clear (list_values);

	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  i++;
	  list_values++;
	}

      i = 0;
      for (name = const_column_names;
	   const_attr_descs != NULL && name != NULL; name = name->next)
	{
	  /* if this is the first update, get the attribute descriptor */
	  if (const_attr_descs[i] == NULL)
	    {
	      /* don't get descriptors for shared attrs of views */
	      if (!name->info.name.db_object
		  || !db_is_vclass (name->info.name.db_object))
		{
		  error = db_get_attribute_descriptor (real_object,
						       name->info.name.
						       original, 0, 1,
						       &const_attr_descs[i]);
		}
	      if (error != NO_ERROR)
		{
		  break;
		}
	    }

	  if (error == NO_ERROR)
	    {
	      error = update_object_attribute (parser, otemplate, name,
					       const_attr_descs[i],
					       const_values);
	    }

	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  i++;
	  const_values++;
	}

      if (error != NO_ERROR)
	{
	  (void) dbt_abort_object (otemplate);
	}
      else
	{
	  object = dbt_finish_object (otemplate);
	  if (object == NULL)
	    {
	      error = er_errid ();
	      (void) dbt_abort_object (otemplate);
	    }
	  else
	    {
	      error = mq_evaluate_check_option (parser, check_where, object,
						class_);
	    }
	}
    }

  return error;
}

/*
 * update_object_by_oid - Updates attributes of object by oid
 *   return: 1 if success, otherwise returns error code
 *   parser(in): Parser context
 *   statement(in): Parse tree of a update statement
 *
 * Note:
 */
static int
update_object_by_oid (PARSER_CONTEXT * parser, PT_NODE * statement)
{
  PT_NODE *list_column_names;
  PT_NODE *list_column_values;
  PT_NODE *const_column_names;
  PT_NODE *const_column_values;
  int error = NO_ERROR;
  DB_OBJECT *oid = statement->info.update.object;
  int list_columns = 0;
  int const_columns = 0;
  DB_VALUE list_db_value_stack[DB_VALUE_STACK_MAX];
  DB_VALUE const_db_value_stack[DB_VALUE_STACK_MAX];
  DB_VALUE *list_db_value_list = list_db_value_stack;
  DB_VALUE *const_db_value_list = const_db_value_stack;
  DB_ATTDESC **list_attr_descs = NULL;
  DB_ATTDESC **const_attr_descs = NULL;
  DB_VALUE *db_value = NULL;
  DB_VALUE db_value1;
  int i = 0;
  PT_NODE *pt_value = NULL;
  PT_NODE *node = NULL;
  int no_vals;
  int no_consts;
  PT_NODE *class_;
  PT_NODE *lhs;

  if (!statement->info.update.spec || !(class_
					=
					statement->info.update.spec->info.
					spec.flat_entity_list)
      || !(class_->info.name.db_object))
    {
      PT_INTERNAL_ERROR (parser, "update");
      return ER_GENERIC_ERROR;
    }
  if (!locator_fetch_class (class_->info.name.db_object,
			    DB_FETCH_CLREAD_INSTWRITE))
    {
      return er_errid ();
    }

  error =
    get_assignment_lists (parser, &list_column_names, &list_column_values,
			  &const_column_names, &const_column_values, &no_vals,
			  &no_consts, statement->info.update.assignment);

  lhs = statement->info.update.assignment->info.expr.arg1;
  if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
    {
      lhs = lhs->info.expr.arg1;
    }
  if (lhs->info.name.meta_class == PT_META_ATTR)
    {
      /* we are updating class attributes */
      error = update_class_attributes (parser, class_->info.name.db_object,
				       list_column_names, list_column_values,
				       const_column_names,
				       const_column_values);
    }
  else
    {
      if (list_column_names == NULL && const_column_names == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__, ER_REGU_SYSTEM, 0);
	  error = ER_REGU_SYSTEM;
	}

      if (error == NO_ERROR)
	{
	  list_columns = pt_length_of_list (list_column_names);
	  const_columns = pt_length_of_list (const_column_names);

	  if (list_columns >= DB_VALUE_STACK_MAX)
	    {
	      list_db_value_list = (DB_VALUE *) malloc ((list_columns)
							* sizeof (DB_VALUE));
	    }
	  if (const_columns >= DB_VALUE_STACK_MAX)
	    {
	      const_db_value_list = (DB_VALUE *) malloc ((const_columns)
							 * sizeof (DB_VALUE));
	    }

	  if (!list_db_value_list || !const_db_value_list)
	    {
	      error = ER_REGU_NO_SPACE;
	    }

	  /* allocate attribute descriptors */
	  if (list_columns)
	    {
	      list_attr_descs = (DB_ATTDESC **) malloc ((list_columns)
							*
							sizeof (DB_ATTDESC
								*));
	      if (!list_attr_descs)
		{
		  error = ER_REGU_NO_SPACE;
		}
	      else
		{
		  for (i = 0; i < list_columns; i++)
		    {
		      list_attr_descs[i] = NULL;
		    }
		}
	    }
	  if (const_columns)
	    {
	      const_attr_descs = (DB_ATTDESC **) malloc ((const_columns)
							 *
							 sizeof (DB_ATTDESC
								 *));
	      if (!const_attr_descs)
		{
		  error = ER_REGU_NO_SPACE;
		}
	      else
		{
		  for (i = 0; i < const_columns; i++)
		    {
		      const_attr_descs[i] = NULL;
		    }
		}
	    }
	}

      if (error == NO_ERROR)
	{
	  i = 0;
	  for (pt_value = const_column_values; pt_value != NULL; pt_value
	       = pt_value->next)
	    {
	      db_value = pt_value_to_db (parser, pt_value);

	      if (db_value)
		{
		  const_db_value_list[i] = *db_value;
		}
	      else
		{
		  /* this is probably an error condition */
		  db_make_null ((&const_db_value_list[i]));
		}

	      i++;
	    }

	  i = 0;
	  for (node = list_column_values;
	       list_column_names != NULL && node != NULL; node = node->next)
	    {
	      error = mq_evaluate_expression_having_serial (parser, node,
							    &db_value1, oid,
							    list_column_names->
							    info.name.
							    spec_id);

	      if (error < NO_ERROR)
		{
		  break;
		}

	      list_db_value_list[i] = db_value1;

	      i++;
	    }

	  if (error >= NO_ERROR)
	    {
	      error = update_object_tuple (parser, oid, list_column_names,
					   list_db_value_list,
					   const_column_names,
					   const_db_value_list,
					   list_attr_descs, const_attr_descs,
					   class_,
					   statement->info.update.check_where,
					   0);
	    }

	}

      if (list_db_value_list != list_db_value_stack)
	{
	  free_and_init (list_db_value_list);
	}

      if (const_db_value_list != const_db_value_stack)
	{
	  free_and_init (const_db_value_list);
	}

      /* free attribute descriptors */
      if (list_attr_descs)
	{
	  for (i = 0; i < list_columns; i++)
	    {
	      if (list_attr_descs[i])
		{
		  db_free_attribute_descriptor (list_attr_descs[i]);
		}
	    }
	  free_and_init (list_attr_descs);
	}
      if (const_attr_descs)
	{
	  for (i = 0; i < const_columns; i++)
	    {
	      if (const_attr_descs[i])
		{
		  db_free_attribute_descriptor (const_attr_descs[i]);
		}
	    }
	  free_and_init (const_attr_descs);
	}
    }

  if (error < NO_ERROR)
    return error;
  else
    return 1;			/* we successfully updated 1 object */
}

/*
 * update_objs_for_list_file - Updates oid attributes for every oid
 *				in a list file
 *   return: Number of affected objects if success, otherwise an error code
 *   parser(in): Parser context
 *   list_id(in): A list file of oid's and values
 *   list_column_names(in): Name list of columns
 *   const_column_names(in):
 *   const_column_values(in):
 *   class(in):
 *   check_where(in):
 *   has_uniques(in):
 *
 * Note:
 */
static int
update_objs_for_list_file (PARSER_CONTEXT * parser, QFILE_LIST_ID * list_id,
			   PT_NODE * list_column_names,
			   PT_NODE * const_column_names,
			   PT_NODE * const_column_values, PT_NODE * class_,
			   PT_NODE * check_where, const int has_uniques)
{
  int error = NO_ERROR;
  int cursor_status;
  int list_columns = 0;
  int const_columns = 0;
  DB_VALUE list_db_value_stack[DB_VALUE_STACK_MAX];
  DB_VALUE const_db_value_stack[DB_VALUE_STACK_MAX];
  DB_VALUE *list_db_value_list = list_db_value_stack;
  DB_VALUE *const_db_value_list = const_db_value_stack;
  DB_ATTDESC **list_attr_descs = NULL;
  DB_ATTDESC **const_attr_descs = NULL;
  DB_VALUE *db_value = NULL;
  DB_VALUE oid_value;
  CURSOR_ID cursor_id;
  int count = 0;
  int i = 0;
  PT_NODE *pt_value;
  const char *savepoint_name = NULL;
  int turn_off_unique_check;

  if (!list_column_names && !const_column_names)
    {
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__, ER_REGU_SYSTEM, 0);
      error = ER_REGU_SYSTEM;
      goto done;
    }

  list_columns = pt_length_of_list (list_column_names);
  if (list_columns >= DB_VALUE_STACK_MAX)
    {
      list_db_value_list = (DB_VALUE *) malloc ((list_columns)
						* sizeof (DB_VALUE));
      if (!list_db_value_list)
	{
	  error = ER_REGU_NO_SPACE;
	  goto done;
	}
    }

  const_columns = pt_length_of_list (const_column_names);
  if (const_columns >= DB_VALUE_STACK_MAX)
    {
      const_db_value_list = (DB_VALUE *) malloc ((const_columns)
						 * sizeof (DB_VALUE));
      if (!const_db_value_list)
	{
	  error = ER_REGU_NO_SPACE;
	  goto done;
	}
    }

  /* allocate attribute descriptors */
  if (list_columns)
    {
      list_attr_descs = (DB_ATTDESC **) malloc ((list_columns)
						* sizeof (DB_ATTDESC *));
      if (!list_attr_descs)
	{
	  error = ER_REGU_NO_SPACE;
	  goto done;
	}
      for (i = 0; i < list_columns; i++)
	{
	  list_attr_descs[i] = NULL;
	}
    }
  if (const_columns)
    {
      const_attr_descs = (DB_ATTDESC **) malloc ((const_columns)
						 * sizeof (DB_ATTDESC *));
      if (!const_attr_descs)
	{
	  error = ER_REGU_NO_SPACE;
	  goto done;
	}
      for (i = 0; i < const_columns; i++)
	{
	  const_attr_descs[i] = NULL;
	}
    }

  i = 0;
  for (pt_value = const_column_values; pt_value != NULL; pt_value
       = pt_value->next)
    {
      db_value = pt_value_to_db (parser, pt_value);

      if (db_value)
	{
	  const_db_value_list[i] = *db_value;
	}
      else
	{
	  /* this is probably an error condition */
	  db_make_null ((&const_db_value_list[i]));
	}
      i++;
    }

  /* if the list file contains more than 1 object we need to savepoint
   * the statement to guarantee statement atomicity.
   */
  if (list_id->tuple_cnt > 1 || check_where || has_uniques)
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
  if (list_id->tuple_cnt == 1)
    {
      /* Instance level uniqueness checking is performed on the server
       * when a new single row is inserted.
       */
      turn_off_unique_check = 0;
    }
  else
    {
      /* list_id->tuple_cnt > 1 : multiple row update
       * Statment level uniqueness checking is performed on the client
       */
      turn_off_unique_check = 1;
    }

  if (!cursor_open (&cursor_id, list_id, false, true))
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

      /* the first item on the db_value_list's is an oid.
       * the rest are values to assign to the corresponding
       * oid column names.
       */
      if (cursor_get_current_oid (&cursor_id, &oid_value) != NO_ERROR)
	{
	  error = er_errid ();
	  cursor_close (&cursor_id);
	  if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	  goto done;
	}

      if (cursor_get_tuple_value_list
	  (&cursor_id, pt_length_of_list (list_column_names),
	   list_db_value_list) != NO_ERROR)
	{
	  error = er_errid ();
	  cursor_close (&cursor_id);
	  if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	  goto done;
	}

      if (DB_VALUE_TYPE (&oid_value) == DB_TYPE_NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_OBJECT_ID_NOT_SET,
		  1, class_->info.name.original);
	  error = ER_SM_OBJECT_ID_NOT_SET;
	  cursor_close (&cursor_id);
	  if (savepoint_name && (error != ER_LK_UNILATERALLY_ABORTED))
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	  goto done;
	}

      error = update_object_tuple (parser, DB_GET_OBJECT (&oid_value),
				   list_column_names, list_db_value_list,
				   const_column_names, const_db_value_list,
				   list_attr_descs, const_attr_descs, class_,
				   check_where, turn_off_unique_check);

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

      count++;			/* number of objects affected */
      cursor_status = cursor_next_tuple (&cursor_id);
    }

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
  if (has_uniques)
    {
      error = sm_flush_for_multi_update (class_->info.name.db_object);
      /* if error and a savepoint was created, rollback to savepoint.
       * No need to rollback if the TM aborted the transaction itself.
       */
      if ((error < NO_ERROR) && savepoint_name
	  && (error != ER_LK_UNILATERALLY_ABORTED))
	{
	  (void) tran_abort_upto_savepoint (savepoint_name);
	}
    }

done:

  if (list_db_value_list && list_db_value_list != list_db_value_stack)
    {
      free_and_init (list_db_value_list);
    }

  if (const_db_value_list && const_db_value_list != const_db_value_stack)
    {
      free_and_init (const_db_value_list);
    }

  /* free attribute descriptors */
  if (list_attr_descs)
    {
      for (i = 0; i < list_columns; i++)
	{
	  if (list_attr_descs[i])
	    {
	      db_free_attribute_descriptor (list_attr_descs[i]);
	    }
	}
      free_and_init (list_attr_descs);
    }
  if (const_attr_descs)
    {
      for (i = 0; i < const_columns; i++)
	{
	  if (const_attr_descs[i])
	    {
	      db_free_attribute_descriptor (const_attr_descs[i]);
	    }
	}
      free_and_init (const_attr_descs);
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
 * get_assignment_lists - Returns corresponding lists of names and expressions
 *   return: Error code
 *   parser(in): Parser context
 *   select_names(out):
 *   select_values(out):
 *   const_names(out):
 *   const_values(out):
 *   assign(in): Parse tree of assignment lists
 *
 * Note:
 */
static int
get_assignment_lists (PARSER_CONTEXT * parser, PT_NODE ** select_names,
		      PT_NODE ** select_values, PT_NODE ** const_names,
		      PT_NODE ** const_values, int *no_vals, int *no_consts,
		      PT_NODE * assign)
{
  int error = NO_ERROR;
  PT_NODE *lhs;
  PT_NODE *rhs;
  PT_NODE *att;

  if (select_names)
    {
      *select_names = NULL;
    }
  if (select_values)
    {
      *select_values = NULL;
    }
  if (const_names)
    {
      *const_names = NULL;
    }
  if (const_values)
    {
      *const_values = NULL;
    }
  if (no_vals)
    {
      *no_vals = 0;
    }
  if (no_consts)
    {
      *no_consts = 0;
    }

  while (assign)
    {
      if (assign->node_type != PT_EXPR || assign->info.expr.op != PT_ASSIGN
	  || !(lhs = assign->info.expr.arg1)
	  || !(rhs = assign->info.expr.arg2) || !(lhs->node_type == PT_NAME
						  ||
						  PT_IS_N_COLUMN_UPDATE_EXPR
						  (lhs)) || !select_values
	  || !select_names || !const_values || !const_names || !no_vals
	  || !no_consts)
	{
	  /* bullet proofing, should not get here */
#if defined(CUBRID_DEBUG)
	  fprintf (stdout, "system error detected in %s, line %d.\n",
		   __FILE__, __LINE__);
#endif
	  return ER_GENERIC_ERROR;
	}

      if (lhs->node_type == PT_NAME)
	{
	  ++(*no_vals);
	}
      else
	{			/* PT_IS_N_COLUMN_UPDATE_EXPR(lhs) == true */
	  lhs = lhs->info.expr.arg1;
	  for (att = lhs; att; att = att->next)
	    {
	      if (att->node_type != PT_NAME)
		{
#if defined(CUBRID_DEBUG)
		  fprintf (stdout, "system error detected in %s, line %d.\n",
			   __FILE__, __LINE__);
#endif
		  return ER_GENERIC_ERROR;
		}
	      ++(*no_vals);
	    }
	}
      if (!PT_IS_CONST (rhs)
	  || (PT_IS_HOSTVAR (rhs) && parser->set_host_var == 0))
	{
	  /* assume evaluation needed. */
	  if (*select_names == NULL)
	    {
	      *select_names = lhs;
	      *select_values = rhs;
	    }
	  else
	    {
	      parser_append_node (lhs, *select_names);
	      parser_append_node (rhs, *select_values);
	    }
	}
      else
	{
	  ++(*no_consts);
	  /* we already have a constant value */
	  if (*const_names == NULL)
	    {
	      *const_names = lhs;
	      *const_values = rhs;
	    }
	  else
	    {
	      parser_append_node (lhs, *const_names);
	      parser_append_node (rhs, *const_values);
	    }
	}
      assign = assign->next;
    }

  return error;
}

/*
 * update_class_attributes -
 *   return: Error code
 *   parser(in): Parser context
 *   class_obj(in/out): Class template to be edited
 *   select_names(in):
 *   select_values(in):
 *   const_names(in):
 *   const_values(in):
 *
 * Note:
 */
static int
update_class_attributes (PARSER_CONTEXT * parser, DB_OBJECT * class_obj,
			 PT_NODE * select_names, PT_NODE * select_values,
			 PT_NODE * const_names, PT_NODE * const_values)
{
  int error = NO_ERROR;
  DB_VALUE *db_value = NULL, val;
  PT_NODE *q, *p;
  DB_OTMPL *otemplate = NULL;

  if (!select_names && !const_names)
    {
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__, ER_REGU_SYSTEM, 0);
      return ER_REGU_SYSTEM;
    }

  otemplate = dbt_edit_object (class_obj);

  for (p = const_names, q = const_values; (error == NO_ERROR) && p && q; p
       = p->next, q = q->next)
    {

      if (p->info.name.meta_class != PT_META_ATTR)
	{
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		  ER_REGU_MIX_CLASS_NONCLASS_UPDATE, 0);
	  error = ER_REGU_MIX_CLASS_NONCLASS_UPDATE;
	}
      else
	{
	  db_value = pt_value_to_db (parser, q);

	  if (db_value)
	    {
	      error = dbt_put_internal (otemplate, p->info.name.original,
					db_value);
	    }
	  else
	    {
	      error = ER_GENERIC_ERROR;
	    }
	}
    }

  if (error != NO_ERROR)
    {
      (void) dbt_abort_object (otemplate);
      return error;
    }

  for (p = select_names, q = select_values; (error == NO_ERROR) && p && q; p
       = p->next, q = q->next)
    {

      if (p->info.name.meta_class != PT_META_ATTR)
	{
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		  ER_REGU_MIX_CLASS_NONCLASS_UPDATE, 0);
	  error = ER_REGU_MIX_CLASS_NONCLASS_UPDATE;
	}
      else
	{
	  pt_evaluate_tree (parser, q, &val);
	  if (parser->error_msgs)
	    {
	      db_value = NULL;
	    }
	  else
	    {
	      db_value = &val;
	    }

	  if (db_value)
	    {
	      error = dbt_put_internal (otemplate, p->info.name.original,
					db_value);
	    }
	  else
	    {
	      error = ER_GENERIC_ERROR;
	    }
	}
    }

  if (error != NO_ERROR)
    {
      (void) dbt_abort_object (otemplate);
      return error;
    }

  if (dbt_finish_object (otemplate) == NULL)
    {
      error = er_errid ();
      (void) dbt_abort_object (otemplate);

      return error;
    }
  else
    {
      return NO_ERROR;
    }
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
 *  a BUILDLIST_PROC as it's aptr.  The BUILDLIST_PROC selects the
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
  PT_NODE *select_names;
  PT_NODE *select_values;
  PT_NODE *const_names;
  PT_NODE *const_values;
  int i, j;
  int no_vals;
  int no_consts;
  XASL_NODE *xasl = NULL;
  int size, count = 0;
  char *stream = NULL;
  QUERY_ID query_id = NULL_QUERY_ID;
  QFILE_LIST_ID *list_id = NULL;
  PT_NODE *cl_name_node;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();

  error = get_assignment_lists (parser, &select_names, &select_values,
				&const_names, &const_values, &no_vals,
				&no_consts,
				statement->info.update.assignment);

  if (error == NO_ERROR)
    {
      xasl =
	pt_to_update_xasl (parser, statement, select_names, select_values,
			   const_names, const_values, no_vals, no_consts,
			   has_uniques, non_null_attrs);
    }

  if (xasl)
    {
      UPDATE_PROC_NODE *update = &xasl->proc.update;

      error = xts_map_xasl_to_stream (xasl, &stream, &size);
      if (error != NO_ERROR)
	{
	  PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	}

      for (i = 0; i < update->no_consts; i++)
	{
	  pr_clear_value (update->consts[i]);
	}
      for (i = 0; i < update->no_classes; i++)
	{
	  if (update->partition[i])
	    {
	      for (j = 0; j < update->partition[i]->no_parts; j++)
		{
		  pr_clear_value (update->partition[i]->parts[j]->vals);
		}
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
					 parser->
					 exec_mode | ASYNC_UNEXECUTABLE);
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
	  for (cl_name_node = from->info.spec.flat_entity_list; cl_name_node
	       && error == NO_ERROR; cl_name_node = cl_name_node->next)
	    {
	      error =
		sm_flush_and_decache_objects (cl_name_node->info.name.
					      db_object, true);
	    }
	}
      regu_free_listid (list_id);
    }
  pt_end_query (parser);

  unlink_list (const_names);
  unlink_list (const_values);
  unlink_list (select_names);
  unlink_list (select_values);

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
 * check_for_constraints - Determine whether attributes of the target class have
 *                         UNIQUE and/or NOT NULL constratins, and return a list
 *			   of NOT NULL attributes if exist
 *   return: Error code
 *   parser(in): Parser context
 *   has_unique(out): Indicator representing there is UNIQUE constraint, 1 or 0
 *   not_nulls(out): A list of pointers to NOT NULL attributes, or NULL
 *   assignment(in):  Parse tree of an assignment part of the UPDATE statement
 *   class_obj(in): Class object of the target spec
 *
 * Note:
 */
static int
check_for_constraints (PARSER_CONTEXT * parser, int *has_unique,
		       PT_NODE ** not_nulls, PT_NODE * assignment,
		       DB_OBJECT * class_obj)
{
  PT_NODE *lhs, *att, *pointer;

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
	  return ER_GENERIC_ERROR;
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
	      return ER_GENERIC_ERROR;
	    }
	  if (*has_unique == 0 && sm_att_unique_constrained (class_obj,
							     att->info.name.
							     original))
	    {
	      *has_unique = 1;
	    }
	  if (sm_att_constrained (class_obj, att->info.name.original,
				  SM_ATTFLAG_NON_NULL))
	    {
	      pointer = pt_point (parser, att);
	      if (pointer == NULL)
		{
		  PT_ERRORm (parser, att, MSGCAT_SET_PARSER_RUNTIME,
			     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		  if (*not_nulls)
		    {
		      parser_free_tree (parser, *not_nulls);
		    }
		  *not_nulls = NULL;
		  return MSGCAT_RUNTIME_RESOURCES_EXHAUSTED;
		}
	      *not_nulls = parser_append_node (pointer, *not_nulls);
	    }
	}			/* for ( ; attr; ...) */
    }				/* for ( ; assignment; ...) */

  return NO_ERROR;
}

/*
 * update_check_for_fk_cache_attr() -
 *   return: Error code if update fails
 *   parser(in): Parser context
 *   assignment(in): Parse tree of an assignment clause
 *   class_obj(in): Class object to be checked
 *
 * Note:
 */
static int
update_check_for_fk_cache_attr (PARSER_CONTEXT * parser, PT_NODE * assignment,
				DB_OBJECT * class_obj)
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
	  return ER_GENERIC_ERROR;
	}

      for (; att; att = att->next)
	{
	  if (att->node_type != PT_NAME)
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
 * update_real_class() -
 *   return: Error code if update fails
 *   parser(in): Parser context
 *   spec(in): Parse tree of a class spec to update
 *   statement(in): Parse tree of a update statement
 *
 * Note: If the statement is of type "update class foo ...", this
 *   routine updates class attributes of foo.  If the statement is of
 *   type "update foo ...", this routine updates objects or rows in foo.
 *   It is assumed that class attributes and regular attributes
 *   are not mixed in the same update statement.
 */
static int
update_real_class (PARSER_CONTEXT * parser, PT_NODE * spec,
		   PT_NODE * statement)
{
  int error = NO_ERROR;
  PT_NODE *select_names;
  PT_NODE *select_values;
  PT_NODE *const_names;
  PT_NODE *const_values;
  PT_NODE *non_null_attrs = NULL;
  PT_NODE *lhs;
  DB_OBJECT *class_obj;
  QFILE_LIST_ID *oid_list = NULL;
  int trigger_involved;
  int no_vals;
  int no_consts;
  int server_allowed;
  int has_uniques;
  int is_partition;
  float waitsecs = -2, old_waitsecs = -2;
  PT_NODE *hint_arg;

  /* update a "real" class in this database */

  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;

  /* The IX lock on the class is sufficient.
   * DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE
   */
  if (!locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE))
    {
      goto exit_on_error;
    }

  error = do_is_partitioned_classobj (&is_partition, class_obj, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* if class is partitioned and has any trigger, update must be executed in the client */
  error = sm_class_has_triggers (class_obj, &trigger_involved,
				 ((is_partition) ? TR_EVENT_ALL :
				  TR_EVENT_UPDATE));

  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }
  error = check_for_constraints (parser, &has_uniques, &non_null_attrs,
				 statement->info.update.assignment,
				 class_obj);
  if (error < NO_ERROR)
    {
      goto exit_on_error;
    }

  error =
    update_check_for_fk_cache_attr (parser, statement->info.update.assignment,
				    class_obj);
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Check to see if the update can be done on the server */
  server_allowed = ((!trigger_involved)
		    && (spec->info.spec.flat_entity_list->info.name.
			virt_object == NULL));

  lhs = statement->info.update.assignment->info.expr.arg1;
  if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
    {
      lhs = lhs->info.expr.arg1;
    }
  if (lhs->info.name.meta_class != PT_NORMAL)
    {
      server_allowed = 0;
    }

  if (server_allowed)
    {
      /* do update on server */
      error = update_at_server (parser, spec, statement, &non_null_attrs,
				has_uniques);
    }
  else
    {
      /* do update on client */
      error = get_assignment_lists (parser, &select_names, &select_values,
				    &const_names, &const_values, &no_vals,
				    &no_consts,
				    statement->info.update.assignment);
      lhs = statement->info.update.assignment->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  lhs = lhs->info.expr.arg1;
	}
      if (lhs->info.name.meta_class != PT_META_ATTR)
	{

	  hint_arg = statement->info.update.waitsecs_hint;
	  if (statement->info.update.hint & PT_HINT_LK_TIMEOUT
	      && PT_IS_HINT_NODE (hint_arg))
	    {
	      waitsecs = (float) atof (hint_arg->info.name.original);
	      if (waitsecs >= -1)
		{
		  old_waitsecs = (float) TM_TRAN_WAITSECS ();
		  (void) tran_reset_wait_times (waitsecs);
		}
	    }
	  if (error == NO_ERROR)
	    {
	      /* get the oid's and new values */
	      oid_list =
		get_select_list_to_update (parser, spec, select_values,
					   statement->info.update.search_cond,
					   statement->info.update.using_index,
					   statement->info.update.
					   class_specs);
	    }
	  if (old_waitsecs >= -1)
	    {
	      (void) tran_reset_wait_times (old_waitsecs);
	    }

	  if (!oid_list)
	    {
	      /* an error should be set already, don't lose it */
	      error = ER_GENERIC_ERROR;
	      goto exit_on_error;
	    }

	  /* update each oid */
	  error = update_objs_for_list_file (parser, oid_list, select_names,
					     const_names, const_values,
					     spec->info.spec.flat_entity_list,
					     statement->info.update.
					     check_where, has_uniques);

	  regu_free_listid (oid_list);
	  pt_end_query (parser);
	}
      else
	{
	  /* we are updating class attributes */
	  error = update_class_attributes (parser, class_obj, select_names,
					   select_values, const_names,
					   const_values);
	}

      /* restore tree structure */
      unlink_list (const_names);
      unlink_list (const_values);
      unlink_list (select_names);
      unlink_list (select_values);

    }

wrapup:

  if (non_null_attrs)
    {
      parser_free_tree (parser, non_null_attrs);
    }

  return error;

exit_on_error:

  if (error == NO_ERROR)
    {
      error = er_errid ();
    }

  goto wrapup;
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
  PT_NODE *spec;
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

      spec = statement->info.update.spec;

      if (pt_false_where (parser, statement))
	{
	  /* nothing to update, where part is false */
	}
      else if (statement->info.update.object != NULL)
	{
	  /* this is a update object if it has an object */
	  error = update_object_by_oid (parser, statement);
	}
      else
	{
	  /* the following is the "normal" sql type execution */
	  error = update_real_class (parser, spec, statement);
	}

      if (error < NO_ERROR && er_errid () != NO_ERROR)
	{
	  pt_record_error (parser, parser->statement_number,
			   statement->line_number, statement->column_number,
			   er_msg ());
	}

      result += error;
      statement = statement->next;
    }

  /* if error and a savepoint was created, rollback to savepoint.
   * No need to rollback if the TM aborted the transaction.
   */
  if ((error < NO_ERROR) && rollbacktosp && (error
					     != ER_LK_UNILATERALLY_ABORTED))
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
 * do_execute_update() - Prepare the UPDATE statement
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
  PT_NODE *flat, *not_nulls, *lhs;
  DB_OBJECT *class_obj;
  int has_trigger, has_unique, au_save;
  bool server_update;
  PT_NODE *select_names, *select_values, *const_names, *const_values;
  int no_vals, no_consts;
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

      flat = statement->info.update.spec->info.spec.flat_entity_list;
      class_obj = (flat) ? flat->info.name.db_object : NULL;
      /* the presence of a proxy trigger should force the update
         to be performed through the workspace  */
      AU_SAVE_AND_DISABLE (au_save);	/* because sm_class_has_trigger() calls
					   au_fetch_class() */
      err = sm_class_has_triggers (class_obj, &has_trigger, TR_EVENT_UPDATE);

      AU_RESTORE (au_save);
      /* err = has_proxy_trigger(flat, &has_trigger); */
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}
      /* sm_class_has_triggers() checked if the class has active triggers */
      statement->info.update.has_trigger = (bool) has_trigger;

      err = update_check_for_fk_cache_attr (parser,
					    statement->info.update.assignment,
					    class_obj);
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}

      /* check if the target class has UNIQUE constraint and
         get attributes that has NOT NULL constraint */
      err = check_for_constraints (parser, &has_unique, &not_nulls,
				   statement->info.update.assignment,
				   class_obj);
      if (err < NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
	}

      statement->info.update.has_unique = (bool) has_unique;

      /* determine whether it can be server-side or OID list update */
      server_update = (!has_trigger && (flat != NULL)
		       && (flat->info.name.virt_object == NULL));
      lhs = statement->info.update.assignment->info.expr.arg1;
      if (PT_IS_N_COLUMN_UPDATE_EXPR (lhs))
	{
	  lhs = lhs->info.expr.arg1;
	}
      if (lhs->info.name.meta_class != PT_NORMAL)
	{
	  server_update = false;
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
	  PT_NODE_PRINT_TO_ALIAS (parser, statement, PT_CONVERT_RANGE);
	  qstr = statement->alias_print;
	  parser->dont_prt_long_string = 0;
	  if (parser->long_string_skipped)
	    {
	      statement->cannot_prepare = 1;
	      return NO_ERROR;
	    }
	}

      /* get lists of names and values (expressions and constants)
         from the assignment part of UPDATE statement */
      err = get_assignment_lists (parser, &select_names, &select_values,
				  &const_names, &const_values, &no_vals,
				  &no_consts,
				  statement->info.update.assignment);
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "update");
	  break;		/* stop while loop if error */
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
					  db_identifier (db_get_user ()),
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
	      xasl = pt_to_update_xasl (parser, statement, select_names,
					select_values, const_names,
					const_values, no_vals, no_consts,
					has_unique, &not_nulls);
	      AU_RESTORE (au_save);
	      stream = NULL;
	      if (xasl && (err >= NO_ERROR))
		{
		  int i, j;
		  UPDATE_PROC_NODE *update = &xasl->proc.update;

		  /* convert the created XASL tree to the byte stream for
		     transmission to the server */
		  err = xts_map_xasl_to_stream (xasl, &stream, &size);
		  if (err != NO_ERROR)
		    {
		      PT_ERRORm (parser, statement, MSGCAT_SET_PARSER_RUNTIME,
				 MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
		    }
		  for (i = 0; i < update->no_consts; i++)
		    {
		      pr_clear_value (update->consts[i]);
		    }
		  for (i = 0; i < update->no_classes; i++)
		    {
		      if (update->partition[i])
			{
			  for (j = 0; j < update->partition[i]->no_parts; j++)
			    {
			      pr_clear_value (update->partition[i]->parts[j]->
					      vals);
			    }
			}
		    }
		}
	      else
		{
		  err = er_errid ();
		  pt_record_error (parser, parser->statement_number,
				   statement->line_number,
				   statement->column_number, er_msg ());
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
	  PT_NODE *select_statement;

	  /* make sure that lhs->info.name.meta_class != PT_META_ATTR */
	  select_statement = pt_to_upd_del_query (parser, select_values,
						  statement->info.update.spec,
						  statement->info.update.
						  class_specs,
						  statement->info.update.
						  search_cond,
						  statement->info.update.
						  using_index, 0);

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
	  parser_free_tree (parser, not_nulls);	/* check_for_constraints() */
	}

      /* restore tree structure; get_assignment_lists() */
      unlink_list (const_names);
      unlink_list (const_values);
      unlink_list (select_names);
      unlink_list (select_values);
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
  int err, result;
  PT_NODE *flat;
  const char *savepoint_name;
  DB_OBJECT *class_obj;
  PT_NODE *select_names, *select_values, *const_names, *const_values;
  int no_vals, no_consts;
  QFILE_LIST_ID *list_id;
  int au_save;
  float waitsecs = -2, old_waitsecs = -2;
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
	  err = update_object_by_oid (parser, statement);
	  continue;		/* continue to next UPDATE statement */
	}

      /* check if this statement is not necessary to execute,
         e.g. false where or not prepared correctly;
         Note that in LDB case, the statement was not prepared. */
      if (!statement->info.update.do_class_attrs && !statement->xasl_id)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next UPDATE statement */
	}

      flat = statement->info.update.spec->info.spec.flat_entity_list;
      class_obj = (flat) ? flat->info.name.db_object : NULL;
      /* The IX lock on the class is sufficient.
         DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
      if (locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE) == NULL)
	{
	  err = er_errid ();
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
	  err = sm_flush_objects (class_obj);
	  if (err != NO_ERROR)
	    {
	      break;		/* stop while loop if error */
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
	  /* get lists of names and values (expressions and constants)
	     from the assignment part of UPDATE statement */
	  err = get_assignment_lists (parser, &select_names, &select_values,
				      &const_names, &const_values, &no_vals,
				      &no_consts,
				      statement->info.update.assignment);

	  hint_arg = statement->info.update.waitsecs_hint;
	  if (statement->info.update.hint & PT_HINT_LK_TIMEOUT
	      && PT_IS_HINT_NODE (hint_arg))
	    {
	      waitsecs = (float) atof (hint_arg->info.name.original);
	      if (waitsecs >= -1)
		{
		  old_waitsecs = (float) TM_TRAN_WAITSECS ();
		  (void) tran_reset_wait_times (waitsecs);
		}
	    }
	  AU_SAVE_AND_DISABLE (au_save);	/* this prevents authorization
						   checking during execution */
	  if (statement->info.update.do_class_attrs)
	    {
	      /* in case of update class attributes, */
	      err = update_class_attributes (parser, class_obj, select_names,
					     select_values, const_names,
					     const_values);
	    }
	  else
	    {
	      /* in the case of OID list update, now update the seleted OIDs */
	      err = update_objs_for_list_file (parser, list_id, select_names,
					       const_names, const_values,
					       flat,
					       statement->info.update.
					       check_where,
					       (int) statement->info.update.
					       has_unique);
	    }

	  AU_RESTORE (au_save);
	  if (old_waitsecs >= -1)
	    {
	      (void) tran_reset_wait_times (old_waitsecs);
	    }

	  /* restore tree structure; get_assignment_lists() */
	  unlink_list (const_names);
	  unlink_list (const_values);
	  unlink_list (select_names);
	  unlink_list (select_values);
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
		  err = sm_flush_and_decache_objects (class_obj, true);
		}
	      if (err >= NO_ERROR)
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

      if ((err >= NO_ERROR) && class_obj && db_is_vclass (class_obj))
	{
	  err = sm_flush_objects (class_obj);
	}

      if ((err < NO_ERROR) && er_errid () != NO_ERROR)
	{
	  pt_record_error (parser, parser->statement_number,
			   statement->line_number, statement->column_number,
			   er_msg ());
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
			       QFILE_LIST_ID ** result_p, PT_NODE * from,
			       PT_NODE * where, PT_NODE * using_index,
			       PT_NODE * class_specs);
static int delete_object_tuple (const PARSER_CONTEXT * parser,
				const DB_VALUE * values);
static int delete_object_by_oid (const PARSER_CONTEXT * parser,
				 const PT_NODE * statement);
static int delete_list_by_oids (PARSER_CONTEXT * parser,
				QFILE_LIST_ID * list_id);
static int build_xasl_for_server_delete (PARSER_CONTEXT * parser,
					 const PT_NODE * from,
					 PT_NODE * statement);
static int has_unique_constraint (DB_OBJECT * mop);
static int delete_real_class (PARSER_CONTEXT * parser, PT_NODE * spec,
			      PT_NODE * statement);

/*
 * select_delete_list() -
 *   return: Error code
 *   parser(in/out): Parser context
 *   result(out): QFILE_LIST_ID for query result
 *   from(in): From clause in the query
 *   where(in): Where clause in the query
 *   using_index(in): Using index in the query
 *   class_specs(in): Class spec for the query
 *
 * Note : The list_id is allocated during query execution
 */
static int
select_delete_list (PARSER_CONTEXT * parser, QFILE_LIST_ID ** result_p,
		    PT_NODE * from,
		    PT_NODE * where, PT_NODE * using_index,
		    PT_NODE * class_specs)
{
  PT_NODE *statement = NULL;
  QFILE_LIST_ID *result = NULL;

  if (from
      && (from->node_type == PT_SPEC)
      && from->info.spec.range_var
      && ((statement = pt_to_upd_del_query (parser, NULL,
					    from, class_specs,
					    where, using_index,
					    0 /* not server update */ ))
	  != NULL))
    {

      /* If we are updating a proxy, the select is not yet fully translated.
         if wer are updating anything else, this is a no-op. */
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

  if (values && DB_VALUE_TYPE (values) == DB_TYPE_OBJECT)
    {
      object = DB_GET_OBJECT (values);

      if (db_is_deleted (object))
	{
	  /* nested delete trigger may delete the object already */
	  return NO_ERROR;
	}

      /* authorizations checked in compiler--turn off but remember in
         parser so we can re-enable in case we run out of memory and
         longjmp to the cleanup routine. */
      error = db_drop (object);
    }
  else
    {
      error = ER_OBJ_INVALID_ARGUMENTS;
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return error;
}

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
  DB_VALUE oid;
  CURSOR_ID cursor_id;
  int count = 0;		/* how many objects were deleted? */
  const char *savepoint_name = NULL;
  int flush_to_server = -1;
  DB_OBJECT *mop = NULL;

  /* if the list file contains more than 1 object we need to savepoint
     the statement to guarantee statement atomicity. */
  if (list_id->tuple_cnt >= 1)
    {
      savepoint_name =
	mq_generate_name (parser, "UdsP", &delete_savepoint_number);

      error = tran_savepoint (savepoint_name, false);
    }

  if (error == NO_ERROR)
    {
      if (!cursor_open (&cursor_id, list_id, false, false))
	{
	  error = ER_GENERIC_ERROR;
	}
      else
	{
	  cursor_id.query_id = parser->query_id;
	  cursor_status = cursor_next_tuple (&cursor_id);

	  while ((error >= NO_ERROR) && cursor_status == DB_CURSOR_SUCCESS)
	    {
	      /* the db_value has the oid to delete. */
	      error = cursor_get_tuple_value_list (&cursor_id, 1, &oid);
	      if (flush_to_server != 0)
		{
		  mop = DB_GET_OBJECT (&oid);
		  if (flush_to_server == -1)
		    {
		      flush_to_server = has_unique_constraint (mop);
		    }
		}

	      if (error >= NO_ERROR)
		{
		  error = delete_object_tuple (parser, &oid);
		  if (error == ER_HEAP_UNKNOWN_OBJECT && do_Trigger_involved)
		    {
		      er_clear ();
		      error = NO_ERROR;
		      cursor_status = cursor_next_tuple (&cursor_id);
		      continue;
		    }
		}

	      if ((error >= NO_ERROR) && flush_to_server)
		{
		  error = (locator_flush_instance (mop) == NO_ERROR) ?
		    0 : (er_errid () != NO_ERROR ? er_errid () : -1);
		}

	      if (error >= NO_ERROR)
		{
		  count++;	/* another object has been deleted */
		  cursor_status = cursor_next_tuple (&cursor_id);
		}
	    }

	  if ((error >= NO_ERROR) && cursor_status != DB_CURSOR_END)
	    {
	      error = ER_GENERIC_ERROR;
	    }
	  cursor_close (&cursor_id);

	  /* if error and a savepoint was created, rollback to savepoint.
	     No need to rollback if the TM aborted the transaction
	     itself.
	   */
	  if ((error < NO_ERROR) && savepoint_name
	      && error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      (void) tran_abort_upto_savepoint (savepoint_name);
	    }
	}
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
 *   from(in): Class spec to delete
 *   statement(in): Parse tree of a delete statement.
 *
 * Note:
 *  The xasl tree has an DELETE_PROC node as the top node and
 *  a BUILDLIST_PROC as it's aptr.  The BUILDLIST_PROC selects the
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
build_xasl_for_server_delete (PARSER_CONTEXT * parser, const PT_NODE * from,
			      PT_NODE * statement)
{
  int error = NO_ERROR;
  XASL_NODE *xasl = NULL;
  DB_OBJECT *class_obj;
  int size, count = 0;
  char *stream = NULL;
  QUERY_ID query_id = NULL_QUERY_ID;
  QFILE_LIST_ID *list_id = NULL;

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();
  class_obj = from->info.spec.flat_entity_list->info.name.db_object;

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
					 parser->
					 exec_mode | ASYNC_UNEXECUTABLE);
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
 * delete_real_class() - Deletes objects or rows in ldb class or ldb table
 *   return: Error code if delete fails
 *   parser(in/out): Parser context
 *   spec(in): Class spec to delete
 *   statement(in): Delete statement
 */
static int
delete_real_class (PARSER_CONTEXT * parser, PT_NODE * spec,
		   PT_NODE * statement)
{
  int error = NO_ERROR;
  QFILE_LIST_ID *oid_list = NULL;
  int trigger_involved;
  MOBJ class_;
  DB_OBJECT *class_obj;
  float waitsecs = -2, old_waitsecs = -2;
  PT_NODE *hint_arg;

  /* delete a "real" class in this database */

  class_obj = spec->info.spec.flat_entity_list->info.name.db_object;
  /* The IX lock on the class is sufficient.
     DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
  class_ = locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE);
  if (!class_)
    {
      return er_errid ();
    }

  error =
    sm_class_has_triggers (class_obj, &trigger_involved, TR_EVENT_DELETE);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* do delete on server if there is no trigger involved and the
     class is a real class */
  if ((!trigger_involved)
      && (spec->info.spec.flat_entity_list->info.name.virt_object == NULL))
    {
      error = build_xasl_for_server_delete (parser, spec, statement);
    }
  else
    {
      hint_arg = statement->info.delete_.waitsecs_hint;
      if (statement->info.delete_.hint & PT_HINT_LK_TIMEOUT
	  && PT_IS_HINT_NODE (hint_arg))
	{
	  waitsecs = (float) atof (hint_arg->info.name.original);
	  if (waitsecs >= -1)
	    {
	      old_waitsecs = (float) TM_TRAN_WAITSECS ();
	      (void) tran_reset_wait_times (waitsecs);
	    }
	}
      if (error >= NO_ERROR)
	{
	  /* get the oid's and new values */
	  error = select_delete_list (parser,
				      &oid_list,
				      spec,
				      statement->info.delete_.
				      search_cond,
				      statement->info.delete_.
				      using_index,
				      statement->info.delete_.class_specs);
	}
      if (old_waitsecs >= -1)
	{
	  (void) tran_reset_wait_times (old_waitsecs);
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
      else if (!spec)
	{
	  /* this is an delete object if it has no spec */
	  error = delete_object_by_oid (parser, statement);
	}
      else
	{
	  /* the following is the "normal" sql type execution */
	  error = delete_real_class (parser, spec, statement);
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
  bool server_delete;
  XASL_ID *xasl_id;

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

      flat = statement->info.delete_.spec->info.spec.flat_entity_list;
      class_obj = (flat) ? flat->info.name.db_object : NULL;
      /* the presence of a proxy trigger should force the delete
         to be performed through the workspace  */
      AU_SAVE_AND_DISABLE (au_save);	/* because sm_class_has_trigger() calls
					   au_fetch_class() */
      err = sm_class_has_triggers (class_obj, &has_trigger, TR_EVENT_DELETE);
      AU_RESTORE (au_save);
      /* err = has_proxy_trigger(flat, &has_trigger); */
      if (err != NO_ERROR)
	{
	  PT_INTERNAL_ERROR (parser, "delete");
	  break;		/* stop while loop if error */
	}
      /* sm_class_has_triggers() checked if the class has active triggers */
      statement->info.delete_.has_trigger = (bool) has_trigger;

      /* determine whether it can be server-side or OID list deletion */
      server_delete = (!has_trigger && (flat != NULL)
		       && (flat->info.name.virt_object == NULL));
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
	  PT_NODE_PRINT_TO_ALIAS (parser, statement, PT_CONVERT_RANGE);
	  qstr = statement->alias_print;
	  parser->dont_prt_long_string = 0;
	  if (parser->long_string_skipped)
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
					  db_identifier (db_get_user ()),
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
				   statement->column_number, er_msg ());
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
	  select_statement = pt_to_upd_del_query (parser, NULL,
						  delete_info->spec,
						  delete_info->class_specs,
						  delete_info->search_cond,
						  delete_info->using_index,
						  0);
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
  int err, result;
  PT_NODE *flat;
  const char *savepoint_name;
  DB_OBJECT *class_obj;
  QFILE_LIST_ID *list_id;
  int au_save;
  float waitsecs = -2, old_waitsecs = -2;
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
      /* Delete object case:
         delete object by OID */

      if (!statement->info.delete_.spec)
	{
	  err = delete_object_by_oid (parser, statement);
	  continue;		/* continue to next DELETE statement */
	}

      /* check if this statement is not necessary to execute,
         e.g. false where or not prepared correctly;
         Note that in LDB case, the statement was not prepared. */
      if (!statement->xasl_id)
	{
	  statement->etc = NULL;
	  err = NO_ERROR;
	  continue;		/* continue to next DELETE statement */
	}

      /* Server-side deletion or OID list deletion case:
         execute the prepared(stored) XASL (DELETE_PROC or SELECT statement) */

      flat = statement->info.delete_.spec->info.spec.flat_entity_list;
      class_obj = (flat) ? flat->info.name.db_object : NULL;
      /* The IX lock on the class is sufficient.
         DB_FETCH_QUERY_WRITE => DB_FETCH_CLREAD_INSTWRITE */
      if (locator_fetch_class (class_obj, DB_FETCH_CLREAD_INSTWRITE) == NULL)
	{
	  err = er_errid ();
	  break;		/* stop while loop if error */
	}

      /* flush necessary objects before execute */
      err = sm_flush_objects (class_obj);
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

      /* in the case of OID list deletion, now delete the seleted OIDs */
      if (!statement->info.delete_.server_delete
	  && (err >= NO_ERROR) && list_id)
	{
	  hint_arg = statement->info.delete_.waitsecs_hint;
	  if (statement->info.delete_.hint & PT_HINT_LK_TIMEOUT
	      && PT_IS_HINT_NODE (hint_arg))
	    {
	      waitsecs = (float) atof (hint_arg->info.name.original);
	      if (waitsecs >= -1)
		{
		  old_waitsecs = (float) TM_TRAN_WAITSECS ();
		  (void) tran_reset_wait_times (waitsecs);
		}
	    }
	  AU_SAVE_AND_DISABLE (au_save);	/* this prevents authorization
						   checking during execution */
	  /* delete each oid */
	  err = delete_list_by_oids (parser, list_id);
	  AU_RESTORE (au_save);
	  if (old_waitsecs >= -1)
	    {
	      (void) tran_reset_wait_times (old_waitsecs);
	    }
	}

      /* free returned QFILE_LIST_ID */
      if (list_id)
	{
	  if (list_id->tuple_cnt > 0 && statement->info.delete_.server_delete)
	    {
	      err = sm_flush_and_decache_objects (class_obj, true);
	    }
	  if (err >= NO_ERROR)
	    {
	      err = list_id->tuple_cnt;	/* as a result */
	    }
	  regu_free_listid (list_id);
	}
      /* end the query; reset query_id and call qmgr_end_query() */
      if (parser->query_id > 0)
	{
	  if (er_errid () != ER_LK_UNILATERALLY_ABORTED)
	    qmgr_end_query (parser->query_id);
	  parser->query_id = -1;
	}

      /* accumulate intermediate results */
      if (err >= NO_ERROR)
	{
	  result += err;
	}

      if ((err >= NO_ERROR) && class_obj && db_is_vclass (class_obj))
	{
	  err = sm_flush_objects (class_obj);
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

  pt_evaluate_tree (parser, expr, &expr_value);
  if (parser->error_msgs)
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
      error = pt_associate_label_with_value (into_label, into_val);
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
  INSERT_DEFAULT = 4
} SERVER_PREFERENCE;

/* used to generate unique savepoint names */
static int insert_savepoint_number = 0;

/* 0 for no server inserts, a bit vector otherwise */
static int server_preference = -1;

static int insert_object_attr (const PARSER_CONTEXT * parser,
			       DB_OTMPL * otemplate, DB_VALUE * value,
			       PT_NODE * name, DB_ATTDESC * attr_desc);
static int check_for_cons (PARSER_CONTEXT * parser,
			   int *has_unique,
			   PT_NODE ** non_null_attrs,
			   const PT_NODE * attr_list, DB_OBJECT * class_obj);
static int insert_check_for_fk_cache_attr (PARSER_CONTEXT * parser,
					   const PT_NODE * attr_list,
					   DB_OBJECT * class_obj);
static int is_server_insert_allowed (PARSER_CONTEXT * parser,
				     PT_NODE ** non_null_attrs,
				     int *has_uniques, int *server_allowed,
				     const PT_NODE * statement,
				     const PT_NODE * class_);
static int insert_subquery_results (PARSER_CONTEXT * parser,
				    PT_NODE * statement,
				    PT_NODE * class_,
				    const char **savepoint_name);
static int is_attr_not_in_insert_list (const PARSER_CONTEXT * parser,
				       PT_NODE * name_list, const char *name);
static int check_missing_non_null_attrs (const PARSER_CONTEXT * parser,
					 const PT_NODE * statement);
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
      error = dbt_dput_internal (otemplate, attr_desc, value);
    }

  return error;
}

/*
 * do_insert_at_server() - Brief description of this function
 *   return: Error code if insert fails
 *   parser(in/out): Parser context
 *   statement(in): Parse tree of a insert statement.
 *   non_null_attrs(in):
 *   has_uniques(in):
 *
 * Note: Build an xasl tree for a server insert and execute it.
 *
 *  The xasl tree has an INSERT_PROC node as the top node and
 *  a BUILDLIST_PROC as it's aptr.  The BUILDLIST_PROC selects the
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
		     PT_NODE * statement,
		     PT_NODE * non_null_attrs, const int has_uniques)
{
  int error = NO_ERROR;
  XASL_NODE *xasl = NULL;
  int size, count = 0;
  char *stream = NULL;
  QUERY_ID query_id = NULL_QUERY_ID;
  QFILE_LIST_ID *list_id = NULL;
  int i;

  if (!parser || !statement || statement->node_type != PT_INSERT)
    {
      return ER_GENERIC_ERROR;
    }

  /* mark the beginning of another level of xasl packing */
  pt_enter_packing_buf ();
  xasl = pt_to_insert_xasl (parser, statement, has_uniques, non_null_attrs);

  if (xasl)
    {
      INSERT_PROC_NODE *insert = &xasl->proc.insert;

      error = xts_map_xasl_to_stream (xasl, &stream, &size);
      if (error != NO_ERROR)
	{
	  PT_ERRORm (parser, statement,
		     MSGCAT_SET_PARSER_RUNTIME,
		     MSGCAT_RUNTIME_RESOURCES_EXHAUSTED);
	}

      if (insert->partition)
	{
	  for (i = 0; i < insert->partition->no_parts; i++)
	    {
	      pr_clear_value (insert->partition->parts[i]->vals);
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
      error = query_prepare_and_execute (stream,
					 size,
					 &query_id,
					 parser->host_var_count +
					 parser->auto_param_count,
					 parser->host_variables,
					 &list_id,
					 parser->
					 exec_mode | ASYNC_UNEXECUTABLE);
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
 * check_for_cons() - Determines whether an attribute has the constaint
 *   return: Error code
 *   parser(in): Parser context
 *   has_unique(in/out):
 *   non_null_attrs(in/out):
 *   attr_list(in): Parse tree of an insert statement attribute list
 *   class_obj(in): Class object

 */
static int
check_for_cons (PARSER_CONTEXT * parser, int *has_unique,
		PT_NODE ** non_null_attrs, const PT_NODE * attr_list,
		DB_OBJECT * class_obj)
{
  PT_NODE *pointer;

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
 *                                 allowed
 *   return: Returns an error if any input nodes are
 *           misformed.
 *   parser(in): Parser context
 *   non_null_attrs(in/out): Parse tree for attributes with the NOT NULL
 *                           constraint
 *   has_uniques(in/out):
 *   server_allowed(in/out): Boolean flag
 *   statement(in): Parse tree of a insert statement
 *   class(in): Parse tree of the target class
 *
 *
 */
static int
is_server_insert_allowed (PARSER_CONTEXT * parser,
			  PT_NODE ** non_null_attrs,
			  int *has_uniques, int *server_allowed,
			  const PT_NODE * statement, const PT_NODE * class_)
{
  int error = NO_ERROR;
  int trigger_involved;
  PT_NODE *attrs, *attr;
  PT_NODE *vals, *val;

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

  if (error < NO_ERROR)
    {
      return error;
    }

  if (server_preference < 0)
    {
      server_preference = PRM_INSERT_MODE;
    }
  /* check the insert form against the preference */
  if (statement->info.insert.is_value == PT_IS_SUBQUERY)
    {
      if (!(server_preference & INSERT_SELECT))
	{
	  return 0;
	}
    }
  else if (statement->info.insert.is_value == PT_IS_VALUE)
    {
      vals = statement->info.insert.value_clause;
      if (!(server_preference & INSERT_VALUES))
	{
	  return 0;
	}
    }
  else if (statement->info.insert.is_value == PT_IS_DEFAULT_VALUE)
    {
      if (!(server_preference & INSERT_DEFAULT))
	{
	  return 0;
	}
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
	  return 0;
	}
      /* pt_to_regu in parser_generate_xasl can't handle subquery
         because the context isn't set up. Again its feasible
         with some work.
       */
      if (PT_IS_QUERY_NODE_TYPE (val->node_type))
	{
	  return 0;
	}
      val = val->next;
    }

  error = NO_ERROR;
  if (statement->info.insert.into_var != NULL)
    {
      return error;
    }

  /* check option could be done on the server by adding another predicate
     to the insert_info block. However, one must also take care that
     subqueries in this predicate have there xasl blocks properly
     attached to the insert xasl block. Currently, pt_to_pred
     will do that only if being called from parser_generate_xasl.
     This may mean that do_server_insert should call parser_generate_xasl,
     and have a portion of its code put.
   */
  if (statement->info.insert.where != NULL)
    {
      return error;
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
 * do_insert_template() - Inserts an object or row into an object template
 *   return: Error code
 *   parser(in): Short description of the param1
 *   otemplate(in/out): class template to be inserted
 *   statement(in): Parse tree of an insert statement
 *   savepoint_name(in):
 */
int
do_insert_template (PARSER_CONTEXT * parser, DB_OTMPL ** otemplate,
		    PT_NODE * statement, const char **savepoint_name)
{
  const char *into_label;
  DB_VALUE *ins_val, *val, db_value, partcol;
  int error = NO_ERROR;
  PT_NODE *attr, *vc;
  PT_NODE *into;
  PT_NODE *class_;
  PT_NODE *non_null_attrs = NULL;
  DB_ATTDESC **attr_descs = NULL;
  int i, degree, row_count = 0;
  int server_allowed, has_uniques;
  PARTITION_SELECT_INFO *psi = NULL;
  PARTITION_INSERT_CACHE *pic = NULL, *picwork;
  MOP retobj;
  float waitsecs = -2, old_waitsecs = -2;
  PT_NODE *hint_arg;

  db_make_null (&db_value);
  db_make_null (&partcol);

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

  error = is_server_insert_allowed (parser, &non_null_attrs,
				    &has_uniques, &server_allowed,
				    statement, class_);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (server_allowed)
    {
      error = do_insert_at_server (parser, statement,
				   non_null_attrs, has_uniques);
    }
  else if (statement->info.insert.is_value == PT_IS_SUBQUERY
	   && (vc = statement->info.insert.value_clause) != NULL)
    {
      /* execute subquery & insert its results into target class */
      row_count =
	insert_subquery_results (parser, statement, class_, savepoint_name);
      error = (row_count < 0) ? row_count : NO_ERROR;
    }
  else if (statement->info.insert.is_value == PT_IS_VALUE
	   || statement->info.insert.is_value == PT_IS_DEFAULT_VALUE)
    {
      /* there is one value to insert into target class
         If we are doing PT_IS_DEFAULT_VALUE value_clause
         will be NULL and we will only make a template with
         no values put in. */
      row_count = 1;

      /* partition adjust */
      if (class_->info.name.partition_of != NULL)
	{
	  if (do_init_partition_select (class_->info.name.db_object, &psi) ||
	      set_get_element (psi->pattr->data.set, 0, &partcol) != NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_PARTITION_WORK_FAILED, 0);
	      error = er_errid ();
	      if (psi != NULL)
		{
		  do_clear_partition_select (psi);
		  psi = NULL;
		}
	      goto cleanup;
	    }
	  *otemplate = NULL;	/*  for delayed insert */
	}
      else
	{
	  /* now create the object using templates, and then dbt_put
	     each value for each corresponding attribute.
	     Of course, it is presumed that
	     the order in which attributes are defined in the class as
	     well as in the actual insert statement is preserved. */
	  *otemplate =
	    dbt_create_object_internal (class_->info.name.db_object);
	  if (*otemplate == NULL)
	    {
	      error = er_errid ();
	      goto cleanup;
	    }
	}

      vc = statement->info.insert.value_clause;
      attr = statement->info.insert.attr_list;
      degree = pt_length_of_list (attr);

      /* allocate attribute descriptors */
      if (attr)
	{
	  attr_descs = (DB_ATTDESC **) calloc (degree, sizeof (DB_ATTDESC *));
	  if (!attr_descs)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_ATTDESC *));
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto cleanup;
	    }
	}

      hint_arg = statement->info.insert.waitsecs_hint;
      if (statement->info.insert.hint & PT_HINT_LK_TIMEOUT
	  && PT_IS_HINT_NODE (hint_arg))
	{
	  waitsecs = (float) atof (hint_arg->info.name.original);
	  if (waitsecs >= -1)
	    {
	      old_waitsecs = (float) TM_TRAN_WAITSECS ();
	      (void) tran_reset_wait_times (waitsecs);
	    }
	}
      i = 0;
      while (attr && vc)
	{
	  if (vc->node_type == PT_INSERT && !*savepoint_name)
	    {
	      /* this is a nested insert.  recurse to create object
	         template for the nested insert. */
	      DB_OTMPL *temp = NULL;

	      error = do_insert_template (parser, &temp, vc, savepoint_name);
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
	      pt_evaluate_tree_having_serial (parser, vc, &db_value);
	      if (parser->error_msgs)
		{
		  (void) pt_report_to_ersys (parser, PT_EXECUTION);
		  error = er_errid ();
		  db_value_clear (&db_value);
		  break;
		}
	    }

	  if (!*otemplate)
	    {			/* partition adjust */
	      const char *mbs2;
	      if (error < NO_ERROR)
		{
		  db_make_null (&db_value);
		}

	      mbs2 = DB_GET_STRING (&partcol);
	      if (mbs2 == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_PARTITION_WORK_FAILED, 0);
		  break;
		}

	      if (intl_mbs_casecmp (attr->info.name.original, mbs2) == 0)
		{
		  /* partition column */
		  error = do_select_partition (psi, &db_value, &retobj);
		  if (error != NO_ERROR)
		    break;

		  *otemplate = dbt_create_object_internal (retobj);
		  if (*otemplate == NULL)
		    {
		      break;
		    }

		  /* cache back & insert */
		  for (picwork = pic; picwork; picwork = picwork->next)
		    {
		      error = insert_object_attr (parser, *otemplate,
						  picwork->val,
						  picwork->attr,
						  picwork->desc);
		      if (error != NO_ERROR)
			{
			  break;
			}
		    }
		}
	    }

	  if (error >= NO_ERROR)
	    {
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
		  if (*otemplate)
		    {
		      error =
			insert_object_attr (parser, *otemplate, &db_value,
					    attr, attr_descs[i]);
		    }
		  else
		    {
		      error = do_insert_partition_cache (&pic, attr,
							 attr_descs[i],
							 &db_value);
		    }
		}
	    }
	  /* else vc is a SELECT query whose result is empty */

	  /* pt_evaluate_tree() always clones the db_value.
	     Thus we must clear it.
	   */
	  db_value_clear (&db_value);

	  if (!parser->error_msgs)
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
      if (old_waitsecs >= -1)
	{
	  (void) tran_reset_wait_times (old_waitsecs);
	}
      if (pic)
	{
	  if (*otemplate == NULL && (error >= NO_ERROR))
	    {
	      /* key is not specified */
	      error =
		insert_predefined_values_into_partition (parser,
							 otemplate,
							 class_->info.name.
							 db_object,
							 &partcol, psi, pic);
	    }
	  do_clear_partition_cache (pic);
	}
      db_value_clear (&partcol);
      if (psi != NULL)
	{
	  do_clear_partition_select (psi);
	  psi = NULL;
	}
    }

  if (non_null_attrs)
    {
      parser_free_tree (parser, non_null_attrs);
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
	  if (statement->info.insert.is_value == PT_IS_VALUE
	      || statement->info.insert.is_value == PT_IS_DEFAULT_VALUE)
	    {

	      (void) dbt_set_label (*otemplate, ins_val);
	    }
	  else if ((val = (DB_VALUE *) (statement->etc)) != NULL)
	    {
	      db_make_object (ins_val, DB_GET_OBJECT (val));
	    }

	  /* enter {label, ins_val} pair into the label_table */
	  error = pt_associate_label_with_value (into_label, ins_val);
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

  if (psi != NULL)
    {
      do_clear_partition_select (psi);
      psi = NULL;
    }

  return error;
}

/*
 * insert_subquery_results() - Execute subquery & insert its results into
 *                                a target class
 *   return: Error code
 *   parser(in): Handle to the parser used to process & derive subquery qry
 *   statemet(in/out):
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
			 PT_NODE * statement,
			 PT_NODE * class_, const char **savepoint_name)
{
  int error = NO_ERROR;
  CURSOR_ID cursor_id;
  DB_OTMPL *otemplate = NULL;
  DB_OBJECT *obj;
  PT_NODE *attr, *qry, *attrs;
  DB_VALUE *vals = NULL, *val = NULL, *ins_val = NULL, partcol;
  int degree, k, first, cnt, i;
  DB_ATTDESC **attr_descs = NULL;
  PARTITION_SELECT_INFO *psi = NULL;
  PARTITION_INSERT_CACHE *pic = NULL, *picwork;
  MOP retobj;

  if (!parser
      || !statement
      || statement->node_type != PT_INSERT
      || statement->info.insert.is_value != PT_IS_SUBQUERY
      || (qry = statement->info.insert.value_clause) == NULL
      || (attrs = statement->info.insert.attr_list) == NULL)
    {
      return ER_GENERIC_ERROR;
    }

  cnt = 0;
  db_make_null (&partcol);

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
      if (error < NO_ERROR)
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
	      if (class_->info.name.partition_of != NULL)
		{
		  if (do_init_partition_select (class_->info.name.db_object,
						&psi)
		      || set_get_element (psi->pattr->data.set, 0,
					  &partcol) != NO_ERROR)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			      ER_PARTITION_WORK_FAILED, 0);
		      cnt = er_errid ();
		      goto cleanup;
		    }
		}

	      /* for each tuple in subquery result do */
	      first = 1;
	      while (cursor_next_tuple (&cursor_id) == DB_CURSOR_SUCCESS)
		{
		  /* get current tuple of subquery result */
		  if (cursor_get_tuple_value_list (&cursor_id, degree, vals)
		      != NO_ERROR)
		    {
		      break;
		    }

		  /* create an instance of the target class using templates */
		  if (psi)
		    {
		      otemplate = NULL;	/* for delayed insert */
		    }
		  else
		    {
		      otemplate =
			dbt_create_object_internal (class_->info.name.
						    db_object);
		      if (otemplate == NULL)
			{
			  break;
			}
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
				  || !db_is_vclass (attr->info.name.db_object))
				{
				  error = db_get_attribute_descriptor
				    (class_->info.name.db_object,
				     attr->info.name.original, 0, 1,
				     &attr_descs[k]);
				}
			    }
			}

		      if (!otemplate)
			{	/* partition adjust */
			  const char *mbs2 = DB_GET_STRING (&partcol);
			  if (mbs2 == NULL)
			    {
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_PARTITION_WORK_FAILED, 0);
			      break;
			    }

			  if (intl_mbs_casecmp (attr->info.name.original,
						mbs2) == 0)
			    {
			      /* partition column */
			      error = do_select_partition (psi, val, &retobj);
			      if (error)
				{
				  break;
				}

			      otemplate = dbt_create_object_internal (retobj);
			      if (otemplate == NULL)
				{
				  break;
				}

			      /* cache back & insert */
			      for (picwork = pic; picwork;
				   picwork = picwork->next)
				{
				  error = insert_object_attr (parser,
							      otemplate,
							      picwork->val,
							      picwork->attr,
							      picwork->desc);
				  if (error)
				    {
				      break;
				    }
				}
			    }
			}

		      if (error >= NO_ERROR)
			{
			  if (otemplate)
			    {
			      error = insert_object_attr (parser, otemplate,
							  val, attr,
							  attr_descs[k]);
			    }
			  else
			    {
			      error = do_insert_partition_cache (&pic, attr,
								 attr_descs[k],
								 val);
			    }
			}

		      if (error < NO_ERROR)
			{
			  dbt_abort_object (otemplate);
			  cursor_close (&cursor_id);
			  cnt = er_errid ();
			  goto cleanup;
			}
		    }

		  if (pic && !otemplate && error >= NO_ERROR)
		    {
		      /* key is not sepcified */
		      error =
			insert_predefined_values_into_partition
			(parser, &otemplate, class_->info.name.db_object,
			 &partcol, psi, pic);
		    }

		  /* apply the object template */
		  obj = dbt_finish_object (otemplate);

		  if (obj && error >= NO_ERROR)
		    {
		      error = 
			mq_evaluate_check_option (parser, 
						  statement->info.insert.where,
						  obj, class_);
		    }

		  if (obj == NULL || error < NO_ERROR)
		    {
		      cursor_close (&cursor_id);
		      if (obj == NULL)
			{
			  dbt_abort_object (otemplate);
			}

		      db_value_clear (&partcol);

		      cnt = er_errid ();
		      goto cleanup;
		    }

		  /* treat the first new instance as the insert's "result" */
		  if (first)
		    {
		      first = 0;
		      ins_val = db_value_create ();
		      if (ins_val == NULL)
			{
			  cnt = er_errid ();
			  goto cleanup;
			}

		      db_make_object (ins_val, obj);
		      statement->etc = (void *) ins_val;
		    }

		  /* keep track of how many we have inserted */
		  cnt++;

		  if (pic != NULL)
		    {
		      do_clear_partition_cache (pic);
		      pic = NULL;
		    }
		}
	      db_value_clear (&partcol);
	      if (psi != NULL)
		{
		  do_clear_partition_select (psi);
		  psi = NULL;
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

  if (pic != NULL)
    {
      do_clear_partition_cache (pic);
      pic = NULL;
    }

  if (psi != NULL)
    {
      do_clear_partition_select (psi);
      psi = NULL;
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
      if (intl_mbs_casecmp (tmp->info.name.original, name) == 0)
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
 *   parser(in): Short description of the param1
 *   statement(in): Short description of the param2
 */
static int
check_missing_non_null_attrs (const PARSER_CONTEXT * parser,
			      const PT_NODE * statement)
{
  DB_ATTRIBUTE *attr;
  DB_OBJECT *class_;
  int error = NO_ERROR;

  if (!parser
      || !statement
      || !statement->info.insert.spec
      || !statement->info.insert.spec->info.spec.entity_name
      || !(class_ = statement->info.insert.spec->
	   info.spec.entity_name->info.name.db_object))
    {
      return ER_GENERIC_ERROR;
    }

  attr = db_get_attributes (class_);
  while (attr)
    {
      if (db_attribute_is_non_null (attr)
	  && (db_value_type (db_attribute_default (attr)) == DB_TYPE_NULL)
	  && (is_attr_not_in_insert_list (parser,
					  statement->info.insert.attr_list,
					  db_attribute_name (attr))
	      || (statement->info.insert.is_value == PT_IS_DEFAULT_VALUE
		  && db_value_is_null (db_attribute_default (attr))))
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
	node->info.insert.spec->info.spec.flat_entity_list->info.name.
	virt_object) != NULL))
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
  DB_OTMPL *otemplate = NULL;
  int error = NO_ERROR;
  PT_NODE *class_, *vc;
  DB_VALUE *ins_val;
  int save;
  int has_check_option = 0;
  const char *savepoint_name = NULL;

  if (!statement
      || statement->node_type != PT_INSERT
      || !statement->info.insert.spec
      || !statement->info.insert.spec->info.spec.flat_entity_list)
    {
      return ER_GENERIC_ERROR;
    }

  class_ = statement->info.insert.spec->info.spec.flat_entity_list;

  statement->etc = NULL;

  error = check_missing_non_null_attrs (parser, statement);
  if (error != NO_ERROR)
    {
      return error;
    }

  statement = parser_walk_tree (parser, statement, NULL, NULL,
				test_check_option, &has_check_option);

  /* if the insert statement contains more than one insert component,
     we savepoint the insert components to try to guarantee insert
     statement atomicity.
   */

  if (!savepoint_name && has_check_option)
    {
      savepoint_name =
	mq_generate_name (parser, "UisP", &insert_savepoint_number);
      error = tran_savepoint (savepoint_name, false);
    }

  if (statement->info.insert.is_value == PT_IS_SUBQUERY
      && (vc = statement->info.insert.value_clause)
      && pt_false_where (parser, vc))
    {
    }
  else
    {
      /* DO NOT RETURN UNTIL AFTER AU_ENABLE! */
      AU_DISABLE (save);
      parser->au_save = save;

      error = do_insert_template (parser, &otemplate, statement,
				  &savepoint_name);

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

	  if (error >= NO_ERROR)
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

      AU_ENABLE (save);

      if (parser->abort)
	{
	  error = er_errid ();
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
         This is in place of the extern function:
         db_abort_to_savepoint(savepoint_name);
       */
    }

  return error;
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
do_prepare_insert (const PARSER_CONTEXT * parser, const PT_NODE * statement)
{
  int err;
  err = NO_ERROR;
  return err;
}				/* do_prepare_insert() */

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
  err = NO_ERROR;
  return err;
}				/* do_execute_insert() */

/*
 * insert_predefined_values_into_partition () - Execute the prepared INSERT
 *                                                 statement
 *   return: Error code
 *   parser(in): Parser context
 *   rettemp(out): Partitioned new instance template
 *   classobj(in): Class's db_object
 *   partcol(in): Partition's key column name
 *   psi(in): Partition selection information
 *   pic(in): Partiton insert cache
 *
 * Note: attribute's auto increment value or default value
 *       is fetched, partiton selection is performed,
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
  char oid_str[36];
  DB_VALUE next_val, auto_inc_val, oid_str_val;
  int r = 0, found;
  char auto_increment_name[SM_MAX_IDENTIFIER_LENGTH];
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

      if (intl_mbs_casecmp (db_attribute_name (dbattr), mbs2) == 0)
	{
	  /* partition column */

	  /* 1. check auto increment */
	  if (dbattr->flags & SM_ATTFLAG_AUTO_INCREMENT)
	    {
	      /* set auto increment value */
	      if (dbattr->auto_increment == NULL)
		{
		  class_name = sm_class_name (dbattr->class_mop);
		  SET_AUTO_INCREMENT_SERIAL_NAME (auto_increment_name,
						  class_name,
						  dbattr->header.name);
		  r = do_get_serial_obj_id (&serial_obj_id, &found,
					    auto_increment_name);
		  if (r == NO_ERROR && found)
		    {
		      dbattr->auto_increment = db_object (&serial_obj_id);
		    }
		}

	      if (dbattr->auto_increment != NULL)
		{
		  db_make_null (&next_val);

		  sprintf (oid_str, "%d %d %d",
			   dbattr->auto_increment->oid_info.oid.pageid,
			   dbattr->auto_increment->oid_info.oid.slotid,
			   dbattr->auto_increment->oid_info.oid.volid);
		  db_make_string (&oid_str_val, oid_str);

		  error = qp_get_serial_next_value (&next_val, &oid_str_val);
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

  pt_evaluate_tree (parser, target, &target_value);
  if (parser->error_msgs)
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

      if (!obj || parser->error_msgs)
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
	      pt_evaluate_tree (parser, vc, &db_value);
	      if (parser->error_msgs)
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
      if (parser->error_msgs)
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
	      error = pt_associate_label_with_value (into_label, ins_val);
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

#if defined(CUBRID_DEBUG)
  PT_NODE_PRINT_TO_ALIAS (parser, statement, PT_CONVERT_RANGE);
#endif

  pt_null_etc (statement);

  xasl = parser_generate_xasl (parser, statement);

  if (xasl && !pt_has_error (parser))
    {
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
						 parser->
						 auto_param_count,
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
			      pt_associate_label_with_value (into_label,
							     db_value_copy
							     (v));
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
  PT_NODE_PRINT_TO_ALIAS (parser, statement, PT_CONVERT_RANGE);
  qstr = statement->alias_print;
  parser->dont_prt_long_string = 0;
  if (parser->long_string_skipped)
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
      err = qmgr_drop_query_plan (qstr, db_identifier (db_get_user ()),
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

  /* check if this statement is not necessary to execute,
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

  /* flush necessary objects before execute */
  if (!parser->dont_flush && ws_has_updated ())
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
		pt_associate_label_with_value (into_label, db_value_copy (v));

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
      name = pt_print_bytes_l (parser, statement->info.drop.spec_list);
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

#if 0
      /* serial replication is not schema replication but data replication */
    case PT_CREATE_SERIAL:
      name = pt_print_bytes (parser, statement->info.serial.serial_name);
      repl_schema.statement_type = CUBRID_STMT_CREATE_SERIAL;
      break;

    case PT_ALTER_SERIAL:
      name = pt_print_bytes (parser, statement->info.serial.serial_name);
      repl_schema.statement_type = CUBRID_STMT_ALTER_SERIAL;
      break;

    case PT_DROP_SERIAL:
      name = pt_print_bytes (parser, statement);
      repl_schema.statement_type = CUBRID_STMT_DROP_SERIAL;
      break;
#endif

    case PT_DROP_VARIABLE:
      name = pt_print_bytes (parser, statement->info.drop_variable.var_names);
      repl_schema.statement_type = CUBRID_STMT_DROP_LABEL;
      break;

    case PT_CREATE_STORED_PROCEDURE:
      name = pt_print_bytes (parser, statement);
      repl_schema.statement_type = CUBRID_STMT_CREATE_STORED_PROCEDURE;
      break;

    case PT_DROP_STORED_PROCEDURE:
      name = pt_print_bytes (parser, statement);
      repl_schema.statement_type = CUBRID_STMT_DROP_STORED_PROCEDURE;
      break;

    case PT_CREATE_USER:
      name = pt_print_bytes (parser, statement->info.create_user.user_name);
      repl_schema.statement_type = CUBRID_STMT_CREATE_USER;
      break;

    case PT_ALTER_USER:
      name = pt_print_bytes (parser, statement->info.alter_user.user_name);
      repl_schema.statement_type = CUBRID_STMT_ALTER_USER;
      break;

    case PT_DROP_USER:
      name = pt_print_bytes (parser, statement->info.drop_user.user_name);
      repl_schema.statement_type = CUBRID_STMT_DROP_USER;
      break;

    case PT_GRANT:
      name = pt_print_bytes (parser, statement->info.grant.user_list);
      repl_schema.statement_type = CUBRID_STMT_GRANT;
      break;

    case PT_REVOKE:
      name = pt_print_bytes (parser, statement->info.revoke.user_list);
      repl_schema.statement_type = CUBRID_STMT_REVOKE;
      break;

    case PT_CREATE_TRIGGER:
      name =
	pt_print_bytes (parser, statement->info.create_trigger.trigger_name);
      repl_schema.statement_type = CUBRID_STMT_CREATE_TRIGGER;
      break;

    case PT_RENAME_TRIGGER:
      name = pt_print_bytes (parser, statement->info.rename_trigger.old_name);
      repl_schema.statement_type = CUBRID_STMT_RENAME_TRIGGER;
      break;

    case PT_DROP_TRIGGER:
      name = pt_print_bytes (parser,
			     statement->info.drop_trigger.trigger_spec_list);
      repl_schema.statement_type = CUBRID_STMT_DROP_TRIGGER;
      break;

    case PT_REMOVE_TRIGGER:
      name =
	pt_print_bytes (parser,
			statement->info.remove_trigger.trigger_spec_list);
      repl_schema.statement_type = CUBRID_STMT_REMOVE_TRIGGER;
      break;

    case PT_ALTER_TRIGGER:
      name = pt_print_bytes_l (parser,
			       statement->info.alter_trigger.
			       trigger_spec_list);
      repl_schema.statement_type = CUBRID_STMT_SET_TRIGGER;
      break;

    default:
      break;
    }

  if (name == NULL)
    {
      return NO_ERROR;
    }

  repl_info.repl_info_type = REPL_INFO_TYPE_SCHEMA;
  repl_schema.name = (char *) pt_get_varchar_bytes (name);
  repl_schema.ddl = parser_print_tree (parser, statement);

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
	  if (PRM_XASL_MAX_PLAN_CACHE_ENTRIES > 0)
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
					  stmt->info.trigger_action.
					  expression);
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
