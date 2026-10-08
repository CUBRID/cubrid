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
 * query_opfunc.c - The manipulation of data stored in the XASL nodes
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <assert.h>

#include "query_opfunc.h"
#include "qfile_tuple_layout.h"

#include "system_parameter.h"
#include "error_manager.h"
#include "fetch.h"
#include "list_file.h"
#include "object_domain.h"
#include "object_domain_convert.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "set_object.h"
#include "query_executor.h"
#include "databases_file.h"
#include "tz_support.h"
#include "memory_hash.h"
#include "numeric_opfunc.h"
#include "tz_support.h"
#include "db_date.h"
#include "dbtype.h"
#include "query_dump.h"
#include "query_list.h"
#include "db_json.hpp"
#include "arithmetic.h"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "xserver_interface.h"
#include "intl_support.h"

#include "dbtype.h"

#include <chrono>
#include <regex>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define NOT_NULL_VALUE(a, b)	((a) ? (a) : (b))
#define INITIAL_OID_STACK_SIZE  1

#define	SYS_CONNECT_BY_PATH_MEM_STEP	256

/* value pointer staging for the private-buffer tuple writers: stack for the usual column counts */
#define QDATA_TUPLE_VALS_STACK 64

static bool qdata_is_zero_value_date (DB_VALUE * dbval_p);

static int qdata_copy_values_to_tuple (THREAD_ENTRY * thread_p, DB_VALUE ** vals, int n,
				       qfile_tuple_value_type_list * type_list, qfile_tuple_record * tuple_record_p);

static int qdata_add_short (short s1, short s2, DB_VALUE * result_p);
static int qdata_add_int (int i1, int i2, DB_VALUE * result_p);
static int qdata_add_bigint (DB_BIGINT i1, DB_BIGINT i2, DB_VALUE * result_p);
static int qdata_add_float (float f1, float f2, DB_VALUE * result_p);
static int qdata_add_double (double d1, double d2, DB_VALUE * result_p);
static double qdata_coerce_numeric_to_double (DB_VALUE * numeric_val_p);
static void qdata_coerce_dbval_to_numeric (DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_numeric_to_monetary (DB_VALUE * numeric_val_p, DB_VALUE * monetary_val_p, DB_VALUE * result_p);
static int qdata_add_monetary (double d1, double d2, DB_CURRENCY type, DB_VALUE * result_p);
static int qdata_add_bigint_to_time (DB_VALUE * time_val_p, DB_BIGINT add_time, DB_VALUE * result_p);
static int qdata_add_short_to_utime_asymmetry (DB_VALUE * utime_val_p, short s, unsigned int *utime,
					       DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_int_to_utime_asymmetry (DB_VALUE * utime_val_p, int i, unsigned int *utime, DB_VALUE * result_p,
					     TP_DOMAIN * domain_p);
static int qdata_add_short_to_utime (DB_VALUE * utime_val_p, short s, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_int_to_utime (DB_VALUE * utime_val_p, int i, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_bigint_to_utime (DB_VALUE * utime_val_p, DB_BIGINT bi, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_short_to_timestamptz (DB_VALUE * ts_tz_val_p, short s, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_int_to_timestamptz (DB_VALUE * ts_tz_val_p, int i, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_bigint_to_timestamptz (DB_VALUE * ts_tz_val_p, DB_BIGINT bi, DB_VALUE * result_p,
					    TP_DOMAIN * domain_p);
static int qdata_add_short_to_datetime (DB_VALUE * datetime_val_p, short s, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_int_to_datetime (DB_VALUE * datetime_val_p, int i, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_bigint_to_datetime (DB_VALUE * datetime_val_p, DB_BIGINT bi, DB_VALUE * result_p,
					 TP_DOMAIN * domain_p);
static int qdata_add_short_to_date (DB_VALUE * date_val_p, short s, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_int_to_date (DB_VALUE * date_val_p, int i, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_bigint_to_date (DB_VALUE * date_val_p, DB_BIGINT i, DB_VALUE * result_p, TP_DOMAIN * domain_p);

static int qdata_add_chars_to_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p);
static int qdata_add_sequence_to_dbval (DB_VALUE * seq_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					TP_DOMAIN * domain_p);
static int qdata_add_time_to_dbval (DB_VALUE * time_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_utime_to_dbval (DB_VALUE * utime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
				     TP_DOMAIN * domain_p);
static int qdata_add_timestamptz_to_dbval (DB_VALUE * ts_tz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_datetime_to_dbval (DB_VALUE * datetime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					TP_DOMAIN * domain_p);
static int qdata_add_datetimetz_to_dbval (DB_VALUE * datetimetz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_date_to_dbval (DB_VALUE * date_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
				    TP_DOMAIN * domain_p);
static int qdata_add_datetime_value (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p,
				     TP_DOMAIN * domain_p);
static int qdata_coerce_result_to_domain (DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_cast_to_domain (DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p);

static int qdata_subtract_short (short s1, short s2, DB_VALUE * result_p);
static int qdata_subtract_int (int i1, int i2, DB_VALUE * result_p);
static int qdata_subtract_bigint (DB_BIGINT i1, DB_BIGINT i2, DB_VALUE * result_p);
static int qdata_subtract_float (float f1, float f2, DB_VALUE * result_p);
static int qdata_subtract_double (double d1, double d2, DB_VALUE * result_p);
static int qdata_subtract_monetary (double d1, double d2, DB_CURRENCY currency, DB_VALUE * result_p);
static int qdata_subtract_time (DB_TIME u1, DB_TIME u2, DB_VALUE * result_p);
static int qdata_subtract_utime (DB_UTIME u1, DB_UTIME u2, DB_VALUE * result_p);
static int qdata_subtract_utime_to_short_asymmetry (DB_VALUE * utime_val_p, short s, unsigned int *utime,
						    DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_subtract_utime_to_int_asymmetry (DB_VALUE * utime_val_p, int i, unsigned int *utime,
						  DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_subtract_datetime_to_int (DB_DATETIME * dt1, DB_BIGINT i2, DB_VALUE * result_p);
static int qdata_subtract_datetime (DB_DATETIME * dt1, DB_DATETIME * dt2, DB_VALUE * result_p);
static int qdata_subtract_datetime_to_int_asymmetry (DB_VALUE * datetime_val_p, DB_BIGINT i, DB_DATETIME * datetime,
						     DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_subtract_number_to_datetime (DB_VALUE * number_p, DB_VALUE * datetime_p, DB_VALUE * result_p);
static int qdata_subtract_sequence_to_dbval (DB_VALUE * seq_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					     TP_DOMAIN * domain_p);
static int qdata_subtract_time_to_dbval (DB_VALUE * time_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_utime_to_dbval (DB_VALUE * utime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					  TP_DOMAIN * domain_p);
static int qdata_subtract_timestampltz_to_dbval (DB_VALUE * ts_ltz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
						 TP_DOMAIN * domain_p);
static int qdata_subtract_timestamptz_to_dbval (DB_VALUE * utime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
						TP_DOMAIN * domain_p);
static int qdata_subtract_datetime_to_dbval (DB_VALUE * datetime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					     TP_DOMAIN * domain_p);
static int qdata_subtract_datetimetz_to_dbval (DB_VALUE * dt_tz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					       TP_DOMAIN * domain_p);
static int qdata_subtract_date_to_dbval (DB_VALUE * date_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					 TP_DOMAIN * domain_p);
static int qdata_subtract_datetime_value (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p,
					  TP_DOMAIN * domain_p);

static int qdata_multiply_short (short s1, short s2, DB_VALUE * result_p);
static int qdata_multiply_int (int i1, int i2, DB_VALUE * result_p);
static int qdata_multiply_bigint (DB_BIGINT bi1, DB_BIGINT bi2, DB_VALUE * result_p);
static int qdata_multiply_float (float f1, float f2, DB_VALUE * result_p);
static int qdata_multiply_double (double d1, double d2, DB_VALUE * result_p);
static int qdata_multiply_monetary (DB_VALUE * monetary_val_p, double d, DB_VALUE * result_p);
static int qdata_multiply_sequence_to_dbval (DB_VALUE * seq_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					     TP_DOMAIN * domain_p);

static bool qdata_is_divided_zero (DB_VALUE * dbval_p);
static int qdata_divide_short (short s1, short s2, DB_VALUE * result_p);
static int qdata_divide_int (int i1, int i2, DB_VALUE * result_p);
static int qdata_divide_bigint (DB_BIGINT bi1, DB_BIGINT bi2, DB_VALUE * result_p);
static int qdata_divide_float (float f1, float f2, DB_VALUE * result_p);
static int qdata_divide_double (double d1, double d2, DB_VALUE * result_p, bool is_check_overflow);
static int qdata_divide_monetary (double d1, double d2, DB_CURRENCY currency, DB_VALUE * result_p,
				  bool is_check_overflow);

static bool qdata_number_as_short (DB_VALUE * value, short *s);
static bool qdata_number_as_int (DB_VALUE * value, int *i);
static bool qdata_number_as_bigint (DB_VALUE * value, DB_BIGINT * bi);
static bool qdata_number_as_float (DB_VALUE * value, float *f);
static bool qdata_number_as_double (DB_VALUE * value, double *d);
static int qdata_number_numeric (OPERATOR_TYPE opcode, DB_VALUE * value1, DB_VALUE * value2, DB_VALUE * result_p);
static int qdata_number_monetary (OPERATOR_TYPE opcode, DB_VALUE * value1, DB_VALUE * value2, DB_VALUE * result_p);
static int qdata_number_operator (OPERATOR_TYPE opcode, DB_TYPE result_type, DB_VALUE * value1, DB_VALUE * value2,
				  DB_VALUE * result_p);
static int qdata_collection_operator (OPERATOR_TYPE opcode, DB_TYPE result_type, DB_VALUE * value1,
				      DB_VALUE * value2, DB_VALUE * result_p, TP_DOMAIN * domain_p);

static DB_VALUE *qdata_get_dbval_from_constant_regu_variable (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var,
							      VAL_DESCR * val_desc_p);
static int qdata_convert_dbvals_to_set (THREAD_ENTRY * thread_p, DB_TYPE stype, REGU_VARIABLE * func,
					VAL_DESCR * val_desc_p, OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec);
static int qdata_evaluate_generic_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
					    OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec);
static int qdata_get_class_of_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
					OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec);

static int qdata_convert_table_to_set (THREAD_ENTRY * thread_p, DB_TYPE stype, REGU_VARIABLE * func,
				       VAL_DESCR * val_desc_p);

static int qdata_insert_substring_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
					    OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec);

static int qdata_elt (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		      QFILE_TUPLE_RECORD * tplrec);
static int qdata_benchmark (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
			    OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec);

static int qdata_regexp_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
				  OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec);

static int qdata_convert_operands_to_value_and_call (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p,
						     VAL_DESCR * val_desc_p, OID * obj_oid_p,
						     QFILE_TUPLE_RECORD * tplrec, int (*function_to_call) (DB_VALUE *,
													   DB_VALUE *
													   const *,
													   int const));

static bool
qdata_is_zero_value_date (DB_VALUE * dbval_p)
{
  DB_TYPE type;
  DB_UTIME *utime;
  DB_DATE *date;
  DB_DATETIME *datetime;
  DB_TIMESTAMPTZ *ts_tz;
  DB_DATETIMETZ *dt_tz;

  if (DB_IS_NULL (dbval_p))	/* NULL is not zero value */
    {
      return false;
    }

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);
  if (TP_IS_DATE_TYPE (type))
    {
      switch (type)
	{
	case DB_TYPE_TIMESTAMP:
	case DB_TYPE_TIMESTAMPLTZ:
	  utime = db_get_timestamp (dbval_p);
	  return (*utime == 0);
	case DB_TYPE_TIMESTAMPTZ:
	  ts_tz = db_get_timestamptz (dbval_p);
	  return (ts_tz->timestamp == 0);
	case DB_TYPE_DATETIME:
	case DB_TYPE_DATETIMELTZ:
	  datetime = db_get_datetime (dbval_p);
	  return (datetime->date == 0 && datetime->time == 0);
	case DB_TYPE_DATETIMETZ:
	  dt_tz = db_get_datetimetz (dbval_p);
	  return (dt_tz->datetime.date == 0 && dt_tz->datetime.time == 0);
	case DB_TYPE_DATE:
	  date = db_get_date (dbval_p);
	  return (*date == 0);
	default:
	  break;
	}
    }

  return false;
}

/*
 * qdata_set_value_list_to_null () -
 *   return:
 *   val_list(in)       : Value List
 *
 * Note: Set all db_values on the value list to null.
 */
void
qdata_set_value_list_to_null (val_list_node * val_list_p)
{
  QPROC_DB_VALUE_LIST db_val_list;

  if (val_list_p == NULL)
    {
      return;
    }

  db_val_list = val_list_p->valp;
  while (db_val_list)
    {
      pr_clear_value (db_val_list->val);
      db_val_list = db_val_list->next;
    }
}

/*
 * COPY ROUTINES
 */

/*
 * qdata_copy_db_value () -
 *   return: int (true on success, false on failure)
 *   dbval1(in) : Destination db_value node
 *   dbval2(in) : Source db_value node
 *
 * Note: Copy source value to destination value.
 */
bool
qdata_copy_db_value (DB_VALUE * dest_p, const DB_VALUE * src_p)
{
  const PR_TYPE *pr_type_p;
  DB_TYPE src_type;

  /* check if there is nothing to do, so we don't clobber a db_value if we happen to try to copy it to itself */
  if (dest_p == src_p)
    {
      return true;
    }

  /* clear any value from a previous iteration */
  (void) pr_clear_value (dest_p);

  src_type = DB_VALUE_DOMAIN_TYPE (src_p);
  pr_type_p = pr_type_from_id (src_type);
  if (pr_type_p == NULL)
    {
      return false;
    }

  if (pr_type_p->setval (dest_p, src_p, true) == NO_ERROR)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * qdata_copy_valptr_list_to_tuple () -
 *   return: NO_ERROR, or ER_code
 *   valptr_list(in)    : Value pointer list
 *   vd(in)     : Value descriptor
 *   type_list(in)     : layout descriptor of the list the tuple is written into
 *   tplrec(in) : Tuple descriptor
 *
 * Note: Copy valptr_list values to tuple descriptor.  Regu variables
 * that are hidden columns are not copied to the list file tuple.
 * The values are fetched once, then the tuple assembler measures and fills (the BIG-tuple / SET-type path of
 * qexec_generate_tuple_descriptor, so the value array is stack resident for the usual column counts).
 */
int
qdata_copy_valptr_list_to_tuple (THREAD_ENTRY * thread_p, valptr_list_node * valptr_list_p, val_descr * val_desc_p,
				 qfile_tuple_value_type_list * type_list, qfile_tuple_record * tuple_record_p)
{
  REGU_VARIABLE_LIST reg_var_p;
  DB_VALUE *vals_buf[QDATA_TUPLE_VALS_STACK], **vals = vals_buf;
  int k, n, error = NO_ERROR;

  if (valptr_list_p->valptr_cnt > QDATA_TUPLE_VALS_STACK)
    {
      vals = (DB_VALUE **) db_private_alloc (thread_p, valptr_list_p->valptr_cnt * sizeof (DB_VALUE *));
      if (vals == NULL)
	{
	  return ER_FAILED;
	}
    }

  /* fetch each value once (qdata_get_dbval_from_constant_regu_variable evaluates the regu variable) */
  n = 0;
  reg_var_p = valptr_list_p->valptrp;
  for (k = 0; k < valptr_list_p->valptr_cnt; k++, reg_var_p = reg_var_p->next)
    {
      if (unlikely (reg_var_p->value.flags & REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  continue;
	}
      vals[n] = qdata_get_dbval_from_constant_regu_variable (thread_p, &reg_var_p->value, val_desc_p);
      if (vals[n] == NULL)
	{
	  error = ER_FAILED;
	  goto end;
	}
      n++;
    }

  error = qdata_copy_values_to_tuple (thread_p, vals, n, type_list, tuple_record_p);

end:
  if (vals != vals_buf)
    {
      db_private_free (thread_p, vals);
    }
  return error;
}

/*
 * qdata_copy_values_to_tuple () - assemble n values into the (growable) private tuple buffer of tuple_record_p
 *   return: NO_ERROR, or ER_code
 */
static int
qdata_copy_values_to_tuple (THREAD_ENTRY * thread_p, DB_VALUE ** vals, int n, qfile_tuple_value_type_list * type_list,
			    qfile_tuple_record * tuple_record_p)
{
  int lens_buf[QDATA_TUPLE_VALS_STACK], *lens = lens_buf;
  int size, error;
  bool has_null;

  if (n > QDATA_TUPLE_VALS_STACK)
    {
      lens = (int *) db_private_alloc (thread_p, n * sizeof (int));
      if (lens == NULL)
	{
	  return ER_FAILED;
	}
    }

  size = qfile_tuple_size_from_values (type_list, vals, lens, n, &has_null);
  if (size < 0)
    {
      error = ER_FAILED;
      goto end;
    }

  if (tuple_record_p->size < size)
    {
      /* grow in page multiples so a stream of BIG tuples does not realloc every row */
      int tpl_size = CEIL_PTVDIV (size, DB_PAGESIZE) * DB_PAGESIZE;

      if (tuple_record_p->size == 0)
	{
	  tuple_record_p->tpl = (char *) db_private_alloc (thread_p, tpl_size);
	}
      else
	{
	  tuple_record_p->tpl = (char *) db_private_realloc (thread_p, tuple_record_p->tpl, tpl_size);
	}
      if (tuple_record_p->tpl == NULL)
	{
	  error = ER_FAILED;
	  goto end;
	}
      tuple_record_p->size = tpl_size;
    }

  error = qfile_tuple_fill_from_values (type_list, vals, lens, n, tuple_record_p->tpl, size, has_null);

end:
  if (lens != lens_buf)
    {
      db_private_free (thread_p, lens);
    }
  return error;
}

int
qdata_copy_val_list_to_tuple (THREAD_ENTRY * thread_p, VAL_LIST * val_list, qfile_tuple_value_type_list * type_list,
			      qfile_tuple_record * tuple_record_p)
{
  QPROC_DB_VALUE_LIST val_list_iterator;
  DB_VALUE *vals_buf[QDATA_TUPLE_VALS_STACK], **vals = vals_buf;
  int n, error;

  if (val_list->val_cnt > QDATA_TUPLE_VALS_STACK)
    {
      vals = (DB_VALUE **) db_private_alloc (thread_p, val_list->val_cnt * sizeof (DB_VALUE *));
      if (vals == NULL)
	{
	  return ER_FAILED;
	}
    }

  for (n = 0, val_list_iterator = val_list->valp; val_list_iterator && n < val_list->val_cnt;
       val_list_iterator = val_list_iterator->next, n++)
    {
      vals[n] = val_list_iterator->val;
    }

  error = qdata_copy_values_to_tuple (thread_p, vals, n, type_list, tuple_record_p);

  if (vals != vals_buf)
    {
      db_private_free (thread_p, vals);
    }
  return error;
}

extern int
qdata_tuple_to_val_list (THREAD_ENTRY * thread_p, qfile_tuple_value_type_list * type_list, qfile_tuple_record * tplrec,
			 VAL_LIST * val_list)
{
  QPROC_DB_VALUE_LIST val_list_iterator;
  int val_list_index;
  int err_code;
  bool is_null;

  /* sequential column reads through the slot cache are O(n) overall */
  for (val_list_iterator = val_list->valp, val_list_index = 0; val_list_iterator
       && val_list_index < val_list->val_cnt; val_list_iterator = val_list_iterator->next, val_list_index++)
    {
      pr_clear_value (val_list_iterator->val);

      err_code =
	qfile_slot_read_column_value (tplrec, val_list_index, type_list->domp[val_list_index], val_list_iterator->val,
				      false /* Don't copy */ , &is_null);
      if (err_code != NO_ERROR)
	{
	  return err_code;
	}
      if (is_null)
	{
	  db_make_null (val_list_iterator->val);
	}
    }
  return NO_ERROR;
}

/*
 * qdata_collect_tuple_values () - collect visible values and optionally measure them against a settled layout.
 *   Specialize the loop so the collect-only path does not test a sizing mode for every column.
 */
template < bool size_values > static QPROC_TPLDESCR_STATUS
qdata_collect_tuple_values (THREAD_ENTRY * thread_p, valptr_list_node * valptr_list_p, val_descr * val_desc_p,
			    qfile_tuple_descriptor * tuple_desc_p, const qfile_tuple_value_type_list * type_list)
{
  REGU_VARIABLE_LIST reg_var_p;
  REGU_VARIABLE *regu_var_p;
  DB_VALUE *value;
  int i, values_size = 0;
  bool has_null = false;

  tuple_desc_p->tpl_size = 0;
  tuple_desc_p->f_cnt = 0;

  if (size_values)
    {
      assert (type_list != NULL && type_list->layout_ready);
      assert (tuple_desc_p->f_len != NULL || type_list->type_cnt == 0);
    }

  reg_var_p = valptr_list_p->valptrp;
  for (i = 0; i < valptr_list_p->valptr_cnt; i++, reg_var_p = reg_var_p->next)
    {
      regu_var_p = &reg_var_p->value;
      if (unlikely (regu_var_p->flags & REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  continue;
	}
      value = qdata_get_dbval_from_constant_regu_variable (thread_p, regu_var_p, val_desc_p);
      tuple_desc_p->f_valp[tuple_desc_p->f_cnt] = value;
      if (value == NULL)
	{
	  return QPROC_TPLDESCR_FAILURE;
	}

      /* SET data-type cannot use tuple descriptor. */
      if (unlikely (pr_is_set_type (DB_VALUE_DOMAIN_TYPE (value))))
	{
	  return QPROC_TPLDESCR_RETRY_SET_TYPE;
	}

      if (size_values)
	{
	  assert (tuple_desc_p->f_cnt < type_list->type_cnt);
	  values_size = qfile_tuple_size_add_value (&type_list->column_layout_array[tuple_desc_p->f_cnt], value,
						    &tuple_desc_p->f_len[tuple_desc_p->f_cnt], values_size, &has_null);
	  if (values_size < 0)
	    {
	      return QPROC_TPLDESCR_FAILURE;
	    }
	}
      tuple_desc_p->f_cnt++;
    }

  if (size_values)
    {
      assert (tuple_desc_p->f_cnt == type_list->type_cnt);
      tuple_desc_p->has_null = has_null;
      tuple_desc_p->tpl_size = qfile_tuple_size_finalize (type_list, values_size, has_null);
      /* Finish collecting before deciding to retry a BIG record. */
      if (tuple_desc_p->tpl_size >= QFILE_MAX_TUPLE_SIZE_IN_PAGE)
	{
	  return QPROC_TPLDESCR_RETRY_BIG_REC;
	}
    }
  return QPROC_TPLDESCR_SUCCESS;
}

/*
 * qdata_generate_tuple_desc_for_valptr_list () - collect and size the destination list's tuple descriptor.
 *   return: QPROC_TPLDESCR_SUCCESS, QPROC_TPLDESCR_RETRY_xxx, or QPROC_TPLDESCR_FAILURE
 *   list_id(in/out): destination with f_valp/f_len already allocated
 *
 * The list opened with the plan's domains (qdata_get_valptr_type_list), so its layout is settled before the first
 * tuple and no row resolves a column: collection and sizing are one pass. The compressed string, if any, is
 * deallocated later, after copying the db_value into the tuple.
 */
QPROC_TPLDESCR_STATUS
qdata_generate_tuple_desc_for_valptr_list (THREAD_ENTRY * thread_p, valptr_list_node * valptr_list_p,
					   val_descr * val_desc_p, qfile_list_id * list_id)
{
  assert (list_id->type_list.layout_ready);
  return qdata_collect_tuple_values < true > (thread_p, valptr_list_p, val_desc_p, &list_id->tpl_descr,
					      &list_id->type_list);
}

/*
 * qdata_set_valptr_list_unbound () -
 *   return: NO_ERROR, or ER_code
 *   valptr_list(in)    : Value pointer list
 *   vd(in)     : Value descriptor
 *
 * Note: Set valptr_list values UNBOUND.
 */
int
qdata_set_valptr_list_unbound (THREAD_ENTRY * thread_p, valptr_list_node * valptr_list_p, val_descr * val_desc_p)
{
  REGU_VARIABLE_LIST reg_var_p;
  DB_VALUE *dbval_p;
  int i;

  reg_var_p = valptr_list_p->valptrp;
  for (i = 0; i < valptr_list_p->valptr_cnt; i++)
    {
      dbval_p = qdata_get_dbval_from_constant_regu_variable (thread_p, &reg_var_p->value, val_desc_p);

      if (dbval_p != NULL)
	{
	  if (!REGU_VARIABLE_IS_FLAGED (&reg_var_p->value, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	    {
	      /* this may be shared with another regu variable that was already evaluated */
	      pr_clear_value (dbval_p);

	      if (db_value_domain_init (dbval_p, DB_VALUE_DOMAIN_TYPE (dbval_p), DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE)
		  != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	}

      reg_var_p = reg_var_p->next;
    }

  return NO_ERROR;
}

/*
 * ARITHMETIC EXPRESSION EVALUATION ROUTINES
 *
 * An addition, subtraction, multiplication or division runs as the ARITH rule names it over its two values' types
 * (domain_arith_rule, DOMAIN_ARITH): the kind names the operator that computes the value and the type is the value's
 * (qdata_arith_dbval). The typed leaves below compute; none decides a type from its operands - the rule the resolver
 * reads before any row is the one the row reads.
 */

static int
qdata_add_short (short s1, short s2, DB_VALUE * result_p)
{
  short result;

  result = s1 + s2;

  if (OR_CHECK_ADD_OVERFLOW (s1, s2, result))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  db_make_short (result_p, result);
  return NO_ERROR;
}

static int
qdata_add_int (int i1, int i2, DB_VALUE * result_p)
{
  int result;

  result = i1 + i2;

  if (OR_CHECK_ADD_OVERFLOW (i1, i2, result))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  db_make_int (result_p, result);
  return NO_ERROR;
}

static int
qdata_add_bigint (DB_BIGINT bi1, DB_BIGINT bi2, DB_VALUE * result_p)
{
  DB_BIGINT result;

  result = bi1 + bi2;

  if (OR_CHECK_ADD_OVERFLOW (bi1, bi2, result))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  db_make_bigint (result_p, result);
  return NO_ERROR;
}

static int
qdata_add_float (float f1, float f2, DB_VALUE * result_p)
{
  float result;

  result = f1 + f2;

  if (OR_CHECK_FLOAT_OVERFLOW (result))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  db_make_float (result_p, result);
  return NO_ERROR;
}

static int
qdata_add_double (double d1, double d2, DB_VALUE * result_p)
{
  double result;

  result = d1 + d2;

  if (OR_CHECK_DOUBLE_OVERFLOW (result))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  db_make_double (result_p, result);
  return NO_ERROR;
}

static double
qdata_coerce_numeric_to_double (DB_VALUE * numeric_val_p)
{
  DB_VALUE dbval_tmp;
  DB_DATA_STATUS data_stat;

  db_value_domain_init (&dbval_tmp, DB_TYPE_DOUBLE, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  (void) numeric_db_value_coerce_from_num (numeric_val_p, &dbval_tmp, &data_stat);

  return db_get_double (&dbval_tmp);
}

static void
qdata_coerce_dbval_to_numeric (DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_DATA_STATUS data_stat;

  db_value_domain_init (result_p, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  (void) numeric_db_value_coerce_to_num (dbval_p, result_p, &data_stat);
}

static int
qdata_add_numeric_to_monetary (DB_VALUE * numeric_val_p, DB_VALUE * monetary_val_p, DB_VALUE * result_p)
{
  double d1, d2, dtmp;

  d1 = qdata_coerce_numeric_to_double (numeric_val_p);
  d2 = (db_get_monetary (monetary_val_p))->amount;

  dtmp = d1 + d2;

  db_make_monetary (result_p, (db_get_monetary (monetary_val_p))->type, dtmp);

  return NO_ERROR;
}

static int
qdata_add_monetary (double d1, double d2, DB_CURRENCY type, DB_VALUE * result_p)
{
  double result;

  result = d1 + d2;

  if (OR_CHECK_DOUBLE_OVERFLOW (result))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  db_make_monetary (result_p, type, result);
  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
qdata_add_int_to_time (DB_VALUE * time_val_p, unsigned int add_time, DB_VALUE * result_p)
{
  unsigned int result, utime;
  DB_TIME *time;
  int hour, minute, second;

  time = db_get_time (time_val_p);
  utime = (unsigned int) *time % SECONDS_OF_ONE_DAY;

  result = (utime + add_time) % SECONDS_OF_ONE_DAY;

  db_time_decode (&result, &hour, &minute, &second);

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      db_make_time (result_p, hour, minute, second);
    }
  else
    {
      DB_TYPE type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_INTEGER:
	  db_make_int (result_p, (hour * 100 + minute) * 100 + second);
	  break;

	case DB_TYPE_SHORT:
	  db_make_short (result_p, (hour * 100 + minute) * 100 + second);
	  break;

	default:
	  db_make_time (result_p, hour, minute, second);
	  break;
	}
    }

  return NO_ERROR;
}
#endif

static int
qdata_add_bigint_to_time (DB_VALUE * time_val_p, DB_BIGINT add_time, DB_VALUE * result_p)
{
  DB_TIME utime, result;
  int hour, minute, second;
  int error = NO_ERROR;

  utime = *(db_get_time (time_val_p)) % SECONDS_OF_ONE_DAY;
  add_time = add_time % SECONDS_OF_ONE_DAY;
  if (add_time < 0)
    {
      return qdata_subtract_time (utime, (DB_TIME) (-add_time), result_p);
    }

  result = (utime + add_time) % SECONDS_OF_ONE_DAY;
  db_time_decode (&result, &hour, &minute, &second);

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      error = db_make_time (result_p, hour, minute, second);
    }
  else
    {
      DB_TYPE type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_BIGINT:
	  error = db_make_bigint (result_p, (hour * 100 + minute) * 100 + second);
	  break;

	case DB_TYPE_INTEGER:
	  error = db_make_int (result_p, (hour * 100 + minute) * 100 + second);
	  break;

	default:
	  error = db_make_time (result_p, hour, minute, second);
	  break;
	}
    }

  return error;
}

static int
qdata_add_short_to_utime_asymmetry (DB_VALUE * utime_val_p, short s, unsigned int *utime, DB_VALUE * result_p,
				    TP_DOMAIN * domain_p)
{
  DB_VALUE tmp;

  if (s == DB_INT16_MIN)	/* check for asymmetry */
    {
      if (*utime <= 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
	  return ER_QPROC_TIME_UNDERFLOW;
	}

      (*utime)--;
      s++;
    }

  db_make_short (&tmp, -(s));
  return (qdata_subtract_dbval (utime_val_p, &tmp, result_p, domain_p));
}

static int
qdata_add_int_to_utime_asymmetry (DB_VALUE * utime_val_p, int i, unsigned int *utime, DB_VALUE * result_p,
				  TP_DOMAIN * domain_p)
{
  DB_VALUE tmp;

  if (i == DB_INT32_MIN)	/* check for asymmetry */
    {
      if (*utime <= 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
	  return ER_QPROC_TIME_UNDERFLOW;
	}

      (*utime)--;
      i++;
    }

  db_make_int (&tmp, -i);
  return (qdata_subtract_dbval (utime_val_p, &tmp, result_p, domain_p));
}

static int
qdata_add_bigint_to_utime_asymmetry (DB_VALUE * utime_val_p, DB_BIGINT bi, unsigned int *utime, DB_VALUE * result_p,
				     TP_DOMAIN * domain_p)
{
  DB_VALUE tmp;

  if (bi == DB_BIGINT_MIN)	/* check for asymmetry */
    {
      if (*utime <= 1)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
	  return ER_QPROC_TIME_UNDERFLOW;
	}

      (*utime)--;
      bi++;
    }

  db_make_bigint (&tmp, -bi);
  return (qdata_subtract_dbval (utime_val_p, &tmp, result_p, domain_p));
}

static int
qdata_add_short_to_utime (DB_VALUE * utime_val_p, short s, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_UTIME *utime;
  DB_UTIME utmp, u1, u2;
  DB_DATE date;
  DB_TIME time;
  DB_TYPE type;
  DB_BIGINT bigint = 0;
  int d, m, y, h, mi, sec;

  utime = db_get_timestamp (utime_val_p);

  if (s < 0)
    {
      return qdata_add_short_to_utime_asymmetry (utime_val_p, s, utime, result_p, domain_p);
    }

  u1 = s;
  u2 = *utime;
  utmp = u1 + u2;

  if (OR_CHECK_UNS_ADD_OVERFLOW (u1, u2, utmp) || INT_MAX < utmp)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      db_make_timestamp (result_p, utmp);
    }
  else
    {
      type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_BIGINT:
	  (void) db_timestamp_decode_ses (&utmp, &date, &time);
	  db_date_decode (&date, &m, &d, &y);
	  db_time_decode (&time, &h, &mi, &sec);
	  bigint = (y * 100 + m) * 100 + d;
	  bigint = ((bigint * 100 + h) * 100 + mi) * 100 + sec;
	  db_make_bigint (result_p, bigint);
	  break;

	default:
	  db_make_timestamp (result_p, utmp);
	  break;
	}
    }

  return NO_ERROR;
}

static int
qdata_add_int_to_utime (DB_VALUE * utime_val_p, int i, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_UTIME *utime;
  DB_UTIME utmp, u1, u2;
  DB_DATE date;
  DB_TIME time;
  DB_TYPE type;
  DB_BIGINT bigint;
  int d, m, y, h, mi, s;

  utime = db_get_timestamp (utime_val_p);

  if (i < 0)
    {
      return qdata_add_int_to_utime_asymmetry (utime_val_p, i, utime, result_p, domain_p);
    }

  u1 = i;
  u2 = *utime;
  utmp = u1 + u2;

  if (OR_CHECK_UNS_ADD_OVERFLOW (u1, u2, utmp) || INT_MAX < utmp)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      db_make_timestamp (result_p, utmp);
    }
  else
    {
      type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_BIGINT:
	  (void) db_timestamp_decode_ses (&utmp, &date, &time);
	  db_date_decode (&date, &m, &d, &y);
	  db_time_decode (&time, &h, &mi, &s);
	  bigint = (y * 100 + m) * 100 + d;
	  bigint = ((bigint * 100 + h) * 100 + mi) * 100 + s;
	  db_make_bigint (result_p, bigint);
	  break;

	default:
	  db_make_timestamp (result_p, utmp);
	  break;
	}
    }

  return NO_ERROR;
}

static int
qdata_add_bigint_to_utime (DB_VALUE * utime_val_p, DB_BIGINT bi, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_UTIME *utime;
  DB_BIGINT utmp, u1, u2;
  DB_DATE date;
  DB_TIME time;
  DB_TYPE type;
  DB_BIGINT bigint;
  int d, m, y, h, mi, s;

  utime = db_get_timestamp (utime_val_p);

  if (bi < 0)
    {
      return qdata_add_bigint_to_utime_asymmetry (utime_val_p, bi, utime, result_p, domain_p);
    }

  u1 = bi;
  u2 = *utime;
  utmp = u1 + u2;

  if (OR_CHECK_UNS_ADD_OVERFLOW (u1, u2, utmp) || INT_MAX < utmp)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      db_make_timestamp (result_p, (unsigned int) utmp);	/* truncate to 4bytes time_t */
    }
  else
    {
      type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_BIGINT:
	  {
	    DB_TIMESTAMP timestamp = (DB_TIMESTAMP) utmp;
	    (void) db_timestamp_decode_ses (&timestamp, &date, &time);
	    db_date_decode (&date, &m, &d, &y);
	    db_time_decode (&time, &h, &mi, &s);
	    bigint = (y * 100 + m) * 100 + d;
	    bigint = ((bigint * 100 + h) * 100 + mi) * 100 + s;
	    db_make_bigint (result_p, bigint);
	  }
	  break;

	default:
	  db_make_timestamp (result_p, (unsigned int) utmp);
	  break;
	}
    }

  return NO_ERROR;
}

static int
qdata_add_short_to_timestamptz (DB_VALUE * ts_tz_val_p, short s, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int err = NO_ERROR;
  DB_TIMESTAMPTZ *ts_tz_p;
  DB_TIMESTAMPTZ ts_tz_res, ts_tz_fixed;
  DB_UTIME utime;
  DB_UTIME utmp, u1, u2;
  DB_DATE date;
  DB_TIME time;
  DB_TYPE type;
  DB_BIGINT bigint = 0;
  int d, m, y, h, mi, sec;
  DB_VALUE tmp_utime_val, tmp_utime_val_res;

  ts_tz_p = db_get_timestamptz (ts_tz_val_p);
  utime = ts_tz_p->timestamp;

  if (s < 0)
    {
      db_make_timestamp (&tmp_utime_val, utime);
      err =
	qdata_add_short_to_utime_asymmetry (&tmp_utime_val, s, &utime, &tmp_utime_val_res,
					    tp_domain_resolve_default (DB_TYPE_TIMESTAMP));
      if (err != NO_ERROR)
	{
	  goto exit;
	}

      assert (DB_VALUE_TYPE (&tmp_utime_val_res) == DB_TYPE_TIMESTAMP);
      utmp = *db_get_timestamp (&tmp_utime_val_res);

      goto return_timestamp_tz;
    }

  u1 = s;
  u2 = utime;
  utmp = u1 + u2;

  if (OR_CHECK_UNS_ADD_OVERFLOW (u1, u2, utmp) || INT_MAX < utmp)
    {
      err = ER_QPROC_OVERFLOW_ADDITION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      goto exit;
    }

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      goto return_timestamp_tz;
    }
  else
    {
      type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_BIGINT:
	  ts_tz_res.timestamp = utmp;
	  ts_tz_res.tz_id = ts_tz_p->tz_id;
	  err = tz_timestamptz_fix_zone (&ts_tz_res, &ts_tz_fixed);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }
	  err = db_timestamp_decode_w_tz_id (&ts_tz_fixed.timestamp, &ts_tz_fixed.tz_id, &date, &time);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }
	  db_date_decode (&date, &m, &d, &y);
	  db_time_decode (&time, &h, &mi, &sec);
	  bigint = (y * 100 + m) * 100 + d;
	  bigint = ((bigint * 100 + h) * 100 + mi) * 100 + sec;
	  db_make_bigint (result_p, bigint);
	  break;

	default:
	  break;
	}
    }

return_timestamp_tz:
  ts_tz_res.timestamp = utmp;
  ts_tz_res.tz_id = ts_tz_p->tz_id;

  err = tz_timestamptz_fix_zone (&ts_tz_res, &ts_tz_fixed);
  if (err != NO_ERROR)
    {
      return err;
    }
  db_make_timestamptz (result_p, &ts_tz_fixed);

exit:
  return err;
}

static int
qdata_add_int_to_timestamptz (DB_VALUE * ts_tz_val_p, int i, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int err = NO_ERROR;
  DB_TIMESTAMPTZ *ts_tz_p;
  DB_TIMESTAMPTZ ts_tz_res, ts_tz_fixed;
  DB_UTIME utime;
  DB_UTIME utmp, u1, u2;
  DB_DATE date;
  DB_TIME time;
  DB_TYPE type;
  DB_BIGINT bigint = 0;
  int d, m, y, h, mi, sec;
  DB_VALUE tmp_utime_val, tmp_utime_val_res;

  ts_tz_p = db_get_timestamptz (ts_tz_val_p);
  utime = ts_tz_p->timestamp;

  if (i < 0)
    {
      db_make_timestamp (&tmp_utime_val, utime);
      err =
	qdata_add_int_to_utime_asymmetry (&tmp_utime_val, i, &utime, &tmp_utime_val_res,
					  tp_domain_resolve_default (DB_TYPE_TIMESTAMP));
      if (err != NO_ERROR)
	{
	  goto exit;
	}

      assert (DB_VALUE_TYPE (&tmp_utime_val_res) == DB_TYPE_TIMESTAMP);
      utmp = *db_get_timestamp (&tmp_utime_val_res);

      goto return_timestamp_tz;
    }

  u1 = i;
  u2 = utime;
  utmp = u1 + u2;

  if (OR_CHECK_UNS_ADD_OVERFLOW (u1, u2, utmp) || INT_MAX < utmp)
    {
      err = ER_QPROC_OVERFLOW_ADDITION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      goto exit;
    }

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      goto return_timestamp_tz;
    }
  else
    {
      type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_BIGINT:
	  ts_tz_res.timestamp = utmp;
	  ts_tz_res.tz_id = ts_tz_p->tz_id;
	  err = tz_timestamptz_fix_zone (&ts_tz_res, &ts_tz_fixed);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }
	  err = db_timestamp_decode_w_tz_id (&ts_tz_fixed.timestamp, &ts_tz_fixed.tz_id, &date, &time);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }
	  db_date_decode (&date, &m, &d, &y);
	  db_time_decode (&time, &h, &mi, &sec);
	  bigint = (y * 100 + m) * 100 + d;
	  bigint = ((bigint * 100 + h) * 100 + mi) * 100 + sec;
	  db_make_bigint (result_p, bigint);
	  break;

	default:
	  break;
	}
    }

return_timestamp_tz:
  ts_tz_res.timestamp = utmp;
  ts_tz_res.tz_id = ts_tz_p->tz_id;

  err = tz_timestamptz_fix_zone (&ts_tz_res, &ts_tz_fixed);
  if (err != NO_ERROR)
    {
      return err;
    }
  db_make_timestamptz (result_p, &ts_tz_fixed);

exit:
  return err;
}

static int
qdata_add_bigint_to_timestamptz (DB_VALUE * ts_tz_val_p, DB_BIGINT bi, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int err = NO_ERROR;
  DB_TIMESTAMPTZ *ts_tz_p;
  DB_TIMESTAMPTZ ts_tz_res, ts_tz_fixed;
  DB_UTIME utime;
  DB_DATE date;
  DB_TIME time;
  DB_TYPE type;
  DB_BIGINT u1, u2, utmp, bigint = 0;
  int d, m, y, h, mi, sec;
  DB_VALUE tmp_utime_val, tmp_utime_val_res;

  ts_tz_p = db_get_timestamptz (ts_tz_val_p);
  utime = ts_tz_p->timestamp;

  if (bi < 0)
    {
      db_make_timestamp (&tmp_utime_val, utime);
      err =
	qdata_add_bigint_to_utime_asymmetry (&tmp_utime_val, bi, &utime, &tmp_utime_val_res,
					     tp_domain_resolve_default (DB_TYPE_TIMESTAMP));
      if (err != NO_ERROR)
	{
	  goto exit;
	}

      assert (DB_VALUE_TYPE (&tmp_utime_val_res) == DB_TYPE_TIMESTAMP);
      utmp = *db_get_timestamp (&tmp_utime_val_res);

      goto return_timestamp_tz;
    }

  u1 = bi;
  u2 = utime;
  utmp = u1 + u2;

  if (OR_CHECK_UNS_ADD_OVERFLOW (u1, u2, utmp) || INT_MAX < utmp)
    {
      err = ER_QPROC_OVERFLOW_ADDITION;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 0);
      goto exit;
    }

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      goto return_timestamp_tz;
    }
  else
    {
      type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_BIGINT:
	  utime = (DB_UTIME) utmp;
	  ts_tz_res.timestamp = utime;
	  ts_tz_res.tz_id = ts_tz_p->tz_id;
	  err = tz_timestamptz_fix_zone (&ts_tz_res, &ts_tz_fixed);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }
	  err = db_timestamp_decode_w_tz_id (&ts_tz_fixed.timestamp, &ts_tz_fixed.tz_id, &date, &time);
	  if (err != NO_ERROR)
	    {
	      goto exit;
	    }
	  db_date_decode (&date, &m, &d, &y);
	  db_time_decode (&time, &h, &mi, &sec);
	  bigint = (y * 100 + m) * 100 + d;
	  bigint = ((bigint * 100 + h) * 100 + mi) * 100 + sec;
	  db_make_bigint (result_p, bigint);
	  break;

	default:
	  break;
	}
    }

return_timestamp_tz:
  utime = (DB_UTIME) utmp;
  ts_tz_res.timestamp = utime;
  ts_tz_res.tz_id = ts_tz_p->tz_id;

  err = tz_timestamptz_fix_zone (&ts_tz_res, &ts_tz_fixed);
  if (err != NO_ERROR)
    {
      return err;
    }
  db_make_timestamptz (result_p, &ts_tz_fixed);

exit:
  return err;
}

static int
qdata_add_short_to_datetime (DB_VALUE * datetime_val_p, short s, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_DATETIME *datetime;
  DB_DATETIME tmp;
  int error = NO_ERROR;

  datetime = db_get_datetime (datetime_val_p);

  error = db_add_int_to_datetime (datetime, s, &tmp);
  if (error == NO_ERROR)
    {
      db_make_datetime (result_p, &tmp);
    }
  return error;
}

static int
qdata_add_int_to_datetime (DB_VALUE * datetime_val_p, int i, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_DATETIME *datetime;
  DB_DATETIME tmp;
  int error = NO_ERROR;

  datetime = db_get_datetime (datetime_val_p);

  error = db_add_int_to_datetime (datetime, i, &tmp);
  if (error == NO_ERROR)
    {
      db_make_datetime (result_p, &tmp);
    }
  return error;
}

static int
qdata_add_bigint_to_datetime (DB_VALUE * datetime_val_p, DB_BIGINT bi, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_DATETIME *datetime;
  DB_DATETIME tmp;
  int error = NO_ERROR;

  datetime = db_get_datetime (datetime_val_p);

  error = db_add_int_to_datetime (datetime, bi, &tmp);
  if (error == NO_ERROR)
    {
      db_make_datetime (result_p, &tmp);
    }
  return error;
}

static int
qdata_add_short_to_date (DB_VALUE * date_val_p, short s, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_DATE *date;
  unsigned int utmp, u1, u2;
  int day, month, year;

  date = db_get_date (date_val_p);
  if (s < 0)
    {
      return qdata_add_short_to_utime_asymmetry (date_val_p, s, date, result_p, domain_p);
    }

  u1 = (unsigned int) s;
  u2 = (unsigned int) *date;
  utmp = u1 + u2;

  if (OR_CHECK_UNS_ADD_OVERFLOW (u1, u2, utmp) || utmp > DB_DATE_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  db_date_decode (&utmp, &month, &day, &year);

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      db_make_date (result_p, month, day, year);
    }
  else
    {
      DB_TYPE type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_SHORT:
	  db_make_short (result_p, (year * 100 + month) * 100 + day);
	  break;

	default:
	  db_make_date (result_p, month, day, year);
	  break;
	}
    }

  return NO_ERROR;
}

static int
qdata_add_int_to_date (DB_VALUE * date_val_p, int i, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_DATE *date;
  unsigned int utmp, u1, u2;
  int day, month, year;

  date = db_get_date (date_val_p);

  if (i < 0)
    {
      return qdata_add_int_to_utime_asymmetry (date_val_p, i, date, result_p, domain_p);
    }

  u1 = (unsigned int) i;
  u2 = (unsigned int) *date;
  utmp = u1 + u2;

  if (OR_CHECK_UNS_ADD_OVERFLOW (u1, u2, utmp) || utmp > DB_DATE_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  db_date_decode (&utmp, &month, &day, &year);
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      db_make_date (result_p, month, day, year);
    }
  else
    {
      DB_TYPE type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_INTEGER:
	  db_make_int (result_p, (year * 100 + month) * 100 + day);
	  break;

	default:
	  db_make_date (result_p, month, day, year);
	  break;
	}
    }

  return NO_ERROR;
}

static int
qdata_add_bigint_to_date (DB_VALUE * date_val_p, DB_BIGINT bi, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_DATE *date;
  DB_BIGINT utmp, u1, u2;
  DB_DATE tmp_date;
  int day, month, year;

  date = db_get_date (date_val_p);

  if (bi < 0)
    {
      return qdata_add_bigint_to_utime_asymmetry (date_val_p, bi, date, result_p, domain_p);
    }

  u1 = bi;
  u2 = *date;
  utmp = u1 + u2;

  if (OR_CHECK_UNS_ADD_OVERFLOW (u1, u2, utmp) || utmp > DB_DATE_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  tmp_date = (DB_DATE) utmp;
  db_date_decode (&tmp_date, &month, &day, &year);
  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      db_make_date (result_p, month, day, year);
    }
  else
    {
      DB_TYPE type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_BIGINT:
	  db_make_bigint (result_p, (year * 100 + month) * 100 + day);
	  break;

	default:
	  db_make_date (result_p, month, day, year);
	  break;
	}
    }

  return NO_ERROR;
}

static int
qdata_add_chars_to_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p)
{
  DB_DATA_STATUS data_stat;

  if ((db_string_concatenate (dbval1_p, dbval2_p, result_p, &data_stat) != NO_ERROR) || (data_stat != DATA_STATUS_OK))
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
qdata_add_sequence_to_dbval (DB_VALUE * seq_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_SET *set_tmp;
  DB_SEQ *seq_tmp, *seq_tmp1;
  DB_VALUE dbval_tmp;
  int i, card, card1;
#if !defined(NDEBUG)
  DB_TYPE type1, type2;
#endif

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (seq_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  assert (TP_IS_SET_TYPE (type1));
  assert (TP_IS_SET_TYPE (type2));
#endif

  if (domain_p == NULL)
    {
      return ER_FAILED;
    }

  db_make_null (&dbval_tmp);

  if (TP_DOMAIN_TYPE (domain_p) == DB_TYPE_SEQUENCE)
    {
      if (tp_value_coerce (seq_val_p, result_p, domain_p) != DOMAIN_COMPATIBLE)
	{
	  return ER_FAILED;
	}

      seq_tmp = db_get_set (dbval_p);
      card = db_seq_size (seq_tmp);
      seq_tmp1 = db_get_set (result_p);
      card1 = db_seq_size (seq_tmp1);

      for (i = 0; i < card; i++)
	{
	  if (db_seq_get (seq_tmp, i, &dbval_tmp) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  if (db_seq_put (seq_tmp1, card1 + i, &dbval_tmp) != NO_ERROR)
	    {
	      pr_clear_value (&dbval_tmp);
	      return ER_FAILED;
	    }

	  pr_clear_value (&dbval_tmp);
	}
    }
  else
    {
      /* set or multiset */
      if (set_union (db_get_set (seq_val_p), db_get_set (dbval_p), &set_tmp, domain_p) < 0)
	{
	  return ER_FAILED;
	}

      pr_clear_value (result_p);
      set_make_collection (result_p, set_tmp);
    }

  return NO_ERROR;
}

static int
qdata_add_time_to_dbval (DB_VALUE * time_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_bigint_to_time (time_val_p, (DB_BIGINT) db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_add_bigint_to_time (time_val_p, (DB_BIGINT) db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_add_bigint_to_time (time_val_p, db_get_bigint (dbval_p), result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_add_utime_to_dbval (DB_VALUE * utime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_TYPE type;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_short_to_utime (utime_val_p, db_get_short (dbval_p), result_p, domain_p);

    case DB_TYPE_INTEGER:
      return qdata_add_int_to_utime (utime_val_p, db_get_int (dbval_p), result_p, domain_p);

    case DB_TYPE_BIGINT:
      return qdata_add_bigint_to_utime (utime_val_p, db_get_bigint (dbval_p), result_p, domain_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_add_timestamptz_to_dbval (DB_VALUE * ts_tz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type;
  TP_DOMAIN *domain_p;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  domain_p = tp_domain_resolve_default (DB_TYPE_TIMESTAMPTZ);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_short_to_timestamptz (ts_tz_val_p, db_get_short (dbval_p), result_p, domain_p);

    case DB_TYPE_INTEGER:
      return qdata_add_int_to_timestamptz (ts_tz_val_p, db_get_int (dbval_p), result_p, domain_p);

    case DB_TYPE_BIGINT:
      return qdata_add_bigint_to_timestamptz (ts_tz_val_p, db_get_bigint (dbval_p), result_p, domain_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_add_datetime_to_dbval (DB_VALUE * datetime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_TYPE type;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_short_to_datetime (datetime_val_p, db_get_short (dbval_p), result_p, domain_p);

    case DB_TYPE_INTEGER:
      return qdata_add_int_to_datetime (datetime_val_p, db_get_int (dbval_p), result_p, domain_p);

    case DB_TYPE_BIGINT:
      return qdata_add_bigint_to_datetime (datetime_val_p, db_get_bigint (dbval_p), result_p, domain_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_add_datetimetz_to_dbval (DB_VALUE * datetimetz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  int error = NO_ERROR;
  DB_VALUE dt_val, dt_val_res;
  DB_DATETIMETZ *dt_tz_p = db_get_datetimetz (datetimetz_val_p);
  DB_DATETIMETZ dt_tz_res, dt_tz_fixed;

  db_make_datetime (&dt_val, &dt_tz_p->datetime);
  error = qdata_add_datetime_to_dbval (&dt_val, dbval_p, &dt_val_res, tp_domain_resolve_default (DB_TYPE_DATETIME));
  if (error != NO_ERROR)
    {
      return error;
    }

  dt_tz_res.datetime = *db_get_datetime (&dt_val_res);
  dt_tz_res.tz_id = dt_tz_p->tz_id;

  error = tz_datetimetz_fix_zone (&dt_tz_res, &dt_tz_fixed);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_datetimetz (result_p, &dt_tz_fixed);
  return NO_ERROR;
}

static int
qdata_add_date_to_dbval (DB_VALUE * date_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_TYPE type;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_short_to_date (date_val_p, db_get_short (dbval_p), result_p, domain_p);

    case DB_TYPE_INTEGER:
      return qdata_add_int_to_date (date_val_p, db_get_int (dbval_p), result_p, domain_p);

    case DB_TYPE_BIGINT:
      return qdata_add_bigint_to_date (date_val_p, db_get_bigint (dbval_p), result_p, domain_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_coerce_result_to_domain (DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int error = NO_ERROR;
  TP_DOMAIN_STATUS dom_status;

  if (domain_p != NULL)
    {
      dom_status = tp_value_coerce (result_p, result_p, domain_p);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, result_p, domain_p);
	  assert_release (error != NO_ERROR);
	}
    }

  return error;
}

static int
qdata_cast_to_domain (DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int error = NO_ERROR;
  TP_DOMAIN_STATUS dom_status;

  if (domain_p != NULL)
    {
      dom_status = tp_value_cast (dbval_p, result_p, domain_p, false);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval_p, domain_p);
	  assert_release (error != NO_ERROR);
	}
    }

  return error;
}

/* The error of an operand coercion that fails, as tp_value_auto_cast sets it: it names the value the cast took - for an
 * ENUM added to a string, its name, which is cast to VARCHAR first */
int
qdata_operand_coercion_error (TP_DOMAIN_STATUS status, const DB_VALUE * value, const TP_DOMAIN * target)
{
  if (DB_VALUE_DOMAIN_TYPE (value) != DB_TYPE_ENUMERATION)
    {
      return tp_domain_status_er_set (status, ARG_FILE_LINE, value, target);
    }
  DB_VALUE name;
  db_make_null (&name);
  (void) tp_value_cast (value, &name, tp_domain_resolve_default (DB_TYPE_VARCHAR), false);
  const int error = tp_domain_status_er_set (status, ARG_FILE_LINE, &name, target);
  pr_clear_value (&name);
  return error;
}

#if !defined (NDEBUG)
/* Whether two types are one for an operand coercion: a character, bit or collection type stands for its type family */
static bool
qdata_operand_coercion_type_holds (DB_TYPE value, DB_TYPE resolved)
{
  return value == resolved || (TP_IS_CHAR_TYPE (value) && TP_IS_CHAR_TYPE (resolved))
    || (TP_IS_BIT_TYPE (value) && TP_IS_BIT_TYPE (resolved)) || (TP_IS_SET_TYPE (value) && TP_IS_SET_TYPE (resolved));
}

/*
 * qdata_assert_operand_coercion_resolved () - debug cross-check: the operand coercion a caller resolved for two values
 *   that are not NULL is the resolver's type rules over the values' own types: the same target types, and the
 *   converter of the value's own type, CHAR and VARCHAR standing for each other (their converters read any string)
 */
static void
qdata_assert_operand_coercion_resolved (OPERATOR_TYPE opcode, const TP_VALUE_CONVERTER * conv,
					const TP_DOMAIN * const *operand_domain, const DB_VALUE * dbval1_p,
					const DB_VALUE * dbval2_p)
{
  const DB_VALUE *values[2] = { dbval1_p, dbval2_p };
  const DOMAIN_OPERAND operands[2] = {
    {NULL, DB_VALUE_DOMAIN_TYPE (dbval1_p), -1, false}, {NULL, DB_VALUE_DOMAIN_TYPE (dbval2_p), -1, false}
  };
  DOMAIN_OPERAND_COERCION expected;
  domain_resolve_operand_coercion (opcode, operands, &expected);
  for (int i = 0; i < 2; i++)
    {
      if (conv[i] == NULL && expected.conv[i] == NULL)
	{
	  /* neither converts the value: its type is the operator's to take */
	  continue;
	}
      const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (values[i]);
      const TP_DOMAIN *resolved = operand_domain[i];
      bool same = resolved != NULL
	&& qdata_operand_coercion_type_holds (TP_DOMAIN_TYPE (expected.operand_domain[i]), TP_DOMAIN_TYPE (resolved));
      if (same && conv[i] != expected.conv[i])
	{
	  const DB_TYPE sibling =
	    type == DB_TYPE_CHAR ? DB_TYPE_VARCHAR : type == DB_TYPE_VARCHAR ? DB_TYPE_CHAR : type;
	  same = sibling != type && conv[i] != NULL && expected.conv[i] != NULL
	    && conv[i] == tp_value_find_converter (sibling, resolved, DOMAIN_CONVERT_ASSIGN);
	}
      if (!same)
	{
	  fprintf (stderr, "planned pre-cast: opcode=%d operand=%d value=%d/%d planned=%d expected=%d\n",
		   (int) opcode, i, (int) DB_VALUE_DOMAIN_TYPE (values[0]), (int) DB_VALUE_DOMAIN_TYPE (values[1]),
		   resolved != NULL ? (int) TP_DOMAIN_TYPE (resolved) : -1,
		   expected.operand_domain[i] != NULL ? (int) TP_DOMAIN_TYPE (expected.operand_domain[i]) : -1);
	}
      assert (same);
    }
}

#endif

/*
 * qdata_coerce_arith_operands () - an addition, subtraction, multiplication or division over its operands' operand
 *   coercion, resolved before any row, then the typed operator, which casts nothing
 *   return: NO_ERROR or ER_code
 *   opcode(in): T_ADD, T_SUB, T_MUL or T_DIV
 *   conv(in), operand_domain(in): conv[0..1] and operand_domain[0..1] of the operand coercion - a node's
 *	       RESOLVED_DOMAIN, a SUM's or AVG's DOMAIN_OPERAND_COERCION (domain_resolve_operand_coercion); conv NULL
 *	       converts nothing
 *   temporaries(in): [2] an operand its scope converted once already: the operator takes it in place
 *	       of the conversion; NULL none
 *
 * Over two values that are not NULL, each operand the plan converts gets a value of its own, in this order - the
 * second operand first but for a subtraction - and a conversion that fails has tp_value_auto_cast's outcome: NULL
 * under return_null_on_function_errors, the error otherwise. A NULL operand converts nothing: an operator answers a
 * NULL operand before any operand coercion.
 */
int
qdata_coerce_arith_operands (OPERATOR_TYPE opcode, const TP_VALUE_CONVERTER * conv,
			     const TP_DOMAIN * const *operand_domain, DB_VALUE * dbval1_p, DB_VALUE * dbval2_p,
			     DB_VALUE * result_p, TP_DOMAIN * domain_p, const DB_VALUE * const *temporaries)
{
  assert (opcode == T_ADD || opcode == T_SUB || opcode == T_MUL || opcode == T_DIV);
  if (conv == NULL || dbval1_p == NULL || dbval2_p == NULL || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return qdata_arith_dbval (opcode, dbval1_p, dbval2_p, result_p, domain_p);
    }
#if !defined (NDEBUG)
  qdata_assert_operand_coercion_resolved (opcode, conv, operand_domain, dbval1_p, dbval2_p);
#endif
  DB_VALUE *operand[2] = { dbval1_p, dbval2_p };
  DB_VALUE converted[2];
  int used = 0;
  int error = NO_ERROR;
  for (int k = 0; k < 2 && error == NO_ERROR; k++)
    {
      const int i = opcode == T_SUB ? k : 1 - k;
      if (conv[i] == NULL)
	{
	  continue;
	}
      if (temporaries != NULL && temporaries[i] != NULL)
	{
	  /* a copy that frees nothing: the scope's value stays its owner's */
	  converted[i] = *temporaries[i];
	  converted[i].need_clear = false;
	  operand[i] = &converted[i];
	  continue;
	}
      used |= 1 << i;
      const TP_DOMAIN_STATUS status = tp_value_convert (conv[i], operand_domain[i], operand[i], &converted[i]);
      if (status != DOMAIN_COMPATIBLE)
	{
	  if (!prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	    {
	      error = qdata_operand_coercion_error (status, operand[i], operand_domain[i]);
	      break;
	    }
	  pr_clear_value (&converted[i]);
	  db_make_null (&converted[i]);
	  er_clear ();
	}
      operand[i] = &converted[i];
    }
  if (error == NO_ERROR)
    {
      error = qdata_arith_dbval (opcode, operand[0], operand[1], result_p, domain_p);
    }
  for (int i = 0; i < 2; i++)
    {
      if (used & (1 << i))
	{
	  pr_clear_value (&converted[i]);
	}
    }
  return error;
}

/*
 * qdata_add_dbval () - the addition of two values: qdata_arith_dbval over the ARITH rule resolved from their types
 *   return: NO_ERROR, or ER_code
 */
int
qdata_add_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  return qdata_arith_dbval (T_ADD, dbval1_p, dbval2_p, result_p, domain_p);
}

/*
 * qdata_add_datetime_value () - a date or time plus a number (DOMAIN_ARITH_DATE): the date and time additions by the
 *   date's type, a number first operand swapped behind the date
 *   return: NO_ERROR, or ER_code
 */
static int
qdata_add_datetime_value (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int error = NO_ERROR;

  if (TP_IS_NUMERIC_TYPE (DB_VALUE_DOMAIN_TYPE (dbval1_p)))
    {
      DB_VALUE *temp = dbval1_p;

      dbval1_p = dbval2_p;
      dbval2_p = temp;
    }

  switch (DB_VALUE_DOMAIN_TYPE (dbval1_p))
    {
    case DB_TYPE_TIME:
      error = qdata_add_time_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_TIMESTAMP:
      error = qdata_add_utime_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_TIMESTAMPLTZ:
      {
	DB_TIMESTAMPTZ ts_tz, *ts_tz_p;
	DB_VALUE ts_tz_val, tmp_val_res;

	ts_tz.timestamp = *db_get_timestamp (dbval1_p);

	error = tz_create_session_tzid_for_timestamp (&ts_tz.timestamp, &ts_tz.tz_id);
	if (error != NO_ERROR)
	  {
	    break;
	  }

	db_make_timestamptz (&ts_tz_val, &ts_tz);

	error = qdata_add_timestamptz_to_dbval (&ts_tz_val, dbval2_p, &tmp_val_res);
	if (error != NO_ERROR)
	  {
	    break;
	  }
	if (DB_VALUE_TYPE (&tmp_val_res) == DB_TYPE_TIMESTAMPTZ)
	  {
	    ts_tz_p = db_get_timestamptz (&tmp_val_res);
	    db_make_timestampltz (result_p, ts_tz_p->timestamp);
	  }
	else
	  {
	    assert (DB_VALUE_TYPE (&tmp_val_res) == DB_TYPE_BIGINT);
	    pr_clone_value (&tmp_val_res, result_p);
	  }
	break;
      }

    case DB_TYPE_TIMESTAMPTZ:
      error = qdata_add_timestamptz_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      /* we are adding only numbers, safe to handle DATETIMELTZ as DATETIME */
      error = qdata_add_datetime_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      if (error == NO_ERROR && DB_VALUE_DOMAIN_TYPE (dbval1_p) == DB_TYPE_DATETIMELTZ)
	{
	  db_make_datetimeltz (result_p, db_get_datetime (result_p));
	}
      break;

    case DB_TYPE_DATETIMETZ:
      error = qdata_add_datetimetz_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_DATE:
      error = qdata_add_date_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    default:
      /* the rule names a date or time operand */
      assert (false);
      break;
    }

  return error;
}

/*
 * qdata_sum_acc_start () - open the accumulator on the first value of a group
 *   return: NO_ERROR, or ER_FAILED on a type the accumulator does not take
 *   acc(in/out) : accumulator; becomes active in the value's mode
 *   dbv(in)     : first NUMERIC/SHORT/INTEGER/BIGINT/DOUBLE/FLOAT value; not NULL-valued
 */
static int
qdata_sum_acc_start (SUM_ACC * acc, const DB_VALUE * dbv)
{
  DB_TYPE vtype = DB_VALUE_DOMAIN_TYPE (dbv);

  switch (vtype)
    {
    case DB_TYPE_NUMERIC:
      numeric_sum_acc_load_dbv (acc, dbv);
      return NO_ERROR;
    case DB_TYPE_SHORT:
      acc->v.int_sum = (int64_t) db_get_short (dbv);
      break;
    case DB_TYPE_INTEGER:
      acc->v.int_sum = (int64_t) db_get_int (dbv);
      break;
    case DB_TYPE_BIGINT:
      acc->v.int_sum = (int64_t) db_get_bigint (dbv);
      break;
    case DB_TYPE_DOUBLE:
      acc->v.dbl_sum = db_get_double (dbv);
      break;
    case DB_TYPE_FLOAT:
      acc->v.dbl_sum = (double) db_get_float (dbv);
      break;
    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }

  acc->sum_type = sum_acc_sum_type_for (vtype);
  acc->is_active = true;
  return NO_ERROR;
}

/*
 * qdata_sum_acc_add_dbv () - add one value to an active accumulator
 *   return: NO_ERROR, or ER_QPROC_OVERFLOW_ADDITION with the same overflow
 *           semantics as the per-row addition
 *   acc(in/out) : active accumulator; sum_type matches the value's type
 *   dbv(in)     : the value; not NULL-valued
 */
int
qdata_sum_acc_add_dbv (SUM_ACC * acc, const DB_VALUE * dbv)
{
  DB_TYPE vtype;

  assert (acc != NULL && acc->is_active && dbv != NULL);

  vtype = DB_VALUE_DOMAIN_TYPE (dbv);
  if (acc->sum_type != sum_acc_sum_type_for (vtype))
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }

  switch (vtype)
    {
    case DB_TYPE_NUMERIC:
      return numeric_sum_acc_add_dbv (acc, dbv);
    case DB_TYPE_SHORT:
      acc->v.int_sum += (int64_t) db_get_short (dbv);
      if (OR_CHECK_SHORT_OVERFLOW (acc->v.int_sum))
	{
	  goto overflow;
	}
      break;
    case DB_TYPE_INTEGER:
      acc->v.int_sum += (int64_t) db_get_int (dbv);
      if (OR_CHECK_INT_OVERFLOW (acc->v.int_sum))
	{
	  goto overflow;
	}
      break;
    case DB_TYPE_BIGINT:
      if (__builtin_add_overflow (acc->v.int_sum, (int64_t) db_get_bigint (dbv), &acc->v.int_sum))
	{
	  goto overflow;
	}
      break;
    case DB_TYPE_DOUBLE:
      acc->v.dbl_sum += db_get_double (dbv);
      if (OR_CHECK_DOUBLE_OVERFLOW (acc->v.dbl_sum))
	{
	  goto overflow;
	}
      break;
    case DB_TYPE_FLOAT:
      acc->v.dbl_sum += (double) db_get_float (dbv);
      if (OR_CHECK_DOUBLE_OVERFLOW (acc->v.dbl_sum))
	{
	  goto overflow;
	}
      break;
    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }

  return NO_ERROR;

overflow:
  acc->is_active = false;
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
  return ER_QPROC_OVERFLOW_ADDITION;
}

/*
 * qdata_sum_acc_accumulate () - accumulate one value, dispatching on its type
 *   return: NO_ERROR, or an error code
 *   acc(in/out)   : accumulator
 *   is_first(in)  : true for the first value of the group; seed_from is ignored
 *   seed_from(in) : caller's running value, or NULL; used to restore a partial
 *                   sum when the accumulator is empty
 *   value(in)     : value to accumulate; not NULL-valued
 *
 * Note: The first value is always accumulated here rather than kept in the
 *       caller's DB_VALUE. The analytic path may finalize that value mid-partition,
 *       and AVG can overwrite it with a DOUBLE.
 */
int
qdata_sum_acc_accumulate (SUM_ACC * acc, bool is_first, const DB_VALUE * seed_from, const DB_VALUE * value)
{
  assert (acc != NULL && value != NULL);

  if (is_first)
    {
      /* new group: discard whatever state the previous one left behind */
      acc->is_active = false;
    }
  else if (!acc->is_active && !DB_IS_NULL (seed_from)
	   && sum_acc_sum_type_for (DB_VALUE_DOMAIN_TYPE (seed_from)) != DB_TYPE_NULL)
    {
      /* an empty accumulator under a running value: a spilled partial sum came
       * back as a plain DB_VALUE. Fold it in first or it is lost. */
      if (qdata_sum_acc_start (acc, seed_from) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return acc->is_active ? qdata_sum_acc_add_dbv (acc, value) : qdata_sum_acc_start (acc, value);
}

/*
 * qdata_sum_acc_merge () - merge one partial accumulator into another
 *   return: NO_ERROR, or an error code
 *   acc(in/out) : active destination accumulator
 *   other(in)   : active source accumulator; left untouched
 *
 * Note: Partial accumulators for the same aggregate have the same sum_type.
 *       Typed merges re-check the input type's range; NUMERIC accumulators
 *       merge directly in the word domain.
 */
int
qdata_sum_acc_merge (SUM_ACC * acc, const SUM_ACC * other)
{
  assert (acc != NULL && acc->is_active);
  assert (other != NULL && other->is_active);

  if (acc->sum_type != other->sum_type)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }

  switch ((DB_TYPE) acc->sum_type)
    {
    case DB_TYPE_SHORT:
      acc->v.int_sum += other->v.int_sum;
      if (OR_CHECK_SHORT_OVERFLOW (acc->v.int_sum))
	{
	  goto overflow;
	}
      return NO_ERROR;
    case DB_TYPE_INTEGER:
      acc->v.int_sum += other->v.int_sum;
      if (OR_CHECK_INT_OVERFLOW (acc->v.int_sum))
	{
	  goto overflow;
	}
      return NO_ERROR;
    case DB_TYPE_BIGINT:
      if (__builtin_add_overflow (acc->v.int_sum, other->v.int_sum, &acc->v.int_sum))
	{
	  goto overflow;
	}
      return NO_ERROR;
    case DB_TYPE_DOUBLE:
      acc->v.dbl_sum += other->v.dbl_sum;
      if (OR_CHECK_DOUBLE_OVERFLOW (acc->v.dbl_sum))
	{
	  goto overflow;
	}
      return NO_ERROR;
    case DB_TYPE_NUMERIC:
      return numeric_sum_acc_merge (acc, other);
    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }

overflow:
  acc->is_active = false;
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
  return ER_QPROC_OVERFLOW_ADDITION;
}

/*
 * qdata_sum_acc_snapshot () - write the accumulator's running sum into a DB_VALUE,
 *                             keeping the accumulator active
 *   return: NO_ERROR, or an error code
 *   acc(in)     : active accumulator; NOT deactivated
 *   result(out) : the running sum as a DB_VALUE
 *
 * Note: Used by cumulative analytic functions, which emit a running value per
 *       sort key group and continue accumulating. Typed sums convert losslessly;
 *       NUMERIC mode rounds a copy, leaving the live accumulator unchanged.
 */
int
qdata_sum_acc_snapshot (const SUM_ACC * acc, DB_VALUE * result)
{
  assert (acc != NULL && acc->is_active && result != NULL);

  switch ((DB_TYPE) acc->sum_type)
    {
    case DB_TYPE_SHORT:
      db_make_short (result, (short) acc->v.int_sum);
      return NO_ERROR;
    case DB_TYPE_INTEGER:
      db_make_int (result, (int) acc->v.int_sum);
      return NO_ERROR;
    case DB_TYPE_BIGINT:
      db_make_bigint (result, (DB_BIGINT) acc->v.int_sum);
      return NO_ERROR;
    case DB_TYPE_DOUBLE:
      db_make_double (result, acc->v.dbl_sum);
      return NO_ERROR;
    case DB_TYPE_NUMERIC:
      return numeric_sum_acc_snapshot (acc, result);
    default:
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }
}

/*
 * qdata_sum_acc_finalize () - finalize the accumulator into the running-sum DB_VALUE
 *   return: NO_ERROR, or an error code
 *   acc(in/out) : active accumulator; deactivated on the way out
 *   result(out) : the running sum as a DB_VALUE
 *
 * Note: Typed sums need no rounding or packing; their range is re-checked on
 *       every add, so the final narrowing casts are lossless. NUMERIC mode
 *       performs the single per-group rounding here.
 */
int
qdata_sum_acc_finalize (SUM_ACC * acc, DB_VALUE * result)
{
  int ret = qdata_sum_acc_snapshot (acc, result);

  acc->is_active = false;
  return ret;
}

/*
 * qdata_sum_acc_flatten_for_spill () - flatten the accumulator into its running
 *                                      DB_VALUE before writing it to a spill file
 *   return: NO_ERROR, or an error code
 *
 * Note: Uses the same conversion as qdata_sum_acc_finalize (). The separate name
 *       marks the one call site where it happens mid-group. NUMERIC accumulators
 *       cannot be stored in list file columns, so the partial sum is stored as
 *       a DB_VALUE and restored by the accumulate seed path when reloaded.
 *       Typed sums convert losslessly.
 */
int
qdata_sum_acc_flatten_for_spill (SUM_ACC * acc, DB_VALUE * result)
{
  return qdata_sum_acc_finalize (acc, result);
}

/*
 * qdata_concatenate_dbval () -
 *   return: NO_ERROR, or ER_code
 *   dbval1(in)		  : First db_value node
 *   dbval2(in)		  : Second db_value node
 *   result_p(out)	  : Resultant db_value node
 *   domain_p(in)	  : DB domain of result
 *   max_allowed_size(in) : max allowed size for result
 *   warning_context(in)  : used only to display truncation warning context
 *
 * Note: Concatenates a db_values to string db value.
 *	 Value to be added is truncated in case the allowed size would be
 *	 exceeded . Truncation is done without modifying the value (a new
 *	 temporary value is used).
 *	 A warning is logged the first time the allowed size is exceeded
 *	 (when the value to add has already exceeded the size, no warning is
 *	 logged).
 */
int
qdata_concatenate_dbval (THREAD_ENTRY * thread_p, DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p,
			 tp_domain * domain_p, const int max_allowed_size, const char *warning_context)
{
  DB_TYPE type2, type1;
  int error = NO_ERROR;
  DB_VALUE arg_val, db_temp;
  int res_size = 0, val_size = 0;
  bool warning_size_exceeded = false;
  int spare_bytes = 0;
  bool save_need_clear;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  if (!QSTR_IS_ANY_CHAR_OR_BIT (type1))
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
    }
  db_make_null (&arg_val);
  db_make_null (&db_temp);

  res_size = db_get_string_size (dbval1_p);

  switch (type2)
    {
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      val_size = db_get_string_size (dbval2_p);
      if (res_size >= max_allowed_size)
	{
	  assert (warning_size_exceeded == false);
	  break;
	}
      else if (res_size + val_size > max_allowed_size)
	{
	  warning_size_exceeded = true;
	  error = db_string_limit_size_string (dbval2_p, &db_temp, max_allowed_size - res_size, &spare_bytes);
	  if (error != NO_ERROR)
	    {
	      break;
	    }

	  error = qdata_add_chars_to_dbval (dbval1_p, &db_temp, result_p);

	  if (spare_bytes > 0)
	    {
	      /* The adjusted 'db_temp' string was truncated to the last full multibyte character. Increase the
	       * 'result' with 'spare_bytes' remained from the last truncated multibyte character. This prevents
	       * GROUP_CONCAT to add other single-byte chars (or char with fewer bytes than 'spare_bytes' to current
	       * aggregate. */
	      save_need_clear = result_p->need_clear;
	      qstr_make_typed_string (DB_VALUE_DOMAIN_TYPE (result_p), result_p, DB_VALUE_PRECISION (result_p),
				      db_get_string (result_p), db_get_string_size (result_p) + spare_bytes,
				      db_get_string_codeset (dbval1_p), db_get_string_collation (dbval1_p));
	      result_p->need_clear = save_need_clear;
	    }
	}
      else
	{
	  error = qdata_add_chars_to_dbval (dbval1_p, dbval2_p, result_p);
	}
      break;
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_MONETARY:
    case DB_TYPE_TIME:
    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_ENUMERATION:
      {
	TP_DOMAIN_STATUS err_dom;
	err_dom = tp_value_cast (dbval2_p, &arg_val, domain_p, false);

	if (err_dom == DOMAIN_COMPATIBLE)
	  {
	    val_size = db_get_string_size (&arg_val);

	    if (res_size >= max_allowed_size)
	      {
		assert (warning_size_exceeded == false);
		break;
	      }
	    else if (res_size + val_size > max_allowed_size)
	      {
		warning_size_exceeded = true;
		error = db_string_limit_size_string (&arg_val, &db_temp, max_allowed_size - res_size, &spare_bytes);
		if (error != NO_ERROR)
		  {
		    break;
		  }

		error = qdata_add_chars_to_dbval (dbval1_p, &db_temp, result_p);

		if (spare_bytes > 0)
		  {
		    save_need_clear = result_p->need_clear;
		    qstr_make_typed_string (DB_VALUE_DOMAIN_TYPE (result_p), result_p, DB_VALUE_PRECISION (result_p),
					    db_get_string (result_p), db_get_string_size (result_p) + spare_bytes,
					    db_get_string_codeset (dbval1_p), db_get_string_collation (dbval1_p));
		    result_p->need_clear = save_need_clear;
		  }
	      }
	    else
	      {
		error = qdata_add_chars_to_dbval (dbval1_p, &arg_val, result_p);
	      }
	  }
	else
	  {
	    error = tp_domain_status_er_set (err_dom, ARG_FILE_LINE, dbval2_p, domain_p);
	  }
      }
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
    }

  pr_clear_value (&arg_val);
  pr_clear_value (&db_temp);
  if (error == NO_ERROR && warning_size_exceeded == true)
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_QPROC_SIZE_STRING_TRUNCATED, 1, warning_context);
    }

  return error;
}


/*
 * qdata_increment_dbval () -
 *   return: NO_ERROR, or ER_code
 *   dbval1(in) : db_value node
 *   res(in)    :
 *   incval(in) :
 *
 * Note: Increment the db_value.
 * If overflow happens, reset the db_value as 0.
 */
int
qdata_increment_dbval (DB_VALUE * dbval_p, DB_VALUE * result_p, int inc_val)
{
  DB_TYPE type1;
  short stmp, s1;
  int itmp, i1;
  DB_BIGINT bitmp, bi1;

  type1 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type1)
    {
    case DB_TYPE_SHORT:
      s1 = db_get_short (dbval_p);
      stmp = s1 + inc_val;
      if ((inc_val > 0 && OR_CHECK_ADD_OVERFLOW (s1, inc_val, stmp))
	  || (inc_val < 0 && OR_CHECK_SUB_UNDERFLOW (s1, -inc_val, stmp)))
	{
	  stmp = 0;
	}

      db_make_short (result_p, stmp);
      break;

    case DB_TYPE_INTEGER:
      i1 = db_get_int (dbval_p);
      itmp = i1 + inc_val;
      if ((inc_val > 0 && OR_CHECK_ADD_OVERFLOW (i1, inc_val, itmp))
	  || (inc_val < 0 && OR_CHECK_SUB_UNDERFLOW (i1, -inc_val, itmp)))
	{
	  itmp = 0;
	}

      db_make_int (result_p, itmp);
      break;

    case DB_TYPE_BIGINT:
      bi1 = db_get_bigint (dbval_p);
      bitmp = bi1 + inc_val;
      if ((inc_val > 0 && OR_CHECK_ADD_OVERFLOW (bi1, inc_val, bitmp))
	  || (inc_val < 0 && OR_CHECK_SUB_UNDERFLOW (bi1, -inc_val, bitmp)))
	{
	  bitmp = 0;
	}

      db_make_bigint (result_p, bitmp);
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
qdata_subtract_short (short s1, short s2, DB_VALUE * result_p)
{
  short stmp;

  stmp = s1 - s2;

  if (OR_CHECK_SUB_UNDERFLOW (s1, s2, stmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_FAILED;
    }

  db_make_short (result_p, stmp);
  return NO_ERROR;
}

static int
qdata_subtract_int (int i1, int i2, DB_VALUE * result_p)
{
  int itmp;

  itmp = i1 - i2;

  if (OR_CHECK_SUB_UNDERFLOW (i1, i2, itmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_FAILED;
    }

  db_make_int (result_p, itmp);
  return NO_ERROR;
}

static int
qdata_subtract_bigint (DB_BIGINT bi1, DB_BIGINT bi2, DB_VALUE * result_p)
{
  DB_BIGINT bitmp;

  bitmp = bi1 - bi2;

  if (OR_CHECK_SUB_UNDERFLOW (bi1, bi2, bitmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_FAILED;
    }

  db_make_bigint (result_p, bitmp);
  return NO_ERROR;
}

static int
qdata_subtract_float (float f1, float f2, DB_VALUE * result_p)
{
  float ftmp;

  ftmp = f1 - f2;

  if (OR_CHECK_FLOAT_OVERFLOW (ftmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_FAILED;
    }

  db_make_float (result_p, ftmp);
  return NO_ERROR;
}

static int
qdata_subtract_double (double d1, double d2, DB_VALUE * result_p)
{
  double dtmp;

  dtmp = d1 - d2;

  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_FAILED;
    }

  db_make_double (result_p, dtmp);
  return NO_ERROR;
}

static int
qdata_subtract_monetary (double d1, double d2, DB_CURRENCY currency, DB_VALUE * result_p)
{
  double dtmp;

  dtmp = d1 - d2;

  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
      return ER_FAILED;
    }

  db_make_monetary (result_p, currency, dtmp);
  return NO_ERROR;
}

static int
qdata_subtract_time (DB_TIME u1, DB_TIME u2, DB_VALUE * result_p)
{
  DB_TIME utmp;
  int hour, minute, second;

  if (u1 < u2)
    {
      u1 += SECONDS_OF_ONE_DAY;
    }

  utmp = u1 - u2;
  db_time_decode (&utmp, &hour, &minute, &second);
  db_make_time (result_p, hour, minute, second);

  return NO_ERROR;
}

static int
qdata_subtract_utime (DB_UTIME u1, DB_UTIME u2, DB_VALUE * result_p)
{
  DB_UTIME utmp;

  utmp = u1 - u2;
  if (OR_CHECK_UNS_SUB_UNDERFLOW (u1, u2, utmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_FAILED;
    }

  db_make_timestamp (result_p, utmp);
  return NO_ERROR;
}

static int
qdata_subtract_utime_to_short_asymmetry (DB_VALUE * utime_val_p, short s, unsigned int *utime, DB_VALUE * result_p,
					 TP_DOMAIN * domain_p)
{
  DB_VALUE tmp;
  int error = NO_ERROR;

  if (s == DB_INT16_MIN)	/* check for asymmetry. */
    {
      if (*utime == DB_UINT32_MAX)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
	  return ER_QPROC_OVERFLOW_ADDITION;
	}

      (*utime)++;
      s++;
    }

  db_make_short (&tmp, -(s));
  error = qdata_add_dbval (utime_val_p, &tmp, result_p, domain_p);

  return error;
}

static int
qdata_subtract_utime_to_int_asymmetry (DB_VALUE * utime_val_p, int i, unsigned int *utime, DB_VALUE * result_p,
				       TP_DOMAIN * domain_p)
{
  DB_VALUE tmp;
  int error = NO_ERROR;

  if (i == DB_INT32_MIN)	/* check for asymmetry. */
    {
      if (*utime == DB_UINT32_MAX)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
	  return ER_QPROC_OVERFLOW_ADDITION;
	}

      (*utime)++;
      i++;
    }

  db_make_int (&tmp, -(i));
  error = qdata_add_dbval (utime_val_p, &tmp, result_p, domain_p);

  return error;
}

static int
qdata_subtract_utime_to_bigint_asymmetry (DB_VALUE * utime_val_p, DB_BIGINT bi, unsigned int *utime,
					  DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_VALUE tmp;
  int error = NO_ERROR;

  if (bi == DB_BIGINT_MIN)	/* check for asymmetry. */
    {
      if (*utime == DB_UINT32_MAX)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
	  return ER_QPROC_OVERFLOW_ADDITION;
	}

      (*utime)++;
      bi++;
    }

  db_make_bigint (&tmp, -(bi));
  error = qdata_add_dbval (utime_val_p, &tmp, result_p, domain_p);

  return error;
}

static int
qdata_subtract_datetime_to_int (DB_DATETIME * dt1, DB_BIGINT i2, DB_VALUE * result_p)
{
  DB_DATETIME datetime_tmp;
  int error;

  error = db_subtract_int_from_datetime (dt1, i2, &datetime_tmp);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_datetime (result_p, &datetime_tmp);
  return NO_ERROR;
}

static int
qdata_subtract_datetime (DB_DATETIME * dt1, DB_DATETIME * dt2, DB_VALUE * result_p)
{
  DB_BIGINT u1, u2, tmp;

  u1 = ((DB_BIGINT) dt1->date) * MILLISECONDS_OF_ONE_DAY + dt1->time;
  u2 = ((DB_BIGINT) dt2->date) * MILLISECONDS_OF_ONE_DAY + dt2->time;

  tmp = u1 - u2;
  if (OR_CHECK_SUB_UNDERFLOW (u1, u2, tmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_TIME_UNDERFLOW, 0);
      return ER_FAILED;
    }

  db_make_bigint (result_p, tmp);
  return NO_ERROR;
}

static int
qdata_subtract_datetime_to_int_asymmetry (DB_VALUE * datetime_val_p, DB_BIGINT i, DB_DATETIME * datetime,
					  DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_VALUE tmp;
  int error = NO_ERROR;

  if (i == DB_BIGINT_MIN)	/* check for asymmetry. */
    {
      if (datetime->time == 0)
	{
	  datetime->date--;
	  datetime->time = MILLISECONDS_OF_ONE_DAY;
	}

      datetime->time--;
      i++;
    }

  db_make_bigint (&tmp, -(i));
  error = qdata_add_dbval (datetime_val_p, &tmp, result_p, domain_p);

  return error;
}

static int
qdata_subtract_sequence_to_dbval (DB_VALUE * seq_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_SET *set_tmp;
#if !defined(NDEBUG)
  DB_TYPE type1, type2;
#endif

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (seq_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  assert (TP_IS_SET_TYPE (type1));
  assert (TP_IS_SET_TYPE (type2));
#endif

  if (domain_p == NULL)
    {
      return ER_FAILED;
    }

  if (set_difference (db_get_set (seq_val_p), db_get_set (dbval_p), &set_tmp, domain_p) < 0)
    {
      return ER_FAILED;
    }

  set_make_collection (result_p, set_tmp);
  return NO_ERROR;
}

static int
qdata_subtract_time_to_dbval (DB_VALUE * time_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type;
  DB_TIME *timeval, *timeval1;
  int subval;
  int err = NO_ERROR;

  timeval = db_get_time (time_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      subval = (int) db_get_short (dbval_p);
      if (subval < 0)
	{
	  return qdata_add_bigint_to_time (time_val_p, (DB_BIGINT) (-subval), result_p);
	}
      return qdata_subtract_time ((DB_TIME) (*timeval % SECONDS_OF_ONE_DAY), (DB_TIME) subval, result_p);

    case DB_TYPE_INTEGER:
      subval = (int) (db_get_int (dbval_p) % SECONDS_OF_ONE_DAY);
      if (subval < 0)
	{
	  return qdata_add_bigint_to_time (time_val_p, (DB_BIGINT) (-subval), result_p);
	}
      return qdata_subtract_time ((DB_TIME) (*timeval % SECONDS_OF_ONE_DAY), (DB_TIME) subval, result_p);

    case DB_TYPE_BIGINT:
      subval = (int) (db_get_bigint (dbval_p) % SECONDS_OF_ONE_DAY);
      if (subval < 0)
	{
	  return qdata_add_bigint_to_time (time_val_p, (DB_BIGINT) (-subval), result_p);
	}
      return qdata_subtract_time ((DB_TIME) (*timeval % SECONDS_OF_ONE_DAY), (DB_TIME) subval, result_p);

    case DB_TYPE_TIME:
      timeval1 = db_get_time (dbval_p);
      db_make_int (result_p, ((int) *timeval - (int) *timeval1));
      break;

    default:
      break;
    }

  return err;
}

static int
qdata_subtract_utime_to_dbval (DB_VALUE * utime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_TYPE type;
  DB_UTIME *utime, *utime1;
  DB_TIMESTAMPTZ *ts_tz1;
  DB_DATETIME *datetime;
  DB_DATETIME tmp_datetime;
  DB_DATETIMETZ datetime_tz_1;
  unsigned int u1;
  short s2;
  int i2;
  DB_BIGINT bi2;

  utime = db_get_timestamp (utime_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      u1 = (unsigned int) *utime;
      s2 = db_get_short (dbval_p);
      if (s2 < 0)
	{
	  /* We're really adding.  */
	  return qdata_subtract_utime_to_short_asymmetry (utime_val_p, s2, utime, result_p, domain_p);
	}

      return qdata_subtract_utime (*utime, (DB_UTIME) s2, result_p);

    case DB_TYPE_INTEGER:
      u1 = (unsigned int) *utime;
      i2 = db_get_int (dbval_p);
      if (i2 < 0)
	{
	  /* We're really adding.  */
	  return qdata_subtract_utime_to_int_asymmetry (utime_val_p, i2, utime, result_p, domain_p);
	}

      return qdata_subtract_utime (*utime, (DB_UTIME) i2, result_p);

    case DB_TYPE_BIGINT:
      u1 = (unsigned int) *utime;
      bi2 = db_get_bigint (dbval_p);
      if (bi2 < 0)
	{
	  /* We're really adding. */
	  return qdata_subtract_utime_to_bigint_asymmetry (utime_val_p, bi2, utime, result_p, domain_p);
	}

      return qdata_subtract_utime (*utime, (DB_UTIME) bi2, result_p);

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      utime1 = db_get_timestamp (dbval_p);
      db_make_int (result_p, ((int) *utime - (int) *utime1));
      break;

    case DB_TYPE_TIMESTAMPTZ:
      ts_tz1 = db_get_timestamptz (dbval_p);
      db_make_int (result_p, ((int) *utime - (int) ts_tz1->timestamp));
      break;

    case DB_TYPE_DATETIME:
      datetime = db_get_datetime (dbval_p);

      (void) db_timestamp_decode_ses (utime, &tmp_datetime.date, &tmp_datetime.time);

      return qdata_subtract_datetime (&tmp_datetime, datetime, result_p);

    case DB_TYPE_DATETIMELTZ:
      datetime = db_get_datetime (dbval_p);
      (void) db_timestamp_decode_utc (utime, &tmp_datetime.date, &tmp_datetime.time);

      return qdata_subtract_datetime (&tmp_datetime, datetime, result_p);

    case DB_TYPE_DATETIMETZ:
      datetime_tz_1 = *db_get_datetimetz (dbval_p);
      (void) db_timestamp_decode_utc (utime, &tmp_datetime.date, &tmp_datetime.time);

      return qdata_subtract_datetime (&tmp_datetime, &datetime_tz_1.datetime, result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_subtract_timestampltz_to_dbval (DB_VALUE * ts_ltz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
				      TP_DOMAIN * domain_p)
{
  DB_TYPE type;
  DB_UTIME *utime_p;
  DB_VALUE utime_val, tmp_val_res;
  int err = NO_ERROR;

  utime_p = db_get_timestamp (ts_ltz_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
      /* perform operation as simple UTIME */
      db_make_timestamp (&utime_val, *utime_p);
      err =
	qdata_subtract_utime_to_dbval (&utime_val, dbval_p, &tmp_val_res,
				       tp_domain_resolve_default (DB_TYPE_TIMESTAMP));
      if (err != NO_ERROR)
	{
	  break;
	}

      if (DB_VALUE_TYPE (&tmp_val_res) == DB_TYPE_TIMESTAMP)
	{
	  db_make_timestampltz (result_p, *db_get_timestamp (&tmp_val_res));
	}
      else
	{
	  assert (tmp_val_res.need_clear == false);
	  pr_clone_value (&tmp_val_res, result_p);
	}
      break;
    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_subtract_timestamptz_to_dbval (DB_VALUE * ts_tz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
				     TP_DOMAIN * domain_p)
{
  int err = NO_ERROR;
  DB_TYPE type;
  DB_UTIME *utime1 = NULL, *utime2 = NULL;
  DB_TIMESTAMPTZ *ts_tz1_p = NULL, *ts_tz2_p = NULL, ts_tz_res, ts_tz_res_fixed;
  DB_DATETIME *datetime = NULL;
  DB_DATETIME tmp_datetime;
  DB_DATETIMETZ datetime_tz_1;
  DB_DATE date;
  DB_TIME time;
  unsigned int u1;
  short s2;
  int i2;
  DB_BIGINT bi2;

  DB_VALUE tmp_val_res;
  tmp_val_res.data.utime = 0;

  ts_tz1_p = db_get_timestamptz (ts_tz_val_p);
  utime1 = &ts_tz1_p->timestamp;
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      u1 = (unsigned int) *utime1;
      s2 = db_get_short (dbval_p);
      if (s2 < 0)
	{
	  /* We're really adding.  */
	  return qdata_subtract_utime_to_short_asymmetry (ts_tz_val_p, s2, utime1, result_p, domain_p);
	}

      err = qdata_subtract_utime (*utime1, (DB_UTIME) s2, &tmp_val_res);
      break;

    case DB_TYPE_INTEGER:
      u1 = (unsigned int) *utime1;
      i2 = db_get_int (dbval_p);
      if (i2 < 0)
	{
	  /* We're really adding.  */
	  return qdata_subtract_utime_to_int_asymmetry (ts_tz_val_p, i2, utime1, result_p, domain_p);
	}

      err = qdata_subtract_utime (*utime1, (DB_UTIME) i2, &tmp_val_res);
      break;

    case DB_TYPE_BIGINT:
      u1 = (unsigned int) *utime1;
      bi2 = db_get_bigint (dbval_p);
      if (bi2 < 0)
	{
	  /* We're really adding. */
	  return qdata_subtract_utime_to_bigint_asymmetry (ts_tz_val_p, bi2, utime1, result_p, domain_p);
	}

      err = qdata_subtract_utime (*utime1, (DB_UTIME) bi2, &tmp_val_res);
      break;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      utime2 = db_get_timestamp (dbval_p);
      db_make_int (result_p, ((int) *utime1 - (int) *utime2));
      return err;

    case DB_TYPE_TIMESTAMPTZ:
      ts_tz2_p = db_get_timestamptz (dbval_p);
      db_make_int (result_p, ((int) *utime1 - (int) ts_tz2_p->timestamp));
      return err;

    case DB_TYPE_DATETIME:
      datetime = db_get_datetime (dbval_p);

      err = db_timestamp_decode_w_tz_id (utime1, &ts_tz1_p->tz_id, &date, &time);
      if (err != NO_ERROR)
	{
	  break;
	}

      tmp_datetime.date = date;
      tmp_datetime.time = time * 1000;

      return qdata_subtract_datetime (&tmp_datetime, datetime, result_p);

    case DB_TYPE_DATETIMELTZ:
      datetime = db_get_datetime (dbval_p);
      db_timestamp_decode_utc (utime1, &date, &time);

      tmp_datetime.date = date;
      tmp_datetime.time = time * 1000;

      return qdata_subtract_datetime (&tmp_datetime, datetime, result_p);

    case DB_TYPE_DATETIMETZ:
      datetime_tz_1 = *db_get_datetimetz (dbval_p);
      db_timestamp_decode_utc (utime1, &date, &time);

      if (err != NO_ERROR)
	{
	  break;
	}

      tmp_datetime.date = date;
      tmp_datetime.time = time * 1000;

      return qdata_subtract_datetime (&tmp_datetime, &datetime_tz_1.datetime, result_p);

    default:
      break;
    }

  if (err == NO_ERROR)
    {
      assert (DB_VALUE_TYPE (&tmp_val_res) == DB_TYPE_TIMESTAMP);
      /* create TIMESTAMPTZ from result UTIME by adjusting TZ_ID */
      ts_tz_res.timestamp = *db_get_timestamp (&tmp_val_res);
      ts_tz_res.tz_id = ts_tz1_p->tz_id;
      err = tz_timestamptz_fix_zone (&ts_tz_res, &ts_tz_res_fixed);
      if (err != NO_ERROR)
	{
	  return err;
	}

      db_make_timestamptz (result_p, &ts_tz_res_fixed);
    }
  return err;
}

static int
qdata_subtract_datetime_to_dbval (DB_VALUE * datetime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
				  TP_DOMAIN * domain_p)
{
  DB_TYPE type;
  DB_DATETIME *datetime1_p;

  datetime1_p = db_get_datetime (datetime_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      {
	short s2;
	s2 = db_get_short (dbval_p);
	if (s2 < 0)
	  {
	    /* We're really adding.  */
	    return qdata_subtract_datetime_to_int_asymmetry (datetime_val_p, s2, datetime1_p, result_p, domain_p);
	  }

	return qdata_subtract_datetime_to_int (datetime1_p, s2, result_p);
      }

    case DB_TYPE_INTEGER:
      {
	int i2;
	i2 = db_get_int (dbval_p);
	if (i2 < 0)
	  {
	    /* We're really adding.  */
	    return qdata_subtract_datetime_to_int_asymmetry (datetime_val_p, i2, datetime1_p, result_p, domain_p);
	  }

	return qdata_subtract_datetime_to_int (datetime1_p, i2, result_p);
      }

    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bi2;

	bi2 = db_get_bigint (dbval_p);
	if (bi2 < 0)
	  {
	    /* We're really adding.  */
	    return qdata_subtract_datetime_to_int_asymmetry (datetime_val_p, bi2, datetime1_p, result_p, domain_p);
	  }

	return qdata_subtract_datetime_to_int (datetime1_p, bi2, result_p);
      }

    case DB_TYPE_TIMESTAMP:
      {
	DB_BIGINT u1, u2;
	DB_DATETIME datetime2;

	(void) db_timestamp_decode_ses (db_get_timestamp (dbval_p), &datetime2.date, &datetime2.time);

	u1 = ((DB_BIGINT) datetime1_p->date) * MILLISECONDS_OF_ONE_DAY + datetime1_p->time;
	u2 = ((DB_BIGINT) datetime2.date) * MILLISECONDS_OF_ONE_DAY + datetime2.time;

	return db_make_bigint (result_p, u1 - u2);
      }

    case DB_TYPE_TIMESTAMPLTZ:
      {
	DB_BIGINT u1, u2;
	DB_DATETIME datetime2;

	(void) db_timestamp_decode_ses (db_get_timestamp (dbval_p), &datetime2.date, &datetime2.time);

	u1 = ((DB_BIGINT) datetime1_p->date) * MILLISECONDS_OF_ONE_DAY + datetime1_p->time;
	u2 = ((DB_BIGINT) datetime2.date) * MILLISECONDS_OF_ONE_DAY + datetime2.time;

	return db_make_bigint (result_p, u1 - u2);
      }

    case DB_TYPE_TIMESTAMPTZ:
      {
	DB_BIGINT u1, u2;
	DB_DATETIME datetime2;
	DB_TIMESTAMPTZ ts_tz2;

	ts_tz2 = *db_get_timestamptz (dbval_p);

	(void) db_timestamp_decode_ses (&ts_tz2.timestamp, &datetime2.date, &datetime2.time);

	u1 = ((DB_BIGINT) datetime1_p->date) * MILLISECONDS_OF_ONE_DAY + datetime1_p->time;
	u2 = ((DB_BIGINT) datetime2.date) * MILLISECONDS_OF_ONE_DAY + datetime2.time;

	return db_make_bigint (result_p, u1 - u2);
      }

    case DB_TYPE_DATETIME:
      {
	DB_BIGINT u1, u2;
	DB_DATETIME *datetime2_p;

	datetime2_p = db_get_datetime (dbval_p);

	u1 = ((DB_BIGINT) datetime1_p->date) * MILLISECONDS_OF_ONE_DAY + datetime1_p->time;
	u2 = ((DB_BIGINT) datetime2_p->date) * MILLISECONDS_OF_ONE_DAY + datetime2_p->time;

	return db_make_bigint (result_p, u1 - u2);
      }

    case DB_TYPE_DATETIMELTZ:
      {
	DB_BIGINT u1, u2;
	DB_DATETIMETZ dt_tz1;
	DB_DATETIME *dt_utc2_p;
	int err;

	err = tz_create_datetimetz_from_ses (datetime1_p, &dt_tz1);
	if (err != NO_ERROR)
	  {
	    return err;
	  }

	dt_utc2_p = db_get_datetime (dbval_p);

	u1 = ((DB_BIGINT) dt_tz1.datetime.date) * MILLISECONDS_OF_ONE_DAY + dt_tz1.datetime.time;
	u2 = ((DB_BIGINT) dt_utc2_p->date) * MILLISECONDS_OF_ONE_DAY + dt_utc2_p->time;

	return db_make_bigint (result_p, u1 - u2);
      }

    case DB_TYPE_DATETIMETZ:
      {
	DB_BIGINT u1, u2;
	DB_DATETIMETZ *datetimetz2_p;
	DB_DATETIME datetime2;
	int err;

	datetimetz2_p = db_get_datetimetz (dbval_p);
	err = tz_utc_datetimetz_to_local (&datetimetz2_p->datetime, &datetimetz2_p->tz_id, &datetime2);

	if (err != NO_ERROR)
	  {
	    return err;
	  }

	u1 = ((DB_BIGINT) datetime1_p->date) * MILLISECONDS_OF_ONE_DAY + datetime1_p->time;
	u2 = ((DB_BIGINT) datetime2.date) * MILLISECONDS_OF_ONE_DAY + datetime2.time;

	return db_make_bigint (result_p, u1 - u2);
      }

    case DB_TYPE_DATE:
      {
	DB_BIGINT u1, u2;

	u1 = ((DB_BIGINT) datetime1_p->date) * MILLISECONDS_OF_ONE_DAY + datetime1_p->time;
	u2 = ((DB_BIGINT) * db_get_date (dbval_p)) * MILLISECONDS_OF_ONE_DAY;

	return db_make_bigint (result_p, u1 - u2);
      }

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_subtract_datetimetz_to_dbval (DB_VALUE * dt_tz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
				    TP_DOMAIN * domain_p)
{
  int err = NO_ERROR;
  DB_TYPE type;
  DB_DATETIMETZ *dt_tz1_p;
  DB_DATETIME *datetime1_p;

  dt_tz1_p = db_get_datetimetz (dt_tz_val_p);
  datetime1_p = &(dt_tz1_p->datetime);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      {
	DB_VALUE dt_val, dt_val_res;
	DB_DATETIMETZ dt_tz, dt_tz_fixed;

	db_make_datetime (&dt_val, datetime1_p);

	err =
	  qdata_subtract_datetime_to_dbval (&dt_val, dbval_p, &dt_val_res,
					    tp_domain_resolve_default (DB_TYPE_DATETIME));
	if (err != NO_ERROR)
	  {
	    break;
	  }

	dt_tz.datetime = *db_get_datetime (&dt_val_res);
	dt_tz.tz_id = dt_tz1_p->tz_id;

	err = tz_datetimetz_fix_zone (&dt_tz, &dt_tz_fixed);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	db_make_datetimetz (result_p, &dt_tz_fixed);
	break;
      }

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
      {
	DB_BIGINT u1, u2;
	DB_DATETIME datetime2;
	DB_UTIME *utime2_p;

	/* create a DATETIME in UTC reference */
	if (type == DB_TYPE_TIMESTAMPTZ)
	  {
	    DB_TIMESTAMPTZ *ts_tz2_p;

	    ts_tz2_p = db_get_timestamptz (dbval_p);
	    utime2_p = &(ts_tz2_p->timestamp);
	  }
	else
	  {
	    utime2_p = db_get_timestamp (dbval_p);
	  }
	(void) db_timestamp_decode_utc (utime2_p, &datetime2.date, &datetime2.time);

	u1 = ((DB_BIGINT) dt_tz1_p->datetime.date) * MILLISECONDS_OF_ONE_DAY + dt_tz1_p->datetime.time;
	u2 = ((DB_BIGINT) datetime2.date) * MILLISECONDS_OF_ONE_DAY + datetime2.time;

	return db_make_bigint (result_p, u1 - u2);
      }

    case DB_TYPE_DATETIME:
      {
	DB_BIGINT u1, u2;
	DB_DATETIME *datetime2_p;
	DB_DATETIME datetime1;

	/* from DT with TZ to local */
	datetime2_p = db_get_datetime (dbval_p);

	err = tz_utc_datetimetz_to_local (&dt_tz1_p->datetime, &dt_tz1_p->tz_id, &datetime1);
	if (err != NO_ERROR)
	  {
	    return err;
	  }

	u1 = ((DB_BIGINT) datetime1.date) * MILLISECONDS_OF_ONE_DAY + datetime1.time;
	u2 = ((DB_BIGINT) datetime2_p->date) * MILLISECONDS_OF_ONE_DAY + datetime2_p->time;

	return db_make_bigint (result_p, u1 - u2);
      }

    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATETIMELTZ:
      {
	DB_BIGINT u1, u2;
	DB_DATETIMETZ *dt_tz2_p;
	DB_DATETIME *datetime2_p;

	/* both datetimes are in UTC, no need to consider timezones */
	if (type == DB_TYPE_DATETIMETZ)
	  {
	    dt_tz2_p = db_get_datetimetz (dbval_p);
	    datetime2_p = &(dt_tz2_p->datetime);
	  }
	else
	  {
	    datetime2_p = db_get_datetime (dbval_p);
	  }

	u1 = ((DB_BIGINT) datetime1_p->date) * MILLISECONDS_OF_ONE_DAY + datetime1_p->time;
	u2 = ((DB_BIGINT) datetime2_p->date) * MILLISECONDS_OF_ONE_DAY + datetime2_p->time;

	return db_make_bigint (result_p, u1 - u2);
      }

    case DB_TYPE_DATE:
      {
	DB_BIGINT u1, u2;
	DB_DATETIME *datetime2_p;
	DB_DATETIME datetime1;

	/* from DT with TZ to local */
	datetime2_p = db_get_datetime (dbval_p);

	err = tz_utc_datetimetz_to_local (&dt_tz1_p->datetime, &dt_tz1_p->tz_id, &datetime1);
	if (err != NO_ERROR)
	  {
	    return err;
	  }

	u1 = ((DB_BIGINT) datetime1.date) * MILLISECONDS_OF_ONE_DAY + datetime1.time;
	u2 = ((DB_BIGINT) * db_get_date (dbval_p)) * MILLISECONDS_OF_ONE_DAY;

	return db_make_bigint (result_p, u1 - u2);
      }

    default:
      break;
    }

  return err;
}

static int
qdata_subtract_date_to_dbval (DB_VALUE * date_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_TYPE type;
  DB_DATE *date, *date1;
  unsigned int u1, u2, utmp;
  short s2;
  int i2;
  DB_BIGINT bi1, bi2, bitmp;
  int day, month, year;

  date = db_get_date (date_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      u1 = (unsigned int) *date;
      s2 = db_get_short (dbval_p);

      if (s2 < 0)
	{
	  /* We're really adding.  */
	  return qdata_subtract_utime_to_short_asymmetry (date_val_p, s2, date, result_p, domain_p);
	}

      u2 = (unsigned int) s2;
      utmp = u1 - u2;
      if (OR_CHECK_UNS_SUB_UNDERFLOW (u1, u2, utmp) || utmp < DB_DATE_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DATE_UNDERFLOW, 0);
	  return ER_QPROC_DATE_UNDERFLOW;
	}

      db_date_decode (&utmp, &month, &day, &year);
      db_make_date (result_p, month, day, year);
      break;

    case DB_TYPE_BIGINT:
      bi1 = (DB_BIGINT) * date;
      bi2 = db_get_bigint (dbval_p);

      if (bi2 < 0)
	{
	  /* We're really adding.  */
	  return qdata_subtract_utime_to_bigint_asymmetry (date_val_p, bi2, date, result_p, domain_p);
	}

      bitmp = bi1 - bi2;
      if (OR_CHECK_SUB_UNDERFLOW (bi1, bi2, bitmp) || OR_CHECK_UINT_OVERFLOW (bitmp) || bitmp < DB_DATE_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DATE_UNDERFLOW, 0);
	  return ER_FAILED;
	}

      utmp = (unsigned int) bitmp;
      db_date_decode (&utmp, &month, &day, &year);
      db_make_date (result_p, month, day, year);
      break;

    case DB_TYPE_INTEGER:
      u1 = (unsigned int) *date;
      i2 = db_get_int (dbval_p);

      if (i2 < 0)
	{
	  /* We're really adding.  */
	  return qdata_subtract_utime_to_int_asymmetry (date_val_p, i2, date, result_p, domain_p);
	}

      u2 = (unsigned int) i2;
      utmp = u1 - u2;
      if (OR_CHECK_UNS_SUB_UNDERFLOW (u1, u2, utmp) || utmp < DB_DATE_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DATE_UNDERFLOW, 0);
	  return ER_QPROC_DATE_UNDERFLOW;
	}

      db_date_decode (&utmp, &month, &day, &year);
      db_make_date (result_p, month, day, year);
      break;

    case DB_TYPE_DATE:
      date1 = db_get_date (dbval_p);
      db_make_int (result_p, (int) *date - (int) *date1);
      break;

    default:
      break;
    }

  return NO_ERROR;
}

/*
 * qdata_subtract_dbval () - the subtraction of two values: qdata_arith_dbval over the ARITH rule resolved from their types
 *   return: NO_ERROR, or ER_code
 */
int
qdata_subtract_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  return qdata_arith_dbval (T_SUB, dbval1_p, dbval2_p, result_p, domain_p);
}

/*
 * qdata_subtract_number_to_datetime () - a number minus a date or time (DOMAIN_ARITH_DATE with the number first), as
 *   the typed subtractions of a SHORT, an INTEGER and a BIGINT computed it: a SHORT or an INTEGER minus a DATETIME,
 *   DATETIMELTZ or DATETIMETZ is milliseconds as an INTEGER and a BIGINT minus one is no value; a SHORT minus a DATE
 *   reads the difference as a TIME (the SHORT subtraction's answer, kept); the rest subtract as a BIGINT
 *   return: NO_ERROR, or ER_code
 */
static int
qdata_subtract_number_to_datetime (DB_VALUE * number_p, DB_VALUE * datetime_p, DB_VALUE * result_p)
{
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (number_p);
  const DB_TYPE type2 = DB_VALUE_DOMAIN_TYPE (datetime_p);
  DB_BIGINT bi;
  int err = NO_ERROR;

  if (!qdata_number_as_bigint (number_p, &bi))
    {
      /* a floating number: its operand coercion makes it a BIGINT before this */
      return NO_ERROR;
    }

  switch (type2)
    {
    case DB_TYPE_TIME:
      /* the number as seconds within a day */
      if (bi < 0)
	{
	  bi = (bi % SECONDS_OF_ONE_DAY) + SECONDS_OF_ONE_DAY;
	}
      else
	{
	  bi %= SECONDS_OF_ONE_DAY;
	}
      return qdata_subtract_time ((DB_TIME) bi, (DB_TIME) (*db_get_time (datetime_p) % SECONDS_OF_ONE_DAY), result_p);

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      err = qdata_subtract_utime ((DB_UTIME) bi, *db_get_timestamp (datetime_p), result_p);
      if (err == NO_ERROR && type2 == DB_TYPE_TIMESTAMPLTZ)
	{
	  db_make_timestampltz (result_p, *db_get_timestamp (result_p));
	}
      return err;

    case DB_TYPE_TIMESTAMPTZ:
      {
	DB_TIMESTAMPTZ ts_tz_res, ts_tz_fixed, *ts_tz_p;

	ts_tz_p = db_get_timestamptz (datetime_p);
	err = qdata_subtract_utime ((DB_UTIME) bi, ts_tz_p->timestamp, result_p);
	if (err != NO_ERROR)
	  {
	    return err;
	  }
	ts_tz_res.timestamp = *db_get_timestamp (result_p);
	ts_tz_res.tz_id = ts_tz_p->tz_id;
	err = tz_timestamptz_fix_zone (&ts_tz_res, &ts_tz_fixed);
	if (err != NO_ERROR)
	  {
	    return err;
	  }
	db_make_timestamptz (result_p, &ts_tz_fixed);
	return NO_ERROR;
      }

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
      {
	DB_DATETIME datetime_tmp, dt_local;
	int i;

	if (type == DB_TYPE_BIGINT)
	  {
	    /* the BIGINT subtraction has no case for it: no value */
	    return NO_ERROR;
	  }
	i = (int) bi;
	datetime_tmp.date = i / MILLISECONDS_OF_ONE_DAY;
	datetime_tmp.time = i % MILLISECONDS_OF_ONE_DAY;
	if (type2 == DB_TYPE_DATETIME)
	  {
	    return qdata_subtract_datetime (&datetime_tmp, db_get_datetime (datetime_p), result_p);
	  }
	if (type2 == DB_TYPE_DATETIMELTZ)
	  {
	    err = tz_datetimeltz_to_local (db_get_datetime (datetime_p), &dt_local);
	  }
	else
	  {
	    DB_DATETIMETZ dt_tz = *db_get_datetimetz (datetime_p);

	    err = tz_utc_datetimetz_to_local (&dt_tz.datetime, &dt_tz.tz_id, &dt_local);
	  }
	if (err != NO_ERROR)
	  {
	    /* the INTEGER subtraction left the error set and answered no value */
	    return NO_ERROR;
	  }
	return qdata_subtract_datetime (&datetime_tmp, &dt_local, result_p);
      }

    case DB_TYPE_DATE:
      {
	DB_DATE *date = db_get_date (datetime_p);
	unsigned int u1, u2, utmp;
	int day, month, year, hour, minute, second;

	u1 = (unsigned int) bi;
	u2 = (unsigned int) *date;
	utmp = u1 - u2;

	if (bi < 0 || OR_CHECK_UNS_SUB_UNDERFLOW (u1, u2, utmp))
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DATE_UNDERFLOW, 0);
	    return ER_FAILED;
	  }

	if (type == DB_TYPE_SHORT)
	  {
	    db_time_decode (&utmp, &hour, &minute, &second);
	    db_make_time (result_p, hour, minute, second);
	  }
	else
	  {
	    db_date_decode (&utmp, &month, &day, &year);
	    db_make_date (result_p, month, day, year);
	  }
	return NO_ERROR;
      }

    default:
      return NO_ERROR;
    }
}

/*
 * qdata_subtract_datetime_value () - a subtraction over a date or time operand (DOMAIN_ARITH_DATE): the date and time
 *   subtractions by the first operand's type
 *   return: NO_ERROR, or ER_code
 */
static int
qdata_subtract_datetime_value (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int error = NO_ERROR;
  const DB_TYPE type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);

  if (TP_IS_NUMERIC_TYPE (type1))
    {
      return qdata_subtract_number_to_datetime (dbval1_p, dbval2_p, result_p);
    }

  switch (type1)
    {
    case DB_TYPE_TIME:
      error = qdata_subtract_time_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_TIMESTAMP:
      error = qdata_subtract_utime_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_TIMESTAMPLTZ:
      error = qdata_subtract_timestampltz_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_TIMESTAMPTZ:
      error = qdata_subtract_timestamptz_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_DATETIME:
      error = qdata_subtract_datetime_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_DATETIMELTZ:
      {
	/* create a datetime with TZ using session timezone */
	DB_VALUE tmp_val;
	DB_DATETIMETZ dt_tz1;

	dt_tz1.datetime = *db_get_datetime (dbval1_p);
	error = tz_create_session_tzid_for_datetime (&dt_tz1.datetime, true, &dt_tz1.tz_id);
	if (error != NO_ERROR)
	  {
	    break;
	  }

	db_make_datetimetz (&tmp_val, &dt_tz1);

	error = qdata_subtract_datetimetz_to_dbval (&tmp_val, dbval2_p, result_p, domain_p);
      }
      break;

    case DB_TYPE_DATETIMETZ:
      error = qdata_subtract_datetimetz_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_DATE:
      error = qdata_subtract_date_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    default:
      /* the rule names a date or time operand */
      assert (false);
      break;
    }

  return error;
}

static int
qdata_multiply_short (short s1, short s2, DB_VALUE * result_p)
{
  short stmp;

  if (OR_MULT_OVERFLOW (s1, s2, &stmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

  db_make_short (result_p, stmp);

  return NO_ERROR;
}

static int
qdata_multiply_int (int i1, int i2, DB_VALUE * result_p)
{
  int itmp;

  if (OR_MULT_OVERFLOW (i1, i2, &itmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

  db_make_int (result_p, itmp);
  return NO_ERROR;
}

static int
qdata_multiply_bigint (DB_BIGINT bi1, DB_BIGINT bi2, DB_VALUE * result_p)
{
  DB_BIGINT bitmp;

  if (OR_MULT_OVERFLOW (bi1, bi2, &bitmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

  db_make_bigint (result_p, bitmp);
  return NO_ERROR;
}

static int
qdata_multiply_float (float f1, float f2, DB_VALUE * result_p)
{
  float ftmp;

  ftmp = f1 * f2;

  if (OR_CHECK_FLOAT_OVERFLOW (ftmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

  db_make_float (result_p, ftmp);
  return NO_ERROR;
}

static int
qdata_multiply_double (double d1, double d2, DB_VALUE * result_p)
{
  double dtmp;

  dtmp = d1 * d2;

  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

  db_make_double (result_p, dtmp);
  return NO_ERROR;
}

static int
qdata_multiply_monetary (DB_VALUE * monetary_val_p, double d, DB_VALUE * result_p)
{
  double dtmp;

  dtmp = (db_get_monetary (monetary_val_p))->amount * d;

  if (OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

  db_make_monetary (result_p, (db_get_monetary (monetary_val_p))->type, dtmp);

  return NO_ERROR;
}

static int
qdata_multiply_sequence_to_dbval (DB_VALUE * seq_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_SET *set_tmp = NULL;
#if !defined(NDEBUG)
  DB_TYPE type1, type2;
#endif

#if !defined(NDEBUG)
  type1 = DB_VALUE_DOMAIN_TYPE (seq_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  assert (TP_IS_SET_TYPE (type1));
  assert (TP_IS_SET_TYPE (type2));
#endif

  if (set_intersection (db_get_set (seq_val_p), db_get_set (dbval_p), &set_tmp, domain_p) < 0)
    {
      return ER_FAILED;
    }

  set_make_collection (result_p, set_tmp);
  return NO_ERROR;
}

/*
 * qdata_multiply_dbval () - the multiplication of two values: qdata_arith_dbval over the ARITH rule resolved from their types
 *   return: NO_ERROR, or ER_code
 */
int
qdata_multiply_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  return qdata_arith_dbval (T_MUL, dbval1_p, dbval2_p, result_p, domain_p);
}

static bool
qdata_is_divided_zero (DB_VALUE * dbval_p)
{
  DB_TYPE type;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return db_get_short (dbval_p) == 0;

    case DB_TYPE_INTEGER:
      return db_get_int (dbval_p) == 0;

    case DB_TYPE_BIGINT:
      return db_get_bigint (dbval_p) == 0;

    case DB_TYPE_FLOAT:
      return fabs ((double) db_get_float (dbval_p)) <= DBL_EPSILON;

    case DB_TYPE_DOUBLE:
      return fabs (db_get_double (dbval_p)) <= DBL_EPSILON;

    case DB_TYPE_MONETARY:
      return db_get_monetary (dbval_p)->amount <= DBL_EPSILON;

    case DB_TYPE_NUMERIC:
      return numeric_db_value_is_zero (dbval_p);

    default:
      break;
    }

  return false;
}

static int
qdata_divide_short (short s1, short s2, DB_VALUE * result_p)
{
  short stmp;

  if (OR_CHECK_SHORT_DIV_OVERFLOW (s1, s2))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
      return ER_FAILED;
    }

  stmp = s1 / s2;
  db_make_short (result_p, stmp);

  return NO_ERROR;
}

static int
qdata_divide_int (int i1, int i2, DB_VALUE * result_p)
{
  int itmp;

  if (OR_CHECK_INT_DIV_OVERFLOW (i1, i2))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
      return ER_FAILED;
    }

  itmp = i1 / i2;
  db_make_int (result_p, itmp);

  return NO_ERROR;
}

static int
qdata_divide_bigint (DB_BIGINT bi1, DB_BIGINT bi2, DB_VALUE * result_p)
{
  DB_BIGINT bitmp;

  if (OR_CHECK_BIGINT_DIV_OVERFLOW (bi1, bi2))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
      return ER_FAILED;
    }

  bitmp = bi1 / bi2;
  db_make_bigint (result_p, bitmp);

  return NO_ERROR;
}

static int
qdata_divide_float (float f1, float f2, DB_VALUE * result_p)
{
  float ftmp;

  ftmp = f1 / f2;

  if (OR_CHECK_FLOAT_OVERFLOW (ftmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
      return ER_FAILED;
    }

  db_make_float (result_p, ftmp);
  return NO_ERROR;
}

static int
qdata_divide_double (double d1, double d2, DB_VALUE * result_p, bool is_check_overflow)
{
  double dtmp;

  dtmp = d1 / d2;

  if (is_check_overflow && OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
      return ER_FAILED;
    }

  db_make_double (result_p, dtmp);
  return NO_ERROR;
}

static int
qdata_divide_monetary (double d1, double d2, DB_CURRENCY currency, DB_VALUE * result_p, bool is_check_overflow)
{
  double dtmp;

  dtmp = d1 / d2;

  if (is_check_overflow && OR_CHECK_DOUBLE_OVERFLOW (dtmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
      return ER_FAILED;
    }

  db_make_monetary (result_p, currency, dtmp);
  return NO_ERROR;
}

/*
 * qdata_divide_dbval () - the division of two values: qdata_arith_dbval over the ARITH rule resolved from their types
 *   return: NO_ERROR, or ER_code
 */
int
qdata_divide_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  return qdata_arith_dbval (T_DIV, dbval1_p, dbval2_p, result_p, domain_p);
}

/*
 * The number of two numbers (DOMAIN_ARITH_NUMBER): both operands read as the C type of the result type, promoted as
 * the typed operators promoted a narrower operand - a short or an int as a float, a bigint as a double, a NUMERIC as a
 * double through numeric_db_value_coerce_from_num - then the typed leaf of the result type. A reader answers false for
 * a value of a type the result type does not read (an operand its operand coercion did not convert): the operator
 * leaves no value then, as the typed operators did.
 */
static bool
qdata_number_as_short (DB_VALUE * value, short *s)
{
  if (DB_VALUE_DOMAIN_TYPE (value) != DB_TYPE_SHORT)
    {
      return false;
    }
  *s = db_get_short (value);
  return true;
}

static bool
qdata_number_as_int (DB_VALUE * value, int *i)
{
  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_SHORT:
      *i = db_get_short (value);
      return true;
    case DB_TYPE_INTEGER:
      *i = db_get_int (value);
      return true;
    default:
      return false;
    }
}

static bool
qdata_number_as_bigint (DB_VALUE * value, DB_BIGINT * bi)
{
  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_SHORT:
      *bi = db_get_short (value);
      return true;
    case DB_TYPE_INTEGER:
      *bi = db_get_int (value);
      return true;
    case DB_TYPE_BIGINT:
      *bi = db_get_bigint (value);
      return true;
    default:
      return false;
    }
}

static bool
qdata_number_as_float (DB_VALUE * value, float *f)
{
  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_SHORT:
      *f = (float) db_get_short (value);
      return true;
    case DB_TYPE_INTEGER:
      *f = (float) db_get_int (value);
      return true;
    case DB_TYPE_BIGINT:
      *f = (float) db_get_bigint (value);
      return true;
    case DB_TYPE_FLOAT:
      *f = db_get_float (value);
      return true;
    default:
      return false;
    }
}

static bool
qdata_number_as_double (DB_VALUE * value, double *d)
{
  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_SHORT:
      *d = db_get_short (value);
      return true;
    case DB_TYPE_INTEGER:
      *d = db_get_int (value);
      return true;
    case DB_TYPE_BIGINT:
      *d = (double) db_get_bigint (value);
      return true;
    case DB_TYPE_FLOAT:
      *d = db_get_float (value);
      return true;
    case DB_TYPE_DOUBLE:
      *d = db_get_double (value);
      return true;
    case DB_TYPE_NUMERIC:
      *d = qdata_coerce_numeric_to_double (value);
      return true;
    case DB_TYPE_MONETARY:
      *d = db_get_monetary (value)->amount;
      return true;
    default:
      return false;
    }
}

/*
 * qdata_number_numeric () - the NUMERIC of a NUMERIC with an integer or another NUMERIC: two NUMERICs by the float
 *   NUMERIC operations; a NUMERIC with an integer by the NUMERIC operations over the integer coerced to NUMERIC
 *   (qdata_coerce_dbval_to_numeric), the addition taking the coerced integer first and the others their operands in
 *   order, as the typed operators did
 *   return: NO_ERROR, or the operator's overflow error
 */
static int
qdata_number_numeric (OPERATOR_TYPE opcode, DB_VALUE * value1, DB_VALUE * value2, DB_VALUE * result_p)
{
  const DB_TYPE type1 = DB_VALUE_DOMAIN_TYPE (value1);
  const DB_TYPE type2 = DB_VALUE_DOMAIN_TYPE (value2);
  DB_VALUE coerced;
  int error;

  if (type1 == DB_TYPE_NUMERIC && type2 == DB_TYPE_NUMERIC)
    {
      switch (opcode)
	{
	case T_ADD:
	  error = float_numeric_db_value_add (value1, value2, result_p);
	  break;
	case T_SUB:
	  error = float_numeric_db_value_sub (value1, value2, result_p);
	  break;
	case T_MUL:
	  error = float_numeric_db_value_mul (value1, value2, result_p);
	  break;
	default:
	  error = float_numeric_db_value_div (value1, value2, result_p);
	  break;
	}
    }
  else
    {
      const bool numeric_first = type1 == DB_TYPE_NUMERIC;
      DB_VALUE *numeric = numeric_first ? value1 : value2;
      DB_VALUE *other = numeric_first ? value2 : value1;

      if (DB_VALUE_DOMAIN_TYPE (numeric) != DB_TYPE_NUMERIC
	  || !TP_IS_DISCRETE_NUMBER_TYPE (DB_VALUE_DOMAIN_TYPE (other)))
	{
	  /* no value: an operand its operand coercion did not convert */
	  return NO_ERROR;
	}
      qdata_coerce_dbval_to_numeric (other, &coerced);
      switch (opcode)
	{
	case T_ADD:
	  error = numeric_db_value_add (&coerced, numeric, result_p);
	  break;
	case T_SUB:
	  error = numeric_first ? numeric_db_value_sub (value1, &coerced, result_p)
	    : numeric_db_value_sub (&coerced, value2, result_p);
	  break;
	case T_MUL:
	  error = numeric_db_value_mul (numeric, &coerced, result_p);
	  break;
	default:
	  error = numeric_first ? numeric_db_value_div (value1, &coerced, result_p)
	    : numeric_db_value_div (&coerced, value2, result_p);
	  break;
	}
    }

  if (error != NO_ERROR)
    {
      const int overflow =
	opcode == T_ADD ? ER_QPROC_OVERFLOW_ADDITION : opcode == T_SUB ? ER_QPROC_OVERFLOW_SUBTRACTION : opcode ==
	T_MUL ? ER_QPROC_OVERFLOW_MULTIPLICATION : ER_QPROC_OVERFLOW_DIVISION;

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, overflow, 0);
      return overflow;
    }
  return NO_ERROR;
}

/*
 * qdata_number_monetary () - the MONETARY of a MONETARY with a number: in the currency of the MONETARY operand, the
 *   left one's when both are; a NUMERIC added to a MONETARY without the overflow check (qdata_add_numeric_to_monetary),
 *   an integer divisor of a MONETARY dividend without it too, as the typed operators did
 *   return: NO_ERROR, or the operator's overflow error
 */
static int
qdata_number_monetary (OPERATOR_TYPE opcode, DB_VALUE * value1, DB_VALUE * value2, DB_VALUE * result_p)
{
  const DB_TYPE type1 = DB_VALUE_DOMAIN_TYPE (value1);
  const DB_TYPE type2 = DB_VALUE_DOMAIN_TYPE (value2);
  DB_VALUE *monetary = type1 == DB_TYPE_MONETARY ? value1 : value2;
  double d1, d2;

  if (DB_VALUE_DOMAIN_TYPE (monetary) != DB_TYPE_MONETARY || !qdata_number_as_double (value1, &d1)
      || !qdata_number_as_double (value2, &d2))
    {
      /* no value: an operand its operand coercion did not convert */
      return NO_ERROR;
    }

  switch (opcode)
    {
    case T_ADD:
      if (type1 == DB_TYPE_NUMERIC || type2 == DB_TYPE_NUMERIC)
	{
	  return qdata_add_numeric_to_monetary (type1 == DB_TYPE_NUMERIC ? value1 : value2, monetary, result_p);
	}
      return qdata_add_monetary (d1, d2, db_get_monetary (monetary)->type, result_p);

    case T_SUB:
      return qdata_subtract_monetary (d1, d2, db_get_monetary (monetary)->type, result_p);

    case T_MUL:
      return qdata_multiply_monetary (monetary, type1 == DB_TYPE_MONETARY ? d2 : d1, result_p);

    default:
      return qdata_divide_monetary (d1, d2, db_get_monetary (monetary)->type, result_p,
				    !(type1 == DB_TYPE_MONETARY && TP_IS_DISCRETE_NUMBER_TYPE (type2)));
    }
}

/*
 * qdata_number_operator () - the number of two numbers: the result type's leaf over the operands read as that type
 *   return: NO_ERROR, or ER_code
 *   result_type(in): the type the ARITH rule resolved for the pair (domain_arith_number)
 */
static int
qdata_number_operator (OPERATOR_TYPE opcode, DB_TYPE result_type, DB_VALUE * value1, DB_VALUE * value2,
		       DB_VALUE * result_p)
{
  switch (result_type)
    {
    case DB_TYPE_SHORT:
      {
	short s1, s2;

	if (!qdata_number_as_short (value1, &s1) || !qdata_number_as_short (value2, &s2))
	  {
	    return NO_ERROR;
	  }
	switch (opcode)
	  {
	  case T_ADD:
	    return qdata_add_short (s1, s2, result_p);
	  case T_SUB:
	    return qdata_subtract_short (s1, s2, result_p);
	  case T_MUL:
	    return qdata_multiply_short (s1, s2, result_p);
	  default:
	    return qdata_divide_short (s1, s2, result_p);
	  }
      }

    case DB_TYPE_INTEGER:
      {
	int i1, i2;

	if (!qdata_number_as_int (value1, &i1) || !qdata_number_as_int (value2, &i2))
	  {
	    return NO_ERROR;
	  }
	switch (opcode)
	  {
	  case T_ADD:
	    return qdata_add_int (i1, i2, result_p);
	  case T_SUB:
	    return qdata_subtract_int (i1, i2, result_p);
	  case T_MUL:
	    return qdata_multiply_int (i1, i2, result_p);
	  default:
	    return qdata_divide_int (i1, i2, result_p);
	  }
      }

    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bi1, bi2;

	if (!qdata_number_as_bigint (value1, &bi1) || !qdata_number_as_bigint (value2, &bi2))
	  {
	    return NO_ERROR;
	  }
	switch (opcode)
	  {
	  case T_ADD:
	    return qdata_add_bigint (bi1, bi2, result_p);
	  case T_SUB:
	    return qdata_subtract_bigint (bi1, bi2, result_p);
	  case T_MUL:
	    return qdata_multiply_bigint (bi1, bi2, result_p);
	  default:
	    return qdata_divide_bigint (bi1, bi2, result_p);
	  }
      }

    case DB_TYPE_FLOAT:
      {
	float f1, f2;

	if (!qdata_number_as_float (value1, &f1) || !qdata_number_as_float (value2, &f2))
	  {
	    return NO_ERROR;
	  }
	switch (opcode)
	  {
	  case T_ADD:
	    return qdata_add_float (f1, f2, result_p);
	  case T_SUB:
	    return qdata_subtract_float (f1, f2, result_p);
	  case T_MUL:
	    return qdata_multiply_float (f1, f2, result_p);
	  default:
	    return qdata_divide_float (f1, f2, result_p);
	  }
      }

    case DB_TYPE_DOUBLE:
      {
	double d1, d2;

	if (!qdata_number_as_double (value1, &d1) || !qdata_number_as_double (value2, &d2))
	  {
	    return NO_ERROR;
	  }
	switch (opcode)
	  {
	  case T_ADD:
	    return qdata_add_double (d1, d2, result_p);
	  case T_SUB:
	    return qdata_subtract_double (d1, d2, result_p);
	  case T_MUL:
	    return qdata_multiply_double (d1, d2, result_p);
	  default:
	    {
	      /* the typed divisions checked a DOUBLE quotient for overflow by the divisor's type: a DOUBLE, or a FLOAT
	       * under a dividend that is not a NUMERIC */
	      const DB_TYPE type1 = DB_VALUE_DOMAIN_TYPE (value1);
	      const DB_TYPE type2 = DB_VALUE_DOMAIN_TYPE (value2);

	      return qdata_divide_double (d1, d2, result_p,
					  type2 == DB_TYPE_DOUBLE || (type2 == DB_TYPE_FLOAT
								      && type1 != DB_TYPE_NUMERIC));
	    }
	  }
      }

    case DB_TYPE_NUMERIC:
      return qdata_number_numeric (opcode, value1, value2, result_p);

    case DB_TYPE_MONETARY:
      return qdata_number_monetary (opcode, value1, value2, result_p);

    default:
      assert (false);
      return NO_ERROR;
    }
}

/*
 * qdata_collection_operator () - two collections (DOMAIN_ARITH_COLLECTION): their union or sequence append,
 *   difference or intersection into the result domain; without one, the partial resolve of the rule's type (the
 *   fetch resolves the full domain from the result's value)
 *   return: NO_ERROR, or ER_code
 */
static int
qdata_collection_operator (OPERATOR_TYPE opcode, DB_TYPE result_type, DB_VALUE * value1, DB_VALUE * value2,
			   DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  if (domain_p == NULL)
    {
      domain_p = tp_domain_resolve_default (result_type);
    }
  switch (opcode)
    {
    case T_ADD:
      return qdata_add_sequence_to_dbval (value1, value2, result_p, domain_p);
    case T_SUB:
      return qdata_subtract_sequence_to_dbval (value1, value2, result_p, domain_p);
    default:
      assert (opcode == T_MUL);
      return qdata_multiply_sequence_to_dbval (value1, value2, result_p, domain_p);
    }
}

#if !defined (NDEBUG)
/*
 * qdata_assert_arith_resolved () - debug cross-check before the operator: its two values come coerced - the ARITH rule
 *   over their own types converts neither (the operator casts nothing; a caller that did not plan the operand
 *   coercion fails here)
 */
static void
qdata_assert_arith_resolved (OPERATOR_TYPE opcode, DB_TYPE left_target, DB_TYPE right_target,
			     const DB_VALUE * dbval1_p, const DB_VALUE * dbval2_p)
{
  const DB_TYPE type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  const DB_TYPE type2 = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  if (left_target != type1 || right_target != type2)
    {
      fprintf (stderr, "unplanned pre-cast: opcode=%d values=%d/%d targets=%d/%d\n", (int) opcode, (int) type1,
	       (int) type2, (int) left_target, (int) right_target);
    }
  assert (left_target == type1 && right_target == type2);
}

/*
 * qdata_assert_arith_value () - debug cross-check after the operator: the value it made has the type the rule named.
 *   A string or bit result matches by type family (db_string_concatenate gives a CHAR for an empty result), a
 *   collection takes its result domain's type, and compat_mode mysql reads a date or time result as the number the
 *   result value held.
 */
static void
qdata_assert_arith_value (const DOMAIN_ARITH * arith, const DB_VALUE * result_p)
{
  if (DB_IS_NULL (result_p) || arith->kind == DOMAIN_ARITH_COLLECTION
      || prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
    {
      return;
    }
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (result_p);
  if (type != arith->type && !(TP_IS_CHAR_TYPE (type) && TP_IS_CHAR_TYPE (arith->type))
      && !(TP_IS_BIT_TYPE (type) && TP_IS_BIT_TYPE (arith->type)))
    {
      fprintf (stderr, "arithmetic value: kind=%d type=%d resolved=%d\n", (int) arith->kind, (int) type,
	       (int) arith->type);
    }
  assert (type == arith->type || (TP_IS_CHAR_TYPE (type) && TP_IS_CHAR_TYPE (arith->type))
	  || (TP_IS_BIT_TYPE (type) && TP_IS_BIT_TYPE (arith->type)));
}
#endif

/*
 * qdata_arith_dbval () - an addition, subtraction, multiplication or division of two values, as the ARITH rule names
 *   it over their types (domain_arith_rule): the kind names the operator that computes the value and the type is the
 *   value's. The operands come in the types their operand coercion gave them (qdata_coerce_arith_operands) and the
 *   operator casts nothing: over them the rule converts nothing, and the value's type it names is the resolver's
 *   before any row. The rule is read over the values, not a plan: a value pointer, an accumulator or a list column
 *   may hold a type its compiled domain does not describe, and the operator computes what the values are.
 *   return: NO_ERROR, or ER_code
 *   opcode(in): T_ADD, T_SUB, T_MUL or T_DIV
 *   domain_p(in): the domain the result is coerced to; NULL none
 */
int
qdata_arith_dbval (OPERATOR_TYPE opcode, DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p,
		   TP_DOMAIN * domain_p)
{
  DOMAIN_ARITH resolved;
  const DOMAIN_ARITH *arith = &resolved;
  DB_TYPE left_target, right_target;
  int error = NO_ERROR;

  assert (opcode == T_ADD || opcode == T_SUB || opcode == T_MUL || opcode == T_DIV);

  if (domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL)
    {
      return NO_ERROR;
    }

  (void) domain_arith_rule (opcode, dbval1_p != NULL ? DB_VALUE_DOMAIN_TYPE (dbval1_p) : DB_TYPE_NULL,
			    dbval2_p != NULL ? DB_VALUE_DOMAIN_TYPE (dbval2_p) : DB_TYPE_NULL, &left_target,
			    &right_target, &resolved);

  if (arith->kind == DOMAIN_ARITH_CONCAT)
    {
      /* plus as concatenation, which answers a NULL operand itself */
      return qdata_strcat_dbval (dbval1_p, dbval2_p, result_p, domain_p);
    }

  if (arith->kind == DOMAIN_ARITH_NO_VALUE || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

#if !defined (NDEBUG)
  qdata_assert_arith_resolved (opcode, left_target, right_target, dbval1_p, dbval2_p);
#endif

  if (opcode == T_DIV && qdata_is_divided_zero (dbval2_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_ZERO_DIVIDE, 0);
      return ER_FAILED;
    }

  if ((opcode == T_ADD || opcode == T_SUB) && arith->kind != DOMAIN_ARITH_NUMBER
      && (qdata_is_zero_value_date (dbval1_p) || qdata_is_zero_value_date (dbval2_p)))
    {
      /* an addition or subtraction with a zero date returns null */
      db_make_null (result_p);
      if (!prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ATTEMPT_TO_USE_ZERODATE, 0);
	  return ER_ATTEMPT_TO_USE_ZERODATE;
	}
      return NO_ERROR;
    }

  switch (arith->kind)
    {
    case DOMAIN_ARITH_NUMBER:
      error = qdata_number_operator (opcode, arith->type, dbval1_p, dbval2_p, result_p);
      break;

    case DOMAIN_ARITH_DATE:
      error = opcode == T_ADD ? qdata_add_datetime_value (dbval1_p, dbval2_p, result_p, domain_p)
	: qdata_subtract_datetime_value (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DOMAIN_ARITH_STRING:
      error = qdata_add_chars_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DOMAIN_ARITH_COLLECTION:
      error = qdata_collection_operator (opcode, arith->type, dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DOMAIN_ARITH_REJECT_OR_NULL:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  break;
	}
      [[fallthrough]];

    case DOMAIN_ARITH_REJECT:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;

    default:
      assert (false);
      break;
    }

  if (error != NO_ERROR)
    {
      return error;
    }

#if !defined (NDEBUG)
  qdata_assert_arith_value (arith, result_p);
#endif

  return qdata_coerce_result_to_domain (result_p, domain_p);
}

/*
 * qdata_unary_minus_dbval () -
 *   return: NO_ERROR, or ER_code
 *   res(out)   : Resultant db_value node
 *   dbval1(in) : First db_value node
 *
 * Note: Take unary minus of db_value.
 */
int
qdata_unary_minus_dbval (DB_VALUE * result_p, DB_VALUE * dbval_p)
{
  DB_TYPE res_type;
  short stmp;
  int itmp;
  DB_BIGINT bitmp;
  double dtmp;
  DB_VALUE cast_value;
  int er_status = NO_ERROR;

  res_type = DB_VALUE_DOMAIN_TYPE (dbval_p);
  if (res_type == DB_TYPE_NULL || DB_IS_NULL (dbval_p))
    {
      return NO_ERROR;
    }

  switch (res_type)
    {
    case DB_TYPE_INTEGER:
      itmp = db_get_int (dbval_p);
      if (itmp == INT_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_UMINUS, 0);
	  return ER_QPROC_OVERFLOW_UMINUS;
	}
      db_make_int (result_p, (-1) * itmp);
      break;

    case DB_TYPE_BIGINT:
      bitmp = db_get_bigint (dbval_p);
      if (bitmp == DB_BIGINT_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_UMINUS, 0);
	  return ER_QPROC_OVERFLOW_UMINUS;
	}
      db_make_bigint (result_p, (-1) * bitmp);
      break;

    case DB_TYPE_FLOAT:
      db_make_float (result_p, (-1) * db_get_float (dbval_p));
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      er_status = tp_value_str_auto_cast_to_number (dbval_p, &cast_value, &res_type);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && res_type != DB_TYPE_DOUBLE))
	{
	  return er_status;
	}

      assert (res_type == DB_TYPE_DOUBLE);

      dbval_p = &cast_value;

      [[fallthrough]];

    case DB_TYPE_DOUBLE:
      db_make_double (result_p, (-1) * db_get_double (dbval_p));
      break;

    case DB_TYPE_NUMERIC:
      {
	bool is_float_numeric = false;
	int precision = 0, scale = 0;
	db_get_numeric_precision_and_scale (dbval_p, &precision, &scale, &is_float_numeric);

	bool is_value_negative = !dbval_p->domain.numeric_info.is_value_negative;
	if (is_value_negative && numeric_db_value_is_zero (dbval_p))
	  {
	    /* Prevent -0; zero is always treated as positive. */
	    is_value_negative = false;
	  }

	db_make_numeric (result_p, db_get_numeric (dbval_p), precision, scale, DB_NUMERIC_BUF_SIZE, is_value_negative,
			 is_float_numeric);
      }
      break;

    case DB_TYPE_MONETARY:
      dtmp = (-1) * (db_get_monetary (dbval_p))->amount;
      db_make_monetary (result_p, (db_get_monetary (dbval_p))->type, dtmp);
      break;

    case DB_TYPE_SHORT:
      stmp = db_get_short (dbval_p);
      if (stmp == SHRT_MIN)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_UMINUS, 0);
	  return ER_QPROC_OVERFLOW_UMINUS;
	}
      db_make_short (result_p, (-1) * stmp);
      break;

    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_status = ER_QPROC_INVALID_DATATYPE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, er_status, 0);
	}
      break;
    }

  return er_status;
}

/*
 * qdata_extract_dbval () -
 *   return: NO_ERROR, or ER_code
 *   extr_operand(in)   : Specifies datetime field to be extracted
 *   dbval(in)  : Extract source db_value node
 *   res(out)   : Resultant db_value node
 *   domain(in) :
 *
 * Note: Extract a datetime field from db_value.
 */
int
qdata_extract_dbval (const MISC_OPERAND extr_operand, DB_VALUE * dbval_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  if (db_string_extract_dbval (extr_operand, dbval_p, result_p, domain_p) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * qdata_strcat_dbval () -
 *   return:
 *   dbval1(in) :
 *   dbval2(in) :
 *   res(in)    :
 *   domain(in) :
 */
int
qdata_strcat_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  DB_TYPE type1, type2;
  int error = NO_ERROR;
  DB_VALUE cast_value1;
  DB_VALUE cast_value2;
  TP_DOMAIN *cast_dom1 = NULL;
  TP_DOMAIN *cast_dom2 = NULL;
  TP_DOMAIN_STATUS dom_status;

  if (domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL)
    {
      return NO_ERROR;
    }

  type1 = dbval1_p ? DB_VALUE_DOMAIN_TYPE (dbval1_p) : DB_TYPE_NULL;
  type2 = dbval2_p ? DB_VALUE_DOMAIN_TYPE (dbval2_p) : DB_TYPE_NULL;

  /* string STRCAT date: cast date to string, concat as strings */
  /* string STRCAT number: cast number to string, concat as strings */
  if (TP_IS_CHAR_TYPE (type1) && (TP_IS_DATE_OR_TIME_TYPE (type2) || TP_IS_NUMERIC_TYPE (type2)))
    {
      cast_dom2 = tp_domain_resolve_value (dbval1_p, NULL);
    }
  else if ((TP_IS_DATE_OR_TIME_TYPE (type1) || TP_IS_NUMERIC_TYPE (type1)) && TP_IS_CHAR_TYPE (type2))
    {
      cast_dom1 = tp_domain_resolve_value (dbval2_p, NULL);
    }

  db_make_null (&cast_value1);
  db_make_null (&cast_value2);

  if (cast_dom1 != NULL)
    {
      dom_status = tp_value_auto_cast (dbval1_p, &cast_value1, cast_dom1);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval1_p, cast_dom1);
	  pr_clear_value (&cast_value1);
	  pr_clear_value (&cast_value2);
	  return error;
	}
      dbval1_p = &cast_value1;
    }

  if (cast_dom2 != NULL)
    {
      dom_status = tp_value_auto_cast (dbval2_p, &cast_value2, cast_dom2);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval2_p, cast_dom2);
	  pr_clear_value (&cast_value1);
	  pr_clear_value (&cast_value2);
	  return error;
	}
      dbval2_p = &cast_value2;
    }

  type1 = dbval1_p ? DB_VALUE_DOMAIN_TYPE (dbval1_p) : DB_TYPE_NULL;
  type2 = dbval2_p ? DB_VALUE_DOMAIN_TYPE (dbval2_p) : DB_TYPE_NULL;

  if (DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      /* ORACLE7 ServerSQL Language Reference Manual 3-4; Although ORACLE treats zero-length character strings as
       * nulls, concatenating a zero-length character string with another operand always results in the other operand,
       * rather than a null. However, this may not continue to be true in future versions of ORACLE. To concatenate an
       * expression that might be null, use the NVL function to explicitly convert the expression to a zero-length
       * string. */
      if (!prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING))
	{
	  return NO_ERROR;
	}

      if ((DB_IS_NULL (dbval1_p) && QSTR_IS_ANY_CHAR_OR_BIT (type2))
	  || (DB_IS_NULL (dbval2_p) && QSTR_IS_ANY_CHAR_OR_BIT (type1)))
	{
	  ;			/* go ahead */
	}
      else
	{
	  return NO_ERROR;
	}
    }

  switch (type1)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_NUMERIC:
    case DB_TYPE_MONETARY:
      {
	/* a number first operand adds, as the typed additions did: a number or a date or time second operand, no
	 * value for any other */
	DB_TYPE left_target, right_target;
	DOMAIN_ARITH arith;

	(void) domain_arith_rule (T_ADD, type1, type2, &left_target, &right_target, &arith);
	if (arith.kind == DOMAIN_ARITH_NUMBER)
	  {
	    error = qdata_number_operator (T_ADD, arith.type, dbval1_p, dbval2_p, result_p);
	  }
	else if (arith.kind == DOMAIN_ARITH_DATE)
	  {
	    error = qdata_add_datetime_value (dbval1_p, dbval2_p, result_p, domain_p);
	  }
	break;
      }

    case DB_TYPE_NULL:
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      if (dbval1_p != NULL && dbval2_p != NULL)
	{
	  error = qdata_add_chars_to_dbval (dbval1_p, dbval2_p, result_p);
	}
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      if (!TP_IS_SET_TYPE (type2))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
      error = qdata_add_sequence_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_TIME:
      error = qdata_add_time_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      error = qdata_add_utime_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      if (error == NO_ERROR && type1 == DB_TYPE_TIMESTAMPLTZ)
	{
	  db_make_timestampltz (result_p, *db_get_timestamp (result_p));
	}
      break;

    case DB_TYPE_TIMESTAMPTZ:
      error = qdata_add_timestamptz_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      error = qdata_add_datetime_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      if (error != NO_ERROR && type1 == DB_TYPE_DATETIMELTZ)
	{
	  db_make_datetimeltz (result_p, db_get_datetime (result_p));
	}
      break;

    case DB_TYPE_DATETIMETZ:
      error = qdata_add_datetimetz_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_DATE:
      error = qdata_add_date_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      error = ER_FAILED;
      break;
    }

  if (error != NO_ERROR)
    {
      return error;
    }

  if (cast_dom1)
    {
      pr_clear_value (&cast_value1);
    }
  if (cast_dom2)
    {
      pr_clear_value (&cast_value2);
    }

  return qdata_coerce_result_to_domain (result_p, domain_p);
}

/*
 * MISCELLANEOUS
 */

/*
 * qdata_get_single_tuple_from_list_id () -
 *   return: NO_ERROR or error code
 *   list_id(in)        : List file identifier
 *   single_tuple(in)   : VAL_LIST
 */
int
qdata_get_single_tuple_from_list_id (THREAD_ENTRY * thread_p, qfile_list_id * list_id_p, val_list_node * single_tuple_p)
{
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  QFILE_LIST_SCAN_ID scan_id;
  const PR_TYPE *pr_type_p;
  bool is_null;
  TP_DOMAIN *domain_p;
  INT64 tuple_count;
  int value_count, i;
  QPROC_DB_VALUE_LIST value_list;
  int error_code;

  tuple_count = list_id_p->tuple_cnt;
  value_count = list_id_p->type_list.type_cnt;

  /* value_count can be greater than single_tuple_p->val_cnt when the subquery has a hidden column. Under normal
   * situation, those are same. */
  if (tuple_count > 1 || value_count < single_tuple_p->val_cnt)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_QRY_SINGLE_TUPLE, 0);
      return ER_QPROC_INVALID_QRY_SINGLE_TUPLE;
    }

  if (tuple_count == 1)
    {
      error_code = qfile_open_list_scan (list_id_p, &scan_id);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      if (qfile_scan_list_next (thread_p, &scan_id, &tuple_record, PEEK) != S_SUCCESS)
	{
	  qfile_close_scan (thread_p, &scan_id);
	  return ER_FAILED;
	}

      for (i = 0, value_list = single_tuple_p->valp; i < single_tuple_p->val_cnt; i++, value_list = value_list->next)
	{
	  domain_p = list_id_p->type_list.domp[i];
	  if (domain_p == NULL || domain_p->type == NULL)
	    {
	      qfile_close_scan (thread_p, &scan_id);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_QRY_SINGLE_TUPLE, 0);
	      return ER_QPROC_INVALID_QRY_SINGLE_TUPLE;
	    }

	  if (db_value_domain_init (value_list->val, TP_DOMAIN_TYPE (domain_p), domain_p->precision, domain_p->scale) !=
	      NO_ERROR)
	    {
	      qfile_close_scan (thread_p, &scan_id);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_QRY_SINGLE_TUPLE, 0);
	      return ER_QPROC_INVALID_QRY_SINGLE_TUPLE;
	    }

	  pr_type_p = domain_p->type;
	  if (pr_type_p == NULL)
	    {
	      qfile_close_scan (thread_p, &scan_id);
	      return ER_FAILED;
	    }

	  if (qfile_slot_read_column_value (&tuple_record, i, domain_p, value_list->val, true, &is_null) != NO_ERROR)
	    {
	      qfile_close_scan (thread_p, &scan_id);
	      return ER_FAILED;
	    }
	  if (is_null)
	    {
	      /* If value is NULL, properly initialize the result */
	      db_value_domain_init (value_list->val, pr_type_p->id, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	    }
	}

      qfile_close_scan (thread_p, &scan_id);
    }

  return NO_ERROR;
}

/*
 * qdata_get_valptr_type_list () -
 *   return: NO_ERROR, or ER_code
 *   valptr_list(in)    : Value pointer list
 *   type_list(out)     : Set to the result type list
 *
 * Note: Find the result type list of value pointer list and set to
 * type list.  Regu variables that are hidden columns are not
 * entered as part of the type list because they are not entered
 * in the list file.
 *
 * A column the compiler left variable takes the plan's domain for this execution: the list holds that domain
 * from its first tuple on, a column over a session variable read too. A variable column without one fails the
 * unresolved-domain check (execution).
 */
int
qdata_get_valptr_type_list (THREAD_ENTRY * thread_p, valptr_list_node * valptr_list_p,
			    qfile_tuple_value_type_list * type_list_p, const VAL_DESCR * vd)
{
  REGU_VARIABLE_LIST reg_var_p;
  int i, count;

  if (type_list_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1);
      return ER_FAILED;
    }

  reg_var_p = valptr_list_p->valptrp;
  count = 0;

  for (i = 0; i < valptr_list_p->valptr_cnt; i++)
    {
      if (!REGU_VARIABLE_IS_FLAGED (&reg_var_p->value, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  count++;
	}

      reg_var_p = reg_var_p->next;
    }

  type_list_p->type_cnt = count;
  type_list_p->domp = NULL;

  if (type_list_p->type_cnt != 0)
    {
      type_list_p->domp = (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *) * type_list_p->type_cnt);
      if (type_list_p->domp == NULL)
	{
	  return ER_FAILED;
	}
    }

  reg_var_p = valptr_list_p->valptrp;
  for (i = 0; i < type_list_p->type_cnt;)
    {
      if (!REGU_VARIABLE_IS_FLAGED (&reg_var_p->value, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  /* the column regu's domain now: its execution domain once this execution gave it one */
	  TP_DOMAIN *now = qexec_get_node_domain (vd, reg_var_p->value.domain, reg_var_p->value.plan_item);
	  const TP_DOMAIN *domain = qexec_consumer_domain (vd, now, reg_var_p->value.plan_item);
	  if (domain == NULL)
	    {
	      db_private_free_and_init (thread_p, type_list_p->domp);
	      return qexec_domain_unresolved (vd, reg_var_p->value.plan_item, reg_var_p->value.domain);
	    }
	  type_list_p->domp[i++] = (TP_DOMAIN *) domain;
	}

      reg_var_p = reg_var_p->next;
    }

  return NO_ERROR;
}

int
qdata_get_val_list_type_list (THREAD_ENTRY * thread_p, VAL_LIST * val_list, qfile_tuple_value_type_list * type_list_p)
{
  QPROC_DB_VALUE_LIST val_list_iterator;
  int val_list_index;

  if (type_list_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1);
      assert (false);
      return ER_FAILED;
    }

  type_list_p->type_cnt = val_list->val_cnt;
  if (type_list_p->type_cnt == 0)
    {
      type_list_p->domp = NULL;
      return NO_ERROR;
    }

  type_list_p->domp = (TP_DOMAIN **) malloc (sizeof (TP_DOMAIN *) * type_list_p->type_cnt);
  if (type_list_p->domp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (TP_DOMAIN *) * type_list_p->type_cnt);
      return ER_FAILED;
    }

  for (val_list_iterator = val_list->valp, val_list_index = 0; val_list_iterator != NULL;
       val_list_iterator = val_list_iterator->next, val_list_index++)
    {
      type_list_p->domp[val_list_index] = val_list_iterator->dom;
    }

  return NO_ERROR;
}

/*
 * qdata_get_dbval_from_constant_regu_variable () -
 *   return: DB_VALUE *, or NULL
 *   regu_var(in): Regulator Variable
 *   vd(in)      : Value descriptor
 *
 * Note: Find the db_value represented by regu_var node and
 *       return a pointer to it.
 *
 * Note: Regulator variable should point to only constant values.
 */
static DB_VALUE *
qdata_get_dbval_from_constant_regu_variable (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var_p,
					     VAL_DESCR * val_desc_p)
{
  DB_VALUE *peek_value_p;
  DB_TYPE dom_type, val_type;
  TP_DOMAIN_STATUS dom_status;
  int result;
  HL_HEAPID save_heapid = 0;

  assert (regu_var_p != NULL);
  assert (regu_var_p->domain != NULL);

  if (REGU_VARIABLE_IS_FLAGED (regu_var_p, REGU_VARIABLE_UPD_INS_LIST))
    {
      REGU_VARIABLE_SET_FLAG (regu_var_p, REGU_VARIABLE_STRICT_TYPE_CAST);
    }

  result = fetch_peek_dbval (thread_p, regu_var_p, val_desc_p, NULL, NULL, NULL, &peek_value_p);
  if (result != NO_ERROR)
    {
      return NULL;
    }

  if (!DB_IS_NULL (peek_value_p))
    {
      val_type = DB_VALUE_TYPE (peek_value_p);
      assert (val_type != DB_TYPE_NULL);

      /* the column's domain in this execution: the one its fetch took, or its plan's - the list was opened with the
       * plan's domains (qdata_get_valptr_type_list), so the value is cast to the same domain the column holds */
      TP_DOMAIN *now = qexec_get_node_domain (val_desc_p, regu_var_p->domain, regu_var_p->plan_item);
      TP_DOMAIN *domain = (TP_DOMAIN *) qexec_consumer_domain (val_desc_p, now, regu_var_p->plan_item);
      if (domain == NULL)
	{
	  domain = now;
	}
      dom_type = TP_DOMAIN_TYPE (domain);
      if (dom_type != DB_TYPE_NULL)
	{
	  assert (dom_type != DB_TYPE_NULL);

	  if (val_type == DB_TYPE_OID)
	    {
	      assert ((dom_type == DB_TYPE_OID) || (dom_type == DB_TYPE_VOBJ));
	    }
	  else if (val_type != dom_type
		   || (val_type == DB_TYPE_NUMERIC
		       && (peek_value_p->domain.numeric_info.precision != domain->precision
			   || peek_value_p->domain.numeric_info.scale != domain->scale)))
	    {
	      if (REGU_VARIABLE_IS_FLAGED (regu_var_p, REGU_VARIABLE_ANALYTIC_WINDOW))
		{
		  /* do not cast at here, is handled at analytic function evaluation later */
		  ;
		}
	      else
		{
		  if (REGU_VARIABLE_IS_FLAGED (regu_var_p, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
		    {
		      save_heapid = db_change_private_heap (thread_p, 0);
		    }

		  dom_status = tp_value_auto_cast (peek_value_p, peek_value_p, domain);
		  if (save_heapid != 0)
		    {
		      (void) db_change_private_heap (thread_p, save_heapid);
		      save_heapid = 0;
		    }
		  if (dom_status != DOMAIN_COMPATIBLE)
		    {
		      result = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_value_p, domain);
		      return NULL;
		    }
		  assert (dom_type == DB_VALUE_TYPE (peek_value_p)
			  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) && DB_IS_NULL (peek_value_p)));
		}
	    }
	}
    }

  return peek_value_p;
}

/*
 * qdata_convert_dbvals_to_set () -
 *   return: NO_ERROR, or ER_code
 *   stype(in)  : set type
 *   func(in)   : regu variable (guaranteed TYPE_FUNC)
 *   vd(in)     : Value descriptor
 *   obj_oid(in): object identifier
 *   tpl(in)    : list file tuple
 *
 * Note: Convert a list of vars into a sequence and return a pointer to it.
 */
static int
qdata_convert_dbvals_to_set (THREAD_ENTRY * thread_p, DB_TYPE stype, REGU_VARIABLE * regu_func_p,
			     VAL_DESCR * val_desc_p, OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec)
{
  DB_VALUE dbval, *result_p = NULL;
  DB_COLLECTION *collection_p = NULL;
  SETOBJ *setobj_p = NULL;
  int n, size;
  REGU_VARIABLE_LIST regu_var_p = NULL, operand = NULL;
  int error_code = NO_ERROR;
  TP_DOMAIN *domain_p = NULL;

  result_p = regu_func_p->value.funcp->value;
  operand = regu_func_p->value.funcp->operand;
  domain_p = qexec_get_node_domain (val_desc_p, regu_func_p->domain, regu_func_p->plan_item);
  db_make_null (&dbval);

  if (stype == DB_TYPE_SET)
    {
      collection_p = db_set_create_basic (NULL, NULL);
    }
  else if (stype == DB_TYPE_MULTISET)
    {
      collection_p = db_set_create_multi (NULL, NULL);
    }
  else if (stype == DB_TYPE_SEQUENCE || stype == DB_TYPE_VOBJ)
    {
      size = 0;
      for (regu_var_p = operand; regu_var_p; regu_var_p = regu_var_p->next)
	{
	  size++;
	}

      collection_p = db_seq_create (NULL, NULL, size);
    }
  else
    {
      return ER_FAILED;
    }

  error_code = set_get_setobj (collection_p, &setobj_p, 1);
  if (error_code != NO_ERROR || !setobj_p)
    {
      goto error;
    }

  /*
   * DON'T set the "set"'s domain if it's really a vobj; they don't
   * play by quite the same rules.  The domain coming in here is some
   * flavor of vobj domain,  which is definitely *not* what the
   * components of the sequence will be.  Putting the domain in here
   * evidently causes the vobj's to get packed up in list files in some
   * way that readers can't cope with.
   */
  if (stype != DB_TYPE_VOBJ)
    {
      setobj_put_domain (setobj_p, domain_p);
    }

  n = 0;
  while (operand)
    {
      if (fetch_copy_dbval (thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tplrec, &dbval) != NO_ERROR)
	{
	  goto error;
	}

      if ((stype == DB_TYPE_VOBJ) && (n == 2))
	{
	  if (DB_IS_NULL (&dbval))
	    {
	      set_free (collection_p);
	      return NO_ERROR;
	    }
	}

      /* using setobj_put_value transfers "ownership" of the db_value memory to the set. This avoids a redundant
       * clone/free. */
      error_code = setobj_put_value (setobj_p, n, &dbval);

      /*
       * if we attempt to add a duplicate value to a set,
       * clear the value, but do not set an error code
       */
      if (error_code == SET_DUPLICATE_VALUE)
	{
	  pr_clear_value (&dbval);
	  error_code = NO_ERROR;
	}

      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      operand = operand->next;
      n++;
    }

  set_make_collection (result_p, collection_p);
  if (stype == DB_TYPE_VOBJ)
    {
      db_value_alter_type (result_p, DB_TYPE_VOBJ);
    }

  return NO_ERROR;

error:
  pr_clear_value (&dbval);
  if (collection_p != NULL)
    {
      set_free (collection_p);
    }
  return ((error_code == NO_ERROR) ? ER_FAILED : error_code);
}

/*
 * qdata_evaluate_generic_function () - Evaluates a generic function.
 *   return: NO_ERROR, or ER_code
 *   funcp(in)  :
 *   vd(in)     :
 *   obj_oid(in)        :
 *   tpl(in)    :
 */
static int
qdata_evaluate_generic_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
				 OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_GENERIC_FUNCTION_FAILURE, 0);
  return ER_FAILED;
}

/*
 * qdata_get_class_of_function () -
 *   return: NO_ERROR, or ER_code
 *   funcp(in)  :
 *   vd(in)     :
 *   obj_oid(in)        :
 *   tpl(in)    :
 *
 * Note: This routine returns the class of its argument.
 */
static int
qdata_get_class_of_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
			     OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec)
{
  OID class_oid;
  OID *instance_oid_p;
  DB_VALUE *val_p, element;
  DB_TYPE type;
  int err;

  if (fetch_peek_dbval (thread_p, &function_p->operand->value, val_desc_p, NULL, obj_oid_p, tplrec, &val_p) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (DB_IS_NULL (val_p))
    {
      db_make_null (function_p->value);
      return NO_ERROR;
    }

  type = DB_VALUE_DOMAIN_TYPE (val_p);
  if (type == DB_TYPE_VOBJ)
    {
      /* grab the real oid */
      if (db_seq_get (db_get_set (val_p), 2, &element) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      val_p = &element;
      type = DB_VALUE_DOMAIN_TYPE (val_p);
    }

  if (type != DB_TYPE_OID)
    {
      return ER_FAILED;
    }

  instance_oid_p = db_get_oid (val_p);
  err = heap_get_class_oid (thread_p, instance_oid_p, &class_oid);
  if (err != S_SUCCESS)
    {
      ASSERT_ERROR_AND_SET (err);
      return err;
    }

  db_make_oid (function_p->value, &class_oid);

  return NO_ERROR;
}

/*
 * qdata_evaluate_function () -
 *   return: NO_ERROR, or ER_code
 *   func(in)   :
 *   vd(in)     :
 *   obj_oid(in)        :
 *   tpl(in)    :
 *
 * Note: Evaluate given function.
 */
int
qdata_evaluate_function (THREAD_ENTRY * thread_p, regu_variable_node * function_p, val_descr * val_desc_p,
			 OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec)
{
  FUNCTION_TYPE *funcp;

  /* should sync with fetch_peek_dbval () */

  funcp = function_p->value.funcp;
  /* clear any value from a previous iteration */
  pr_clear_value (funcp->value);

  switch (funcp->ftype)
    {
    case F_SET:
      return qdata_convert_dbvals_to_set (thread_p, DB_TYPE_SET, function_p, val_desc_p, obj_oid_p, tplrec);

    case F_MULTISET:
      return qdata_convert_dbvals_to_set (thread_p, DB_TYPE_MULTISET, function_p, val_desc_p, obj_oid_p, tplrec);

    case F_SEQUENCE:
      return qdata_convert_dbvals_to_set (thread_p, DB_TYPE_SEQUENCE, function_p, val_desc_p, obj_oid_p, tplrec);

    case F_VID:
      return qdata_convert_dbvals_to_set (thread_p, DB_TYPE_VOBJ, function_p, val_desc_p, obj_oid_p, tplrec);

    case F_TABLE_SET:
      return qdata_convert_table_to_set (thread_p, DB_TYPE_SET, function_p, val_desc_p);

    case F_TABLE_MULTISET:
      return qdata_convert_table_to_set (thread_p, DB_TYPE_MULTISET, function_p, val_desc_p);

    case F_TABLE_SEQUENCE:
      return qdata_convert_table_to_set (thread_p, DB_TYPE_SEQUENCE, function_p, val_desc_p);

    case F_GENERIC:
      return qdata_evaluate_generic_function (thread_p, funcp, val_desc_p, obj_oid_p, tplrec);

    case F_CLASS_OF:
      return qdata_get_class_of_function (thread_p, funcp, val_desc_p, obj_oid_p, tplrec);

    case F_INSERT_SUBSTRING:
      return qdata_insert_substring_function (thread_p, funcp, val_desc_p, obj_oid_p, tplrec);

    case F_ELT:
      return qdata_elt (thread_p, funcp, val_desc_p, obj_oid_p, tplrec);

    case F_BENCHMARK:
      return qdata_benchmark (thread_p, funcp, val_desc_p, obj_oid_p, tplrec);

    case F_JSON_ARRAY:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_array);

    case F_JSON_ARRAY_APPEND:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_array_append);

    case F_JSON_ARRAY_INSERT:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_array_insert);

    case F_JSON_CONTAINS:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_contains);

    case F_JSON_CONTAINS_PATH:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_contains_path);

    case F_JSON_DEPTH:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_depth);

    case F_JSON_EXTRACT:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_extract);

    case F_JSON_GET_ALL_PATHS:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_get_all_paths);

    case F_JSON_INSERT:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_insert);

    case F_JSON_KEYS:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_keys);

    case F_JSON_LENGTH:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_length);

    case F_JSON_MERGE:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_merge_preserve);

    case F_JSON_MERGE_PATCH:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_merge_patch);

    case F_JSON_OBJECT:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_object);

    case F_JSON_PRETTY:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_pretty);

    case F_JSON_QUOTE:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_quote);

    case F_JSON_REMOVE:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_remove);

    case F_JSON_REPLACE:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_replace);

    case F_JSON_SEARCH:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_search);

    case F_JSON_SET:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_set);

    case F_JSON_TYPE:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_type_dbval);

    case F_JSON_UNQUOTE:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_unquote);

    case F_JSON_VALID:
      return qdata_convert_operands_to_value_and_call (thread_p, funcp, val_desc_p, obj_oid_p, tplrec,
						       db_evaluate_json_valid);

    case F_REGEXP_COUNT:
    case F_REGEXP_INSTR:
    case F_REGEXP_LIKE:
    case F_REGEXP_REPLACE:
    case F_REGEXP_SUBSTR:
      return qdata_regexp_function (thread_p, funcp, val_desc_p, obj_oid_p, tplrec);

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }
}

/*
 * qdata_convert_table_to_set () -
 *   return: NO_ERROR, or ER_code
 *   stype(in)  : set type
 *   func(in)   : regu variable (guaranteed TYPE_FUNC)
 *   vd(in)     : Value descriptor
 *
 * Note: Convert a list file into a set/sequence and return a pointer to it.
 */
static int
qdata_convert_table_to_set (THREAD_ENTRY * thread_p, DB_TYPE stype, REGU_VARIABLE * function_p, VAL_DESCR * val_desc_p)
{
  QFILE_LIST_SCAN_ID scan_id;
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  SCAN_CODE scan_code;
  QFILE_LIST_ID *list_id_p;
  int i, seq_pos;
  DB_VALUE dbval, *result_p;
  DB_COLLECTION *collection_p = NULL;
  SETOBJ *setobj_p;
  DB_TYPE type;
  const PR_TYPE *pr_type_p;
  int error;
  REGU_VARIABLE_LIST operand;
  TP_DOMAIN *domain_p;
  bool is_null;

  result_p = function_p->value.funcp->value;
  operand = function_p->value.funcp->operand;

  /* execute linked query */
  EXECUTE_REGU_VARIABLE_XASL (thread_p, &(operand->value), val_desc_p);

  if (CHECK_REGU_VARIABLE_XASL_STATUS (&(operand->value)) != XASL_SUCCESS)
    {
      return ER_FAILED;
    }

  domain_p = qexec_get_node_domain (val_desc_p, function_p->domain, function_p->plan_item);
  list_id_p = operand->value.value.srlist_id->list_id;
  db_make_null (&dbval);

  if (stype == DB_TYPE_SET)
    {
      collection_p = db_set_create_basic (NULL, NULL);
    }
  else if (stype == DB_TYPE_MULTISET)
    {
      collection_p = db_set_create_multi (NULL, NULL);
    }
  else if (stype == DB_TYPE_SEQUENCE || stype == DB_TYPE_VOBJ)
    {
      collection_p = db_seq_create (NULL, NULL, (list_id_p->tuple_cnt * list_id_p->type_list.type_cnt));
    }
  else
    {
      return ER_FAILED;
    }

  error = set_get_setobj (collection_p, &setobj_p, 1);
  if (error != NO_ERROR || !setobj_p)
    {
      set_free (collection_p);
      return ER_FAILED;
    }

  /*
   * Don't need to worry about the vobj case here; this function can't
   * be called in a context where it's expected to produce a vobj.  See
   * xd_dbvals_to_set for the contrasting case.
   */
  setobj_put_domain (setobj_p, domain_p);
  if (qfile_open_list_scan (list_id_p, &scan_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  seq_pos = 0;
  while (true)
    {
      scan_code = qfile_scan_list_next (thread_p, &scan_id, &tuple_record, PEEK);
      if (scan_code != S_SUCCESS)
	{
	  break;
	}

      for (i = 0; i < list_id_p->type_list.type_cnt; i++)
	{
	  /* grab column i and add it to the col */
	  type = TP_DOMAIN_TYPE (list_id_p->type_list.domp[i]);
	  pr_type_p = pr_type_from_id (type);
	  if (pr_type_p == NULL)
	    {
	      qfile_close_scan (thread_p, &scan_id);
	      return ER_FAILED;
	    }

	  if (qfile_slot_read_column_value (&tuple_record, i, list_id_p->type_list.domp[i], &dbval, true, &is_null) !=
	      NO_ERROR)
	    {
	      qfile_close_scan (thread_p, &scan_id);
	      return ER_FAILED;
	    }

	  /*
	   * using setobj_put_value transfers "ownership" of the
	   * db_value memory to the set. This avoids a redundant clone/free.
	   */
	  error = setobj_put_value (setobj_p, seq_pos++, &dbval);

	  /*
	   * if we attempt to add a duplicate value to a set,
	   * clear the value, but do not set an error
	   */
	  if (error == SET_DUPLICATE_VALUE)
	    {
	      pr_clear_value (&dbval);
	      error = NO_ERROR;
	    }

	  if (error != NO_ERROR)
	    {
	      set_free (collection_p);
	      pr_clear_value (&dbval);
	      qfile_close_scan (thread_p, &scan_id);
	      return ER_FAILED;
	    }
	}
    }

  qfile_close_scan (thread_p, &scan_id);
  set_make_collection (result_p, collection_p);

  return NO_ERROR;
}

/*
 * qdata_evaluate_connect_by_root () - CONNECT_BY_ROOT operator evaluation func
 *    return:
 *  xasl_p(in):
 *  regu_p(in):
 *  result_val_p(in/out):
 *  vd(in):
 */
bool
qdata_evaluate_connect_by_root (THREAD_ENTRY * thread_p, void *xasl_p, regu_variable_node * regu_p,
				DB_VALUE * result_val_p, val_descr * vd)
{
  QFILE_TUPLE tpl;
  QFILE_LIST_ID *list_id_p;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = QFILE_TUPLE_RECORD_INITIALIZER;
  const QFILE_TUPLE_POSITION *bitval = NULL;
  QFILE_TUPLE_POSITION p_pos;
  QPROC_DB_VALUE_LIST valp;
  DB_VALUE p_pos_dbval;
  XASL_NODE *xasl, *xptr;
  int length, i;

  /* a constant argument (a bind, a literal) is the root's value as it is every row's: the compiler folded such a
   * CONNECT_BY_ROOT when it knew the value, and a plan that does not depend on the values evaluates it here */
  if (regu_p->type == TYPE_POS_VALUE || regu_p->type == TYPE_DBVAL)
    {
      return fetch_copy_dbval (thread_p, regu_p, vd, NULL, NULL, NULL, result_val_p) == NO_ERROR;
    }

  /* sanity checks */
  if (regu_p->type != TYPE_CONSTANT)
    {
      return false;
    }

  xasl = (XASL_NODE *) xasl_p;
  if (!xasl)
    {
      return false;
    }

  if (!XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      return false;
    }

  xptr = xasl->connect_by_ptr;
  if (!xptr)
    {
      return false;
    }

  tpl = xptr->proc.connect_by.curr_tuple;

  /* walk the parents up to root */

  list_id_p = xptr->list_id;

  if (qfile_open_list_scan (list_id_p, &s_id) != NO_ERROR)
    {
      return false;
    }

  /* we start with tpl itself */
  qfile_slot_set_tuple_ptr_and_layout (&tuple_rec, tpl, 0, &s_id.list_id.type_list);	/* raw CONNECT BY tuple: bind to the list's descriptor */

  do
    {
      /* get the parent node */
      if (qexec_get_tuple_column_value (&tuple_rec, xptr->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET,
					&p_pos_dbval, &tp_Bit_domain) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return false;
	}

      bitval = REINTERPRET_CAST (const QFILE_TUPLE_POSITION *, db_get_bit (&p_pos_dbval, &length));

      if (bitval)
	{
	  p_pos.status = s_id.status;
	  p_pos.position = S_ON;
	  p_pos.vpid = bitval->vpid;
	  p_pos.offset = bitval->offset;
	  p_pos.tpl = NULL;
	  p_pos.tplno = bitval->tplno;

	  if (qfile_jump_scan_tuple_position (thread_p, &s_id, &p_pos, &tuple_rec, PEEK) != S_SUCCESS)
	    {
	      qfile_close_scan (thread_p, &s_id);
	      return false;
	    }
	}
    }
  while (bitval);		/* the parent tuple pos is null for the root node */

  /* here tuple_rec.tpl is the root tuple; get the required column */

  for (i = 0, valp = xptr->val_list->valp; valp; i++, valp = valp->next)
    {
      if (valp->val == regu_p->value.dbvalptr)
	{
	  break;
	}
    }

  if (i < xptr->val_list->val_cnt)
    {
      /* the column's domain in this execution: the argument reads a value pointer, whose domain is its producer's
       * resolution (a derived-table column a bind types) when the compiler left it variable */
      const TP_DOMAIN *column_domain = qexec_consumer_domain (vd, regu_p->domain, regu_p->plan_item);
      if (column_domain == NULL)
	{
	  qfile_close_scan (thread_p, &s_id);
	  (void) qexec_domain_unresolved (vd, regu_p->plan_item, regu_p->domain);
	  return false;
	}
      if (qexec_get_tuple_column_value (&tuple_rec, i, result_val_p, (TP_DOMAIN *) column_domain) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return false;
	}
    }
  else
    {
      /* TYPE_CONSTANT but not in val_list, check if it is inst_num() (orderby_num() is not allowed) */
      if (regu_p->value.dbvalptr == xasl->instnum_val)
	{
	  if (pr_clone_value (xasl->instnum_val, result_val_p) != NO_ERROR)
	    {
	      qfile_close_scan (thread_p, &s_id);
	      return false;
	    }
	}
      else
	{
	  qfile_close_scan (thread_p, &s_id);
	  return false;
	}
    }

  qfile_close_scan (thread_p, &s_id);

  return true;
}

/*
 * qdata_evaluate_qprior () - PRIOR in SELECT list evaluation func
 *    return:
 *  xasl_p(in):
 *  regu_p(in):
 *  result_val_p(in/out):
 *  vd(in):
 */
bool
qdata_evaluate_qprior (THREAD_ENTRY * thread_p, void *xasl_p, regu_variable_node * regu_p, DB_VALUE * result_val_p,
		       val_descr * vd)
{
  QFILE_TUPLE tpl;
  QFILE_LIST_ID *list_id_p;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = QFILE_TUPLE_RECORD_INITIALIZER;
  const QFILE_TUPLE_POSITION *bitval = NULL;
  QFILE_TUPLE_POSITION p_pos;
  DB_VALUE p_pos_dbval;
  XASL_NODE *xasl, *xptr;
  int length;

  /* a constant argument (a bind, a literal) is the parent's value as it is every row's, the root's included: the
   * compiler folded such a PRIOR when it knew the value, and a plan that does not depend on the values evaluates it
   * here */
  if (regu_p->type == TYPE_POS_VALUE || regu_p->type == TYPE_DBVAL)
    {
      return fetch_copy_dbval (thread_p, regu_p, vd, NULL, NULL, NULL, result_val_p) == NO_ERROR;
    }

  xasl = (XASL_NODE *) xasl_p;

  /* sanity checks */
  if (!xasl)
    {
      return false;
    }

  if (!XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      return false;
    }

  xptr = xasl->connect_by_ptr;
  if (!xptr)
    {
      return false;
    }

  tpl = xptr->proc.connect_by.curr_tuple;

  list_id_p = xptr->list_id;

  if (qfile_open_list_scan (list_id_p, &s_id) != NO_ERROR)
    {
      return false;
    }

  qfile_slot_set_tuple_ptr_and_layout (&tuple_rec, tpl, 0, &s_id.list_id.type_list);	/* raw CONNECT BY tuple: bind to the list's descriptor */

  /* get the parent node */
  if (qexec_get_tuple_column_value (&tuple_rec, xptr->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET,
				    &p_pos_dbval, &tp_Bit_domain) != NO_ERROR)
    {
      qfile_close_scan (thread_p, &s_id);
      return false;
    }

  bitval = REINTERPRET_CAST (const QFILE_TUPLE_POSITION *, db_get_bit (&p_pos_dbval, &length));

  if (bitval)
    {
      p_pos.status = s_id.status;
      p_pos.position = S_ON;
      p_pos.vpid = bitval->vpid;
      p_pos.offset = bitval->offset;
      p_pos.tpl = NULL;
      p_pos.tplno = bitval->tplno;

      if (qfile_jump_scan_tuple_position (thread_p, &s_id, &p_pos, &tuple_rec, PEEK) != S_SUCCESS)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return false;
	}
    }
  else
    {
      /* the parent tuple pos is null for the root node */
      qfile_slot_reset (&tuple_rec);
    }

  if (tuple_rec.tpl != NULL)
    {
      /* fetch val list from the parent tuple */
      if (fetch_val_list (thread_p, xptr->proc.connect_by.prior_regu_list_pred, vd, NULL, NULL, &tuple_rec, PEEK) !=
	  NO_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return false;
	}
      if (fetch_val_list (thread_p, xptr->proc.connect_by.prior_regu_list_rest, vd, NULL, NULL, &tuple_rec, PEEK) !=
	  NO_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return false;
	}

      /* replace values in T_QPRIOR argument with values from parent tuple */
      qexec_replace_prior_regu_vars_prior_expr (thread_p, regu_p, xptr, xptr);

      /* evaluate the modified regu_p */
      if (fetch_copy_dbval (thread_p, regu_p, vd, NULL, NULL, &tuple_rec, result_val_p) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return false;
	}
    }
  else
    {
      db_make_null (result_val_p);
    }

  qfile_close_scan (thread_p, &s_id);

  return true;
}

/*
 * qdata_evaluate_sys_connect_by_path () - SYS_CONNECT_BY_PATH function
 *	evaluation func
 *    return:
 *  select_xasl(in):
 *  regu_p1(in): column
 *  regu_p2(in): character
 *  result_val_p(in/out):
 */
bool
qdata_evaluate_sys_connect_by_path (THREAD_ENTRY * thread_p, void *xasl_p, regu_variable_node * regu_p,
				    DB_VALUE * value_char, DB_VALUE * result_p, val_descr * vd)
{
  QFILE_TUPLE tpl;
  QFILE_LIST_ID *list_id_p;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = QFILE_TUPLE_RECORD_INITIALIZER;
  const QFILE_TUPLE_POSITION *bitval = NULL;
  QFILE_TUPLE_POSITION p_pos;
  QPROC_DB_VALUE_LIST valp;
  DB_VALUE p_pos_dbval, cast_value, arg_dbval;
  XASL_NODE *xasl, *xptr;
  int length, i;
  char *result_path = NULL, *path_tmp = NULL;
  int len_result_path;
  size_t len_tmp = 0, len;
  char *sep = NULL;
  DB_VALUE *arg_dbval_p = NULL;
  DB_VALUE **save_values = NULL;
  bool use_extended = false;	/* flag for using extended form, accepting an expression as the first argument of
				 * SYS_CONNECT_BY_PATH() */
  bool need_clear_arg_dbval = false;

  assert (DB_IS_NULL (result_p));

  /* sanity checks */
  xasl = (XASL_NODE *) xasl_p;
  if (!xasl)
    {
      return false;
    }

  if (!XASL_IS_FLAGED (xasl, XASL_HAS_CONNECT_BY))
    {
      return false;
    }

  xptr = xasl->connect_by_ptr;
  if (!xptr)
    {
      return false;
    }

  tpl = xptr->proc.connect_by.curr_tuple;

  /* column */
  if (regu_p->type != TYPE_CONSTANT)
    {
      /* NOTE: if the column is non-string, a cast will be made (see T_CAST).  This is specific to sys_connect_by_path
       * because the result is always varchar (by comparison to connect_by_root which has the result of the root
       * specifiec column). The cast is propagated from the parser tree into the regu variabile, which has the
       * TYPE_INARITH type with arithptr with type T_CAST and right argument the real column, which will be further
       * used for column retrieving in the xasl->val_list->valp. */

      if (regu_p->type == TYPE_INARITH)
	{
	  if (regu_p->value.arithptr && regu_p->value.arithptr->opcode == T_CAST)
	    {
	      /* correct column */
	      regu_p = regu_p->value.arithptr->rightptr;
	    }
	}
    }

  /* set the flag for using extended form, but keep the single-column argument code too for being faster for its
   * particular case */
  if (regu_p->type != TYPE_CONSTANT)
    {
      use_extended = true;
    }
  else
    {
      arg_dbval_p = &arg_dbval;
      db_make_null (arg_dbval_p);
    }

  /* character */
  i = (int) strlen (DB_GET_STRING_SAFE (value_char));
  sep = (char *) db_private_alloc (thread_p, sizeof (char) * (i + 1));
  if (sep == NULL)
    {
      return false;
    }
  sep[0] = 0;
  if (i > 0)
    {
      strcpy (sep, DB_GET_STRING_SAFE (value_char));
    }

  /* walk the parents up to root */

  list_id_p = xptr->list_id;

  if (qfile_open_list_scan (list_id_p, &s_id) != NO_ERROR)
    {
      goto error2;
    }

  if (!use_extended)
    {
      /* column index */
      for (i = 0, valp = xptr->val_list->valp; valp; i++, valp = valp->next)
	{
	  if (valp->val == regu_p->value.dbvalptr)
	    {
	      break;
	    }
	}

      if (i >= xptr->val_list->val_cnt)
	{
	  /* TYPE_CONSTANT but not in val_list, check if it is inst_num() (orderby_num() is not allowed) */
	  if (regu_p->value.dbvalptr == xasl->instnum_val)
	    {
	      arg_dbval_p = xasl->instnum_val;
	    }
	  else
	    {
	      goto error;
	    }
	}
    }
  else
    {
      /* save val_list */
      if (xptr->val_list->val_cnt > 0)
	{
	  save_values = (DB_VALUE **) db_private_alloc (thread_p, sizeof (DB_VALUE *) * xptr->val_list->val_cnt);
	  if (save_values == NULL)
	    {
	      goto error;
	    }

	  memset (save_values, 0, sizeof (DB_VALUE *) * xptr->val_list->val_cnt);
	  for (i = 0, valp = xptr->val_list->valp; valp && i < xptr->val_list->val_cnt; i++, valp = valp->next)
	    {
	      save_values[i] = db_value_copy (valp->val);
	    }
	}
    }

  /* we start with tpl itself */
  qfile_slot_set_tuple_ptr_and_layout (&tuple_rec, tpl, 0, &s_id.list_id.type_list);	/* raw CONNECT BY tuple: bind to the list's descriptor */

  len_result_path = SYS_CONNECT_BY_PATH_MEM_STEP;
  result_path = (char *) db_private_alloc (thread_p, sizeof (char) * len_result_path);
  if (result_path == NULL)
    {
      goto error;
    }

  strcpy (result_path, "");

  do
    {
      need_clear_arg_dbval = false;
      if (!use_extended)
	{
	  /* get the required column, in its domain of this execution (as CONNECT_BY_ROOT reads its argument) */
	  if (i < xptr->val_list->val_cnt)
	    {
	      const TP_DOMAIN *column_domain = qexec_consumer_domain (vd, regu_p->domain, regu_p->plan_item);
	      if (column_domain == NULL)
		{
		  (void) qexec_domain_unresolved (vd, regu_p->plan_item, regu_p->domain);
		  goto error;
		}
	      if (qexec_get_tuple_column_value (&tuple_rec, i, arg_dbval_p, (TP_DOMAIN *) column_domain) != NO_ERROR)
		{
		  goto error;
		}
	      need_clear_arg_dbval = true;
	    }
	}
      else
	{
	  /* fetch value list */
	  if (fetch_val_list (thread_p, xptr->proc.connect_by.regu_list_pred, vd, NULL, NULL, &tuple_rec, PEEK) !=
	      NO_ERROR)
	    {
	      goto error;
	    }
	  if (fetch_val_list (thread_p, xptr->proc.connect_by.regu_list_rest, vd, NULL, NULL, &tuple_rec, PEEK) !=
	      NO_ERROR)
	    {
	      goto error;
	    }

	  /* evaluate argument expression */
	  if (fetch_peek_dbval (thread_p, regu_p, vd, NULL, NULL, &tuple_rec, &arg_dbval_p) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if (DB_IS_NULL (arg_dbval_p))
	{
	  db_make_null (&cast_value);
	}
      else
	{
	  /* cast result to string; this call also allocates the container */
	  if (qdata_cast_to_domain (arg_dbval_p, &cast_value, &tp_String_domain) != NO_ERROR)
	    {
	      goto error;
	    }

	  if (need_clear_arg_dbval)
	    {
	      pr_clear_value (arg_dbval_p);
	      need_clear_arg_dbval = false;
	    }
	}

      len = (strlen (sep) + (DB_IS_NULL (&cast_value) ? 0 : db_get_string_size (&cast_value))
	     + strlen (result_path) + 1);
      if (len > len_tmp || path_tmp == NULL)
	{
	  /* free previously alloced */
	  if (path_tmp)
	    {
	      db_private_free_and_init (thread_p, path_tmp);
	    }

	  len_tmp = len;
	  path_tmp = (char *) db_private_alloc (thread_p, sizeof (char) * len_tmp);
	  if (path_tmp == NULL)
	    {
	      pr_clear_value (&cast_value);
	      goto error;
	    }
	}

      strcpy (path_tmp, sep);
      strcat (path_tmp, DB_GET_STRING_SAFE (&cast_value));

      strcat (path_tmp, result_path);

      /* free the container for cast_value */
      if (pr_clear_value (&cast_value) != NO_ERROR)
	{
	  goto error;
	}

      bool is_resize = false;
      int need_size = (int) strlen (path_tmp) + 1;
      while (need_size > len_result_path)
	{
	  len_result_path += SYS_CONNECT_BY_PATH_MEM_STEP;
	  is_resize = true;
	}

      if (is_resize)
	{
	  db_private_free_and_init (thread_p, result_path);
	  result_path = (char *) db_private_alloc (thread_p, sizeof (char) * len_result_path);
	  if (result_path == NULL)
	    {
	      goto error;
	    }
	}

      strcpy (result_path, path_tmp);

      /* get the parent node */
      if (qexec_get_tuple_column_value (&tuple_rec, xptr->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET,
					&p_pos_dbval, &tp_Bit_domain) != NO_ERROR)
	{
	  goto error;
	}

      bitval = REINTERPRET_CAST (const QFILE_TUPLE_POSITION *, db_get_bit (&p_pos_dbval, &length));

      if (bitval)
	{
	  p_pos.status = s_id.status;
	  p_pos.position = S_ON;
	  p_pos.vpid = bitval->vpid;
	  p_pos.offset = bitval->offset;
	  p_pos.tpl = NULL;
	  p_pos.tplno = bitval->tplno;

	  if (qfile_jump_scan_tuple_position (thread_p, &s_id, &p_pos, &tuple_rec, PEEK) != S_SUCCESS)
	    {
	      goto error;
	    }
	}
    }
  while (bitval);		/* the parent tuple pos is null for the root node */

  qfile_close_scan (thread_p, &s_id);

  db_make_string (result_p, result_path);
  result_p->need_clear = true;

  if (use_extended)
    {
      /* restore val_list */
      if (xptr->val_list->val_cnt > 0)
	{
	  for (i = 0, valp = xptr->val_list->valp; valp && i < xptr->val_list->val_cnt; i++, valp = valp->next)
	    {
	      if (pr_clear_value (valp->val) != NO_ERROR)
		{
		  goto error2;
		}
	      if (pr_clone_value (save_values[i], valp->val) != NO_ERROR)
		{
		  goto error2;
		}
	    }

	  for (i = 0; i < xptr->val_list->val_cnt; i++)
	    {
	      if (save_values[i])
		{
		  if (pr_free_ext_value (save_values[i]) != NO_ERROR)
		    {
		      goto error2;
		    }
		  save_values[i] = NULL;
		}
	    }
	  db_private_free_and_init (thread_p, save_values);
	}
    }

  if (path_tmp)
    {
      db_private_free_and_init (thread_p, path_tmp);
    }

  if (sep)
    {
      db_private_free_and_init (thread_p, sep);
    }

  if (need_clear_arg_dbval)
    {
      pr_clear_value (arg_dbval_p);
    }

  return true;

error:
  qfile_close_scan (thread_p, &s_id);

  if (save_values)
    {
      for (i = 0; i < xptr->val_list->val_cnt; i++)
	{
	  if (save_values[i])
	    {
	      pr_free_ext_value (save_values[i]);
	    }
	}
      db_private_free_and_init (thread_p, save_values);
    }

error2:
  if (result_path)
    {
      db_private_free_and_init (thread_p, result_path);
      result_p->need_clear = false;
    }

  if (path_tmp)
    {
      db_private_free_and_init (thread_p, path_tmp);
    }

  if (sep)
    {
      db_private_free_and_init (thread_p, sep);
    }

  if (need_clear_arg_dbval)
    {
      pr_clear_value (arg_dbval_p);
    }

  return false;
}

/*
 * qdata_bit_not_dbval () - bitwise not
 *   return: NO_ERROR, or ER_code
 *   dbval_p(in) : db_value node
 *   result_p(out) : resultant db_value node
 *   domain_p(in) :
 *
 */
int
qdata_bit_not_dbval (DB_VALUE * dbval_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  DB_TYPE type;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval_p))
    {
      return NO_ERROR;
    }

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_NULL:
      db_make_null (result_p);
      break;

    case DB_TYPE_INTEGER:
      db_make_bigint (result_p, ~((INT64) db_get_int (dbval_p)));
      break;

    case DB_TYPE_BIGINT:
      db_make_bigint (result_p, ~db_get_bigint (dbval_p));
      break;

    case DB_TYPE_SHORT:
      db_make_bigint (result_p, ~((INT64) db_get_short (dbval_p)));
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
    }

  return NO_ERROR;
}

/*
 * qdata_bit_and_dbval () - bitwise and
 *   return: NO_ERROR, or ER_code
 *   dbval1_p(in) : first db_value node
 *   dbval2_p(in) : second db_value node
 *   result_p(out) : resultant db_value node
 *   domain_p(in) :
 *
 */
int
qdata_bit_and_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  DB_TYPE type[2];
  DB_BIGINT bi[2];
  DB_VALUE *dbval[2];
  int i;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  type[0] = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type[1] = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  dbval[0] = dbval1_p;
  dbval[1] = dbval2_p;

  for (i = 0; i < 2; i++)
    {
      switch (type[i])
	{
	case DB_TYPE_NULL:
	  db_make_null (result_p);
	  break;

	case DB_TYPE_INTEGER:
	  bi[i] = (DB_BIGINT) db_get_int (dbval[i]);
	  break;

	case DB_TYPE_BIGINT:
	  bi[i] = db_get_bigint (dbval[i]);
	  break;

	case DB_TYPE_SHORT:
	  bi[i] = (DB_BIGINT) db_get_short (dbval[i]);
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (type[0] != DB_TYPE_NULL && type[1] != DB_TYPE_NULL)
    {
      db_make_bigint (result_p, bi[0] & bi[1]);
    }

  return NO_ERROR;
}

/*
 * qdata_bit_or_dbval () - bitwise or
 *   return: NO_ERROR, or ER_code
 *   dbval1_p(in) : first db_value node
 *   dbval2_p(in) : second db_value node
 *   result_p(out) : resultant db_value node
 *   domain_p(in) :
 *
 */
int
qdata_bit_or_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  DB_TYPE type[2];
  DB_BIGINT bi[2];
  DB_VALUE *dbval[2];
  int i;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  type[0] = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type[1] = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  dbval[0] = dbval1_p;
  dbval[1] = dbval2_p;

  for (i = 0; i < 2; i++)
    {
      switch (type[i])
	{
	case DB_TYPE_NULL:
	  db_make_null (result_p);
	  break;

	case DB_TYPE_INTEGER:
	  bi[i] = (DB_BIGINT) db_get_int (dbval[i]);
	  break;

	case DB_TYPE_BIGINT:
	  bi[i] = db_get_bigint (dbval[i]);
	  break;

	case DB_TYPE_SHORT:
	  bi[i] = (DB_BIGINT) db_get_short (dbval[i]);
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (type[0] != DB_TYPE_NULL && type[1] != DB_TYPE_NULL)
    {
      db_make_bigint (result_p, bi[0] | bi[1]);
    }

  return NO_ERROR;
}

/*
 * qdata_bit_xor_dbval () - bitwise xor
 *   return: NO_ERROR, or ER_code
 *   dbval1_p(in) : first db_value node
 *   dbval2_p(in) : second db_value node
 *   result_p(out) : resultant db_value node
 *   domain_p(in) :
 *
 */
int
qdata_bit_xor_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  DB_TYPE type[2];
  DB_BIGINT bi[2];
  DB_VALUE *dbval[2];
  int i;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  type[0] = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type[1] = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  dbval[0] = dbval1_p;
  dbval[1] = dbval2_p;

  for (i = 0; i < 2; i++)
    {
      switch (type[i])
	{
	case DB_TYPE_NULL:
	  db_make_null (result_p);
	  break;

	case DB_TYPE_INTEGER:
	  bi[i] = (DB_BIGINT) db_get_int (dbval[i]);
	  break;

	case DB_TYPE_BIGINT:
	  bi[i] = db_get_bigint (dbval[i]);
	  break;

	case DB_TYPE_SHORT:
	  bi[i] = (DB_BIGINT) db_get_short (dbval[i]);
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (type[0] != DB_TYPE_NULL && type[1] != DB_TYPE_NULL)
    {
      db_make_bigint (result_p, bi[0] ^ bi[1]);
    }

  return NO_ERROR;
}

/*
 * qdata_bit_shift_dbval () - bitshift
 *   return: NO_ERROR, or ER_code
 *   dbval1_p(in) : first db_value node
 *   dbval2_p(in) : second db_value node
 *   result_p(out) : resultant db_value node
 *   domain_p(in) :
 *
 */
int
qdata_bit_shift_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, OPERATOR_TYPE op, DB_VALUE * result_p,
		       tp_domain * domain_p)
{
  DB_TYPE type[2];
  DB_BIGINT bi[2];
  DB_VALUE *dbval[2];
  int i;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  type[0] = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type[1] = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  dbval[0] = dbval1_p;
  dbval[1] = dbval2_p;

  for (i = 0; i < 2; i++)
    {
      switch (type[i])
	{
	case DB_TYPE_NULL:
	  db_make_null (result_p);
	  break;

	case DB_TYPE_INTEGER:
	  bi[i] = (DB_BIGINT) db_get_int (dbval[i]);
	  break;

	case DB_TYPE_BIGINT:
	  bi[i] = db_get_bigint (dbval[i]);
	  break;

	case DB_TYPE_SHORT:
	  bi[i] = (DB_BIGINT) db_get_short (dbval[i]);
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (type[0] != DB_TYPE_NULL && type[1] != DB_TYPE_NULL)
    {
      if (bi[1] < (DB_BIGINT) (sizeof (DB_BIGINT) * 8) && bi[1] >= 0)
	{
	  if (op == T_BITSHIFT_LEFT)
	    {
	      db_make_bigint (result_p, ((UINT64) bi[0]) << ((UINT64) bi[1]));
	    }
	  else
	    {
	      db_make_bigint (result_p, ((UINT64) bi[0]) >> ((UINT64) bi[1]));
	    }
	}
      else
	{
	  db_make_bigint (result_p, 0);
	}
    }

  return NO_ERROR;
}

/*
 * qdata_divmod_dbval () - DIV/MOD operator
 *   return: NO_ERROR, or ER_code
 *   dbval1_p(in) : first db_value node
 *   dbval2_p(in) : second db_value node
 *   result_p(out) : resultant db_value node
 *   domain_p(in) :
 *
 */
int
qdata_divmod_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, OPERATOR_TYPE op, DB_VALUE * result_p,
		    tp_domain * domain_p)
{
  DB_TYPE type[2];
  DB_BIGINT bi[2];
  DB_VALUE *dbval[2];
  int i;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  type[0] = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type[1] = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  dbval[0] = dbval1_p;
  dbval[1] = dbval2_p;

  for (i = 0; i < 2; i++)
    {
      switch (type[i])
	{
	case DB_TYPE_NULL:
	  db_make_null (result_p);
	  break;

	case DB_TYPE_INTEGER:
	  bi[i] = (DB_BIGINT) db_get_int (dbval[i]);
	  break;

	case DB_TYPE_BIGINT:
	  bi[i] = db_get_bigint (dbval[i]);
	  break;

	case DB_TYPE_SHORT:
	  bi[i] = (DB_BIGINT) db_get_short (dbval[i]);
	  break;

	default:
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (type[0] != DB_TYPE_NULL && type[1] != DB_TYPE_NULL)
    {
      if (bi[1] != 0)
	{
	  if (op == T_INTDIV)
	    {
	      if (type[0] == DB_TYPE_INTEGER)
		{
		  if (OR_CHECK_INT_DIV_OVERFLOW (bi[0], bi[1]))
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
		      return ER_QPROC_OVERFLOW_DIVISION;
		    }
		  db_make_int (result_p, (INT32) (bi[0] / bi[1]));
		}
	      else if (type[0] == DB_TYPE_BIGINT)
		{
		  if (OR_CHECK_BIGINT_DIV_OVERFLOW (bi[0], bi[1]))
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
		      return ER_QPROC_OVERFLOW_DIVISION;
		    }
		  db_make_bigint (result_p, bi[0] / bi[1]);
		}
	      else
		{
		  if (OR_CHECK_SHORT_DIV_OVERFLOW (bi[0], bi[1]))
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
		      return ER_QPROC_OVERFLOW_DIVISION;
		    }
		  db_make_short (result_p, (INT16) (bi[0] / bi[1]));
		}
	    }
	  else if (OR_CHECK_BIGINT_DIV_OVERFLOW (bi[0], bi[1]))
	    {
	      /* MIN % -1 is 0; computing it would trap on the machine divide instruction */
	      if (type[0] == DB_TYPE_INTEGER)
		{
		  db_make_int (result_p, 0);
		}
	      else if (type[0] == DB_TYPE_BIGINT)
		{
		  db_make_bigint (result_p, 0);
		}
	      else
		{
		  db_make_short (result_p, 0);
		}
	    }
	  else
	    {
	      if (type[0] == DB_TYPE_INTEGER)
		{
		  db_make_int (result_p, (INT32) (bi[0] % bi[1]));
		}
	      else if (type[0] == DB_TYPE_BIGINT)
		{
		  db_make_bigint (result_p, bi[0] % bi[1]);
		}
	      else
		{
		  db_make_short (result_p, (INT16) (bi[0] % bi[1]));
		}
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_ZERO_DIVIDE, 0);
	  return ER_QPROC_ZERO_DIVIDE;
	}
    }

  return NO_ERROR;
}

/*
 * qdata_list_dbs () - lists all databases names
 *   return: NO_ERROR, or ER_code
 *   result_p(out) : resultant db_value node
 *   domain(in): domain
 */
int
qdata_list_dbs (THREAD_ENTRY * thread_p, DB_VALUE * result_p, tp_domain * domain_p)
{
  DB_INFO *db_info_p;

  if (cfg_read_directory (&db_info_p, false) != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_NO_FILE, 1, DATABASES_FILENAME);
	}
      goto error;
    }

  if (db_info_p)
    {
      DB_INFO *list_p;
      char *name_list;
      size_t name_list_size = 0;
      bool is_first;

      for (list_p = db_info_p; list_p != NULL; list_p = list_p->next)
	{
	  if (list_p->name)
	    {
	      name_list_size += strlen (list_p->name) + 1;
	    }
	}

      if (name_list_size != 0)
	{
	  name_list = (char *) db_private_alloc (thread_p, name_list_size);
	  if (name_list == NULL)
	    {
	      cfg_free_directory (db_info_p);
	      goto error;
	    }
	  strcpy (name_list, "");

	  for (list_p = db_info_p, is_first = true; list_p != NULL; list_p = list_p->next)
	    {
	      if (list_p->name)
		{
		  if (!is_first)
		    {
		      strcat (name_list, " ");
		    }
		  else
		    {
		      is_first = false;
		    }
		  strcat (name_list, list_p->name);
		}
	    }

	  cfg_free_directory (db_info_p);

	  if (db_make_string (result_p, name_list) != NO_ERROR)
	    {
	      goto error;
	    }
	  result_p->need_clear = true;
	}
      else
	{
	  cfg_free_directory (db_info_p);
	  db_make_null (result_p);
	}

      if (domain_p != NULL)
	{
	  assert (TP_DOMAIN_TYPE (domain_p) == DB_VALUE_TYPE (result_p));

	  db_string_put_cs_and_collation (result_p, TP_DOMAIN_CODESET (domain_p), TP_DOMAIN_COLLATION (domain_p));
	}
    }
  else
    {
      db_make_null (result_p);
    }

  return NO_ERROR;

error:
  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * qdata_regu_list_to_regu_array () - extracts the regu variables from
 *				  function list to an array. Array must be
 *				  allocated by caller
 *   return: NO_ERROR, or ER_FAILED code
 *   funcp(in)		: function structure pointer
 *   array_size(in)     : max size of array (in number of entries)
 *   regu_array(out)    : array of pointers to regu-vars
 *   num_regu		: number of regu vars actually found in list
 */

int
qdata_regu_list_to_regu_array (function_node * function_p, const int array_size, regu_variable_node * regu_array[],
			       int *num_regu)
{
  REGU_VARIABLE_LIST operand = function_p->operand;
  int i, num_args = 0;


  assert (array_size > 0);
  assert (regu_array != NULL);
  assert (function_p != NULL);
  assert (num_regu != NULL);

  *num_regu = 0;
  /* initialize the argument array */
  for (i = 0; i < array_size; i++)
    {
      regu_array[i] = NULL;
    }

  while (operand)
    {
      if (num_args >= array_size)
	{
	  return ER_FAILED;
	}

      regu_array[num_args] = &operand->value;
      *num_regu = ++num_args;
      operand = operand->next;
    }
  return NO_ERROR;
}

/*
 * qdata_insert_substring_function () - Evaluates insert() function.
 *   return: NO_ERROR, or ER_FAILED code
 *   thread_p   : thread context
 *   funcp(in)  : function structure pointer
 *   vd(in)     : value descriptor
 *   obj_oid(in): object identifier
 *   tpl(in)    : tuple
 */
static int
qdata_insert_substring_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
				 OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec)
{
  DB_VALUE *args[NUM_F_INSERT_SUBSTRING_ARGS];
  REGU_VARIABLE *regu_array[NUM_F_INSERT_SUBSTRING_ARGS];
  int i, error_status = NO_ERROR;
  int num_regu = 0;

  /* initialize the argument array */
  for (i = 0; i < NUM_F_INSERT_SUBSTRING_ARGS; i++)
    {
      args[i] = NULL;
      regu_array[i] = NULL;
    }

  error_status = qdata_regu_list_to_regu_array (function_p, NUM_F_INSERT_SUBSTRING_ARGS, regu_array, &num_regu);
  if (num_regu != NUM_F_INSERT_SUBSTRING_ARGS)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_GENERIC_FUNCTION_FAILURE, 0);
      goto error;
    }
  if (error_status != NO_ERROR)
    {
      goto error;
    }

  for (i = 0; i < NUM_F_INSERT_SUBSTRING_ARGS; i++)
    {
      error_status = fetch_peek_dbval (thread_p, regu_array[i], val_desc_p, NULL, obj_oid_p, tplrec, &args[i]);
      if (error_status != NO_ERROR)
	{
	  goto error;
	}
    }

  error_status = db_string_insert_substring (args[0], args[1], args[2], args[3], function_p->value);
  if (error_status != NO_ERROR)
    {
      goto error;
    }

  return NO_ERROR;

error:
  /* no error message set, keep message already set */
  return ER_FAILED;
}

/*
 * qdata_elt() - returns the argument with the index in the parameter list
 *		equal to the value passed in the first argument. Returns
 *		NULL if the first arguments is NULL, is 0, is negative or is
 *		greater than the number of the other arguments.
 */
static int
qdata_elt (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
	   QFILE_TUPLE_RECORD * tplrec)
{
  DB_VALUE *index = NULL;
  REGU_VARIABLE_LIST operand;
  int error_status = NO_ERROR;
  DB_TYPE index_type;
  DB_BIGINT idx = 0;
  DB_VALUE *operand_value = NULL;

  /* should sync with fetch_peek_dbval () */

  assert (function_p);
  assert (function_p->value);
  assert (function_p->operand);

  error_status = fetch_peek_dbval (thread_p, &function_p->operand->value, val_desc_p, NULL, obj_oid_p, tplrec, &index);
  if (error_status != NO_ERROR)
    {
      goto error_exit;
    }

  index_type = DB_VALUE_DOMAIN_TYPE (index);

  switch (index_type)
    {
    case DB_TYPE_SMALLINT:
      idx = db_get_short (index);
      break;
    case DB_TYPE_INTEGER:
      idx = db_get_int (index);
      break;
    case DB_TYPE_BIGINT:
      idx = db_get_bigint (index);
      break;
    case DB_TYPE_NULL:
      db_make_null (function_p->value);
      goto fast_exit;
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      error_status = ER_QPROC_INVALID_DATATYPE;
      goto error_exit;
    }

  if (idx <= 0)
    {
      /* index is 0 or is negative */
      db_make_null (function_p->value);
      goto fast_exit;
    }

  idx--;
  operand = function_p->operand->next;

  while (idx > 0 && operand != NULL)
    {
      operand = operand->next;
      idx--;
    }

  if (operand == NULL)
    {
      /* index greater than number of arguments */
      db_make_null (function_p->value);
      goto fast_exit;
    }

  error_status = fetch_peek_dbval (thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tplrec, &operand_value);
  if (error_status != NO_ERROR)
    {
      goto error_exit;
    }

  /*
   * operand should already be cast to the right type (CHAR)
   */
  error_status = pr_clone_value (operand_value, function_p->value);

fast_exit:
  return error_status;

error_exit:
  return error_status;
}

//
// qdata_benchmark () - "benchmark" function execution; repeatedly run nested operation
//
static int
qdata_benchmark (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		 QFILE_TUPLE_RECORD * tplrec)
{
  assert (function_p);

  if (function_p == NULL || function_p->operand == NULL || function_p->operand->next == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }

  if (function_p->value == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }

  db_make_null (function_p->value);

  REGU_VARIABLE *count_reguvar = &function_p->operand->value;
  REGU_VARIABLE *target_reguvar = &function_p->operand->next->value;

  DB_VALUE *count_value = NULL;
  DB_VALUE *target_value = NULL;

  int error = fetch_peek_dbval (thread_p, count_reguvar, val_desc_p, NULL, obj_oid_p, tplrec, &count_value);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error;
    }

  if (db_value_is_null (count_value))
    {
      return NO_ERROR;
    }

  INT64 count = 0;

  switch (db_value_domain_type (count_value))
    {
    case DB_TYPE_SMALLINT:
      count = STATIC_CAST (INT64, db_get_short (count_value));
      break;
    case DB_TYPE_INTEGER:
      count = STATIC_CAST (INT64, db_get_int (count_value));
      break;
    case DB_TYPE_BIGINT:
      count = db_get_bigint (count_value);
      break;
    default:
      assert (false);
      return ER_FAILED;
    }

  if (count <= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  using bench_clock = std::chrono::system_clock;
  bench_clock::time_point start_timept = bench_clock::now ();

  for (INT64 step = 0; step < count; step++)
    {
      // we're trying to benchmark the expression in target reguvar by running it many times. even if all operands are
      // constant, we still have to repeat the operations: the load marks the target's nodes as row operands, so the
      // resolve_domains never evaluates them once
      //
      // node that they still may be other optimizations that are not so easily disabled
      error = fetch_peek_dbval (thread_p, target_reguvar, val_desc_p, NULL, obj_oid_p, tplrec, &target_value);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error;
	}
      pr_clear_value (target_value);
    }

  bench_clock::time_point end_timept = bench_clock::now ();
  std::chrono::duration < double >secs = end_timept - start_timept;

  db_make_double (function_p->value, secs.count ());
  return NO_ERROR;
}

/*
 * qdata_regexp_function () - Evaluates regexp related functions.
 *   return: NO_ERROR, or ER_FAILED code
 *   thread_p   : thread context
 *   funcp(in)  : function structure pointer
 *   vd(in)     : value descriptor
 *   obj_oid(in): object identifier
 *   tpl(in)    : tuple
 */
static int
qdata_regexp_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
		       OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec)
{
  DB_VALUE *value;
  REGU_VARIABLE_LIST operand;
  int error_status = NO_ERROR;
  int no_args = 0, index = 0;
  DB_VALUE **args;

  {
    assert (function_p != NULL);
    assert (function_p->value != NULL);
    assert (function_p->operand != NULL);

    operand = function_p->operand;

    while (operand != NULL)
      {
	no_args++;
	operand = operand->next;
      }

    args = (DB_VALUE **) db_private_alloc (thread_p, sizeof (DB_VALUE *) * no_args);

    operand = function_p->operand;
    while (operand != NULL)
      {
	error_status = fetch_peek_dbval (thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tplrec, &value);
	if (error_status != NO_ERROR)
	  {
	    goto exit;
	  }

	args[index++] = value;

	operand = operand->next;
      }

    assert (index == no_args);

    // *INDENT-OFF*
    std::function<int(DB_VALUE*, DB_VALUE*[], const int, cub_compiled_regex**)> regexp_func;
    switch (function_p->ftype)
    {
      case F_REGEXP_COUNT:
        regexp_func = db_string_regexp_count;
        break;
      case F_REGEXP_INSTR:
        regexp_func = db_string_regexp_instr;
        break;
      case F_REGEXP_LIKE:
        regexp_func = db_string_regexp_like;
        break;
      case F_REGEXP_REPLACE:
        regexp_func = db_string_regexp_replace;
        break;
      case F_REGEXP_SUBSTR:
        regexp_func = db_string_regexp_substr;
        break;
      default:
        assert (false);
        break;
    }
    // *INDENT-ON*

    if (function_p->tmp_obj == NULL)
      {
	function_p->tmp_obj = new function_tmp_obj;
	function_p->tmp_obj->compiled_regex = new cub_compiled_regex ();
      }

    cub_compiled_regex *&compiled_regex = function_p->tmp_obj->compiled_regex;
    error_status = regexp_func (function_p->value, args, no_args, &compiled_regex);
    if (error_status != NO_ERROR)
      {
	goto exit;
      }
  }

exit:
  db_private_free (thread_p, args);
  return error_status;
}

static int
qdata_convert_operands_to_value_and_call (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
					  OID * obj_oid_p, QFILE_TUPLE_RECORD * tplrec,
					  int (*function_to_call) (DB_VALUE *, DB_VALUE * const *, int const))
{
  DB_VALUE *value;
  REGU_VARIABLE_LIST operand;
  int error_status = NO_ERROR;
  int no_args = 0, index = 0;
  DB_VALUE **args;

  /* should sync with fetch_peek_dbval () */

  assert (function_p != NULL);
  assert (function_p->value != NULL);
  assert (function_p->operand != NULL);

  operand = function_p->operand;

  while (operand != NULL)
    {
      no_args++;
      operand = operand->next;
    }

  args = (DB_VALUE **) db_private_alloc (thread_p, sizeof (DB_VALUE *) * no_args);

  operand = function_p->operand;
  while (operand != NULL)
    {
      error_status = fetch_peek_dbval (thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tplrec, &value);
      if (error_status != NO_ERROR)
	{
	  goto exit;
	}

      args[index++] = value;

      operand = operand->next;
    }

  assert (index == no_args);

  error_status = function_to_call (function_p->value, args, no_args);
  if (error_status != NO_ERROR)
    {
      goto exit;
    }

exit:
  db_private_free (thread_p, args);
  return error_status;
}

/*
 * qdata_get_cardinality () - gets the cardinality of an index using its name
 *			      and partial key count
 *   return: NO_ERROR, or error code
 *   thread_p(in)   : thread context
 *   db_class_name(in): string DB_VALUE holding name of class
 *   db_index_name(in): string DB_VALUE holding name of index (as it appears
 *			in '_db_index' system catalog table
 *   db_key_position(in): integer DB_VALUE holding the partial key index
 *   result_p(out)    : cardinality (integer or NULL DB_VALUE)
 */
int
qdata_get_cardinality (THREAD_ENTRY * thread_p, DB_VALUE * db_class_name, DB_VALUE * db_index_name,
		       DB_VALUE * db_key_position, DB_VALUE * result_p)
{
  char class_name[SM_MAX_IDENTIFIER_LENGTH];
  char index_name[SM_MAX_IDENTIFIER_LENGTH];
  int key_pos = 0;
  int cardinality = 0;
  int error = NO_ERROR;
  DB_TYPE cl_name_arg_type;
  DB_TYPE idx_name_arg_type;
  DB_TYPE key_pos_arg_type;
  int str_class_name_len;
  int str_index_name_len;

  db_make_null (result_p);

  cl_name_arg_type = DB_VALUE_DOMAIN_TYPE (db_class_name);
  idx_name_arg_type = DB_VALUE_DOMAIN_TYPE (db_index_name);
  key_pos_arg_type = DB_VALUE_DOMAIN_TYPE (db_key_position);

  if (DB_IS_NULL (db_class_name) || DB_IS_NULL (db_index_name) || DB_IS_NULL (db_key_position))
    {
      goto exit;
    }

  if (!QSTR_IS_CHAR (cl_name_arg_type) || !QSTR_IS_CHAR (idx_name_arg_type) || key_pos_arg_type != DB_TYPE_INTEGER)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 1, "Arguments type mismatching.");
      error = ER_UNEXPECTED;
      goto exit;
    }

  str_class_name_len = MIN (SM_MAX_IDENTIFIER_LENGTH - 1, db_get_string_size (db_class_name));
  strncpy (class_name, db_get_string (db_class_name), str_class_name_len);
  class_name[str_class_name_len] = '\0';

  str_index_name_len = MIN (SM_MAX_IDENTIFIER_LENGTH - 1, db_get_string_size (db_index_name));
  strncpy (index_name, db_get_string (db_index_name), str_index_name_len);
  index_name[str_index_name_len] = '\0';

  key_pos = db_get_int (db_key_position);

  error = catalog_get_cardinality_by_name (thread_p, class_name, index_name, key_pos, &cardinality);
  if (error == NO_ERROR)
    {
      if (cardinality < 0)
	{
	  db_make_null (result_p);
	}
      else
	{
	  db_make_int (result_p, cardinality);
	}
    }

exit:
  return error;
}

/*
 * qdata_get_estimated_heap_stat () - gets an estimated heap statistic
 *				      of a table using its name
 *   return: NO_ERROR, or error code
 *   thread_p(in)      : thread context
 *   db_table_name(in) : string DB_VALUE holding the unique_name of the table
 *   result_p(out)     : estimated statistic (bigint or NULL DB_VALUE)
 *   op(in)            : which statistic to return
 *
 * Note: If the specified table does not exist, is a view/vclass, or NULL is given,
 *       result_p is set to NULL and NO_ERROR is returned.
 *       heap_get_class_info() is not used because heap_hfid_cache_get() asserts
 *       that the HFID is non-null and ftype == FILE_HEAP, which fails for views.
 */
int
qdata_get_estimated_heap_stat (THREAD_ENTRY * thread_p, DB_VALUE * db_table_name, DB_VALUE * result_p, OPERATOR_TYPE op)
{
  const char *unique_name_str;
  char lower_name[SM_MAX_IDENTIFIER_LENGTH];
  OID class_oid;
  HFID hfid;
  RECDES recdes;
  HEAP_SCANCACHE scan_cache;
  bool scan_cache_opened = false;
  int npages, avg_length;
  INT64 nobjs;
  int error = NO_ERROR;
  int str_len;

  db_make_null (result_p);

  if (DB_IS_NULL (db_table_name))
    {
      goto exit;
    }

  if (!QSTR_IS_CHAR (DB_VALUE_DOMAIN_TYPE (db_table_name)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 1, "Arguments type mismatching.");
      error = ER_UNEXPECTED;
      goto exit;
    }

  str_len = db_get_string_size (db_table_name);
  if (str_len < 0 || str_len >= SM_MAX_IDENTIFIER_LENGTH)
    {
      goto exit;
    }

  unique_name_str = db_get_string (db_table_name);
  if (unique_name_str == NULL)
    {
      goto exit;
    }

  intl_identifier_lower (unique_name_str, lower_name);

  if (xlocator_find_class_oid (thread_p, lower_name, &class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
    {
      er_clear ();
      goto exit;
    }

  (void) heap_scancache_quick_start_root_hfid (thread_p, &scan_cache);
  scan_cache_opened = true;

  if (heap_get_class_record (thread_p, &class_oid, &recdes, &scan_cache, PEEK) != S_SUCCESS)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  or_class_hfid (&recdes, &hfid);
  if (HFID_IS_NULL (&hfid))
    {
      /* view or virtual class — no heap file; return NULL DB_VALUE */
      goto exit;
    }

  error = heap_scancache_end (thread_p, &scan_cache);
  scan_cache_opened = false;
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (heap_estimate (thread_p, &hfid, &npages, &nobjs, &avg_length) < 0)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit;
    }

  switch (op)
    {
    case T_ESTIMATED_TABLE_ROWS:
      db_make_bigint (result_p, (DB_BIGINT) nobjs);
      break;
    case T_ESTIMATED_AVG_ROW_LENGTH:
      db_make_bigint (result_p, (DB_BIGINT) avg_length);
      break;
    case T_ESTIMATED_DATA_LENGTH:
      db_make_bigint (result_p, (DB_BIGINT) npages * DB_PAGESIZE);
      break;
    case T_ESTIMATED_DATA_FREE:
      {
	DB_BIGINT data_free = (DB_BIGINT) npages * DB_PAGESIZE - (DB_BIGINT) nobjs * avg_length;
	db_make_bigint (result_p, data_free > 0 ? data_free : 0);
      }
      break;
    default:
      assert (false);
      break;
    }

exit:
  if (scan_cache_opened)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
    }
  return error;
}

/*
 * qdata_tuple_to_values_array () - construct an array of values from a
 *				    tuple descriptor
 * return : error code or NO_ERROR
 * thread_p (in)    : thread entry
 * tuple (in)	    : tuple descriptor
 * values (in/out)  : values array
 *
 * Note: Values are cloned in the values array
 */
int
qdata_tuple_to_values_array (THREAD_ENTRY * thread_p, qfile_tuple_descriptor * tuple, DB_VALUE ** values)
{
  DB_VALUE *vals;
  int error = NO_ERROR, i;

  assert_release (tuple != NULL);
  assert_release (values != NULL);

  vals = (DB_VALUE *) db_private_alloc (thread_p, tuple->f_cnt * sizeof (DB_VALUE));
  if (vals == NULL)
    {
      error = ER_FAILED;
      goto error_return;
    }

  for (i = 0; i < tuple->f_cnt; i++)
    {
      error = pr_clone_value (tuple->f_valp[i], &vals[i]);
      if (error != NO_ERROR)
	{
	  goto error_return;
	}
    }

  *values = vals;
  return NO_ERROR;

error_return:
  if (vals != NULL)
    {
      int j;
      for (j = 0; j < i; j++)
	{
	  pr_clear_value (&vals[j]);
	}
      db_private_free (thread_p, vals);
    }
  *values = NULL;
  return error;
}

/*
 * qdata_apply_interpolation_function_coercion () - coerce input value for use in
 *					     MEDIAN function evaluation
 *   returns: error code or NO_ERROR
 *   f_value(in): input value
 *   result_dom(in/out): result domain
 *   d_result(out): result as double precision floating point value
 *   result(out): result as DB_VALUE
 */
int
qdata_apply_interpolation_function_coercion (DB_VALUE * f_value, tp_domain ** result_dom, DB_VALUE * result,
					     FUNC_CODE function)
{
  DB_TYPE type;
  double d_result = 0;
  int error = NO_ERROR;

  assert (f_value != NULL && result_dom != NULL && result != NULL);

  /* update result */
  type = db_value_type (f_value);
  switch (type)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_FLOAT:
    case DB_TYPE_DOUBLE:
    case DB_TYPE_MONETARY:
    case DB_TYPE_NUMERIC:
      /* percentile_disc returns the same type as operand while median and percentile_cont return double */
      if (function != PT_PERCENTILE_DISC)
	{
	  if (type == DB_TYPE_SHORT)
	    {
	      d_result = (double) db_get_short (f_value);
	    }
	  else if (type == DB_TYPE_INTEGER)
	    {
	      d_result = (double) db_get_int (f_value);
	    }
	  else if (type == DB_TYPE_BIGINT)
	    {
	      d_result = (double) db_get_bigint (f_value);
	    }
	  else if (type == DB_TYPE_FLOAT)
	    {
	      d_result = (double) db_get_float (f_value);
	    }
	  else if (type == DB_TYPE_DOUBLE)
	    {
	      d_result = (double) db_get_double (f_value);
	    }
	  else if (type == DB_TYPE_MONETARY)
	    {
	      d_result = (db_get_monetary (f_value))->amount;
	    }
	  else if (type == DB_TYPE_NUMERIC)
	    {
	      numeric_coerce_num_to_double (f_value, db_get_numeric_scale (f_value, NULL), &d_result);
	    }

	  db_make_double (result, d_result);
	}
      else
	{
	  pr_clone_value (f_value, result);
	}

      break;

    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_TIME:
      pr_clone_value (f_value, result);
      break;

    default:
      type = TP_DOMAIN_TYPE (*result_dom);
      if (!TP_IS_NUMERIC_TYPE (type) && !TP_IS_DATE_OR_TIME_TYPE (type))
	{
	  error = qdata_update_interpolation_func_value_and_domain (f_value, result, result_dom);
	  if (error != NO_ERROR)
	    {
	      assert (error == ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN);

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, fcode_get_uppercase_name (function),
		      "DOUBLE, DATETIME, TIME");

	      error = ER_FAILED;
	      goto end;
	    }
	}
      else
	{
	  error = db_value_coerce (f_value, result, *result_dom);
	  if (error != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto end;
	    }
	}
    }

end:
  return error;
}

/*
 * qdata_interpolation_function_values () - interpolate two values for use
 *						 in MEDIAN function evaluation
 *   returns: error code or NO_ERROR
 *   f_value(in): "floor" value (i.e. first value in tuple order)
 *   c_value(in): "ceiling" value (i.e. second value in tuple order)
 *   row_num_d(in): row number as floating point value
 *   f_row_num_d(in): row number of f_value as floating point value
 *   c_row_num_d(in): row number of c_value as floating point value
 *   result_dom(in/out): result domain
 *   d_result(out): result as double precision floating point value
 *   result(out): result as DB_VALUE
 */
int
qdata_interpolation_function_values (DB_VALUE * f_value, DB_VALUE * c_value, double row_num_d, double f_row_num_d,
				     double c_row_num_d, tp_domain ** result_dom, DB_VALUE * result, FUNC_CODE function)
{
  DB_DATE date;
  DB_DATETIME datetime;
  DB_TIMESTAMP utime;
  DB_TIME time;
  DB_TYPE type;
  double d1, d2;
  double d_result;
  int error = NO_ERROR;

  assert (f_value != NULL && c_value != NULL && result_dom != NULL && result != NULL);

  /* calculate according to type The formular bellow is from Oracle's MEDIAN manual result = (CRN - RN) * (value for
   * row at FRN) + (RN - FRN) * (value for row at CRN) */
  type = db_value_type (f_value);
  if (!TP_IS_NUMERIC_TYPE (type) && !TP_IS_DATE_OR_TIME_TYPE (type))
    {
      type = TP_DOMAIN_TYPE (*result_dom);
      if (!TP_IS_NUMERIC_TYPE (type) && !TP_IS_DATE_OR_TIME_TYPE (type))
	{
	  /* try to coerce f_value to double, datetime then time and save domain for next coerce */
	  error = qdata_update_interpolation_func_value_and_domain (f_value, f_value, result_dom);
	  if (error != NO_ERROR)
	    {
	      assert (error == ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN);

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, fcode_get_uppercase_name (function),
		      "DOUBLE, DATETIME, TIME");

	      error = ER_FAILED;
	      goto end;
	    }
	}
      else
	{
	  error = db_value_coerce (f_value, f_value, *result_dom);
	  if (error != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto end;
	    }
	}

      /* coerce c_value */
      error = db_value_coerce (c_value, c_value, *result_dom);
      if (error != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto end;
	}
    }

  type = db_value_type (f_value);
  switch (type)
    {
    case DB_TYPE_SHORT:
      d1 = (double) db_get_short (f_value);
      d2 = (double) db_get_short (c_value);

      /* calculate */
      d_result = (c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2;

      db_make_double (result, d_result);

      break;

    case DB_TYPE_INTEGER:
      d1 = (double) db_get_int (f_value);
      d2 = (double) db_get_int (c_value);

      /* calculate */
      d_result = (c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2;

      db_make_double (result, d_result);

      break;

    case DB_TYPE_BIGINT:
      d1 = (double) db_get_bigint (f_value);
      d2 = (double) db_get_bigint (c_value);

      /* calculate */
      d_result = (c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2;

      db_make_double (result, d_result);

      break;

    case DB_TYPE_FLOAT:
      d1 = (double) db_get_float (f_value);
      d2 = (double) db_get_float (c_value);

      /* calculate */
      d_result = (c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2;

      db_make_double (result, d_result);

      break;

    case DB_TYPE_DOUBLE:
      d1 = db_get_double (f_value);
      d2 = db_get_double (c_value);

      /* calculate */
      d_result = (c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2;

      db_make_double (result, d_result);

      break;

    case DB_TYPE_MONETARY:
      d1 = (db_get_monetary (f_value))->amount;
      d2 = (db_get_monetary (c_value))->amount;

      /* calculate */
      d_result = (c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2;

      db_make_double (result, d_result);

      break;

    case DB_TYPE_NUMERIC:
      numeric_coerce_num_to_double (f_value, db_get_numeric_scale (f_value, NULL), &d1);
      numeric_coerce_num_to_double (c_value, db_get_numeric_scale (c_value, NULL), &d2);

      /* calculate */
      d_result = (c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2;

      db_make_double (result, d_result);

      break;

    case DB_TYPE_DATE:
      d1 = (double) *(db_get_date (f_value));
      d2 = (double) *(db_get_date (c_value));
      d_result = (c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2;

      date = (DB_DATE) floor (d_result);

      db_value_put_encoded_date (result, &date);

      break;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
      if (type == DB_TYPE_DATETIMETZ)
	{
	  datetime = db_get_datetimetz (f_value)->datetime;
	}
      else
	{
	  datetime = *(db_get_datetime (f_value));
	}

      d1 = ((double) datetime.date) * MILLISECONDS_OF_ONE_DAY + datetime.time;

      if (type == DB_TYPE_DATETIMETZ)
	{
	  datetime = db_get_datetimetz (c_value)->datetime;
	}
      else
	{
	  datetime = *(db_get_datetime (c_value));
	}

      d2 = ((double) datetime.date) * MILLISECONDS_OF_ONE_DAY + datetime.time;

      d_result = floor ((c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2);

      datetime.date = (unsigned int) (d_result / MILLISECONDS_OF_ONE_DAY);
      datetime.time = (unsigned int) (((DB_BIGINT) d_result) % MILLISECONDS_OF_ONE_DAY);

      if (type == DB_TYPE_DATETIME)
	{
	  db_make_datetime (result, &datetime);
	}
      else if (type == DB_TYPE_DATETIMELTZ)
	{
	  db_make_datetimeltz (result, &datetime);
	}
      else
	{
	  DB_DATETIMETZ dttz1, dttz2;

	  /* if the two timezones are different, we use the first timezone */
	  dttz1.datetime = datetime;
	  dttz1.tz_id = db_get_datetimetz (f_value)->tz_id;

	  error = tz_datetimetz_fix_zone (&dttz1, &dttz2);
	  if (error != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto end;
	    }

	  db_make_datetimetz (result, &dttz2);
	}

      break;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
      if (type == DB_TYPE_TIMESTAMPTZ)
	{
	  db_timestamp_decode_utc (&db_get_timestamptz (f_value)->timestamp, &date, &time);
	}
      else
	{
	  db_timestamp_decode_utc (db_get_timestamp (f_value), &date, &time);
	}

      d1 = ((double) date) * MILLISECONDS_OF_ONE_DAY + time * 1000;

      if (type == DB_TYPE_TIMESTAMPTZ)
	{
	  db_timestamp_decode_utc (&db_get_timestamptz (c_value)->timestamp, &date, &time);
	}
      else
	{
	  db_timestamp_decode_utc (db_get_timestamp (c_value), &date, &time);
	}

      d2 = ((double) date) * MILLISECONDS_OF_ONE_DAY + time * 1000;

      d_result = floor ((c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2);

      date = (unsigned int) (d_result / MILLISECONDS_OF_ONE_DAY);
      time = (unsigned int) (((DB_BIGINT) d_result) % MILLISECONDS_OF_ONE_DAY);
      time /= 1000;

      error = db_timestamp_encode_utc (&date, &time, &utime);
      if (error != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto end;
	}

      if (type == DB_TYPE_TIMESTAMP)
	{
	  db_make_timestamp (result, utime);
	}
      else if (type == DB_TYPE_TIMESTAMPLTZ)
	{
	  db_make_timestampltz (result, utime);
	}
      else
	{
	  DB_TIMESTAMPTZ tstz1, tstz2;

	  /* if the two timezones are different, we use the first timezone */
	  tstz1.timestamp = utime;
	  tstz1.tz_id = db_get_timestamptz (f_value)->tz_id;

	  error = tz_timestamptz_fix_zone (&tstz1, &tstz2);
	  if (error != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto end;
	    }

	  db_make_timestamptz (result, &tstz2);
	}

      break;

    case DB_TYPE_TIME:
      d1 = (double) (*db_get_time (f_value));
      d2 = (double) (*db_get_time (c_value));

      d_result = floor ((c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2);

      time = (DB_TIME) d_result;

      db_value_put_encoded_time (result, &time);
      break;

    default:
      /* never be here! */
      assert (false);
    }

end:
  return error;
}

/*
 * qdata_get_interpolation_function_result () -
 * return : error code or NO_ERROR
 * thread_p (in)     : thread entry
 * scan_id (in)      :
 * domain (in)       :
 * pos (in)          : the pos for REGU_VAR
 * f_number_d (in)   :
 * c_number_d (in)   :
 * result (out)      :
 * result_dom(in/out):
 *
 */
int
qdata_get_interpolation_function_result (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id, tp_domain * domain,
					 int pos, double row_num_d, double f_row_num_d, double c_row_num_d,
					 DB_VALUE * result, tp_domain ** result_dom, FUNC_CODE function)
{
  int error = NO_ERROR;
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
  DB_VALUE *f_value, *c_value;
  DB_VALUE f_fetch_value, c_fetch_value;
  REGU_VARIABLE regu_var;
  SCAN_CODE scan_code;
  DB_BIGINT bi;

  assert (scan_id != NULL && domain != NULL && result != NULL && result_dom != NULL);

  db_make_null (&f_fetch_value);
  db_make_null (&c_fetch_value);

  /* overflow check */
  if (OR_CHECK_BIGINT_OVERFLOW (f_row_num_d))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 0);

      error = ER_FAILED;
      goto end;
    }

  for (bi = (DB_BIGINT) f_row_num_d; bi >= 0; --bi)
    {
      scan_code = qfile_scan_list_next (thread_p, scan_id, &tuple_record, PEEK);
      if (scan_code != S_SUCCESS)
	{
	  error = ER_FAILED;
	  goto end;
	}
    }

  regu_var.type = TYPE_POSITION;
  regu_var.flags = 0;
  regu_var.xasl = NULL;
  regu_var.domain = domain;
  regu_var.value.pos_descr.pos_no = pos;
  regu_var.value.pos_descr.dom = domain;
  regu_var.vfetch_to = &f_fetch_value;

  error = fetch_peek_dbval (thread_p, &regu_var, NULL, NULL, NULL, &tuple_record, &f_value);
  if (error != NO_ERROR)
    {
      error = ER_FAILED;
      goto end;
    }

  pr_clear_value (result);
  if (f_row_num_d == c_row_num_d)
    {
      error = qdata_apply_interpolation_function_coercion (f_value, result_dom, result, function);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }
  else
    {
      /* move to next tuple */
      scan_code = qfile_scan_list_next (thread_p, scan_id, &tuple_record, PEEK);
      if (scan_code != S_SUCCESS)
	{
	  error = ER_FAILED;
	  goto end;
	}

      regu_var.vfetch_to = &c_fetch_value;

      /* get value */
      error = fetch_peek_dbval (thread_p, &regu_var, NULL, NULL, NULL, &tuple_record, &c_value);
      if (error != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto end;
	}

      error =
	qdata_interpolation_function_values (f_value, c_value, row_num_d, f_row_num_d, c_row_num_d, result_dom, result,
					     function);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

end:

  pr_clear_value (&f_fetch_value);
  pr_clear_value (&c_fetch_value);

  return error;
}

/*
 * qdata_update_interpolation_func_value_and_domain () -
 *   return: NO_ERROR or ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN
 *   src_val(in):
 *   dest_val(out):
 *   domain(in/out):
 *
 */
int
qdata_update_interpolation_func_value_and_domain (DB_VALUE * src_val, DB_VALUE * dest_val, TP_DOMAIN ** domain)
{
  int error = NO_ERROR;
  DB_DOMAIN *tmp_domain = NULL;
  TP_DOMAIN_STATUS status;

  assert (src_val != NULL && dest_val != NULL && domain != NULL);

  tmp_domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);

  status = tp_value_cast (src_val, dest_val, tmp_domain, false);
  if (status != DOMAIN_COMPATIBLE)
    {
      /* try datetime */
      tmp_domain = tp_domain_resolve_default (DB_TYPE_DATETIME);
      status = tp_value_cast (src_val, dest_val, tmp_domain, false);
    }

  /* try time */
  if (status != DOMAIN_COMPATIBLE)
    {
      tmp_domain = tp_domain_resolve_default (DB_TYPE_TIME);
      status = tp_value_cast (src_val, dest_val, tmp_domain, false);
    }

  if (status != DOMAIN_COMPATIBLE)
    {
      error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
      goto end;
    }

  /* clear errors from failed casts if any cast attempt succeeds. */
  if (er_errid () != NO_ERROR)
    {
      er_clear ();
    }

  *domain = tmp_domain;

end:

  return error;
}
