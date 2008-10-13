/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * execute_serial.c - Do create/alter/drop serial statement
 * TODO: rename this file to execute_serial.c
 */

#ident "$Id$"

#include "config.h"

#include "db.h"
#include "dbi.h"
#include "error_manager.h"
#include "parser.h"
#include "parser.h"
#include "schema_manager_3.h"
#include "transform.h"
#include "msgexec.h"
#include "system_parameter.h"
#include "execute_statement_10.h"
#if defined(WINDOWS)
#include "ustring.h"
#endif
#include "dbval.h"

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
  int error = NO_ERROR;

  db_make_null (&value);

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

  class_mop = sm_find_class (SR_CLASS_NAME);
  if (class_mop == NULL)
    {
      return ER_FAILED;
    }

  p = (char *) malloc (strlen (serial_name) + 1);
  if (p == NULL)
    {
      return ER_FAILED;
    }

  intl_mbs_lower (serial_name, p);

  db_make_string (&val, p);

  mop = db_find_unique (class_mop, SR_ATT_NAME, &val);

  if (mop)
    {
      *found = 1;
      *serial_obj_id = *db_identifier (mop);
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
      goto end;
    }

  /*
   * lookup if serial object name already exists?
   */

  name = (char *) PT_NODE_SR_NAME (statement);
  p = (char *) malloc (strlen (name) + 1);
  if (p == NULL)
    {
      goto end;
    }
  intl_mbs_lower (name, p);

  r = do_get_serial_obj_id (&serial_obj_id, &found, p);

  if (r == 0 && found)
    {
      error = MSGCAT_SEMANTIC_SERIAL_ALREADY_EXIST;
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error, name);
      goto end;
    }

  /* get all values as string */
  (void) numeric_coerce_string_to_num ("0", &zero);
  (void)
    numeric_coerce_string_to_num ("10000000000000000000000000000000000000",
				  &e37);
  (void)
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
	  error = MSGCAT_SEMANTIC_SERIAL_INC_VAL_ZERO;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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

  (void) numeric_coerce_string_to_num ("0", &zero);
  (void)
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

  DB_VALUE creator_name_val, user_name_val;
  char *creator_name = NULL, *user_name = NULL;

  char *name = NULL;
  PT_NODE *inc_val_node;
  PT_NODE *max_val_node;
  PT_NODE *min_val_node;

  DB_VALUE zero, e37, under_e36;
  DB_DATA_STATUS data_stat;
  DB_VALUE old_inc_val, old_max_val, old_min_val, current_val;
  DB_VALUE new_inc_val, new_max_val, new_min_val;
  DB_VALUE cmp_result, cmp_result2;
  DB_VALUE class_name_val;

  int new_inc_val_flag = 0;
  int new_cyclic;
  int inc_val_change, max_val_change, min_val_change, cyclic_change;

  int error = NO_ERROR;
  int found = 0, r = 0, save;
  bool au_disable_flag = false;
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
  db_make_null (&class_name_val);

  /*
   * find db_serial_class
   */
  serial_class = db_find_class (SR_CLASS_NAME);
  if (serial_class == NULL)
    {
      error = ER_QPROC_DB_SERIAL_NOT_FOUND;
      goto end;
    }

  /*
   * lookup if serial object name already exists?
   */

  name = (char *) PT_NODE_SR_NAME (statement);

  r = do_get_serial_obj_id (&serial_obj_id, &found, name);

  if (r == 0 && found)
    {
      serial_object = db_object (&serial_obj_id);
      if (serial_object == NULL)
	{
	  error = MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, error,
		      name);
	  goto end;
	}
    }
  else
    {
      error = MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED;
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, error, name);
      goto end;
    }

  error = db_get (serial_object, SR_ATT_CLASS_NAME, &class_name_val);
  if (error < 0)
    {
      goto end;
    }

  if (!DB_IS_NULL (&class_name_val))
    {
      error = MSGCAT_RUNTIME_SERIAL_IS_AUTO_INCREMENT_OBJ;
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, error, name);
      pr_clear_value (&class_name_val);
      goto end;
    }

  /*
   * check if user is creator or dba
   */
  error = db_get (serial_object, "owner.name", &creator_name_val);
  if (error < 0)
    {
      goto end;
    }
  creator_name = DB_GET_STRING (&creator_name_val);

  error = db_get (Au_user, "name", &user_name_val);
  if (error < 0)
    {
      goto end;
    }
  user_name = DB_GET_STRING (&user_name_val);

  if (strcasecmp (creator_name, "extern") != 0
      && strcasecmp (user_name, "dba") != 0
      && strcasecmp (creator_name, user_name) != 0)
    {
      error = MSGCAT_RUNTIME_RT_SERIAL_ALTER_NOT_ALLOWED;
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, error, 0);
      pr_clear_value (&creator_name_val);
      pr_clear_value (&user_name_val);
      goto end;
    }
  pr_clear_value (&creator_name_val);
  pr_clear_value (&user_name_val);

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

  (void) numeric_coerce_string_to_num ("0", &zero);
  (void)
    numeric_coerce_string_to_num ("10000000000000000000000000000000000000",
				  &e37);
  (void)
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
	  error = MSGCAT_SEMANTIC_SERIAL_INC_VAL_ZERO;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
	  goto end;
	}

      error =
	numeric_db_value_compare (&new_min_val, &current_val, &cmp_result);
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
	  if (min_val_change)
	    {
	      error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
	      goto end;
	    }
	  else
	    {
	      error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
	      goto end;
	    }
	}

      error =
	numeric_db_value_compare (&new_max_val, &current_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      if (DB_GET_INT (&cmp_result) < 0)
	{
	  error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	      error = MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
	      goto end;
	    }
	  else if (max_val_change)
	    {
	      error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
	      goto end;
	    }
	  else
	    {
	      error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_OVERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	  error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_UNDERFLOW;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
	  goto end;
	}

      error =
	numeric_db_value_compare (&new_max_val, &current_val, &cmp_result);
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
	      error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
	      goto end;
	    }
	  else
	    {
	      error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
	      goto end;
	    }
	}

      error =
	numeric_db_value_compare (&new_min_val, &current_val, &cmp_result);
      if (error != NO_ERROR)
	{
	  goto end;
	}

      if (DB_GET_INT (&cmp_result) > 0)
	{
	  error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC, error,
		      0);
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
	      error = MSGCAT_SEMANTIC_SERIAL_INC_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
	      goto end;
	    }
	  else if (min_val_change)
	    {
	      error = MSGCAT_SEMANTIC_SERIAL_MIN_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
	      goto end;
	    }
	  else
	    {
	      error = MSGCAT_SEMANTIC_SERIAL_MAX_VAL_INVALID;
	      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_SEMANTIC,
			  error, 0);
	      goto end;
	    }
	}
    }


  /* now create serial object which is insert into db_serial */
  AU_DISABLE (save);
  au_disable_flag = true;

  obj_tmpl = dbt_edit_object (serial_object);
  if (obj_tmpl == NULL)
    {
      error = er_errid ();
      goto end;
    }

  /* increment_val */
  if (inc_val_change)
    {
      error = dbt_put_internal (obj_tmpl, SR_ATT_INCREMENT_VAL, &new_inc_val);
      if (error < 0)
	{
	  goto end;
	}
      pr_clear_value (&new_inc_val);
    }

  /* max_val */
  if (max_val_change)
    {
      error = dbt_put_internal (obj_tmpl, SR_ATT_MAX_VAL, &new_max_val);
      if (error < 0)
	{
	  goto end;
	}
      pr_clear_value (&new_max_val);
    }

  /* min_val */
  if (min_val_change)
    {
      error = dbt_put_internal (obj_tmpl, SR_ATT_MIN_VAL, &new_min_val);
      if (error < 0)
	{
	  goto end;
	}
      pr_clear_value (&new_min_val);
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
  DB_VALUE creator_name_val, user_name_val;
  DB_VALUE class_name_val;
  char *creator_name = NULL, *user_name = NULL;
  char *name;
  int error = NO_ERROR;
  int found = 0, r = 0, save;
  bool au_disable_flag = false;

  db_make_null (&class_name_val);

  if (PRM_BLOCK_DDL_STATEMENT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AU_AUTHORIZATION_FAILURE,
	      0);
      return ER_AU_AUTHORIZATION_FAILURE;
    }

  serial_class = db_find_class (SR_CLASS_NAME);
  if (serial_class == NULL)
    {
      error = ER_QPROC_DB_SERIAL_NOT_FOUND;
      goto end;
    }

  name = (char *) PT_NODE_SR_NAME (statement);

  r = do_get_serial_obj_id (&serial_obj_id, &found, name);

  if (r == 0 && found)
    {
      serial_object = db_object (&serial_obj_id);
      if (serial_object == NULL)
	{
	  error = MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED;
	  PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, error,
		      name);
	  goto end;
	}
    }
  else
    {
      error = MSGCAT_RUNTIME_RT_SERIAL_NOT_DEFINED;
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, error, name);
      goto end;
    }

  error = db_get (serial_object, SR_ATT_CLASS_NAME, &class_name_val);
  if (error < 0)
    {
      goto end;
    }

  if (!DB_IS_NULL (&class_name_val))
    {
      error = MSGCAT_RUNTIME_SERIAL_IS_AUTO_INCREMENT_OBJ;
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, error, name);
      pr_clear_value (&class_name_val);
      goto end;
    }

  /*
   * check if user is creator or dba
   */
  error = db_get (serial_object, "owner.name", &creator_name_val);
  if (error < 0)
    {
      goto end;
    }
  creator_name = DB_GET_STRING (&creator_name_val);

  error = db_get (Au_user, "name", &user_name_val);
  if (error < 0)
    {
      goto end;
    }
  user_name = DB_GET_STRING (&user_name_val);

  if (strcasecmp (creator_name, "extern") != 0
      && strcasecmp (user_name, "dba") != 0
      && strcasecmp (creator_name, user_name) != 0)
    {
      error = MSGCAT_RUNTIME_RT_SERIAL_ALTER_NOT_ALLOWED;
      PT_ERRORmf (parser, statement, MSGCAT_SET_PARSER_RUNTIME, error, 0);
      pr_clear_value (&creator_name_val);
      pr_clear_value (&user_name_val);
      goto end;
    }
  pr_clear_value (&creator_name_val);
  pr_clear_value (&user_name_val);

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
