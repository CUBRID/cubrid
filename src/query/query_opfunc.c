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

#include "system_parameter.h"
#include "error_manager.h"
#include "fetch.h"
#include "list_file.h"
#include "object_primitive.h"
#include "set_object.h"
#include "query_executor.h"
#include "databases_file.h"
#include "tz_support.h"
#include "numeric_opfunc.h"
#include "db_date.h"
#include "dbtype.h"
#include "query_dump.h"
#include "db_json.hpp"
#include "arithmetic.h"

#include "dbtype.h"

#define NOT_NULL_VALUE(a, b)	((a) ? (a) : (b))
#define INITIAL_OID_STACK_SIZE  1

#define	SYS_CONNECT_BY_PATH_MEM_STEP	256

static int qdata_dummy (THREAD_ENTRY * thread_p, DB_VALUE * result_p, int num_args, DB_VALUE ** args);
static bool qdata_is_zero_value_date (DB_VALUE * dbval_p);

static int qdata_add_short (short s, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_int (int i1, int i2, DB_VALUE * result_p);
static int qdata_add_bigint (DB_BIGINT i1, DB_BIGINT i2, DB_VALUE * result_p);
static int qdata_add_float (float f1, float f2, DB_VALUE * result_p);
static int qdata_add_double (double d1, double d2, DB_VALUE * result_p);
static double qdata_coerce_numeric_to_double (DB_VALUE * numeric_val_p);
static void qdata_coerce_dbval_to_numeric (DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_numeric (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_numeric_to_monetary (DB_VALUE * numeric_val_p, DB_VALUE * monetary_val_p, DB_VALUE * result_p);
static int qdata_add_monetary (double d1, double d2, DB_CURRENCY type, DB_VALUE * result_p);
static int qdata_add_bigint_to_time (DB_VALUE * time_val_p, DB_BIGINT add_time, DB_VALUE * result_p);
static int qdata_add_bigint_to_timetz (DB_VALUE * time_tz_val_p, DB_BIGINT add_time, DB_VALUE * result_p);
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

static int qdata_add_short_to_dbval (DB_VALUE * short_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
				     TP_DOMAIN * domain_p);
static int qdata_add_int_to_dbval (DB_VALUE * int_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p);
static int qdata_add_bigint_to_dbval (DB_VALUE * bigint_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
				      TP_DOMAIN * domain_p);
static int qdata_add_float_to_dbval (DB_VALUE * float_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_double_to_dbval (DB_VALUE * double_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_numeric_to_dbval (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_monetary_to_dbval (DB_VALUE * monetary_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_chars_to_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p);
static int qdata_add_sequence_to_dbval (DB_VALUE * seq_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					TP_DOMAIN * domain_p);
static int qdata_add_time_to_dbval (DB_VALUE * time_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_timetz_to_dbval (DB_VALUE * time_tz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_utime_to_dbval (DB_VALUE * utime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
				     TP_DOMAIN * domain_p);
static int qdata_add_timestamptz_to_dbval (DB_VALUE * ts_tz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_datetime_to_dbval (DB_VALUE * datetime_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					TP_DOMAIN * domain_p);
static int qdata_add_datetimetz_to_dbval (DB_VALUE * datetimetz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_add_date_to_dbval (DB_VALUE * date_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
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
static int qdata_subtract_short_to_dbval (DB_VALUE * short_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_int_to_dbval (DB_VALUE * int_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_bigint_to_dbval (DB_VALUE * bigint_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_float_to_dbval (DB_VALUE * float_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_double_to_dbval (DB_VALUE * double_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_numeric_to_dbval (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_monetary_to_dbval (DB_VALUE * monetary_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_sequence_to_dbval (DB_VALUE * seq_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p,
					     TP_DOMAIN * domain_p);
static int qdata_subtract_time_to_dbval (DB_VALUE * time_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_timetz_to_dbval (DB_VALUE * timetz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_subtract_timeltz_to_dbval (DB_VALUE * timetz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
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

static int qdata_multiply_short (DB_VALUE * short_val_p, short s2, DB_VALUE * result_p);
static int qdata_multiply_int (DB_VALUE * int_val_p, int i2, DB_VALUE * result_p);
static int qdata_multiply_bigint (DB_VALUE * bigint_val_p, DB_BIGINT bi2, DB_VALUE * result_p);
static int qdata_multiply_float (DB_VALUE * float_val_p, float f2, DB_VALUE * result_p);
static int qdata_multiply_double (double d1, double d2, DB_VALUE * result_p);
static int qdata_multiply_numeric (DB_VALUE * numeric_val_p, DB_VALUE * dbval, DB_VALUE * result_p);
static int qdata_multiply_monetary (DB_VALUE * monetary_val_p, double d, DB_VALUE * result_p);

static int qdata_multiply_short_to_dbval (DB_VALUE * short_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_multiply_int_to_dbval (DB_VALUE * int_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_multiply_bigint_to_dbval (DB_VALUE * bigint_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_multiply_float_to_dbval (DB_VALUE * float_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_multiply_double_to_dbval (DB_VALUE * double_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_multiply_numeric_to_dbval (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_multiply_monetary_to_dbval (DB_VALUE * monetary_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
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

static int qdata_divide_short_to_dbval (DB_VALUE * short_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_divide_int_to_dbval (DB_VALUE * int_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_divide_bigint_to_dbval (DB_VALUE * bigint_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_divide_float_to_dbval (DB_VALUE * float_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_divide_double_to_dbval (DB_VALUE * double_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_divide_numeric_to_dbval (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);
static int qdata_divide_monetary_to_dbval (DB_VALUE * monetary_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p);

static int qdata_process_distinct_or_sort (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p, QUERY_ID query_id);

static DB_VALUE *qdata_get_dbval_from_constant_regu_variable (THREAD_ENTRY * thread_p, REGU_VARIABLE * regu_var,
							      VAL_DESCR * val_desc_p);
static int qdata_convert_dbvals_to_set (THREAD_ENTRY * thread_p, DB_TYPE stype, REGU_VARIABLE * func,
					VAL_DESCR * val_desc_p, OID * obj_oid_p, QFILE_TUPLE tuple);
static int qdata_evaluate_generic_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
					    OID * obj_oid_p, QFILE_TUPLE tuple);
static int qdata_get_class_of_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
					OID * obj_oid_p, QFILE_TUPLE tuple);

static int qdata_convert_table_to_set (THREAD_ENTRY * thread_p, DB_TYPE stype, REGU_VARIABLE * func,
				       VAL_DESCR * val_desc_p);

static int qdata_group_concat_first_value (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p, DB_VALUE * dbvalue);

static int qdata_group_concat_value (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p, DB_VALUE * dbvalue);

static int qdata_insert_substring_function (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
					    OID * obj_oid_p, QFILE_TUPLE tuple);

static int qdata_elt (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		      QFILE_TUPLE tuple);

static int
qdata_convert_operands_to_value_and_call (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
					  OID * obj_oid_p, QFILE_TUPLE tuple,
					  int (*function_to_call) (DB_VALUE *, DB_VALUE **, int const));

static int
qdata_json_object (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		   QFILE_TUPLE tuple);

static int
qdata_json_array (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		  QFILE_TUPLE tuple);
static int
qdata_json_insert (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		   QFILE_TUPLE tuple);

static int
qdata_json_replace (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		    QFILE_TUPLE tuple);

static int
qdata_json_set (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		QFILE_TUPLE tuple);

static int
qdata_json_keys (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		 QFILE_TUPLE tuple);

static int
qdata_json_remove (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		   QFILE_TUPLE tuple);

static int
qdata_json_array_append (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
			 QFILE_TUPLE tuple);

static int
qdata_json_get_all_paths (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
			  QFILE_TUPLE tuple);

static int
qdata_json_merge (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		  QFILE_TUPLE tuple);

static int (*generic_func_ptrs[]) (THREAD_ENTRY * thread_p, DB_VALUE *, int, DB_VALUE **) =
{
qdata_dummy};

static int qdata_calculate_aggregate_cume_dist_percent_rank (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p,
							     VAL_DESCR * val_desc_p);

static int qdata_update_agg_interpolation_func_value_and_domain (AGGREGATE_TYPE * agg_p, DB_VALUE * val);

static int qdata_evaluate_interpolation_function (THREAD_ENTRY * thread_p, void *func_p, QFILE_LIST_SCAN_ID * scan_id,
						  bool is_analytic);

/*
 * qdata_dummy () -
 *   return:
 *   res(in)    :
 *   num_args(in)       :
 *   args(in)   :
 *
 * Note: dummy generic function.
 */
static int
qdata_dummy (THREAD_ENTRY * thread_p, DB_VALUE * result_p, int num_args, DB_VALUE ** args)
{
  db_make_null (result_p);
  return ER_FAILED;
}

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
qdata_set_value_list_to_null (VAL_LIST * val_list_p)
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
int
qdata_copy_db_value (DB_VALUE * dest_p, DB_VALUE * src_p)
{
  PR_TYPE *pr_type_p;
  DB_TYPE src_type;

  /* check if there is nothing to do, so we don't clobber a db_value if we happen to try to copy it to itself */
  if (dest_p == src_p)
    {
      return true;
    }

  /* clear any value from a previous iteration */
  (void) pr_clear_value (dest_p);

  src_type = DB_VALUE_DOMAIN_TYPE (src_p);
  pr_type_p = PR_TYPE_FROM_ID (src_type);
  if (pr_type_p == NULL)
    {
      return false;
    }

  if ((*(pr_type_p->setval)) (dest_p, src_p, true) == NO_ERROR)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * qdata_copy_db_value_to_tuple_value () -
 *   return: int (true on success, false on failure)
 *   dbval(in)  : Source dbval node
 *   clear_compressed_string(in): true, if need to clear compressed string
 *   tvalp(in)  :  Tuple value
 *   tval_size(out)      : Set to the tuple value size
 *
 * Note: Copy an db_value to an tuple value.
 * THIS ROUTINE ASSUMES THAT THE VALUE WILL FIT IN THE TPL!!!!
 */
int
qdata_copy_db_value_to_tuple_value (DB_VALUE * dbval_p, bool clear_compressed_string, char *tuple_val_p,
				    int *tuple_val_size)
{
  char *val_p;
  int val_size, align, rc;
  OR_BUF buf;
  PR_TYPE *pr_type;
  DB_TYPE dbval_type;

  if (DB_IS_NULL (dbval_p))
    {
      QFILE_PUT_TUPLE_VALUE_FLAG (tuple_val_p, V_UNBOUND);
      QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_val_p, 0);
      *tuple_val_size = QFILE_TUPLE_VALUE_HEADER_SIZE;
    }
  else
    {
      QFILE_PUT_TUPLE_VALUE_FLAG (tuple_val_p, V_BOUND);
      val_p = (char *) tuple_val_p + QFILE_TUPLE_VALUE_HEADER_SIZE;

      dbval_type = DB_VALUE_DOMAIN_TYPE (dbval_p);
      pr_type = PR_TYPE_FROM_ID (dbval_type);
      if (pr_type == NULL)
	{
	  return ER_FAILED;
	}

      val_size = pr_data_writeval_disk_size (dbval_p);
      OR_BUF_INIT (buf, val_p, val_size);
      rc = (*(pr_type->data_writeval)) (&buf, dbval_p);

      if (rc != NO_ERROR)
	{
	  /* ER_TF_BUFFER_OVERFLOW means that val_size or packing is bad. */
	  assert (rc != ER_TF_BUFFER_OVERFLOW);
	  return ER_FAILED;
	}

      /* Good moment to clear the compressed_string that might have been stored in the DB_VALUE */
      if (clear_compressed_string)
	{
	  if (dbval_type == DB_TYPE_VARCHAR || dbval_type == DB_TYPE_VARNCHAR)
	    {
	      rc = pr_clear_compressed_string (dbval_p);
	      if (rc != NO_ERROR)
		{
		  /* This should not happen for now */
		  assert (false);
		  return ER_FAILED;
		}
	    }
	}

      /* I don't know if the following is still true. */
      /* since each tuple data value field is already aligned with MAX_ALIGNMENT, val_size by itself can be used to
       * find the maximum alignment for the following field which is next val_header */

      align = DB_ALIGN (val_size, MAX_ALIGNMENT);	/* to align for the next field */
      *tuple_val_size = QFILE_TUPLE_VALUE_HEADER_SIZE + align;
      QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_val_p, align);

#if !defined(NDEBUG)
      /* suppress valgrind UMW error */
      memset (tuple_val_p + QFILE_TUPLE_VALUE_HEADER_SIZE + val_size, 0, align - val_size);
#endif
    }

  return NO_ERROR;
}

/*
 * qdata_copy_valptr_list_to_tuple () -
 *   return: NO_ERROR, or ER_code
 *   valptr_list(in)    : Value pointer list
 *   vd(in)     : Value descriptor
 *   tplrec(in) : Tuple descriptor
 *
 * Note: Copy valptr_list values to tuple descriptor.  Regu variables
 * that are hidden columns are not copied to the list file tuple
 */
int
qdata_copy_valptr_list_to_tuple (THREAD_ENTRY * thread_p, VALPTR_LIST * valptr_list_p, VAL_DESCR * val_desc_p,
				 QFILE_TUPLE_RECORD * tuple_record_p)
{
  REGU_VARIABLE_LIST reg_var_p;
  DB_VALUE *dbval_p;
  char *tuple_p;
  int k, tval_size, tlen, tpl_size;
  int n_size, toffset;
  bool clear_compressed_string = false;

  tpl_size = 0;
  tlen = QFILE_TUPLE_LENGTH_SIZE;
  toffset = 0;			/* tuple offset position */

  /* skip the length of the tuple, we'll fill it in after we know what it is */
  tuple_p = (char *) (tuple_record_p->tpl) + tlen;
  toffset += tlen;

  /* copy each value into the tuple */
  reg_var_p = valptr_list_p->valptrp;
  for (k = 0; k < valptr_list_p->valptr_cnt; k++, reg_var_p = reg_var_p->next)
    {
      if (!REGU_VARIABLE_IS_FLAGED (&reg_var_p->value, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  dbval_p = qdata_get_dbval_from_constant_regu_variable (thread_p, &reg_var_p->value, val_desc_p);
	  if (dbval_p == NULL)
	    {
	      return ER_FAILED;
	    }

	  if (REGU_VARIABLE_IS_FLAGED (&reg_var_p->value, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	    {
	      clear_compressed_string = false;
	    }
	  else
	    {
	      clear_compressed_string = true;
	    }

	  n_size = qdata_get_tuple_value_size_from_dbval (dbval_p);
	  if (n_size == ER_FAILED)
	    {
	      return ER_FAILED;
	    }

	  if ((tuple_record_p->size - toffset) < n_size)
	    {
	      /* no space left in tuple to put next item, increase the tuple size by the max of n_size and DB_PAGE_SIZE 
	       * since we can't compute the actual tuple size without re-evaluating the expressions.  This guarantees
	       * that we can at least get the next value into the tuple. */
	      tpl_size = MAX (tuple_record_p->size, QFILE_TUPLE_LENGTH_SIZE);
	      tpl_size += MAX (n_size, DB_PAGESIZE);
	      if (tuple_record_p->size == 0)
		{
		  tuple_record_p->tpl = (char *) db_private_alloc (thread_p, tpl_size);
		  if (tuple_record_p->tpl == NULL)
		    {
		      return ER_FAILED;
		    }
		}
	      else
		{
		  tuple_record_p->tpl = (char *) db_private_realloc (thread_p, tuple_record_p->tpl, tpl_size);
		  if (tuple_record_p->tpl == NULL)
		    {
		      return ER_FAILED;
		    }
		}

	      tuple_record_p->size = tpl_size;
	      tuple_p = (char *) (tuple_record_p->tpl) + toffset;
	    }

	  if (qdata_copy_db_value_to_tuple_value (dbval_p, clear_compressed_string, tuple_p, &tval_size) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  tlen += tval_size;
	  tuple_p += tval_size;
	  toffset += tval_size;
	}
    }

  /* now that we know the tuple size, set it. */
  QFILE_PUT_TUPLE_LENGTH (tuple_record_p->tpl, tlen);

  return NO_ERROR;
}

/*
 * qdata_generate_tuple_desc_for_valptr_list () -
 *   return: QPROC_TPLDESCR_SUCCESS on success or
 *           QP_TPLDESCR_RETRY_xxx,
 *           QPROC_TPLDESCR_FAILURE
 *   valptr_list(in)    : Value pointer list
 *   vd(in)     : Value descriptor
 *   tdp(in)    : Tuple descriptor
 *
 * Note: Generate tuple descriptor for given valptr_list values.
 * Regu variables that are hidden columns are not copied
 * to the list file tuple
 */
QPROC_TPLDESCR_STATUS
qdata_generate_tuple_desc_for_valptr_list (THREAD_ENTRY * thread_p, VALPTR_LIST * valptr_list_p, VAL_DESCR * val_desc_p,
					   QFILE_TUPLE_DESCRIPTOR * tuple_desc_p)
{
  REGU_VARIABLE_LIST reg_var_p;
  int i;
  int value_size;
  QPROC_TPLDESCR_STATUS status = QPROC_TPLDESCR_SUCCESS;
  DB_TYPE dbval_type;

  tuple_desc_p->tpl_size = QFILE_TUPLE_LENGTH_SIZE;	/* set tuple size as header size */
  tuple_desc_p->f_cnt = 0;

  /* copy each value pointer into the each tdp field */
  reg_var_p = valptr_list_p->valptrp;
  for (i = 0; i < valptr_list_p->valptr_cnt; i++)
    {
      if (!REGU_VARIABLE_IS_FLAGED (&reg_var_p->value, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  tuple_desc_p->f_valp[tuple_desc_p->f_cnt] =
	    qdata_get_dbval_from_constant_regu_variable (thread_p, &reg_var_p->value, val_desc_p);

	  if (tuple_desc_p->f_valp[tuple_desc_p->f_cnt] == NULL)
	    {
	      status = QPROC_TPLDESCR_FAILURE;
	      goto exit_with_status;
	    }

	  /* Set clear_f_val_at_clone_decache to avoid memory issues */
	  assert (tuple_desc_p->clear_f_val_at_clone_decache != NULL);
	  if (REGU_VARIABLE_IS_FLAGED (&reg_var_p->value, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	    {
	      tuple_desc_p->clear_f_val_at_clone_decache[tuple_desc_p->f_cnt] = true;
	    }
	  else
	    {
	      tuple_desc_p->clear_f_val_at_clone_decache[tuple_desc_p->f_cnt] = false;
	    }

	  dbval_type = DB_VALUE_DOMAIN_TYPE (tuple_desc_p->f_valp[tuple_desc_p->f_cnt]);

	  /* SET data-type cannot use tuple descriptor */
	  if (pr_is_set_type (dbval_type))
	    {
	      status = QPROC_TPLDESCR_RETRY_SET_TYPE;
	      goto exit_with_status;
	    }

	  /* add aligned field size to tuple size */
	  value_size = qdata_get_tuple_value_size_from_dbval (tuple_desc_p->f_valp[tuple_desc_p->f_cnt]);
	  if (value_size == ER_FAILED)
	    {
	      status = QPROC_TPLDESCR_FAILURE;
	      goto exit_with_status;
	    }

	  /* The compressed string will be deallocated later, after copying db_value into tuple. */

	  tuple_desc_p->tpl_size += value_size;
	  tuple_desc_p->f_cnt += 1;	/* increase field number */
	}

      reg_var_p = reg_var_p->next;
    }

  /* BIG RECORD cannot use tuple descriptor */
  if (tuple_desc_p->tpl_size >= QFILE_MAX_TUPLE_SIZE_IN_PAGE)
    {
      status = QPROC_TPLDESCR_RETRY_BIG_REC;
    }

exit_with_status:

  return status;
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
qdata_set_valptr_list_unbound (THREAD_ENTRY * thread_p, VALPTR_LIST * valptr_list_p, VAL_DESCR * val_desc_p)
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
	    }

	  if (db_value_domain_init (dbval_p, DB_VALUE_DOMAIN_TYPE (dbval_p), DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) !=
	      NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}

      reg_var_p = reg_var_p->next;
    }

  return NO_ERROR;
}

/*
 * ARITHMETIC EXPRESSION EVALUATION ROUTINES
 */

static int
qdata_add_short (short s, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  short result, tmp;

  tmp = db_get_short (dbval_p);
  result = s + tmp;

  if (OR_CHECK_ADD_OVERFLOW (s, tmp, result))
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
qdata_add_numeric (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_VALUE dbval_tmp;

  qdata_coerce_dbval_to_numeric (dbval_p, &dbval_tmp);

  if (numeric_db_value_add (&dbval_tmp, numeric_val_p, result_p) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
      return ER_QPROC_OVERFLOW_ADDITION;
    }

  return NO_ERROR;
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
qdata_add_bigint_to_timetz (DB_VALUE * time_tz_val_p, DB_BIGINT add_time, DB_VALUE * result_p)
{
  DB_TIME utime, time_res;
  DB_TIMETZ *time_tz_p, time_tz_res, time_tz_fixed;
  int hour, minute, second;
  int error = NO_ERROR;
  DB_VALUE time_val_res;

  time_tz_p = db_get_timetz (time_tz_val_p);
  utime = (time_tz_p->time) % SECONDS_OF_ONE_DAY;
  add_time = add_time % SECONDS_OF_ONE_DAY;
  if (add_time < 0)
    {
      error = qdata_subtract_time (utime, (DB_TIME) (-add_time), &time_val_res);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
      assert (DB_VALUE_TYPE (&time_val_res) == DB_TYPE_TIME);
      time_res = *db_get_time (&time_val_res);
      goto return_time_tz;
    }

  time_res = (utime + add_time) % SECONDS_OF_ONE_DAY;

  if (prm_get_integer_value (PRM_ID_COMPAT_MODE) != COMPAT_MYSQL)
    {
      goto return_time_tz;
    }
  else
    {
      DB_TYPE type = DB_VALUE_DOMAIN_TYPE (result_p);

      switch (type)
	{
	case DB_TYPE_BIGINT:
	case DB_TYPE_INTEGER:
	  time_tz_res.time = time_res;
	  time_tz_res.tz_id = time_tz_p->tz_id;
	  error = tz_timetz_fix_zone (&time_tz_res, &time_tz_fixed);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }

	  db_time_decode (&time_tz_fixed.time, &hour, &minute, &second);

	  if (type == DB_TYPE_BIGINT)
	    {
	      error = db_make_bigint (result_p, (hour * 100 + minute) * 100 + second);
	    }
	  else
	    {
	      error = db_make_int (result_p, (hour * 100 + minute) * 100 + second);
	    }
	  break;

	default:
	  break;
	}
      /* return timetz */
    }

return_time_tz:
  /* adjust TZ_ID to new time value */
  time_tz_res.time = time_res;
  time_tz_res.tz_id = time_tz_p->tz_id;
  error = tz_timetz_fix_zone (&time_tz_res, &time_tz_fixed);
  if (error != NO_ERROR)
    {
      return error;
    }
  db_make_timetz (result_p, &time_tz_fixed);

exit:
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
qdata_add_short_to_dbval (DB_VALUE * short_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int err = NO_ERROR;
  DB_VALUE tmp_val;
  short s;
  DB_TYPE type;

  s = db_get_short (short_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_short (s, dbval_p, result_p);

    case DB_TYPE_INTEGER:
      return qdata_add_int (s, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_add_bigint (s, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_add_float (s, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_add_double (s, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_add_numeric (dbval_p, short_val_p, result_p);

    case DB_TYPE_MONETARY:
      return qdata_add_monetary (s, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type, result_p);

    case DB_TYPE_TIME:
      return qdata_add_bigint_to_time (dbval_p, (DB_BIGINT) s, result_p);

    case DB_TYPE_TIMELTZ:
      {
	DB_TIMETZ time_tz;
	time_tz.time = *db_get_time (dbval_p);

	err = tz_create_session_tzid_for_time (&time_tz.time, true, &time_tz.tz_id);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	db_make_timetz (&tmp_val, &time_tz);

	err = qdata_add_bigint_to_timetz (&tmp_val, (DB_BIGINT) s, result_p);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	if (DB_VALUE_TYPE (result_p) == DB_TYPE_TIMETZ)
	  {
	    time_tz = *db_get_timetz (result_p);
	    db_make_timeltz (result_p, &time_tz.time);
	  }
	break;
      }

    case DB_TYPE_TIMETZ:
      return qdata_add_bigint_to_timetz (dbval_p, (DB_BIGINT) s, result_p);

    case DB_TYPE_TIMESTAMP:
      return qdata_add_short_to_utime (dbval_p, s, result_p, domain_p);

    case DB_TYPE_TIMESTAMPLTZ:
      {
	DB_TIMESTAMPTZ ts_tz;
	ts_tz.timestamp = *db_get_timestamp (dbval_p);

	err = tz_create_session_tzid_for_timestamp (&ts_tz.timestamp, &ts_tz.tz_id);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	db_make_timestamptz (&tmp_val, &ts_tz);

	err = qdata_add_short_to_timestamptz (&tmp_val, (DB_BIGINT) s, result_p, domain_p);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	if (DB_VALUE_TYPE (result_p) == DB_TYPE_TIMESTAMPTZ)
	  {
	    ts_tz = *db_get_timestamptz (result_p);
	    db_make_timestampltz (result_p, ts_tz.timestamp);
	  }
	break;
      }

    case DB_TYPE_TIMESTAMPTZ:
      return qdata_add_short_to_timestamptz (dbval_p, s, result_p, domain_p);

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      err = qdata_add_short_to_datetime (dbval_p, s, &tmp_val, domain_p);
      if (err == NO_ERROR && type == DB_TYPE_DATETIMELTZ)
	{
	  db_make_datetimeltz (result_p, db_get_datetime (&tmp_val));
	}
      return err;

    case DB_TYPE_DATETIMETZ:
      db_make_short (&tmp_val, s);
      return qdata_add_datetimetz_to_dbval (dbval_p, &tmp_val, result_p);

    case DB_TYPE_DATE:
      return qdata_add_short_to_date (dbval_p, s, result_p, domain_p);

    default:
      break;
    }

  return err;
}

static int
qdata_add_int_to_dbval (DB_VALUE * int_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int i;
  int err = NO_ERROR;
  DB_TYPE type;
  DB_VALUE tmp_val;

  i = db_get_int (int_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_int (i, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_add_int (i, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_add_bigint (i, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_add_float ((float) i, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_add_double (i, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_add_numeric (dbval_p, int_val_p, result_p);
      break;

    case DB_TYPE_MONETARY:
      return qdata_add_monetary (i, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type, result_p);

    case DB_TYPE_TIME:
      return qdata_add_bigint_to_time (dbval_p, (DB_BIGINT) i, result_p);

    case DB_TYPE_TIMELTZ:
      {
	DB_TIMETZ time_tz;
	time_tz.time = *db_get_time (dbval_p);

	err = tz_create_session_tzid_for_time (&time_tz.time, true, &time_tz.tz_id);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	db_make_timetz (&tmp_val, &time_tz);

	err = qdata_add_bigint_to_timetz (&tmp_val, (DB_BIGINT) i, result_p);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	if (DB_VALUE_TYPE (result_p) == DB_TYPE_TIMETZ)
	  {
	    time_tz = *db_get_timetz (result_p);
	    db_make_timeltz (result_p, &time_tz.time);
	  }
	break;
      }

    case DB_TYPE_TIMETZ:
      return qdata_add_bigint_to_timetz (dbval_p, (DB_BIGINT) i, result_p);

    case DB_TYPE_TIMESTAMP:
      return qdata_add_int_to_utime (dbval_p, i, result_p, domain_p);

    case DB_TYPE_TIMESTAMPLTZ:
      {
	DB_TIMESTAMPTZ ts_tz;
	ts_tz.timestamp = *db_get_timestamp (dbval_p);

	err = tz_create_session_tzid_for_timestamp (&ts_tz.timestamp, &ts_tz.tz_id);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	db_make_timestamptz (&tmp_val, &ts_tz);

	err = qdata_add_int_to_timestamptz (&tmp_val, (DB_BIGINT) i, result_p, domain_p);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	if (DB_VALUE_TYPE (result_p) == DB_TYPE_TIMESTAMPTZ)
	  {
	    ts_tz = *db_get_timestamptz (result_p);
	    db_make_timestampltz (result_p, ts_tz.timestamp);
	  }
	break;
      }

    case DB_TYPE_TIMESTAMPTZ:
      return qdata_add_int_to_timestamptz (dbval_p, i, result_p, domain_p);

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      err = qdata_add_int_to_datetime (dbval_p, i, &tmp_val, domain_p);
      if (err == NO_ERROR && type == DB_TYPE_DATETIMELTZ)
	{
	  db_make_datetimeltz (result_p, db_get_datetime (&tmp_val));
	}
      return err;

    case DB_TYPE_DATETIMETZ:
      db_make_int (&tmp_val, i);
      return qdata_add_datetimetz_to_dbval (dbval_p, &tmp_val, result_p);

    case DB_TYPE_DATE:
      return qdata_add_int_to_date (dbval_p, i, result_p, domain_p);

    default:
      break;
    }

  return err;
}

static int
qdata_add_bigint_to_dbval (DB_VALUE * bigint_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int err = NO_ERROR;
  DB_BIGINT bi;
  DB_TYPE type;
  DB_VALUE tmp_val;

  bi = db_get_bigint (bigint_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_bigint (bi, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_add_bigint (bi, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_add_bigint (bi, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_add_float ((float) bi, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_add_double ((double) bi, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_add_numeric (dbval_p, bigint_val_p, result_p);
      break;

    case DB_TYPE_MONETARY:
      return qdata_add_monetary ((double) bi, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type,
				 result_p);

    case DB_TYPE_TIME:
      return qdata_add_bigint_to_time (dbval_p, bi, result_p);

    case DB_TYPE_TIMELTZ:
      {
	DB_TIMETZ time_tz;
	time_tz.time = *db_get_time (dbval_p);

	err = tz_create_session_tzid_for_time (&time_tz.time, true, &time_tz.tz_id);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	db_make_timetz (&tmp_val, &time_tz);

	err = qdata_add_bigint_to_timetz (&tmp_val, bi, result_p);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	if (DB_VALUE_TYPE (result_p) == DB_TYPE_TIMETZ)
	  {
	    time_tz = *db_get_timetz (result_p);
	    db_make_timeltz (result_p, &time_tz.time);
	  }
	break;
      }

    case DB_TYPE_TIMETZ:
      return qdata_add_bigint_to_timetz (dbval_p, bi, result_p);

    case DB_TYPE_TIMESTAMP:
      return qdata_add_bigint_to_utime (dbval_p, bi, result_p, domain_p);

    case DB_TYPE_TIMESTAMPLTZ:
      {
	DB_TIMESTAMPTZ ts_tz;
	ts_tz.timestamp = *db_get_timestamp (dbval_p);

	err = tz_create_session_tzid_for_timestamp (&ts_tz.timestamp, &ts_tz.tz_id);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	db_make_timestamptz (&tmp_val, &ts_tz);

	err = qdata_add_bigint_to_timestamptz (&tmp_val, bi, result_p, domain_p);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	if (DB_VALUE_TYPE (result_p) == DB_TYPE_TIMESTAMPTZ)
	  {
	    ts_tz = *db_get_timestamptz (result_p);
	    db_make_timestampltz (result_p, ts_tz.timestamp);
	  }
	break;
      }

    case DB_TYPE_TIMESTAMPTZ:
      return qdata_add_bigint_to_timestamptz (dbval_p, bi, result_p, domain_p);

    case DB_TYPE_DATE:
      return qdata_add_bigint_to_date (dbval_p, bi, result_p, domain_p);

    case DB_TYPE_DATETIME:
      return qdata_add_bigint_to_datetime (dbval_p, bi, result_p, domain_p);

    case DB_TYPE_DATETIMELTZ:
      err = qdata_add_bigint_to_datetime (dbval_p, bi, &tmp_val, domain_p);
      if (err == NO_ERROR && type == DB_TYPE_DATETIMELTZ)
	{
	  db_make_datetimeltz (result_p, db_get_datetime (&tmp_val));
	}
      return err;

    case DB_TYPE_DATETIMETZ:
      db_make_bigint (&tmp_val, bi);
      return qdata_add_datetimetz_to_dbval (dbval_p, &tmp_val, result_p);

    default:
      break;
    }

  return err;
}

static int
qdata_add_float_to_dbval (DB_VALUE * float_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  float f1;
  DB_TYPE type;

  f1 = db_get_float (float_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_float (f1, (float) db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_add_float (f1, (float) db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_add_double (f1, (double) db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_add_float (f1, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_add_double (f1, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_add_double (f1, qdata_coerce_numeric_to_double (dbval_p), result_p);

    case DB_TYPE_MONETARY:
      return qdata_add_monetary (f1, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type, result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_add_double_to_dbval (DB_VALUE * double_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  double d1;
  DB_TYPE type;

  d1 = db_get_double (double_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_double (d1, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_add_double (d1, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_add_double (d1, (double) db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_add_double (d1, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_add_double (d1, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_add_double (d1, qdata_coerce_numeric_to_double (dbval_p), result_p);

    case DB_TYPE_MONETARY:
      return qdata_add_monetary (d1, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type, result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_add_numeric_to_dbval (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      return qdata_add_numeric (numeric_val_p, dbval_p, result_p);

    case DB_TYPE_NUMERIC:
      if (numeric_db_value_add (numeric_val_p, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
	  return ER_QPROC_OVERFLOW_ADDITION;
	}
      break;

    case DB_TYPE_FLOAT:
      return qdata_add_double (qdata_coerce_numeric_to_double (numeric_val_p), db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_add_double (qdata_coerce_numeric_to_double (numeric_val_p), db_get_double (dbval_p), result_p);

    case DB_TYPE_MONETARY:
      return qdata_add_numeric_to_monetary (numeric_val_p, dbval_p, result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_add_monetary_to_dbval (DB_VALUE * monetary_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type;
  double d1;
  DB_CURRENCY currency;

  d1 = (db_get_monetary (monetary_val_p))->amount;
  currency = (db_get_monetary (monetary_val_p))->type;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_monetary (d1, db_get_short (dbval_p), currency, result_p);

    case DB_TYPE_INTEGER:
      return qdata_add_monetary (d1, db_get_int (dbval_p), currency, result_p);

    case DB_TYPE_BIGINT:
      return qdata_add_monetary (d1, (double) db_get_bigint (dbval_p), currency, result_p);

    case DB_TYPE_FLOAT:
      return qdata_add_monetary (d1, db_get_float (dbval_p), currency, result_p);

    case DB_TYPE_DOUBLE:
      return qdata_add_monetary (d1, db_get_double (dbval_p), currency, result_p);

    case DB_TYPE_NUMERIC:
      return qdata_add_numeric_to_monetary (dbval_p, monetary_val_p, result_p);

    case DB_TYPE_MONETARY:
      /* Note: we probably should return an error if the two monetaries have different monetary types. */
      return qdata_add_monetary (d1, (db_get_monetary (dbval_p))->amount, currency, result_p);

    default:
      break;
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
qdata_add_timetz_to_dbval (DB_VALUE * time_tz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_add_bigint_to_timetz (time_tz_val_p, (DB_BIGINT) db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_add_bigint_to_timetz (time_tz_val_p, (DB_BIGINT) db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_add_bigint_to_timetz (time_tz_val_p, db_get_bigint (dbval_p), result_p);

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
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
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

/*
 * qdata_add_dbval () -
 *   return: NO_ERROR, or ER_code
 *   dbval1(in) : First db_value node
 *   dbval2(in) : Second db_value node
 *   res(out)   : Resultant db_value node
 *   domain(in) :
 *
 * Note: Add two db_values.
 * Overflow checks are only done when both operand maximums have
 * overlapping precision/scale.  That is,
 *     short + integer -> overflow is checked
 *     float + double  -> overflow is not checked.  Maximum float
 *                        value does not overlap maximum double
 *                        precision/scale.
 *                        MAX_FLT + MAX_DBL = MAX_DBL
 */
int
qdata_add_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_TYPE type1;
  DB_TYPE type2;
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

  /* Enumeration */
  if (type1 == DB_TYPE_ENUMERATION)
    {
      if (TP_IS_CHAR_BIT_TYPE (type2))
	{
	  cast_dom1 = tp_domain_resolve_default (DB_TYPE_VARCHAR);
	}
      else
	{
	  cast_dom1 = tp_domain_resolve_default (DB_TYPE_SMALLINT);
	}

      dom_status = tp_value_auto_cast (dbval1_p, &cast_value1, cast_dom1);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval1_p, cast_dom1);
	  return error;
	}
      error = qdata_add_dbval (&cast_value1, dbval2_p, result_p, domain_p);
      pr_clear_value (&cast_value1);
      return error;
    }
  else if (type2 == DB_TYPE_ENUMERATION)
    {
      if (TP_IS_CHAR_BIT_TYPE (type1))
	{
	  cast_dom2 = tp_domain_resolve_default (DB_TYPE_VARCHAR);
	}
      else
	{
	  cast_dom2 = tp_domain_resolve_default (DB_TYPE_SMALLINT);
	}
      dom_status = tp_value_auto_cast (dbval2_p, &cast_value2, cast_dom2);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval2_p, cast_dom2);
	  return error;
	}
      error = qdata_add_dbval (dbval1_p, &cast_value2, result_p, domain_p);
      pr_clear_value (&cast_value2);
      return error;
    }

  /* plus as concat : when both operands are string or bit */
  if (prm_get_bool_value (PRM_ID_PLUS_AS_CONCAT) == true)
    {
      if (TP_IS_CHAR_BIT_TYPE (type1) && TP_IS_CHAR_BIT_TYPE (type2))
	{
	  return qdata_strcat_dbval (dbval1_p, dbval2_p, result_p, domain_p);
	}
    }

  if (DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  db_make_null (&cast_value1);
  db_make_null (&cast_value2);

  /* not all pairs of operands types can be handled; for some of these pairs, reverse the order of operands to match
   * the handled case */
  /* STRING + NUMBER NUMBER + DATE STRING + DATE */
  if ((TP_IS_CHAR_TYPE (type1) && TP_IS_NUMERIC_TYPE (type2))
      || (TP_IS_NUMERIC_TYPE (type1) && TP_IS_DATE_OR_TIME_TYPE (type2)) || (TP_IS_CHAR_TYPE (type1)
									     && TP_IS_DATE_OR_TIME_TYPE (type2)))
    {
      DB_VALUE *temp = NULL;

      temp = dbval1_p;
      dbval1_p = dbval2_p;
      dbval2_p = temp;
      type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);
      type2 = DB_VALUE_DOMAIN_TYPE (dbval2_p);
    }

  /* number + string : cast string to DOUBLE, add as numbers */
  if (TP_IS_NUMERIC_TYPE (type1) && TP_IS_CHAR_TYPE (type2))
    {
      /* cast string to double */
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }
  /* date + number : cast number to bigint, add as date + bigint */
  /* date + string : cast string to bigint, add as date + bigint */
  else if (TP_IS_DATE_OR_TIME_TYPE (type1) && (TP_IS_FLOATING_NUMBER_TYPE (type2) || TP_IS_CHAR_TYPE (type2)))
    {
      /* cast number to BIGINT */
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_BIGINT);
    }
  /* string + string: cast number to bigint, add as date + bigint */
  else if (TP_IS_CHAR_TYPE (type1) && TP_IS_CHAR_TYPE (type2))
    {
      /* cast number to BIGINT */
      cast_dom1 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }

  if (cast_dom2 != NULL)
    {
      dom_status = tp_value_auto_cast (dbval2_p, &cast_value2, cast_dom2);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval2_p, cast_dom2);
	  return error;
	}
      dbval2_p = &cast_value2;
    }

  if (cast_dom1 != NULL)
    {
      dom_status = tp_value_auto_cast (dbval1_p, &cast_value1, cast_dom1);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval1_p, cast_dom1);
	  return error;
	}
      dbval1_p = &cast_value1;
    }

  type1 = dbval1_p ? DB_VALUE_DOMAIN_TYPE (dbval1_p) : DB_TYPE_NULL;
  type2 = dbval2_p ? DB_VALUE_DOMAIN_TYPE (dbval2_p) : DB_TYPE_NULL;

  if (DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  if (qdata_is_zero_value_date (dbval1_p) || qdata_is_zero_value_date (dbval2_p))
    {
      /* add operation with zero date returns null */
      db_make_null (result_p);
      if (!prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ATTEMPT_TO_USE_ZERODATE, 0);
	  return ER_ATTEMPT_TO_USE_ZERODATE;
	}
      return NO_ERROR;
    }

  switch (type1)
    {
    case DB_TYPE_SHORT:
      error = qdata_add_short_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_INTEGER:
      error = qdata_add_int_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_BIGINT:
      error = qdata_add_bigint_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_FLOAT:
      error = qdata_add_float_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_DOUBLE:
      error = qdata_add_double_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_NUMERIC:
      error = qdata_add_numeric_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_MONETARY:
      error = qdata_add_monetary_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      error = qdata_add_chars_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      if (!TP_IS_SET_TYPE (type2))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
      if (domain_p == NULL)
	{
	  if (type1 == type2)
	    {
	      /* partial resolve : set only basic domain; full domain will be resolved in 'fetch', based on the
	       * result's value */
	      domain_p = tp_domain_resolve_default (type1);
	    }
	  else
	    {
	      domain_p = tp_domain_resolve_default (DB_TYPE_MULTISET);
	    }
	}
      error = qdata_add_sequence_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_TIME:
    case DB_TYPE_TIMELTZ:
      /* we are adding only numbers, is safe to handle TIMELTZ as TIME */
      error = qdata_add_time_to_dbval (dbval1_p, dbval2_p, result_p);
      if (error == NO_ERROR && type1 == DB_TYPE_TIMELTZ)
	{
	  db_make_timeltz (result_p, db_get_time (result_p));
	}
      break;

    case DB_TYPE_TIMETZ:
      error = qdata_add_timetz_to_dbval (dbval1_p, dbval2_p, result_p);
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
      if (error == NO_ERROR && type1 == DB_TYPE_DATETIMELTZ)
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
      return ER_QPROC_INVALID_DATATYPE;
    }

  if (error != NO_ERROR)
    {
      return error;
    }

  return qdata_coerce_result_to_domain (result_p, domain_p);
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
			 TP_DOMAIN * domain_p, const int max_allowed_size, const char *warning_context)
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
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
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
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMETZ:
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
qdata_subtract_short_to_dbval (DB_VALUE * short_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  short s;
  DB_TYPE type2;
  DB_VALUE dbval_tmp;
  DB_TIME *timeval, timetmp;
  DB_DATE *date;
  unsigned int u1, u2, utmp;
  int hour, minute, second;
  int err = NO_ERROR;
  DB_VALUE tmp_val;

  s = db_get_short (short_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_subtract_short (s, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_subtract_int (s, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_subtract_bigint (s, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_subtract_float (s, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_subtract_double (s, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      qdata_coerce_dbval_to_numeric (short_val_p, &dbval_tmp);

      if (numeric_db_value_sub (&dbval_tmp, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
	  return ER_QPROC_OVERFLOW_SUBTRACTION;
	}
      break;

    case DB_TYPE_MONETARY:
      return qdata_subtract_monetary (s, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type,
				      result_p);

    case DB_TYPE_TIME:
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMETZ:
      if (s < 0)
	{
	  timetmp = s + SECONDS_OF_ONE_DAY;
	}
      else
	{
	  timetmp = s;
	}

      if (type2 == DB_TYPE_TIMETZ)
	{
	  DB_TIMETZ *timetz_p;

	  timetz_p = db_get_timetz (dbval_p);
	  timeval = &timetz_p->time;
	}
      else
	{
	  timeval = db_get_time (dbval_p);
	}

      err = qdata_subtract_time (timetmp, (DB_TIME) (*timeval % SECONDS_OF_ONE_DAY), result_p);
      if (err == NO_ERROR && type2 == DB_TYPE_TIMELTZ)
	{
	  db_make_timeltz (result_p, db_get_time (result_p));
	}
      else if (err == NO_ERROR && type2 == DB_TYPE_TIMETZ)
	{
	  DB_TIMETZ time_tz, time_tz_fixed;

	  time_tz = *db_get_timetz (dbval_p);
	  time_tz.time = *db_get_time (result_p);
	  err = tz_timetz_fix_zone (&time_tz, &time_tz_fixed);
	  if (err != NO_ERROR)
	    {
	      break;
	    }
	  db_make_timetz (result_p, &time_tz_fixed);
	}
      return err;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
      db_make_bigint (&tmp_val, (DB_BIGINT) s);
      return qdata_subtract_bigint_to_dbval (&tmp_val, dbval_p, result_p);

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
      db_make_int (&tmp_val, (int) s);
      return qdata_subtract_int_to_dbval (&tmp_val, dbval_p, result_p);

    case DB_TYPE_DATE:
      date = db_get_date (dbval_p);

      u1 = (unsigned int) s;
      u2 = (unsigned int) *date;
      utmp = u1 - u2;

      if (s < 0 || OR_CHECK_UNS_SUB_UNDERFLOW (u1, u2, utmp))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DATE_UNDERFLOW, 0);
	  return ER_FAILED;
	}

      db_time_decode (&utmp, &hour, &minute, &second);
      db_make_time (result_p, hour, minute, second);
      break;

    default:
      break;
    }

  return err;
}

static int
qdata_subtract_int_to_dbval (DB_VALUE * int_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  int i;
  DB_TYPE type;
  DB_VALUE dbval_tmp;
  DB_DATE *date;
  DB_DATETIME *datetime, datetime_tmp;
  unsigned int u1, u2, utmp;
  int day, month, year;
  DB_VALUE tmp_val;
  int err = NO_ERROR;

  i = db_get_int (int_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_subtract_int (i, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_subtract_int (i, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_subtract_bigint (i, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_subtract_float ((float) i, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_subtract_double (i, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      qdata_coerce_dbval_to_numeric (int_val_p, &dbval_tmp);

      if (numeric_db_value_sub (&dbval_tmp, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_MONETARY:
      return qdata_subtract_monetary (i, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type,
				      result_p);

    case DB_TYPE_TIME:
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMETZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
      db_make_bigint (&tmp_val, (DB_BIGINT) i);
      return qdata_subtract_bigint_to_dbval (&tmp_val, dbval_p, result_p);

    case DB_TYPE_DATETIME:
      datetime = db_get_datetime (dbval_p);

      datetime_tmp.date = i / MILLISECONDS_OF_ONE_DAY;
      datetime_tmp.time = i % MILLISECONDS_OF_ONE_DAY;

      return qdata_subtract_datetime (&datetime_tmp, datetime, result_p);

    case DB_TYPE_DATETIMELTZ:
      {
	DB_DATETIME dt_local;

	datetime = db_get_datetime (dbval_p);
	err = tz_datetimeltz_to_local (datetime, &dt_local);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	datetime_tmp.date = i / MILLISECONDS_OF_ONE_DAY;
	datetime_tmp.time = i % MILLISECONDS_OF_ONE_DAY;

	return qdata_subtract_datetime (&datetime_tmp, &dt_local, result_p);
      }

    case DB_TYPE_DATETIMETZ:
      {
	DB_DATETIMETZ dt_tz;
	DB_DATETIME dt_local;

	dt_tz = *db_get_datetimetz (dbval_p);

	err = tz_utc_datetimetz_to_local (&dt_tz.datetime, &dt_tz.tz_id, &dt_local);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	datetime_tmp.date = i / MILLISECONDS_OF_ONE_DAY;
	datetime_tmp.time = i % MILLISECONDS_OF_ONE_DAY;

	return qdata_subtract_datetime (&datetime_tmp, &dt_local, result_p);
      }

    case DB_TYPE_DATE:
      date = db_get_date (dbval_p);

      u1 = (unsigned int) i;
      u2 = (unsigned int) *date;
      utmp = u1 - u2;

      if (i < 0 || OR_CHECK_UNS_SUB_UNDERFLOW (u1, u2, utmp))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DATE_UNDERFLOW, 0);
	  return ER_FAILED;
	}

      db_date_decode (&utmp, &month, &day, &year);
      db_make_date (result_p, month, day, year);
      break;

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_subtract_bigint_to_dbval (DB_VALUE * bigint_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_BIGINT bi;
  DB_TYPE type;
  DB_VALUE dbval_tmp;
  DB_TIME *timeval;
  DB_DATE *date;
  unsigned int u1, u2, utmp;
  DB_UTIME *utime;
  int day, month, year;
  int err = NO_ERROR;

  bi = db_get_bigint (bigint_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_subtract_bigint (bi, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_subtract_bigint (bi, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_subtract_bigint (bi, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_subtract_float ((float) bi, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_subtract_double ((double) bi, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      qdata_coerce_dbval_to_numeric (bigint_val_p, &dbval_tmp);

      if (numeric_db_value_sub (&dbval_tmp, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_MONETARY:
      return qdata_subtract_monetary ((double) bi, (db_get_monetary (dbval_p))->amount,
				      (db_get_monetary (dbval_p))->type, result_p);

    case DB_TYPE_TIME:
      if (bi < 0)
	{
	  bi = (bi % SECONDS_OF_ONE_DAY) + SECONDS_OF_ONE_DAY;
	}
      else
	{
	  bi %= SECONDS_OF_ONE_DAY;
	}

      if (type == DB_TYPE_TIMETZ)
	{
	  DB_TIMETZ *timetz_p;

	  timetz_p = db_get_timetz (dbval_p);
	  timeval = &timetz_p->time;
	}
      else
	{
	  timeval = db_get_time (dbval_p);
	}
      err = qdata_subtract_time ((DB_TIME) bi, (DB_TIME) (*timeval % SECONDS_OF_ONE_DAY), result_p);
      if (err == NO_ERROR && type == DB_TYPE_TIMELTZ)
	{
	  db_make_timeltz (result_p, db_get_time (result_p));
	}
      else if (err == NO_ERROR && type == DB_TYPE_TIMETZ)
	{
	  DB_TIMETZ time_tz, time_tz_fixed;

	  time_tz = *db_get_timetz (dbval_p);
	  time_tz.time = *db_get_time (result_p);
	  err = tz_timetz_fix_zone (&time_tz, &time_tz_fixed);
	  if (err != NO_ERROR)
	    {
	      break;
	    }
	  db_make_timetz (result_p, &time_tz_fixed);
	}
      return err;
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      utime = db_get_timestamp (dbval_p);
      err = qdata_subtract_utime ((DB_UTIME) bi, *utime, result_p);
      if (err != NO_ERROR)
	{
	  break;
	}
      if (err == NO_ERROR && type == DB_TYPE_TIMESTAMPLTZ)
	{
	  db_make_timestampltz (result_p, *db_get_timestamp (result_p));
	}
      return err;

    case DB_TYPE_TIMESTAMPTZ:
      {
	DB_TIMESTAMPTZ ts_tz_res, ts_tz_fixed, *ts_tz_p;

	ts_tz_p = db_get_timestamptz (dbval_p);
	utime = &ts_tz_p->timestamp;
	err = qdata_subtract_utime ((DB_UTIME) bi, *utime, result_p);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	ts_tz_res.timestamp = *db_get_timestamp (result_p);
	ts_tz_res.tz_id = ts_tz_p->tz_id;
	err = tz_timestamptz_fix_zone (&ts_tz_res, &ts_tz_fixed);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	db_make_timestamptz (result_p, &ts_tz_fixed);
	return err;
      }

    case DB_TYPE_DATE:
      date = db_get_date (dbval_p);

      u1 = (unsigned int) bi;
      u2 = (unsigned int) *date;
      utmp = u1 - u2;

      if (bi < 0 || OR_CHECK_UNS_SUB_UNDERFLOW (u1, u2, utmp))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DATE_UNDERFLOW, 0);
	  return ER_FAILED;
	}

      db_date_decode (&utmp, &month, &day, &year);
      db_make_date (result_p, month, day, year);
      break;

    default:
      break;
    }

  return err;
}

static int
qdata_subtract_float_to_dbval (DB_VALUE * float_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  float f;
  DB_TYPE type;

  f = db_get_float (float_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_subtract_float (f, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_subtract_float (f, (float) db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_subtract_float (f, (float) db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_subtract_float (f, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_subtract_double (f, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_subtract_double (f, qdata_coerce_numeric_to_double (dbval_p), result_p);

    case DB_TYPE_MONETARY:
      return qdata_subtract_monetary (f, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type,
				      result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_subtract_double_to_dbval (DB_VALUE * double_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  double d;
  DB_TYPE type;

  d = db_get_double (double_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_subtract_double (d, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_subtract_double (d, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_subtract_double (d, (double) db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_subtract_double (d, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_subtract_double (d, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_subtract_double (d, qdata_coerce_numeric_to_double (dbval_p), result_p);

    case DB_TYPE_MONETARY:
      return qdata_subtract_monetary (d, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type,
				      result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_subtract_numeric_to_dbval (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type;
  DB_VALUE dbval_tmp;

  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      qdata_coerce_dbval_to_numeric (dbval_p, &dbval_tmp);

      if (numeric_db_value_sub (numeric_val_p, &dbval_tmp, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_NUMERIC:
      if (numeric_db_value_sub (numeric_val_p, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_SUBTRACTION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_FLOAT:
      return qdata_subtract_double (qdata_coerce_numeric_to_double (numeric_val_p), db_get_float (dbval_p), result_p);
      break;

    case DB_TYPE_DOUBLE:
      return qdata_subtract_double (qdata_coerce_numeric_to_double (numeric_val_p), db_get_double (dbval_p), result_p);
      break;

    case DB_TYPE_MONETARY:
      return qdata_subtract_monetary (qdata_coerce_numeric_to_double (numeric_val_p),
				      (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type, result_p);
      break;

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_subtract_monetary_to_dbval (DB_VALUE * monetary_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  double d;
  DB_CURRENCY currency;
  DB_TYPE type;

  d = (db_get_monetary (monetary_val_p))->amount;
  currency = (db_get_monetary (monetary_val_p))->type;
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
      return qdata_subtract_monetary (d, db_get_short (dbval_p), currency, result_p);

    case DB_TYPE_INTEGER:
      return qdata_subtract_monetary (d, db_get_int (dbval_p), currency, result_p);

    case DB_TYPE_BIGINT:
      return qdata_subtract_monetary (d, (double) db_get_bigint (dbval_p), currency, result_p);

    case DB_TYPE_FLOAT:
      return qdata_subtract_monetary (d, db_get_float (dbval_p), currency, result_p);

    case DB_TYPE_DOUBLE:
      return qdata_subtract_monetary (d, db_get_double (dbval_p), currency, result_p);

    case DB_TYPE_NUMERIC:
      return qdata_subtract_monetary (d, qdata_coerce_numeric_to_double (dbval_p), currency, result_p);

    case DB_TYPE_MONETARY:
      /* Note: we probably should return an error if the two monetaries have different monetary types. */
      return qdata_subtract_monetary (d, (db_get_monetary (dbval_p))->amount, currency, result_p);

    default:
      break;
    }

  return NO_ERROR;
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

    case DB_TYPE_TIMELTZ:
      err = qdata_subtract_timeltz_to_dbval (dbval_p, time_val_p, result_p);
      if (err != NO_ERROR)
	{
	  break;
	}
      assert (DB_VALUE_TYPE (result_p) == DB_TYPE_INTEGER);
      db_make_int (result_p, -(db_get_int (result_p)));
      break;
    case DB_TYPE_TIMETZ:
      err = qdata_subtract_timetz_to_dbval (dbval_p, time_val_p, result_p);
      if (err != NO_ERROR)
	{
	  break;
	}
      assert (DB_VALUE_TYPE (result_p) == DB_TYPE_INTEGER);
      db_make_int (result_p, -(db_get_int (result_p)));
      break;

    default:
      break;
    }

  return err;
}

static int
qdata_subtract_timetz_to_dbval (DB_VALUE * timetz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  int err = NO_ERROR;
  DB_TYPE type;
  DB_TIMETZ *time_tz_p;

  time_tz_p = db_get_timetz (timetz_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      {
	DB_VALUE time_val, time_val_res;
	DB_TIMETZ time_tz_res, time_tz_fixed;

	db_value_put_encoded_time (&time_val, &time_tz_p->time);

	err = qdata_subtract_time_to_dbval (&time_val, dbval_p, &time_val_res);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	time_tz_res.time = *db_get_time (&time_val_res);
	time_tz_res.tz_id = time_tz_p->tz_id;

	err = tz_timetz_fix_zone (&time_tz_res, &time_tz_fixed);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	db_make_timetz (result_p, &time_tz_fixed);
	break;
      }

    case DB_TYPE_TIME:
      {
	DB_TIME *time_2_p;
	DB_TIMETZ timetz_2;

	time_2_p = db_get_time (dbval_p);

	err = tz_create_timetz_from_ses (time_2_p, &timetz_2);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	db_make_int (result_p, ((int) time_tz_p->time - (int) timetz_2.time));
	break;
      }

    case DB_TYPE_TIMELTZ:
      {
	DB_TIME *time_2_p;

	time_2_p = db_get_time (dbval_p);
	db_make_int (result_p, ((int) time_tz_p->time - (int) *time_2_p));
	break;
      }
    case DB_TYPE_TIMETZ:
      {
	DB_TIMETZ *time_tz_2_p;
	int day1, day2;

	time_tz_2_p = db_get_timetz (dbval_p);
	day1 = get_day_from_timetz (time_tz_p);
	day2 = get_day_from_timetz (time_tz_2_p);

	db_make_int (result_p,
		     ((int) time_tz_p->time + day1 * SECONDS_OF_ONE_DAY - (int) time_tz_2_p->time -
		      day2 * SECONDS_OF_ONE_DAY));
	break;
      }

    default:
      break;
    }

  return err;
}

static int
qdata_subtract_timeltz_to_dbval (DB_VALUE * timetz_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  int err = NO_ERROR;
  DB_TYPE type;
  DB_TIME *time_p;

  time_p = db_get_time (timetz_val_p);
  type = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      {
	DB_VALUE time_val, time_val_res;

	/* create a simple TIME value and perform the operation with it */
	db_value_put_encoded_time (&time_val, time_p);
	err = qdata_subtract_time_to_dbval (&time_val, dbval_p, &time_val_res);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	db_make_timeltz (result_p, db_get_time (&time_val_res));
	break;
      }

    case DB_TYPE_TIME:
    case DB_TYPE_TIMETZ:
      {
	DB_VALUE timetz_val;
	DB_TIMETZ timetz;

	/* perform the operation as TIMETZ operand (keep session timezone when mixing with other data types having
	 * timezone) */
	timetz.time = *time_p;
	err = tz_create_session_tzid_for_time (time_p, true, &timetz.tz_id);
	if (err != NO_ERROR)
	  {
	    break;
	  }
	db_make_timetz (&timetz_val, &timetz);

	err = qdata_subtract_timetz_to_dbval (&timetz_val, dbval_p, result_p);
	break;
      }
    case DB_TYPE_TIMELTZ:
      {
	TZ_ID ses_tz_id1, ses_tz_id2;
	DB_TIME *t2_utc, t1_local, t2_local;

	t2_utc = db_get_time (dbval_p);
	err = tz_create_session_tzid_for_time (time_p, true, &ses_tz_id1);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	err = tz_create_session_tzid_for_time (t2_utc, true, &ses_tz_id2);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	err = tz_utc_timetz_to_local (time_p, &ses_tz_id1, &t1_local);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	err = tz_utc_timetz_to_local (t2_utc, &ses_tz_id2, &t2_local);
	if (err != NO_ERROR)
	  {
	    break;
	  }

	db_make_int (result_p, t1_local - t2_local);
	break;
      }

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
  DB_UTIME *utime1, *utime2;
  DB_TIMESTAMPTZ *ts_tz1_p, *ts_tz2_p, ts_tz_res, ts_tz_res_fixed;
  DB_DATETIME *datetime;
  DB_DATETIME tmp_datetime;
  DB_DATETIMETZ datetime_tz_1;
  DB_DATE date;
  DB_TIME time;
  unsigned int u1;
  short s2;
  int i2;
  DB_BIGINT bi2;
  DB_VALUE tmp_val_res;

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
 * qdata_subtract_dbval () -
 *   return: NO_ERROR, or ER_code
 *   dbval1(in) : First db_value node
 *   dbval2(in) : Second db_value node
 *   res(out)   : Resultant db_value node
 *   domain(in) :
 *
 * Note: Subtract dbval2 value from dbval1 value.
 * Overflow checks are only done when both operand maximums have
 * overlapping precision/scale.  That is,
 *     short - integer -> overflow is checked
 *     float - double  -> overflow is not checked.  Maximum float
 *                        value does not overlap maximum double
 *                        precision/scale.
 *                        MAX_FLT - MAX_DBL = -MAX_DBL
 */
int
qdata_subtract_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_TYPE type1;
  DB_TYPE type2;
  int error = NO_ERROR;
  DB_VALUE cast_value1;
  DB_VALUE cast_value2;
  TP_DOMAIN *cast_dom1 = NULL;
  TP_DOMAIN *cast_dom2 = NULL;
  TP_DOMAIN_STATUS dom_status;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  db_make_null (&cast_value1);
  db_make_null (&cast_value2);

  type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  if (type1 == DB_TYPE_ENUMERATION)
    {
      /* The enumeration will always be casted to SMALLINT */
      cast_dom1 = tp_domain_resolve_default (DB_TYPE_SMALLINT);
      dom_status = tp_value_auto_cast (dbval1_p, &cast_value1, cast_dom1);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval1_p, cast_dom1);
	  return error;
	}
      return qdata_subtract_dbval (&cast_value1, dbval2_p, result_p, domain_p);
    }
  else if (type2 == DB_TYPE_ENUMERATION)
    {
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_SMALLINT);
      dom_status = tp_value_auto_cast (dbval2_p, &cast_value2, cast_dom2);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval2_p, cast_dom2);
	  return error;
	}
      return qdata_subtract_dbval (dbval1_p, &cast_value2, result_p, domain_p);
    }

  /* number - string : cast string to number, substract as numbers */
  if (TP_IS_NUMERIC_TYPE (type1) && TP_IS_CHAR_TYPE (type2))
    {
      /* cast string to double */
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }
  /* string - number: cast string to number, substract as numbers */
  else if (TP_IS_CHAR_TYPE (type1) && TP_IS_NUMERIC_TYPE (type2))
    {
      /* cast string to double */
      cast_dom1 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }
  /* string - string: cast string to number, substract as numbers */
  else if (TP_IS_CHAR_TYPE (type1) && TP_IS_CHAR_TYPE (type2))
    {
      /* cast string to double */
      cast_dom1 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }
  /* date - number : cast floating point number to bigint, date - bigint = date */
  else if (TP_IS_DATE_OR_TIME_TYPE (type1) && TP_IS_FLOATING_NUMBER_TYPE (type2))
    {
      /* cast number to BIGINT */
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_BIGINT);
    }
  /* number - date: cast floating point number to bigint, bigint - date= date */
  else if (TP_IS_FLOATING_NUMBER_TYPE (type1) && TP_IS_DATE_OR_TIME_TYPE (type2))
    {
      /* cast number to BIGINT */
      cast_dom1 = tp_domain_resolve_default (DB_TYPE_BIGINT);
    }
  /* TIME - string : cast string to TIME , date - TIME = bigint */
  /* DATE - string : cast string to DATETIME, the other operand to DATETIME DATETIME - DATETIME = bigint */
  else if (TP_IS_DATE_OR_TIME_TYPE (type1) && TP_IS_CHAR_TYPE (type2))
    {
      if (type1 == DB_TYPE_TIME)
	{
	  cast_dom2 = tp_domain_resolve_default (DB_TYPE_TIME);
	}
      else
	{
	  cast_dom2 = tp_domain_resolve_default (DB_TYPE_DATETIME);

	  if (type1 != DB_TYPE_DATETIME)
	    {
	      cast_dom1 = tp_domain_resolve_default (DB_TYPE_DATETIME);
	    }
	}
    }
  /* string - TIME : cast string to TIME, TIME - TIME = bigint */
  /* string - DATE : cast string to DATETIME, the other operand to DATETIME DATETIME - DATETIME = bigint */
  else if (TP_IS_CHAR_TYPE (type1) && TP_IS_DATE_OR_TIME_TYPE (type2))
    {
      if (type2 == DB_TYPE_TIME)
	{
	  cast_dom1 = tp_domain_resolve_default (DB_TYPE_TIME);
	}
      else
	{
	  /* cast string to same 'date' */
	  cast_dom1 = tp_domain_resolve_default (DB_TYPE_DATETIME);
	  if (type2 != DB_TYPE_DATETIME)
	    {
	      cast_dom2 = tp_domain_resolve_default (DB_TYPE_DATETIME);
	    }
	}
    }

  if (cast_dom1 != NULL)
    {
      dom_status = tp_value_auto_cast (dbval1_p, &cast_value1, cast_dom1);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval1_p, cast_dom1);
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
	  return error;
	}
      dbval2_p = &cast_value2;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  if (DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  if (qdata_is_zero_value_date (dbval1_p) || qdata_is_zero_value_date (dbval2_p))
    {
      /* subtract operation with zero date returns null */
      db_make_null (result_p);
      if (!prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ATTEMPT_TO_USE_ZERODATE, 0);
	  return ER_ATTEMPT_TO_USE_ZERODATE;
	}
      return NO_ERROR;
    }

  switch (type1)
    {
    case DB_TYPE_SHORT:
      error = qdata_subtract_short_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_BIGINT:
      error = qdata_subtract_bigint_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_INTEGER:
      error = qdata_subtract_int_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_FLOAT:
      error = qdata_subtract_float_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_DOUBLE:
      error = qdata_subtract_double_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_NUMERIC:
      error = qdata_subtract_numeric_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_MONETARY:
      error = qdata_subtract_monetary_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      if (!TP_IS_SET_TYPE (type2))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
      if (domain_p == NULL)
	{
	  if (type1 == type2 && type1 == DB_TYPE_SET)
	    {
	      /* partial resolve : set only basic domain; full domain will be resolved in 'fetch', based on the
	       * result's value */
	      domain_p = tp_domain_resolve_default (type1);
	    }
	  else
	    {
	      domain_p = tp_domain_resolve_default (DB_TYPE_MULTISET);
	    }
	}
      error = qdata_subtract_sequence_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_TIME:
      error = qdata_subtract_time_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_TIMELTZ:
      error = qdata_subtract_timeltz_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_TIMETZ:
      error = qdata_subtract_timetz_to_dbval (dbval1_p, dbval2_p, result_p);
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

    case DB_TYPE_STRING:
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (error != NO_ERROR)
    {
      return error;
    }

  return qdata_coerce_result_to_domain (result_p, domain_p);
}

static int
qdata_multiply_short (DB_VALUE * short_val_p, short s2, DB_VALUE * result_p)
{
  /* NOTE that we need volatile to prevent optimizer from generating division expression as multiplication */
  volatile short s1, stmp;

  s1 = db_get_short (short_val_p);
  stmp = s1 * s2;

  if (OR_CHECK_MULT_OVERFLOW (s1, s2, stmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

  db_make_short (result_p, stmp);

  return NO_ERROR;
}

static int
qdata_multiply_int (DB_VALUE * int_val_p, int i2, DB_VALUE * result_p)
{
  /* NOTE that we need volatile to prevent optimizer from generating division expression as multiplication */
  volatile int i1, itmp;

  i1 = db_get_int (int_val_p);
  itmp = i1 * i2;

  if (OR_CHECK_MULT_OVERFLOW (i1, i2, itmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

  db_make_int (result_p, itmp);
  return NO_ERROR;
}

static int
qdata_multiply_bigint (DB_VALUE * bigint_val_p, DB_BIGINT bi2, DB_VALUE * result_p)
{
  /* NOTE that we need volatile to prevent optimizer from generating division expression as multiplication */
  volatile DB_BIGINT bi1, bitmp;

  bi1 = db_get_bigint (bigint_val_p);
  bitmp = bi1 * bi2;

  if (OR_CHECK_MULT_OVERFLOW (bi1, bi2, bitmp))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

  db_make_bigint (result_p, bitmp);
  return NO_ERROR;
}

static int
qdata_multiply_float (DB_VALUE * float_val_p, float f2, DB_VALUE * result_p)
{
  float f1, ftmp;

  f1 = db_get_float (float_val_p);
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
qdata_multiply_numeric (DB_VALUE * numeric_val_p, DB_VALUE * dbval, DB_VALUE * result_p)
{
  DB_VALUE dbval_tmp;

  qdata_coerce_dbval_to_numeric (dbval, &dbval_tmp);

  if (numeric_db_value_mul (numeric_val_p, &dbval_tmp, result_p) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
      return ER_FAILED;
    }

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
qdata_multiply_short_to_dbval (DB_VALUE * short_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  short s;
  DB_TYPE type2;

  s = db_get_short (short_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_multiply_short (dbval_p, s, result_p);

    case DB_TYPE_BIGINT:
      return qdata_multiply_bigint (dbval_p, s, result_p);

    case DB_TYPE_INTEGER:
      return qdata_multiply_int (dbval_p, s, result_p);

    case DB_TYPE_FLOAT:
      return qdata_multiply_float (dbval_p, s, result_p);

    case DB_TYPE_DOUBLE:
      return qdata_multiply_double (db_get_double (dbval_p), s, result_p);

    case DB_TYPE_NUMERIC:
      return qdata_multiply_numeric (dbval_p, short_val_p, result_p);

    case DB_TYPE_MONETARY:
      return qdata_multiply_monetary (dbval_p, s, result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_multiply_int_to_dbval (DB_VALUE * int_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type2;

  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_multiply_int (int_val_p, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_multiply_int (int_val_p, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_multiply_bigint (dbval_p, db_get_int (int_val_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_multiply_float (dbval_p, (float) db_get_int (int_val_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_multiply_double (db_get_double (dbval_p), db_get_int (int_val_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_multiply_numeric (dbval_p, int_val_p, result_p);

    case DB_TYPE_MONETARY:
      return qdata_multiply_monetary (dbval_p, db_get_int (int_val_p), result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_multiply_bigint_to_dbval (DB_VALUE * bigint_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type2;

  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_multiply_bigint (bigint_val_p, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_multiply_bigint (bigint_val_p, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_multiply_bigint (bigint_val_p, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_multiply_float (dbval_p, (float) db_get_bigint (bigint_val_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_multiply_double (db_get_double (dbval_p), (double) db_get_bigint (bigint_val_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_multiply_numeric (dbval_p, bigint_val_p, result_p);

    case DB_TYPE_MONETARY:
      return qdata_multiply_monetary (dbval_p, (double) db_get_bigint (bigint_val_p), result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_multiply_float_to_dbval (DB_VALUE * float_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type2;

  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_multiply_float (float_val_p, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_multiply_float (float_val_p, (float) db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_multiply_float (float_val_p, (float) db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_multiply_float (float_val_p, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_multiply_double (db_get_float (float_val_p), db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_multiply_double (db_get_float (float_val_p), qdata_coerce_numeric_to_double (dbval_p), result_p);

    case DB_TYPE_MONETARY:
      return qdata_multiply_monetary (dbval_p, db_get_float (float_val_p), result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_multiply_double_to_dbval (DB_VALUE * double_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  double d;
  DB_TYPE type2;

  d = db_get_double (double_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)

    {
    case DB_TYPE_SHORT:
      return qdata_multiply_double (d, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_multiply_double (d, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_multiply_double (d, (double) db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_multiply_double (d, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_multiply_double (d, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_multiply_double (d, qdata_coerce_numeric_to_double (dbval_p), result_p);

    case DB_TYPE_MONETARY:
      return qdata_multiply_monetary (dbval_p, d, result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_multiply_numeric_to_dbval (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type2;

  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      return qdata_multiply_numeric (numeric_val_p, dbval_p, result_p);

    case DB_TYPE_NUMERIC:
      if (numeric_db_value_mul (numeric_val_p, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_MULTIPLICATION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_FLOAT:
      return qdata_multiply_double (qdata_coerce_numeric_to_double (numeric_val_p), db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_multiply_double (qdata_coerce_numeric_to_double (numeric_val_p), db_get_double (dbval_p), result_p);

    case DB_TYPE_MONETARY:
      return qdata_multiply_monetary (dbval_p, qdata_coerce_numeric_to_double (numeric_val_p), result_p);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_multiply_monetary_to_dbval (DB_VALUE * monetary_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type2;

  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_multiply_monetary (monetary_val_p, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_multiply_monetary (monetary_val_p, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_multiply_monetary (monetary_val_p, (double) db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_multiply_monetary (monetary_val_p, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_multiply_monetary (monetary_val_p, db_get_double (dbval_p), result_p);

    case DB_TYPE_NUMERIC:
      return qdata_multiply_monetary (monetary_val_p, qdata_coerce_numeric_to_double (dbval_p), result_p);

    case DB_TYPE_MONETARY:
      /* Note: we probably should return an error if the two monetaries have different montetary types. */
      return qdata_multiply_monetary (monetary_val_p, (db_get_monetary (dbval_p))->amount, result_p);

    default:
      break;
    }

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
 * qdata_multiply_dbval () -
 *   return: NO_ERROR, or ER_code
 *   dbval1(in) : First db_value node
 *   dbval2(in) : Second db_value node
 *   res(out)   : Resultant db_value node
 *   domain(in) :
 *
 * Note: Multiply two db_values.
 */
int
qdata_multiply_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_TYPE type1;
  DB_TYPE type2;
  int error = NO_ERROR;
  DB_VALUE cast_value1;
  DB_VALUE cast_value2;
  TP_DOMAIN *cast_dom1 = NULL;
  TP_DOMAIN *cast_dom2 = NULL;
  TP_DOMAIN_STATUS dom_status;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  db_make_null (&cast_value1);
  db_make_null (&cast_value2);

  /* number * string : cast string to DOUBLE, multiply as number * DOUBLE */
  if (TP_IS_NUMERIC_TYPE (type1) && TP_IS_CHAR_TYPE (type2))
    {
      /* cast arg2 to double */
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }
  /* string * number: cast string to DOUBLE, multiply as DOUBLE * number */
  else if (TP_IS_CHAR_TYPE (type1) && TP_IS_NUMERIC_TYPE (type2))
    {
      /* cast arg1 to double */
      cast_dom1 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }
  /* string * string: cast both to DOUBLE, multiply as DOUBLE * DOUBLE */
  else if (TP_IS_CHAR_TYPE (type1) && TP_IS_CHAR_TYPE (type2))
    {
      /* cast number to DOUBLE */
      cast_dom1 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }

  if (cast_dom2 != NULL)
    {
      dom_status = tp_value_auto_cast (dbval2_p, &cast_value2, cast_dom2);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval2_p, cast_dom2);
	  return error;
	}
      dbval2_p = &cast_value2;
    }

  if (cast_dom1 != NULL)
    {
      dom_status = tp_value_auto_cast (dbval1_p, &cast_value1, cast_dom1);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval1_p, cast_dom1);
	  return error;
	}
      dbval1_p = &cast_value1;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  if (DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  switch (type1)
    {
    case DB_TYPE_SHORT:
      error = qdata_multiply_short_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_INTEGER:
      error = qdata_multiply_int_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_BIGINT:
      error = qdata_multiply_bigint_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_FLOAT:
      error = qdata_multiply_float_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_DOUBLE:
      error = qdata_multiply_double_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_NUMERIC:
      error = qdata_multiply_numeric_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_MONETARY:
      error = qdata_multiply_monetary_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      if (!TP_IS_SET_TYPE (type2))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
      if (domain_p == NULL)
	{
	  if (type1 == type2 && type1 == DB_TYPE_SET)
	    {
	      /* partial resolve : set only basic domain; full domain will be resolved in 'fetch', based on the
	       * result's value */
	      domain_p = tp_domain_resolve_default (type1);
	    }
	  else
	    {
	      domain_p = tp_domain_resolve_default (DB_TYPE_MULTISET);
	    }
	}
      error = qdata_multiply_sequence_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_TIME:
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMETZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_DATE:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_STRING:
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (error != NO_ERROR)
    {
      return error;
    }

  return qdata_coerce_result_to_domain (result_p, domain_p);
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

  stmp = s1 / s2;
  db_make_short (result_p, stmp);

  return NO_ERROR;
}

static int
qdata_divide_int (int i1, int i2, DB_VALUE * result_p)
{
  int itmp;

  itmp = i1 / i2;
  db_make_int (result_p, itmp);

  return NO_ERROR;
}

static int
qdata_divide_bigint (DB_BIGINT bi1, DB_BIGINT bi2, DB_VALUE * result_p)
{
  DB_BIGINT bitmp;

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

static int
qdata_divide_short_to_dbval (DB_VALUE * short_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  short s;
  DB_TYPE type2;
  DB_VALUE dbval_tmp;

  s = db_get_short (short_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_divide_short (s, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_divide_int (s, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_divide_bigint (s, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_divide_float (s, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_divide_double (s, db_get_double (dbval_p), result_p, true);

    case DB_TYPE_NUMERIC:
      qdata_coerce_dbval_to_numeric (short_val_p, &dbval_tmp);
      if (numeric_db_value_div (&dbval_tmp, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_MONETARY:
      return qdata_divide_monetary (s, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type, result_p,
				    true);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_divide_int_to_dbval (DB_VALUE * int_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  int i;
  DB_TYPE type2;
  DB_VALUE dbval_tmp;

  i = db_get_int (int_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_divide_int (i, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_divide_int (i, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_divide_bigint (i, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_divide_float ((float) i, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_divide_double (i, db_get_double (dbval_p), result_p, true);

    case DB_TYPE_NUMERIC:
      qdata_coerce_dbval_to_numeric (int_val_p, &dbval_tmp);
      if (numeric_db_value_div (&dbval_tmp, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_MONETARY:
      return qdata_divide_monetary (i, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type, result_p,
				    true);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_divide_bigint_to_dbval (DB_VALUE * bigint_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_BIGINT bi;
  DB_TYPE type2;
  DB_VALUE dbval_tmp;

  bi = db_get_bigint (bigint_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_divide_bigint (bi, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_divide_bigint (bi, db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_divide_bigint (bi, db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_divide_float ((float) bi, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_divide_double ((double) bi, db_get_double (dbval_p), result_p, true);

    case DB_TYPE_NUMERIC:
      qdata_coerce_dbval_to_numeric (bigint_val_p, &dbval_tmp);
      if (numeric_db_value_div (&dbval_tmp, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_MONETARY:
      return qdata_divide_monetary ((double) bi, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type,
				    result_p, true);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_divide_float_to_dbval (DB_VALUE * float_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  float f;
  DB_TYPE type2;

  f = db_get_float (float_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_divide_float (f, db_get_short (dbval_p), result_p);

    case DB_TYPE_INTEGER:
      return qdata_divide_float (f, (float) db_get_int (dbval_p), result_p);

    case DB_TYPE_BIGINT:
      return qdata_divide_float (f, (float) db_get_bigint (dbval_p), result_p);

    case DB_TYPE_FLOAT:
      return qdata_divide_float (f, db_get_float (dbval_p), result_p);

    case DB_TYPE_DOUBLE:
      return qdata_divide_double (f, db_get_double (dbval_p), result_p, true);

    case DB_TYPE_NUMERIC:
      return qdata_divide_double (f, qdata_coerce_numeric_to_double (dbval_p), result_p, false);

    case DB_TYPE_MONETARY:
      return qdata_divide_monetary (f, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type, result_p,
				    true);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_divide_double_to_dbval (DB_VALUE * double_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  double d;
  DB_TYPE type2;

  d = db_get_double (double_val_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_divide_double (d, db_get_short (dbval_p), result_p, false);

    case DB_TYPE_INTEGER:
      return qdata_divide_double (d, db_get_int (dbval_p), result_p, false);

    case DB_TYPE_BIGINT:
      return qdata_divide_double (d, (double) db_get_bigint (dbval_p), result_p, false);

    case DB_TYPE_FLOAT:
      return qdata_divide_double (d, db_get_float (dbval_p), result_p, true);

    case DB_TYPE_DOUBLE:
      return qdata_divide_double (d, db_get_double (dbval_p), result_p, true);

    case DB_TYPE_NUMERIC:
      return qdata_divide_double (d, qdata_coerce_numeric_to_double (dbval_p), result_p, false);

    case DB_TYPE_MONETARY:
      return qdata_divide_monetary (d, (db_get_monetary (dbval_p))->amount, (db_get_monetary (dbval_p))->type, result_p,
				    true);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_divide_numeric_to_dbval (DB_VALUE * numeric_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  DB_TYPE type2;
  DB_VALUE dbval_tmp;

  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
      qdata_coerce_dbval_to_numeric (dbval_p, &dbval_tmp);
      if (numeric_db_value_div (numeric_val_p, &dbval_tmp, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_NUMERIC:
      if (numeric_db_value_div (numeric_val_p, dbval_p, result_p) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_DIVISION, 0);
	  return ER_FAILED;
	}
      break;

    case DB_TYPE_FLOAT:
      return qdata_divide_double (qdata_coerce_numeric_to_double (numeric_val_p), db_get_float (dbval_p), result_p,
				  false);

    case DB_TYPE_DOUBLE:
      return qdata_divide_double (qdata_coerce_numeric_to_double (numeric_val_p), db_get_double (dbval_p), result_p,
				  true);

    case DB_TYPE_MONETARY:
      return qdata_divide_monetary (qdata_coerce_numeric_to_double (numeric_val_p), (db_get_monetary (dbval_p))->amount,
				    (db_get_monetary (dbval_p))->type, result_p, true);

    default:
      break;
    }

  return NO_ERROR;
}

static int
qdata_divide_monetary_to_dbval (DB_VALUE * monetary_val_p, DB_VALUE * dbval_p, DB_VALUE * result_p)
{
  double d;
  DB_CURRENCY currency;
  DB_TYPE type2;

  d = (db_get_monetary (monetary_val_p))->amount;
  currency = (db_get_monetary (monetary_val_p))->type;
  type2 = DB_VALUE_DOMAIN_TYPE (dbval_p);

  switch (type2)
    {
    case DB_TYPE_SHORT:
      return qdata_divide_monetary (d, db_get_short (dbval_p), currency, result_p, false);

    case DB_TYPE_INTEGER:
      return qdata_divide_monetary (d, db_get_int (dbval_p), currency, result_p, false);

    case DB_TYPE_BIGINT:
      return qdata_divide_monetary (d, (double) db_get_bigint (dbval_p), currency, result_p, false);

    case DB_TYPE_FLOAT:
      return qdata_divide_monetary (d, db_get_float (dbval_p), currency, result_p, true);

    case DB_TYPE_DOUBLE:
      return qdata_divide_monetary (d, db_get_double (dbval_p), currency, result_p, true);

    case DB_TYPE_NUMERIC:
      return qdata_divide_monetary (d, qdata_coerce_numeric_to_double (dbval_p), currency, result_p, true);

    case DB_TYPE_MONETARY:
      /* Note: we probably should return an error if the two monetaries have different montetary types. */
      return qdata_divide_monetary (d, (db_get_monetary (dbval_p))->amount, currency, result_p, true);

    default:
      break;
    }

  return NO_ERROR;
}

/*
 * qdata_divide_dbval () -
 *   return: NO_ERROR, or ER_code
 *   dbval1(in) : First db_value node
 *   dbval2(in) : Second db_value node
 *   res(out)   : Resultant db_value node
 *   domain(in) :
 *
 * Note: Divide dbval1 by dbval2
 * Overflow checks are only done when the right operand may be
 * smaller than one.  That is,
 *     short / integer -> overflow is not checked.  Result will
 *                        always be smaller than the numerand.
 *     float / short   -> overflow is not checked.  Minimum float
 *                        representation (e-38) overflows to zero
 *                        which we want.
 *     Because of zero divide checks, most of the others will not
 *     overflow but is still being checked in case we are on a
 *     platform where DBL_EPSILON approaches the value of FLT_MIN.
 */
int
qdata_divide_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  DB_TYPE type1;
  DB_TYPE type2;
  int error = NO_ERROR;
  DB_VALUE cast_value1;
  DB_VALUE cast_value2;
  TP_DOMAIN *cast_dom1 = NULL;
  TP_DOMAIN *cast_dom2 = NULL;
  TP_DOMAIN_STATUS dom_status;

  if ((domain_p != NULL && TP_DOMAIN_TYPE (domain_p) == DB_TYPE_NULL) || DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  db_make_null (&cast_value1);
  db_make_null (&cast_value2);

  /* number / string : cast string to DOUBLE, divide as number / DOUBLE */
  if (TP_IS_NUMERIC_TYPE (type1) && TP_IS_CHAR_TYPE (type2))
    {
      /* cast arg2 to double */
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }
  /* string / number: cast string to DOUBLE, divide as DOUBLE / number */
  else if (TP_IS_CHAR_TYPE (type1) && TP_IS_NUMERIC_TYPE (type2))
    {
      /* cast arg1 to double */
      cast_dom1 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }
  /* string / string: cast both to DOUBLE, divide as DOUBLE / DOUBLE */
  else if (TP_IS_CHAR_TYPE (type1) && TP_IS_CHAR_TYPE (type2))
    {
      /* cast number to DOUBLE */
      cast_dom1 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
      cast_dom2 = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }

  if (cast_dom2 != NULL)
    {
      dom_status = tp_value_auto_cast (dbval2_p, &cast_value2, cast_dom2);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval2_p, cast_dom2);
	  return error;
	}
      dbval2_p = &cast_value2;
    }

  if (cast_dom1 != NULL)
    {
      dom_status = tp_value_auto_cast (dbval1_p, &cast_value1, cast_dom1);
      if (dom_status != DOMAIN_COMPATIBLE)
	{
	  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, dbval1_p, cast_dom1);
	  return error;
	}
      dbval1_p = &cast_value1;
    }

  type1 = DB_VALUE_DOMAIN_TYPE (dbval1_p);
  type2 = DB_VALUE_DOMAIN_TYPE (dbval2_p);

  if (DB_IS_NULL (dbval1_p) || DB_IS_NULL (dbval2_p))
    {
      return NO_ERROR;
    }

  if (qdata_is_divided_zero (dbval2_p))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_ZERO_DIVIDE, 0);
      return ER_FAILED;
    }

  switch (type1)
    {
    case DB_TYPE_SHORT:
      error = qdata_divide_short_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_INTEGER:
      error = qdata_divide_int_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_BIGINT:
      error = qdata_divide_bigint_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_FLOAT:
      error = qdata_divide_float_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_DOUBLE:
      error = qdata_divide_double_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_NUMERIC:
      error = qdata_divide_numeric_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_MONETARY:
      error = qdata_divide_monetary_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_TIME:
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMETZ:
    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
    case DB_TYPE_DATE:
    case DB_TYPE_STRING:
    default:
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == false)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return ER_QPROC_INVALID_DATATYPE;
	}
    }

  if (error != NO_ERROR)
    {
      return error;
    }

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
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
      er_status = tp_value_str_auto_cast_to_number (dbval_p, &cast_value, &res_type);
      if (er_status != NO_ERROR
	  || (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS) == true && res_type != DB_TYPE_DOUBLE))
	{
	  return er_status;
	}

      assert (res_type == DB_TYPE_DOUBLE);

      dbval_p = &cast_value;

      /* fall through */

    case DB_TYPE_DOUBLE:
      db_make_double (result_p, (-1) * db_get_double (dbval_p));
      break;

    case DB_TYPE_NUMERIC:
      db_make_numeric (result_p, db_get_numeric (dbval_p), DB_VALUE_PRECISION (dbval_p), DB_VALUE_SCALE (dbval_p));
      if (numeric_db_value_negate (result_p) != NO_ERROR)
	{
	  return ER_FAILED;
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
qdata_extract_dbval (const MISC_OPERAND extr_operand, DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  if (db_string_extract_dbval (extr_operand, dbval_p, result_p, domain_p) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

int
qdata_json_contains_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * dbval3_p, DB_VALUE * result_p,
			   TP_DOMAIN * domain_p)
{
  int error_code = db_json_contains_dbval (dbval1_p, dbval2_p, dbval3_p, result_p);

  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return qdata_coerce_result_to_domain (result_p, domain_p);
}

int
qdata_json_type_dbval (DB_VALUE * dbval1_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int error_code = db_json_type_dbval (dbval1_p, result_p);

  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return qdata_coerce_result_to_domain (result_p, domain_p);
}

int
qdata_json_valid_dbval (DB_VALUE * dbval1_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  int error_code = db_json_valid_dbval (dbval1_p, result_p);

  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  return qdata_coerce_result_to_domain (result_p, domain_p);
}

int
qdata_json_length_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  return db_json_length_dbval (dbval1_p, dbval2_p, result_p);
}

int
qdata_json_depth_dbval (DB_VALUE * dbval1_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
{
  return db_json_depth_dbval (dbval1_p, result_p);
}

int
qdata_json_extract_dbval (const DB_VALUE * json, const DB_VALUE * path, DB_VALUE * json_res, TP_DOMAIN * domain_p)
{
  return db_json_extract_dbval (json, path, json_res);
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
qdata_strcat_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
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
      error = qdata_add_short_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_INTEGER:
      error = qdata_add_int_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_BIGINT:
      error = qdata_add_bigint_to_dbval (dbval1_p, dbval2_p, result_p, domain_p);
      break;

    case DB_TYPE_FLOAT:
      error = qdata_add_float_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_DOUBLE:
      error = qdata_add_double_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_NUMERIC:
      error = qdata_add_numeric_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_MONETARY:
      error = qdata_add_monetary_to_dbval (dbval1_p, dbval2_p, result_p);
      break;

    case DB_TYPE_NULL:
    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
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
    case DB_TYPE_TIMELTZ:
      error = qdata_add_time_to_dbval (dbval1_p, dbval2_p, result_p);
      if (error != NO_ERROR && type1 == DB_TYPE_TIMELTZ)
	{
	  db_make_timeltz (result_p, db_get_time (result_p));
	}
      break;

    case DB_TYPE_TIMETZ:
      error = qdata_add_timetz_to_dbval (dbval1_p, dbval2_p, result_p);
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
 * Aggregate Expression Evaluation Routines
 */

static int
qdata_process_distinct_or_sort (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p, QUERY_ID query_id)
{
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  QFILE_LIST_ID *list_id_p;
  int ls_flag = QFILE_FLAG_DISTINCT;

  /* since max(distinct a) == max(a), handle these without distinct processing */
  if (agg_p->function == PT_MAX || agg_p->function == PT_MIN)
    {
      agg_p->option = Q_ALL;
      return NO_ERROR;
    }

  type_list.type_cnt = 1;
  type_list.domp = (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *));

  if (type_list.domp == NULL)
    {
      return ER_FAILED;
    }

  type_list.domp[0] = agg_p->operand.domain;
  /* if the agg has ORDER BY force setting 'QFILE_FLAG_ALL' : in this case, no additional SORT_LIST will be created,
   * but the one in the AGGREGATE_TYPE structure will be used */
  if (agg_p->sort_list != NULL)
    {
      ls_flag = QFILE_FLAG_ALL;
    }
  list_id_p = qfile_open_list (thread_p, &type_list, NULL, query_id, ls_flag);

  if (list_id_p == NULL)
    {
      db_private_free_and_init (thread_p, type_list.domp);
      return ER_FAILED;
    }

  db_private_free_and_init (thread_p, type_list.domp);

  qfile_close_list (thread_p, agg_p->list_id);
  qfile_destroy_list (thread_p, agg_p->list_id);

  if (qfile_copy_list_id (agg_p->list_id, list_id_p, true) != NO_ERROR)
    {
      QFILE_FREE_AND_INIT_LIST_ID (list_id_p);
      return ER_FAILED;
    }

  QFILE_FREE_AND_INIT_LIST_ID (list_id_p);

  return NO_ERROR;
}

/*
 * qdata_initialize_aggregate_list () -
 *   return: NO_ERROR, or ER_code
 *   agg_list(in)       : Aggregate expression node list
 *   query_id(in)       : Associated query id
 *
 * Note: Initialize the aggregate expression list.
 */
int
qdata_initialize_aggregate_list (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list_p, QUERY_ID query_id)
{
  AGGREGATE_TYPE *agg_p;

  for (agg_p = agg_list_p; agg_p != NULL; agg_p = agg_p->next)
    {

      /* the value of groupby_num() remains unchanged; it will be changed while evaluating groupby_num predicates
       * against each group at 'xs_eval_grbynum_pred()' */
      if (agg_p->function == PT_GROUPBY_NUM)
	{
	  /* nothing to do with groupby_num() */
	  continue;
	}

      agg_p->accumulator.curr_cnt = 0;
      if (db_value_domain_init (agg_p->accumulator.value, DB_VALUE_DOMAIN_TYPE (agg_p->accumulator.value),
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      /* This set is made, because if class is empty, aggregate results should return NULL, except count(*) and count */
      if (agg_p->function == PT_COUNT_STAR || agg_p->function == PT_COUNT)
	{
	  db_make_int (agg_p->accumulator.value, 0);
	}

      /* create temporary list file to handle distincts */
      if (agg_p->option == Q_DISTINCT || agg_p->sort_list != NULL)
	{
	  /* NOTE: cume_dist and percent_rank do NOT need sorting */
	  if (agg_p->function != PT_CUME_DIST && agg_p->function != PT_PERCENT_RANK)
	    {
	      if (qdata_process_distinct_or_sort (thread_p, agg_p, query_id) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }
	}

      if (agg_p->function == PT_CUME_DIST || agg_p->function == PT_PERCENT_RANK)
	{
	  /* init info.dist_percent */
	  agg_p->info.dist_percent.const_array = NULL;
	  agg_p->info.dist_percent.list_len = 0;
	  agg_p->info.dist_percent.nlargers = 0;
	}
      else
	{
	  /* If there are other functions need initializing. Do it here. */
	  ;
	}
    }

  return NO_ERROR;
}

/*
 * qdata_aggregate_accumulator_to_accumulator () - aggregate two accumulators
 *   return: error code or NO_ERROR
 *   thread_p(in): thread
 *   acc(in/out): source1 and target accumulator
 *   acc_dom(in): accumulator domain
 *   func_type(in): function
 *   func_domain(in): function domain
 *   new_acc(in): source2 accumulator
 */
int
qdata_aggregate_accumulator_to_accumulator (THREAD_ENTRY * thread_p, AGGREGATE_ACCUMULATOR * acc,
					    AGGREGATE_ACCUMULATOR_DOMAIN * acc_dom, FUNC_TYPE func_type,
					    TP_DOMAIN * func_domain, AGGREGATE_ACCUMULATOR * new_acc)
{
  TP_DOMAIN *double_domain;
  int error = NO_ERROR;

  switch (func_type)
    {
    case PT_GROUPBY_NUM:
    case PT_COUNT_STAR:
      /* do nothing */
      break;

    case PT_MIN:
    case PT_MAX:
    case PT_COUNT:
    case PT_AGG_BIT_AND:
    case PT_AGG_BIT_OR:
    case PT_AGG_BIT_XOR:
    case PT_AVG:
    case PT_SUM:
      /* these functions only affect acc.value and new_acc can be treated as an ordinary value */
      error = qdata_aggregate_value_to_accumulator (thread_p, acc, acc_dom, func_type, func_domain, new_acc->value);
      break;

    case PT_STDDEV:
    case PT_STDDEV_POP:
    case PT_STDDEV_SAMP:
    case PT_VARIANCE:
    case PT_VAR_POP:
    case PT_VAR_SAMP:
      /* we don't copy operator; default domain is double */
      double_domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);

      if (acc->curr_cnt < 1 && new_acc->curr_cnt >= 1)
	{
	  /* initialize domains */
	  if (db_value_domain_init (acc->value, DB_TYPE_DOUBLE, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  if (db_value_domain_init (acc->value2, DB_TYPE_DOUBLE, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* clear values */
	  pr_clear_value (acc->value);
	  pr_clear_value (acc->value2);

	  /* set values */
	  (*(double_domain->type->setval)) (acc->value, new_acc->value, true);
	  (*(double_domain->type->setval)) (acc->value2, new_acc->value2, true);
	}
      else if (acc->curr_cnt >= 1 && new_acc->curr_cnt >= 1)
	{
	  /* acc.value += new_acc.value */
	  if (qdata_add_dbval (acc->value, new_acc->value, acc->value, double_domain) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* acc.value2 += new_acc.value2 */
	  if (qdata_add_dbval (acc->value2, new_acc->value2, acc->value2, double_domain) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      else
	{
	  /* we don't treat cases when new_acc or both accumulators are uninitialized */
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }

  /* increase tuple count */
  acc->curr_cnt += new_acc->curr_cnt;

  /* all ok */
  return error;
}

/*
 * qdata_aggregate_value_to_accumulator () - aggregate a value to accumulator
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread
 *   acc(in): accumulator
 *   domain(in): accumulator domain
 *   func_type(in): function type
 *   func_domain(in): function domain
 *   value(in): value
 */
int
qdata_aggregate_value_to_accumulator (THREAD_ENTRY * thread_p, AGGREGATE_ACCUMULATOR * acc,
				      AGGREGATE_ACCUMULATOR_DOMAIN * domain, FUNC_TYPE func_type,
				      TP_DOMAIN * func_domain, DB_VALUE * value)
{
  DB_VALUE squared;
  bool copy_operator = false;
  int coll_id;

  if (DB_IS_NULL (value))
    {
      return NO_ERROR;
    }

  coll_id = domain->value_dom->collation_id;

  /* aggregate new value */
  switch (func_type)
    {
    case PT_MIN:
      if (acc->curr_cnt < 1 || (*(domain->value_dom->type->cmpval)) (acc->value, value, 1, 1, NULL, coll_id) > 0)
	{
	  /* we have new minimum */
	  copy_operator = true;
	}
      break;

    case PT_MAX:
      if (acc->curr_cnt < 1 || (*(domain->value_dom->type->cmpval)) (acc->value, value, 1, 1, NULL, coll_id) < 0)
	{
	  /* we have new maximum */
	  copy_operator = true;
	}
      break;

    case PT_COUNT:
      if (acc->curr_cnt < 1)
	{
	  /* first value */
	  db_make_int (acc->value, 1);
	}
      else
	{
	  /* increment */
	  db_make_int (acc->value, db_get_int (acc->value) + 1);
	}
      break;

    case PT_AGG_BIT_AND:
    case PT_AGG_BIT_OR:
    case PT_AGG_BIT_XOR:
      {
	int error;
	DB_VALUE tmp_val;
	db_make_bigint (&tmp_val, (DB_BIGINT) 0);

	if (acc->curr_cnt < 1)
	  {
	    /* init result value */
	    if (!DB_IS_NULL (value))
	      {
		if (qdata_bit_or_dbval (&tmp_val, value, acc->value, domain->value_dom) != NO_ERROR)
		  {
		    return ER_FAILED;
		  }
	      }
	  }
	else
	  {
	    /* update result value */
	    if (!DB_IS_NULL (value))
	      {
		if (DB_IS_NULL (acc->value))
		  {
		    /* basically an initialization */
		    if (qdata_bit_or_dbval (&tmp_val, value, acc->value, domain->value_dom) != NO_ERROR)
		      {
			return ER_FAILED;
		      }
		  }
		else
		  {
		    /* actual computation */
		    if (func_type == PT_AGG_BIT_AND)
		      {
			error = qdata_bit_and_dbval (acc->value, value, acc->value, domain->value_dom);
		      }
		    else if (func_type == PT_AGG_BIT_OR)
		      {
			error = qdata_bit_or_dbval (acc->value, value, acc->value, domain->value_dom);
		      }
		    else
		      {
			error = qdata_bit_xor_dbval (acc->value, value, acc->value, domain->value_dom);
		      }

		    if (error != NO_ERROR)
		      {
			return ER_FAILED;
		      }
		  }
	      }
	  }
      }
      break;

    case PT_AVG:
    case PT_SUM:
      if (acc->curr_cnt < 1)
	{
	  copy_operator = true;
	}
      else
	{
	  /* values are added up in acc.value */
	  if (qdata_add_dbval (acc->value, value, acc->value, domain->value_dom) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
      break;

    case PT_STDDEV:
    case PT_STDDEV_POP:
    case PT_STDDEV_SAMP:
    case PT_VARIANCE:
    case PT_VAR_POP:
    case PT_VAR_SAMP:
      /* coerce value to DOUBLE domain */
      if (tp_value_coerce (value, value, domain->value_dom) != DOMAIN_COMPATIBLE)
	{
	  return ER_FAILED;
	}

      if (acc->curr_cnt < 1)
	{
	  /* calculate X^2 */
	  if (qdata_multiply_dbval (value, value, &squared, domain->value2_dom) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* clear values */
	  pr_clear_value (acc->value);
	  pr_clear_value (acc->value2);

	  /* set values */
	  (*(domain->value_dom->type->setval)) (acc->value, value, true);
	  (*(domain->value2_dom->type->setval)) (acc->value2, &squared, true);
	}
      else
	{
	  /* compute X^2 */
	  if (qdata_multiply_dbval (value, value, &squared, domain->value2_dom) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* acc.value += X */
	  if (qdata_add_dbval (acc->value, value, acc->value, domain->value_dom) != NO_ERROR)
	    {
	      pr_clear_value (&squared);
	      return ER_FAILED;
	    }

	  /* acc.value += X^2 */
	  if (qdata_add_dbval (acc->value2, &squared, acc->value2, domain->value2_dom) != NO_ERROR)
	    {
	      pr_clear_value (&squared);
	      return ER_FAILED;
	    }

	  /* done with squared */
	  pr_clear_value (&squared);
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }

  /* copy operator if necessary */
  if (copy_operator)
    {
      DB_TYPE type = DB_VALUE_DOMAIN_TYPE (value);
      pr_clear_value (acc->value);

      if (TP_DOMAIN_TYPE (domain->value_dom) != type)
	{
	  int coerce_error = db_value_coerce (value, acc->value, domain->value_dom);
	  if (coerce_error != NO_ERROR)
	    {
	      /* set error here */
	      return ER_FAILED;
	    }
	}
      else
	{
	  pr_clone_value (value, acc->value);
	}
    }

  /* clear value and exit nicely */
  return NO_ERROR;
}

/*
 * qdata_evaluate_aggregate_list () -
 *   return: NO_ERROR, or ER_code
 *   agg_list(in): aggregate expression node list
 *   val_desc_p(in): value descriptor
 *   alt_acc_list(in): alternate accumulator list
 *
 * Note: Evaluate given aggregate expression list.
 * Note2: If alt_acc_list is not provided, default accumulators will be used.
 *        Alternate accumulators can not be used for DISTINCT processing or
 *        the GROUP_CONCAT and MEDIAN function.
 */
int
qdata_evaluate_aggregate_list (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list_p, VAL_DESCR * val_desc_p,
			       AGGREGATE_ACCUMULATOR * alt_acc_list)
{
  AGGREGATE_TYPE *agg_p;
  AGGREGATE_ACCUMULATOR *accumulator;
  DB_VALUE dbval, *percentile_val = NULL;
  PR_TYPE *pr_type_p;
  DB_TYPE dbval_type;
  OR_BUF buf;
  char *disk_repr_p = NULL;
  int dbval_size, i, error;
  AGGREGATE_PERCENTILE_INFO *percentile = NULL;

  db_make_null (&dbval);

  for (agg_p = agg_list_p, i = 0; agg_p != NULL; agg_p = agg_p->next, i++)
    {
      /* determine accumulator */
      accumulator = (alt_acc_list != NULL ? &alt_acc_list[i] : &agg_p->accumulator);

      if (agg_p->flag_agg_optimize)
	{
	  continue;
	}

      if (agg_p->function == PT_COUNT_STAR)
	{			/* increment and continue */
	  accumulator->curr_cnt++;
	  continue;
	}

      /* 
       * the value of groupby_num() remains unchanged;
       * it will be changed while evaluating groupby_num predicates
       * against each group at 'xs_eval_grbynum_pred()'
       */
      if (agg_p->function == PT_GROUPBY_NUM)
	{
	  /* nothing to do with groupby_num() */
	  continue;
	}

      if (agg_p->function == PT_CUME_DIST || agg_p->function == PT_PERCENT_RANK)
	{
	  /* CUME_DIST and PERCENT_RANK use a REGU_VAR_LIST reguvar as operator and are treated in a special manner */
	  error = qdata_calculate_aggregate_cume_dist_percent_rank (thread_p, agg_p, val_desc_p);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

	  continue;
	}

      /* 
       * fetch operand value. aggregate regulator variable should only
       * contain constants
       */
      if (fetch_copy_dbval (thread_p, &agg_p->operand, val_desc_p, NULL, NULL, NULL, &dbval) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      /* eliminate null values */
      if (DB_IS_NULL (&dbval))
	{
	  if ((agg_p->function == PT_COUNT || agg_p->function == PT_COUNT_STAR) && DB_IS_NULL (accumulator->value))
	    {
	      /* we might get a NULL count if aggregating with hash table and group has only one tuple; correct that */
	      db_make_int (accumulator->value, 0);
	    }
	  continue;
	}

      /* 
       * handle distincts by inserting each operand into a list file,
       * which will be distinct-ified and counted/summed/averaged
       * in qdata_finalize_aggregate_list ()
       */
      if (agg_p->option == Q_DISTINCT || agg_p->sort_list != NULL)
	{
	  /* convert domain to the median domains (number, date/time) to make 1,2,11 '1','2','11' result the same */
	  if (QPROC_IS_INTERPOLATION_FUNC (agg_p))
	    {
	      /* never be null type */
	      assert (!DB_IS_NULL (&dbval));

	      error = qdata_update_agg_interpolation_func_value_and_domain (agg_p, &dbval);
	      if (error != NO_ERROR)
		{
		  pr_clear_value (&dbval);
		  return ER_FAILED;
		}
	    }

	  dbval_type = DB_VALUE_DOMAIN_TYPE (&dbval);
	  pr_type_p = PR_TYPE_FROM_ID (dbval_type);

	  if (pr_type_p == NULL)
	    {
	      pr_clear_value (&dbval);
	      return ER_FAILED;
	    }

	  dbval_size = pr_data_writeval_disk_size (&dbval);
	  if (dbval_size > 0 && (disk_repr_p = (char *) db_private_alloc (thread_p, dbval_size)) != NULL)
	    {
	      OR_BUF_INIT (buf, disk_repr_p, dbval_size);
	      error = (*(pr_type_p->data_writeval)) (&buf, &dbval);
	      if (error != NO_ERROR)
		{
		  /* ER_TF_BUFFER_OVERFLOW means that val_size or packing is bad. */
		  assert (error != ER_TF_BUFFER_OVERFLOW);

		  db_private_free_and_init (thread_p, disk_repr_p);
		  pr_clear_value (&dbval);
		  return ER_FAILED;
		}
	    }
	  else
	    {
	      pr_clear_value (&dbval);
	      return ER_FAILED;
	    }

	  if (qfile_add_item_to_list (thread_p, disk_repr_p, dbval_size, agg_p->list_id) != NO_ERROR)
	    {
	      db_private_free_and_init (thread_p, disk_repr_p);
	      pr_clear_value (&dbval);
	      return ER_FAILED;
	    }

	  db_private_free_and_init (thread_p, disk_repr_p);
	  pr_clear_value (&dbval);

	  /* for PERCENTILE funcs, we have to check percentile value */
	  if (agg_p->function != PT_PERCENTILE_CONT && agg_p->function != PT_PERCENTILE_DISC)
	    {
	      continue;
	    }
	}

      if (QPROC_IS_INTERPOLATION_FUNC (agg_p))
	{
	  percentile = &agg_p->info.percentile;
	  /* when build value */
	  if (agg_p->function == PT_PERCENTILE_CONT || agg_p->function == PT_PERCENTILE_DISC)
	    {
	      assert (percentile->percentile_reguvar != NULL);

	      error = fetch_peek_dbval (thread_p, percentile->percentile_reguvar, val_desc_p, NULL, NULL, NULL,
					&percentile_val);
	      if (error != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);

		  return ER_FAILED;
		}
	    }

	  if (agg_p->accumulator.curr_cnt < 1)
	    {
	      if (agg_p->function == PT_PERCENTILE_CONT || agg_p->function == PT_PERCENTILE_DISC)
		{
		  if (DB_VALUE_TYPE (percentile_val) != DB_TYPE_DOUBLE)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
		      return ER_FAILED;
		    }

		  percentile->cur_group_percentile = db_get_double (percentile_val);
		  if (percentile->cur_group_percentile < 0 || percentile->cur_group_percentile > 1)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PERCENTILE_FUNC_INVALID_PERCENTILE_RANGE, 1,
			      percentile->cur_group_percentile);
		      return ER_FAILED;
		    }
		}

	      if (agg_p->sort_list == NULL)
		{
		  TP_DOMAIN *tmp_domain_p = NULL;
		  TP_DOMAIN_STATUS status;

		  /* host var or constant */
		  switch (agg_p->opr_dbtype)
		    {
		    case DB_TYPE_SHORT:
		    case DB_TYPE_INTEGER:
		    case DB_TYPE_BIGINT:
		    case DB_TYPE_FLOAT:
		    case DB_TYPE_DOUBLE:
		    case DB_TYPE_MONETARY:
		    case DB_TYPE_NUMERIC:
		    case DB_TYPE_DATE:
		    case DB_TYPE_DATETIME:
		    case DB_TYPE_DATETIMELTZ:
		    case DB_TYPE_DATETIMETZ:
		    case DB_TYPE_TIMESTAMP:
		    case DB_TYPE_TIMESTAMPLTZ:
		    case DB_TYPE_TIMESTAMPTZ:
		    case DB_TYPE_TIME:
		    case DB_TYPE_TIMELTZ:
		    case DB_TYPE_TIMETZ:
		      break;
		    default:
		      assert (agg_p->operand.type == TYPE_CONSTANT || agg_p->operand.type == TYPE_DBVAL);

		      /* try to cast dbval to double, datetime then time */
		      tmp_domain_p = tp_domain_resolve_default (DB_TYPE_DOUBLE);

		      status = tp_value_cast (&dbval, &dbval, tmp_domain_p, false);
		      if (status != DOMAIN_COMPATIBLE)
			{
			  /* try datetime */
			  tmp_domain_p = tp_domain_resolve_default (DB_TYPE_DATETIME);

			  status = tp_value_cast (&dbval, &dbval, tmp_domain_p, false);
			}

		      /* try time */
		      if (status != DOMAIN_COMPATIBLE)
			{
			  tmp_domain_p = tp_domain_resolve_default (DB_TYPE_TIME);

			  status = tp_value_cast (&dbval, &dbval, tmp_domain_p, false);
			}

		      if (status != DOMAIN_COMPATIBLE)
			{
			  error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
				  qdump_function_type_string (agg_p->function), "DOUBLE, DATETIME, TIME");

			  pr_clear_value (&dbval);
			  return error;
			}

		      /* update domain */
		      agg_p->domain = tmp_domain_p;
		    }

		  pr_clear_value (agg_p->accumulator.value);
		  error = pr_clone_value (&dbval, agg_p->accumulator.value);
		  if (error != NO_ERROR)
		    {
		      pr_clear_value (&dbval);
		      return error;
		    }
		}
	    }

	  /* clear value */
	  pr_clear_value (&dbval);

	  /* percentile value check */
	  if (agg_p->function == PT_PERCENTILE_CONT || agg_p->function == PT_PERCENTILE_DISC)
	    {
	      if (DB_VALUE_TYPE (percentile_val) != DB_TYPE_DOUBLE
		  || db_get_double (percentile_val) != percentile->cur_group_percentile)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PERCENTILE_FUNC_PERCENTILE_CHANGED_IN_GROUP, 0);
		  return ER_FAILED;
		}
	    }
	}
      else if (agg_p->function == PT_GROUP_CONCAT)
	{
	  assert (alt_acc_list == NULL);

	  /* group concat function requires special care */
	  if (agg_p->accumulator.curr_cnt < 1)
	    {
	      error = qdata_group_concat_first_value (thread_p, agg_p, &dbval);
	    }
	  else
	    {
	      error = qdata_group_concat_value (thread_p, agg_p, &dbval);
	    }

	  /* increment tuple count */
	  agg_p->accumulator.curr_cnt++;

	  /* clear value */
	  pr_clear_value (&dbval);

	  /* check error */
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      else
	{
	  /* aggregate value */
	  error = qdata_aggregate_value_to_accumulator (thread_p, accumulator, &agg_p->accumulator_domain,
							agg_p->function, agg_p->domain, &dbval);

	  /* increment tuple count */
	  accumulator->curr_cnt++;

	  /* clear value */
	  pr_clear_value (&dbval);

	  /* handle error */
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * qdata_evaluate_aggregate_optimize () -
 *   return:
 *   agg_ptr(in)        :
 *   hfid(in)   :
 *   super_oid(in): The super oid of a class. This should be used when dealing
 *		    with a partition class. It the index is a global index,
 *		    the min/max value from the partition in this case
 *		    will be retrieved from the heap.
 */
int
qdata_evaluate_aggregate_optimize (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p, HFID * hfid_p, OID * super_oid)
{
  int oid_count = 0, null_count = 0, key_count = 0;
  int flag_btree_stat_needed = true;

  if (!agg_p->flag_agg_optimize)
    {
      return ER_FAILED;
    }

  if (hfid_p->vfid.fileid < 0)
    {
      return ER_FAILED;
    }

  if ((agg_p->function == PT_MIN) || (agg_p->function == PT_MAX))
    {
      flag_btree_stat_needed = false;
    }

  if (flag_btree_stat_needed)
    {
      if (BTID_IS_NULL (&agg_p->btid))
	{
	  return ER_FAILED;
	}

      if (btree_get_unique_statistics_for_count (thread_p, &agg_p->btid, &oid_count, &null_count, &key_count) !=
	  NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  switch (agg_p->function)
    {
    case PT_COUNT:
      if (agg_p->option == Q_ALL)
	{
	  db_make_int (agg_p->accumulator.value, oid_count - null_count);
	}
      else
	{
	  db_make_int (agg_p->accumulator.value, key_count);
	}
      break;

    case PT_COUNT_STAR:
      agg_p->accumulator.curr_cnt = oid_count;
      break;

    case PT_MIN:
      if (btree_find_min_or_max_key (thread_p, &agg_p->btid, agg_p->accumulator.value, true) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      break;

    case PT_MAX:
      if (btree_find_min_or_max_key (thread_p, &agg_p->btid, agg_p->accumulator.value, false) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      break;

    default:
      break;
    }

  return NO_ERROR;
}

/*
 * qdata_evaluate_aggregate_hierarchy () - aggregate evaluation optimization
 *					   across a class hierarchy
 * return : error code or NO_ERROR
 * thread_p (in)  : thread entry
 * agg_p (in)	  : aggregate to be evaluated
 * root_hfid (in) : HFID of the root class in the hierarchy
 * root_btid (in) : BTID of the root class in the hierarchy
 * helper (in)	  : hierarchy helper
 */
int
qdata_evaluate_aggregate_hierarchy (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p, HFID * root_hfid, BTID * root_btid,
				    HIERARCHY_AGGREGATE_HELPER * helper)
{
  int error = NO_ERROR, i, cmp = DB_EQ, cur_cnt = 0;
  DB_VALUE result;
  if (!agg_p->flag_agg_optimize)
    {
      return ER_FAILED;
    }

  /* evaluate aggregate on the root class */
  error = qdata_evaluate_aggregate_optimize (thread_p, agg_p, root_hfid, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_null (&result);
  error = pr_clone_value (agg_p->accumulator.value, &result);
  if (error != NO_ERROR)
    {
      return error;
    }

  pr_clear_value (agg_p->accumulator.value);
  /* iterate through classes in the hierarchy and merge aggregate values */
  for (i = 0; i < helper->count && error == NO_ERROR; i++)
    {
      if (!BTID_IS_NULL (&agg_p->btid))
	{
	  assert (helper->btids != NULL);
	  BTID_COPY (&agg_p->btid, &helper->btids[i]);
	}
      error = qdata_evaluate_aggregate_optimize (thread_p, agg_p, &helper->hfids[i], NULL);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
      switch (agg_p->function)
	{
	case PT_COUNT:
	  /* add current value to result */
	  error = qdata_add_dbval (agg_p->accumulator.value, &result, &result, agg_p->domain);
	  pr_clear_value (agg_p->accumulator.value);
	  break;
	case PT_COUNT_STAR:
	  cur_cnt += agg_p->accumulator.curr_cnt;
	  break;
	case PT_MIN:
	  if (DB_IS_NULL (&result))
	    {
	      error = pr_clone_value (agg_p->accumulator.value, &result);
	      if (error != NO_ERROR)
		{
		  goto cleanup;
		}
	    }
	  else
	    {

	      cmp = tp_value_compare (agg_p->accumulator.value, &result, true, true);
	      if (cmp == DB_LT)
		{
		  /* agg_p->value is lower than result so make it the new minimum */
		  pr_clear_value (&result);
		  error = pr_clone_value (agg_p->accumulator.value, &result);
		  if (error != NO_ERROR)
		    {
		      goto cleanup;
		    }
		}
	    }
	  break;

	case PT_MAX:
	  if (DB_IS_NULL (&result))
	    {
	      error = pr_clone_value (agg_p->accumulator.value, &result);
	      if (error != NO_ERROR)
		{
		  goto cleanup;
		}
	    }
	  else
	    {
	      cmp = tp_value_compare (agg_p->accumulator.value, &result, true, true);
	      if (cmp == DB_GT)
		{
		  /* agg_p->value is greater than result so make it the new maximum */
		  pr_clear_value (&result);
		  error = pr_clone_value (agg_p->accumulator.value, &result);
		  if (error != NO_ERROR)
		    {
		      goto cleanup;
		    }
		}
	    }
	  break;

	default:
	  break;
	}
      pr_clear_value (agg_p->accumulator.value);
    }

  if (agg_p->function == PT_COUNT_STAR)
    {
      agg_p->accumulator.curr_cnt = cur_cnt;
    }
  else
    {
      pr_clone_value (&result, agg_p->accumulator.value);
    }

cleanup:
  pr_clear_value (&result);

  if (!BTID_IS_NULL (&agg_p->btid))
    {
      /* restore btid of agg_p */
      BTID_COPY (&agg_p->btid, root_btid);
    }
  return error;
}

/*
 * qdata_finalize_aggregate_list () -
 *   return: NO_ERROR, or ER_code
 *   agg_list(in)       : Aggregate expression node list
 *   keep_list_file(in) : whether keep the list file for reuse
 *
 * Note: Make the final evaluation on the aggregate expression list.
 */
int
qdata_finalize_aggregate_list (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_list_p, bool keep_list_file)
{
  int error = NO_ERROR;
  AGGREGATE_TYPE *agg_p;
  DB_VALUE sqr_val;
  DB_VALUE dbval;
  DB_VALUE xavgval, xavg_1val, x2avgval;
  DB_VALUE xavg2val, varval;
  DB_VALUE dval;
  double dtmp;
  QFILE_LIST_ID *list_id_p;
  QFILE_LIST_SCAN_ID scan_id;
  SCAN_CODE scan_code;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  char *tuple_p;
  PR_TYPE *pr_type_p;
  OR_BUF buf;
  double dbl;

  db_make_null (&sqr_val);
  db_make_null (&dbval);
  db_make_null (&xavgval);
  db_make_null (&xavg_1val);
  db_make_null (&x2avgval);
  db_make_null (&xavg2val);
  db_make_null (&varval);
  db_make_null (&dval);

  for (agg_p = agg_list_p; agg_p != NULL; agg_p = agg_p->next)
    {
      TP_DOMAIN *tmp_domain_ptr = NULL;

      if (agg_p->function == PT_VARIANCE || agg_p->function == PT_STDDEV || agg_p->function == PT_VAR_POP
	  || agg_p->function == PT_STDDEV_POP || agg_p->function == PT_VAR_SAMP || agg_p->function == PT_STDDEV_SAMP)
	{
	  tmp_domain_ptr = tp_domain_resolve_default (DB_TYPE_DOUBLE);
	}

      /* set count-star aggregate values */
      if (agg_p->function == PT_COUNT_STAR)
	{
	  db_make_int (agg_p->accumulator.value, agg_p->accumulator.curr_cnt);
	}

      /* the value of groupby_num() remains unchanged; it will be changed while evaluating groupby_num predicates
       * against each group at 'xs_eval_grbynum_pred()' */
      if (agg_p->function == PT_GROUPBY_NUM)
	{
	  /* nothing to do with groupby_num() */
	  continue;
	}

      if (agg_p->function == PT_CUME_DIST)
	{
	  /* calculate the result for CUME_DIST */
	  dbl = (double) (agg_p->info.dist_percent.nlargers + 1) / (agg_p->accumulator.curr_cnt + 1);
	  assert (dbl <= 1.0 && dbl > 0.0);
	  db_make_double (agg_p->accumulator.value, dbl);

	  /* free const_array */
	  if (agg_p->info.dist_percent.const_array != NULL)
	    {
	      db_private_free_and_init (thread_p, agg_p->info.dist_percent.const_array);
	      agg_p->info.dist_percent.list_len = 0;
	    }
	  continue;
	}
      else if (agg_p->function == PT_PERCENT_RANK)
	{
	  /* calculate the result for PERCENT_RANK */
	  if (agg_p->accumulator.curr_cnt == 0)
	    {
	      dbl = 0.0;
	    }
	  else
	    {
	      dbl = (double) (agg_p->info.dist_percent.nlargers) / agg_p->accumulator.curr_cnt;
	    }
	  assert (dbl <= 1.0 && dbl >= 0.0);
	  db_make_double (agg_p->accumulator.value, dbl);

	  /* free const_array */
	  if (agg_p->info.dist_percent.const_array != NULL)
	    {
	      db_private_free_and_init (thread_p, agg_p->info.dist_percent.const_array);
	      agg_p->info.dist_percent.list_len = 0;
	    }
	  continue;
	}

      /* process list file for sum/avg/count distinct */
      if ((agg_p->option == Q_DISTINCT || agg_p->sort_list != NULL) && agg_p->function != PT_MAX
	  && agg_p->function != PT_MIN)
	{
	  if (agg_p->sort_list != NULL
	      && (TP_DOMAIN_TYPE (agg_p->sort_list->pos_descr.dom) == DB_TYPE_VARIABLE
		  || TP_DOMAIN_COLLATION_FLAG (agg_p->sort_list->pos_descr.dom) != TP_DOMAIN_COLL_NORMAL))
	    {
	      /* set domain of SORT LIST same as the domain from agg list */
	      assert (agg_p->sort_list->pos_descr.pos_no < agg_p->list_id->type_list.type_cnt);
	      agg_p->sort_list->pos_descr.dom = agg_p->list_id->type_list.domp[agg_p->sort_list->pos_descr.pos_no];
	    }

	  if (agg_p->flag_agg_optimize == false)
	    {
	      list_id_p = qfile_sort_list (thread_p, agg_p->list_id, agg_p->sort_list, agg_p->option, false);

	      if (list_id_p != NULL && er_has_error ())
		{
		  /* Some unexpected errors (like ER_INTERRUPTED due to timeout) should be handled. */
		  qfile_close_list (thread_p, list_id_p);
		  qfile_destroy_list (thread_p, list_id_p);
		  list_id_p = NULL;
		  error = er_errid ();
		  goto exit;
		}

	      if (list_id_p == NULL)
		{
		  if (!er_has_error ())
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		    }

		  error = er_errid ();
		  goto exit;
		}

	      agg_p->list_id = list_id_p;

	      if (agg_p->function == PT_COUNT)
		{
		  db_make_int (agg_p->accumulator.value, list_id_p->tuple_cnt);
		}
	      else
		{
		  pr_type_p = list_id_p->type_list.domp[0]->type;

		  /* scan list file, accumulating total for sum/avg */
		  error = qfile_open_list_scan (list_id_p, &scan_id);
		  if (error != NO_ERROR)
		    {
		      qfile_close_list (thread_p, list_id_p);
		      qfile_destroy_list (thread_p, list_id_p);
		      goto exit;
		    }

		  /* median and percentile funcs don't need to read all rows */
		  if (list_id_p->tuple_cnt > 0 && QPROC_IS_INTERPOLATION_FUNC (agg_p))
		    {
		      error = qdata_evaluate_interpolation_function (thread_p, agg_p, &scan_id, false);
		      if (error != NO_ERROR)
			{
			  qfile_close_scan (thread_p, &scan_id);
			  qfile_close_list (thread_p, list_id_p);
			  qfile_destroy_list (thread_p, list_id_p);
			  goto exit;
			}
		    }
		  else
		    {
		      while (true)
			{
			  scan_code = qfile_scan_list_next (thread_p, &scan_id, &tuple_record, PEEK);

			  if (scan_code == S_ERROR && er_has_error ())
			    {
			      /* Some unexpected errors (like ER_INTERRUPTED due to timeout) should be handled. */
			      qfile_close_scan (thread_p, &scan_id);
			      qfile_close_list (thread_p, list_id_p);
			      qfile_destroy_list (thread_p, list_id_p);
			      goto exit;
			    }

			  if (scan_code != S_SUCCESS)
			    {
			      break;
			    }

			  tuple_p = ((char *) tuple_record.tpl + QFILE_TUPLE_LENGTH_SIZE);
			  if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_p) == V_UNBOUND)
			    {
			      continue;
			    }

			  or_init (&buf, (char *) tuple_p + QFILE_TUPLE_VALUE_HEADER_SIZE,
				   QFILE_GET_TUPLE_VALUE_LENGTH (tuple_p));

			  (void) pr_clear_value (&dbval);
			  error = (*(pr_type_p->data_readval)) (&buf, &dbval, list_id_p->type_list.domp[0], -1, true,
								NULL, 0);
			  if (error != NO_ERROR)
			    {
			      qfile_close_scan (thread_p, &scan_id);
			      qfile_close_list (thread_p, list_id_p);
			      qfile_destroy_list (thread_p, list_id_p);
			      goto exit;
			    }

			  if (agg_p->function == PT_VARIANCE || agg_p->function == PT_STDDEV
			      || agg_p->function == PT_VAR_POP || agg_p->function == PT_STDDEV_POP
			      || agg_p->function == PT_VAR_SAMP || agg_p->function == PT_STDDEV_SAMP)
			    {
			      if (tp_value_coerce (&dbval, &dbval, tmp_domain_ptr) != DOMAIN_COMPATIBLE)
				{
				  (void) pr_clear_value (&dbval);
				  qfile_close_scan (thread_p, &scan_id);
				  qfile_close_list (thread_p, list_id_p);
				  qfile_destroy_list (thread_p, list_id_p);
				  error = ER_FAILED;
				  goto exit;
				}
			    }

			  if (DB_IS_NULL (agg_p->accumulator.value))
			    {
			      /* first iteration: can't add to a null agg_ptr->value */
			      PR_TYPE *tmp_pr_type;
			      DB_TYPE dbval_type = DB_VALUE_DOMAIN_TYPE (&dbval);

			      tmp_pr_type = PR_TYPE_FROM_ID (dbval_type);
			      if (tmp_pr_type == NULL)
				{
				  (void) pr_clear_value (&dbval);
				  qfile_close_scan (thread_p, &scan_id);
				  qfile_close_list (thread_p, list_id_p);
				  qfile_destroy_list (thread_p, list_id_p);
				  error = ER_FAILED;
				  goto exit;
				}

			      if (agg_p->function == PT_STDDEV || agg_p->function == PT_VARIANCE
				  || agg_p->function == PT_STDDEV_POP || agg_p->function == PT_VAR_POP
				  || agg_p->function == PT_STDDEV_SAMP || agg_p->function == PT_VAR_SAMP)
				{
				  error = qdata_multiply_dbval (&dbval, &dbval, &sqr_val, tmp_domain_ptr);
				  if (error != NO_ERROR)
				    {
				      (void) pr_clear_value (&dbval);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }

				  (*(tmp_pr_type->setval)) (agg_p->accumulator.value2, &sqr_val, true);
				}
			      if (agg_p->function == PT_GROUP_CONCAT)
				{
				  error = qdata_group_concat_first_value (thread_p, agg_p, &dbval);
				  if (error != NO_ERROR)
				    {
				      (void) pr_clear_value (&dbval);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }
				}
			      else
				{
				  (*(tmp_pr_type->setval)) (agg_p->accumulator.value, &dbval, true);
				}
			    }
			  else
			    {
			      if (agg_p->function == PT_STDDEV || agg_p->function == PT_VARIANCE
				  || agg_p->function == PT_STDDEV_POP || agg_p->function == PT_VAR_POP
				  || agg_p->function == PT_STDDEV_SAMP || agg_p->function == PT_VAR_SAMP)
				{
				  error = qdata_multiply_dbval (&dbval, &dbval, &sqr_val, tmp_domain_ptr);
				  if (error != NO_ERROR)
				    {
				      (void) pr_clear_value (&dbval);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }

				  error =
				    qdata_add_dbval (agg_p->accumulator.value2, &sqr_val, agg_p->accumulator.value2,
						     tmp_domain_ptr);
				  if (error != NO_ERROR)
				    {
				      (void) pr_clear_value (&dbval);
				      (void) pr_clear_value (&sqr_val);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }
				}

			      if (agg_p->function == PT_GROUP_CONCAT)
				{
				  error = qdata_group_concat_value (thread_p, agg_p, &dbval);
				  if (error != NO_ERROR)
				    {
				      (void) pr_clear_value (&dbval);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }
				}
			      else
				{

				  TP_DOMAIN *domain_ptr = NOT_NULL_VALUE (tmp_domain_ptr,
									  agg_p->accumulator_domain.value_dom);
				  /* accumulator domain should be used instead of agg_p->domain for SUM/AVG evaluation
				   * at the end cast the result to agg_p->domain */
				  if ((agg_p->function == PT_AVG)
				      && (dbval.domain.general_info.type == DB_TYPE_NUMERIC))
				    {
				      domain_ptr = NULL;
				    }

				  error =
				    qdata_add_dbval (agg_p->accumulator.value, &dbval, agg_p->accumulator.value,
						     domain_ptr);
				  if (error != NO_ERROR)
				    {
				      (void) pr_clear_value (&dbval);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }
				}
			    }
			}	/* while (true) */
		    }

		  qfile_close_scan (thread_p, &scan_id);
		  agg_p->accumulator.curr_cnt = list_id_p->tuple_cnt;
		}
	    }

	  /* close and destroy temporary list files */
	  if (!keep_list_file)
	    {
	      qfile_close_list (thread_p, agg_p->list_id);
	      qfile_destroy_list (thread_p, agg_p->list_id);
	    }
	}

      if (agg_p->function == PT_GROUP_CONCAT && !DB_IS_NULL (agg_p->accumulator.value))
	{
	  db_string_fix_string_size (agg_p->accumulator.value);
	}
      /* compute averages */
      if (agg_p->accumulator.curr_cnt > 0
	  && (agg_p->function == PT_AVG || agg_p->function == PT_STDDEV || agg_p->function == PT_VARIANCE
	      || agg_p->function == PT_STDDEV_POP || agg_p->function == PT_VAR_POP || agg_p->function == PT_STDDEV_SAMP
	      || agg_p->function == PT_VAR_SAMP))
	{
	  TP_DOMAIN *double_domain_ptr = tp_domain_resolve_default (DB_TYPE_DOUBLE);

	  /* compute AVG(X) = SUM(X)/COUNT(X) */
	  db_make_double (&dbval, agg_p->accumulator.curr_cnt);
	  error = qdata_divide_dbval (agg_p->accumulator.value, &dbval, &xavgval, double_domain_ptr);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (agg_p->function == PT_AVG)
	    {
	      if (tp_value_coerce (&xavgval, agg_p->accumulator.value, double_domain_ptr) != DOMAIN_COMPATIBLE)
		{
		  error = ER_FAILED;
		  goto exit;
		}

	      continue;
	    }

	  if (agg_p->function == PT_STDDEV_SAMP || agg_p->function == PT_VAR_SAMP)
	    {
	      /* compute SUM(X^2) / (n-1) */
	      if (agg_p->accumulator.curr_cnt > 1)
		{
		  db_make_double (&dbval, agg_p->accumulator.curr_cnt - 1);
		}
	      else
		{
		  /* when not enough samples, return NULL */
		  db_make_null (agg_p->accumulator.value);
		  continue;
		}
	    }
	  else
	    {
	      assert (agg_p->function == PT_STDDEV || agg_p->function == PT_STDDEV_POP || agg_p->function == PT_VARIANCE
		      || agg_p->function == PT_VAR_POP);
	      /* compute SUM(X^2) / n */
	      db_make_double (&dbval, agg_p->accumulator.curr_cnt);
	    }

	  error = qdata_divide_dbval (agg_p->accumulator.value2, &dbval, &x2avgval, double_domain_ptr);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }

	  /* compute {SUM(X) / (n)} OR {SUM(X) / (n-1)} for xxx_SAMP agg */
	  error = qdata_divide_dbval (agg_p->accumulator.value, &dbval, &xavg_1val, double_domain_ptr);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }

	  /* compute AVG(X) * {SUM(X) / (n)} , AVG(X) * {SUM(X) / (n-1)} for xxx_SAMP agg */
	  error = qdata_multiply_dbval (&xavgval, &xavg_1val, &xavg2val, double_domain_ptr);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }

	  /* compute VAR(X) = SUM(X^2)/(n) - AVG(X) * {SUM(X) / (n)} OR VAR(X) = SUM(X^2)/(n-1) - AVG(X) * {SUM(X) /
	   * (n-1)} for xxx_SAMP aggregates */
	  error = qdata_subtract_dbval (&x2avgval, &xavg2val, &varval, double_domain_ptr);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }

	  if (agg_p->function == PT_VARIANCE || agg_p->function == PT_STDDEV || agg_p->function == PT_VAR_POP
	      || agg_p->function == PT_STDDEV_POP || agg_p->function == PT_VAR_SAMP
	      || agg_p->function == PT_STDDEV_SAMP)
	    {
	      pr_clone_value (&varval, agg_p->accumulator.value);
	    }

	  if (agg_p->function == PT_STDDEV || agg_p->function == PT_STDDEV_POP || agg_p->function == PT_STDDEV_SAMP)
	    {
	      TP_DOMAIN *tmp_domain_ptr;

	      db_value_domain_init (&dval, DB_TYPE_DOUBLE, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	      /* Construct TP_DOMAIN whose type is DB_TYPE_DOUBLE */
	      tmp_domain_ptr = tp_domain_resolve_default (DB_TYPE_DOUBLE);
	      if (tp_value_coerce (&varval, &dval, tmp_domain_ptr) != DOMAIN_COMPATIBLE)
		{
		  error = ER_FAILED;
		  goto exit;
		}

	      dtmp = db_get_double (&dval);

	      /* mathematically, dtmp should be zero or positive; however, due to some precision errors, in some cases
	       * it can be a very small negative number of which we cannot extract the square root */
	      dtmp = (dtmp < 0.0f ? 0.0f : dtmp);

	      dtmp = sqrt (dtmp);
	      db_make_double (&dval, dtmp);

	      pr_clone_value (&dval, agg_p->accumulator.value);
	    }
	}

      /* Resolve the final result of aggregate function. Since the evaluation value might be changed to keep the
       * precision during the aggregate function evaluation, for example, use DOUBLE instead FLOAT, we need to cast the 
       * result to the original domain. */
      if (agg_p->function == PT_SUM && agg_p->domain != agg_p->accumulator_domain.value_dom)
	{
	  /* cast value */
	  error = db_value_coerce (agg_p->accumulator.value, agg_p->accumulator.value, agg_p->domain);
	  if (error != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }

exit:
  (void) pr_clear_value (&dbval);

  return error;
}

/*
 * MISCELLANEOUS
 */

/*
 * qdata_get_tuple_value_size_from_dbval () - Return the tuple value size
 *	for the db_value
 *   return: tuple_value_size or ER_FAILED
 *   dbval(in)  : db_value node
 */
int
qdata_get_tuple_value_size_from_dbval (DB_VALUE * dbval_p)
{
  int val_size, align;
  int tuple_value_size = 0;
  PR_TYPE *type_p;
  DB_TYPE dbval_type;

  if (DB_IS_NULL (dbval_p))
    {
      tuple_value_size = QFILE_TUPLE_VALUE_HEADER_SIZE;
    }
  else
    {
      dbval_type = DB_VALUE_DOMAIN_TYPE (dbval_p);
      type_p = PR_TYPE_FROM_ID (dbval_type);
      if (type_p)
	{
	  if (type_p->data_lengthval == NULL)
	    {
	      val_size = type_p->disksize;
	    }
	  else
	    {
	      val_size = (*(type_p->data_lengthval)) (dbval_p, 1);
	      if (pr_is_string_type (dbval_type))
		{
		  int precision = DB_VALUE_PRECISION (dbval_p);
		  int string_length = db_get_string_length (dbval_p);

		  if (precision == TP_FLOATING_PRECISION_VALUE)
		    {
		      precision = DB_MAX_STRING_LENGTH;
		    }

		  assert_release (string_length <= precision);

		  if (val_size < 0)
		    {
		      return ER_FAILED;
		    }
		  else if (string_length > precision)
		    {
		      /* The size of db_value is greater than it's precision. This case is abnormal (assertion
		       * failure). Code below is remained for backward compatibility. */
		      if (db_string_truncate (dbval_p, precision) != NO_ERROR)
			{
			  return ER_FAILED;
			}
		      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_DATA_IS_TRUNCATED_TO_PRECISION, 2, precision,
			      string_length);

		      val_size = (*(type_p->data_lengthval)) (dbval_p, 1);
		    }
		}
	    }

	  align = DB_ALIGN (val_size, MAX_ALIGNMENT);	/* to align for the next field */
	  tuple_value_size = QFILE_TUPLE_VALUE_HEADER_SIZE + align;
	}
    }

  return tuple_value_size;
}

/*
 * qdata_get_single_tuple_from_list_id () -
 *   return: NO_ERROR or error code
 *   list_id(in)        : List file identifier
 *   single_tuple(in)   : VAL_LIST
 */
int
qdata_get_single_tuple_from_list_id (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, VAL_LIST * single_tuple_p)
{
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  QFILE_LIST_SCAN_ID scan_id;
  OR_BUF buf;
  PR_TYPE *pr_type_p;
  QFILE_TUPLE_VALUE_FLAG flag;
  int length;
  TP_DOMAIN *domain_p;
  char *ptr;
  int tuple_count, value_count, i;
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

	  flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &length);
	  OR_BUF_INIT (buf, ptr, length);
	  if (flag == V_BOUND)
	    {
	      if ((*(pr_type_p->data_readval)) (&buf, value_list->val, domain_p, -1, true, NULL, 0) != NO_ERROR)
		{
		  qfile_close_scan (thread_p, &scan_id);
		  return ER_FAILED;
		}
	    }
	  else
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
 */
int
qdata_get_valptr_type_list (THREAD_ENTRY * thread_p, VALPTR_LIST * valptr_list_p,
			    QFILE_TUPLE_VALUE_TYPE_LIST * type_list_p)
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
	  type_list_p->domp[i++] = reg_var_p->value.domain;
	}

      reg_var_p = reg_var_p->next;
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

  result = fetch_peek_dbval (thread_p, regu_var_p, val_desc_p, NULL, NULL, NULL, &peek_value_p);
  if (result != NO_ERROR)
    {
      return NULL;
    }

  if (!DB_IS_NULL (peek_value_p))
    {
      val_type = DB_VALUE_TYPE (peek_value_p);
      assert (val_type != DB_TYPE_NULL);

      dom_type = TP_DOMAIN_TYPE (regu_var_p->domain);
      if (dom_type != DB_TYPE_NULL)
	{
	  assert (dom_type != DB_TYPE_NULL);

	  if (val_type == DB_TYPE_OID)
	    {
	      assert ((dom_type == DB_TYPE_OID) || (dom_type == DB_TYPE_VOBJ));
	    }
	  else if (val_type != dom_type
		   || (val_type == DB_TYPE_NUMERIC
		       && (peek_value_p->domain.numeric_info.precision != regu_var_p->domain->precision
			   || peek_value_p->domain.numeric_info.scale != regu_var_p->domain->scale)))
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

		  dom_status = tp_value_auto_cast (peek_value_p, peek_value_p, regu_var_p->domain);
		  if (save_heapid != 0)
		    {
		      (void) db_change_private_heap (thread_p, save_heapid);
		      save_heapid = 0;
		    }
		  if (dom_status != DOMAIN_COMPATIBLE)
		    {
		      result = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, peek_value_p, regu_var_p->domain);
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
			     VAL_DESCR * val_desc_p, OID * obj_oid_p, QFILE_TUPLE tuple)
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
  domain_p = regu_func_p->domain;
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
      if (fetch_copy_dbval (thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tuple, &dbval) != NO_ERROR)
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
				 OID * obj_oid_p, QFILE_TUPLE tuple)
{
#if defined(ENABLE_UNUSED_FUNCTION)
  DB_VALUE *args[NUM_F_GENERIC_ARGS];
  DB_VALUE *result_p = function_p->value;
  DB_VALUE *offset_dbval_p;
  int offset;
  REGU_VARIABLE_LIST operand = function_p->operand;
  int i, num_args;
  int (*function) (THREAD_ENTRY * thread_p, DB_VALUE *, int, DB_VALUE **);

  /* by convention the first argument for the function is the function jump table offset and is not a real argument to
   * the function. */
  if (!operand
      || fetch_peek_dbval (thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tuple, &offset_dbval_p) != NO_ERROR
      || db_value_type (offset_dbval_p) != DB_TYPE_INTEGER)
    {
      goto error;
    }

  offset = db_get_int (offset_dbval_p);
  if (offset >= (SSIZEOF (generic_func_ptrs) / SSIZEOF (generic_func_ptrs[0])))
    {
      goto error;
    }

  function = generic_func_ptrs[offset];
  /* initialize the argument array */
  for (i = 0; i < NUM_F_GENERIC_ARGS; i++)
    {
      args[i] = NULL;
    }

  /* skip the first argument, it is only the offset into the jump table */
  operand = operand->next;
  num_args = 0;

  while (operand)
    {
      num_args++;
      if (num_args > NUM_F_GENERIC_ARGS)
	{
	  goto error;
	}

      if (fetch_peek_dbval (thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tuple, &args[num_args - 1]) !=
	  NO_ERROR)
	{
	  goto error;
	}

      operand = operand->next;
    }

  if ((*function) (thread_p, result_p, num_args, args) != NO_ERROR)
    {
      goto error;
    }

  return NO_ERROR;

error:
#else /* ENABLE_UNUSED_FUNCTION */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_GENERIC_FUNCTION_FAILURE, 0);
  return ER_FAILED;
#endif /* ENABLE_UNUSED_FUNCTION */
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
			     OID * obj_oid_p, QFILE_TUPLE tuple)
{
  OID class_oid;
  OID *instance_oid_p;
  DB_VALUE *val_p, element;
  DB_TYPE type;
  int err;

  if (fetch_peek_dbval (thread_p, &function_p->operand->value, val_desc_p, NULL, obj_oid_p, tuple, &val_p) != NO_ERROR)
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
qdata_evaluate_function (THREAD_ENTRY * thread_p, REGU_VARIABLE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
			 QFILE_TUPLE tuple)
{
  FUNCTION_TYPE *funcp;

  /* should sync with fetch_peek_dbval () */

  funcp = function_p->value.funcp;
  /* clear any value from a previous iteration */
  pr_clear_value (funcp->value);

  switch (funcp->ftype)
    {
    case F_SET:
      return qdata_convert_dbvals_to_set (thread_p, DB_TYPE_SET, function_p, val_desc_p, obj_oid_p, tuple);

    case F_MULTISET:
      return qdata_convert_dbvals_to_set (thread_p, DB_TYPE_MULTISET, function_p, val_desc_p, obj_oid_p, tuple);

    case F_SEQUENCE:
      return qdata_convert_dbvals_to_set (thread_p, DB_TYPE_SEQUENCE, function_p, val_desc_p, obj_oid_p, tuple);

    case F_VID:
      return qdata_convert_dbvals_to_set (thread_p, DB_TYPE_VOBJ, function_p, val_desc_p, obj_oid_p, tuple);

    case F_TABLE_SET:
      return qdata_convert_table_to_set (thread_p, DB_TYPE_SET, function_p, val_desc_p);

    case F_TABLE_MULTISET:
      return qdata_convert_table_to_set (thread_p, DB_TYPE_MULTISET, function_p, val_desc_p);

    case F_TABLE_SEQUENCE:
      return qdata_convert_table_to_set (thread_p, DB_TYPE_SEQUENCE, function_p, val_desc_p);

    case F_GENERIC:
      return qdata_evaluate_generic_function (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_CLASS_OF:
      return qdata_get_class_of_function (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_INSERT_SUBSTRING:
      return qdata_insert_substring_function (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_ELT:
      return qdata_elt (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_OBJECT:
      return qdata_json_object (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_ARRAY:
      return qdata_json_array (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_INSERT:
      return qdata_json_insert (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_REPLACE:
      return qdata_json_replace (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_SET:
      return qdata_json_set (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_KEYS:
      return qdata_json_keys (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_REMOVE:
      return qdata_json_remove (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_ARRAY_APPEND:
      return qdata_json_array_append (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_GET_ALL_PATHS:
      return qdata_json_get_all_paths (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

    case F_JSON_MERGE:
      return qdata_json_merge (thread_p, funcp, val_desc_p, obj_oid_p, tuple);

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
  QFILE_TUPLE_RECORD tuple_record = {
    NULL, 0
  };
  SCAN_CODE scan_code;
  QFILE_LIST_ID *list_id_p;
  int i, seq_pos;
  int val_size;
  OR_BUF buf;
  DB_VALUE dbval, *result_p;
  DB_COLLECTION *collection_p = NULL;
  SETOBJ *setobj_p;
  DB_TYPE type;
  PR_TYPE *pr_type_p;
  int error;
  REGU_VARIABLE_LIST operand;
  TP_DOMAIN *domain_p;
  char *ptr;

  result_p = function_p->value.funcp->value;
  operand = function_p->value.funcp->operand;

  /* execute linked query */
  EXECUTE_REGU_VARIABLE_XASL (thread_p, &(operand->value), val_desc_p);

  if (CHECK_REGU_VARIABLE_XASL_STATUS (&(operand->value)) != XASL_SUCCESS)
    {
      return ER_FAILED;
    }

  domain_p = function_p->domain;
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
	  pr_type_p = PR_TYPE_FROM_ID (type);
	  if (pr_type_p == NULL)
	    {
	      qfile_close_scan (thread_p, &scan_id);
	      return ER_FAILED;
	    }

	  if (qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &val_size) == V_BOUND)
	    {
	      or_init (&buf, ptr, val_size);

	      if ((*(pr_type_p->data_readval)) (&buf, &dbval, list_id_p->type_list.domp[i], -1, true, NULL, 0) !=
		  NO_ERROR)
		{
		  qfile_close_scan (thread_p, &scan_id);
		  return ER_FAILED;
		}
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
qdata_evaluate_connect_by_root (THREAD_ENTRY * thread_p, void *xasl_p, REGU_VARIABLE * regu_p, DB_VALUE * result_val_p,
				VAL_DESCR * vd)
{
  QFILE_TUPLE tpl;
  QFILE_LIST_ID *list_id_p;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = { (QFILE_TUPLE) NULL, 0 };
  QFILE_TUPLE_POSITION p_pos, *bitval;
  QPROC_DB_VALUE_LIST valp;
  DB_VALUE p_pos_dbval;
  XASL_NODE *xasl, *xptr;
  int length, i;

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
  tuple_rec.tpl = tpl;

  do
    {
      /* get the parent node */
      if (qexec_get_tuple_column_value (tuple_rec.tpl, xptr->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET,
					&p_pos_dbval, &tp_Bit_domain) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return false;
	}

      bitval = (QFILE_TUPLE_POSITION *) db_get_bit (&p_pos_dbval, &length);

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

  qfile_close_scan (thread_p, &s_id);

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
      if (qexec_get_tuple_column_value (tuple_rec.tpl, i, result_val_p, regu_p->domain) != NO_ERROR)
	{
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
	      return false;
	    }
	}
      else
	{
	  return false;
	}
    }

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
qdata_evaluate_qprior (THREAD_ENTRY * thread_p, void *xasl_p, REGU_VARIABLE * regu_p, DB_VALUE * result_val_p,
		       VAL_DESCR * vd)
{
  QFILE_TUPLE tpl;
  QFILE_LIST_ID *list_id_p;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = { (QFILE_TUPLE) NULL, 0 };
  QFILE_TUPLE_POSITION p_pos, *bitval;
  DB_VALUE p_pos_dbval;
  XASL_NODE *xasl, *xptr;
  int length;

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

  tuple_rec.tpl = tpl;

  /* get the parent node */
  if (qexec_get_tuple_column_value (tuple_rec.tpl, xptr->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET,
				    &p_pos_dbval, &tp_Bit_domain) != NO_ERROR)
    {
      qfile_close_scan (thread_p, &s_id);
      return false;
    }

  bitval = (QFILE_TUPLE_POSITION *) db_get_bit (&p_pos_dbval, &length);

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
      tuple_rec.tpl = NULL;
    }

  qfile_close_scan (thread_p, &s_id);

  if (tuple_rec.tpl != NULL)
    {
      /* fetch val list from the parent tuple */
      if (fetch_val_list (thread_p, xptr->proc.connect_by.prior_regu_list_pred, vd, NULL, NULL, tuple_rec.tpl, PEEK) !=
	  NO_ERROR)
	{
	  return false;
	}
      if (fetch_val_list (thread_p, xptr->proc.connect_by.prior_regu_list_rest, vd, NULL, NULL, tuple_rec.tpl, PEEK) !=
	  NO_ERROR)
	{
	  return false;
	}

      /* replace values in T_QPRIOR argument with values from parent tuple */
      qexec_replace_prior_regu_vars_prior_expr (thread_p, regu_p, xptr, xptr);

      /* evaluate the modified regu_p */
      if (fetch_copy_dbval (thread_p, regu_p, vd, NULL, NULL, tuple_rec.tpl, result_val_p) != NO_ERROR)
	{
	  return false;
	}
    }
  else
    {
      db_make_null (result_val_p);
    }

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
qdata_evaluate_sys_connect_by_path (THREAD_ENTRY * thread_p, void *xasl_p, REGU_VARIABLE * regu_p,
				    DB_VALUE * value_char, DB_VALUE * result_p, VAL_DESCR * vd)
{
  QFILE_TUPLE tpl;
  QFILE_LIST_ID *list_id_p;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tuple_rec = { (QFILE_TUPLE) NULL, 0 };
  QFILE_TUPLE_POSITION p_pos, *bitval;
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
  tuple_rec.tpl = tpl;

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
	  /* get the required column */
	  if (i < xptr->val_list->val_cnt)
	    {
	      if (qexec_get_tuple_column_value (tuple_rec.tpl, i, arg_dbval_p, regu_p->domain) != NO_ERROR)
		{
		  goto error;
		}
	      need_clear_arg_dbval = true;
	    }
	}
      else
	{
	  /* fetch value list */
	  if (fetch_val_list (thread_p, xptr->proc.connect_by.regu_list_pred, vd, NULL, NULL, tuple_rec.tpl, PEEK) !=
	      NO_ERROR)
	    {
	      goto error;
	    }
	  if (fetch_val_list (thread_p, xptr->proc.connect_by.regu_list_rest, vd, NULL, NULL, tuple_rec.tpl, PEEK) !=
	      NO_ERROR)
	    {
	      goto error;
	    }

	  /* evaluate argument expression */
	  if (fetch_peek_dbval (thread_p, regu_p, vd, NULL, NULL, tuple_rec.tpl, &arg_dbval_p) != NO_ERROR)
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

      while ((int) strlen (path_tmp) + 1 > len_result_path)
	{
	  len_result_path += SYS_CONNECT_BY_PATH_MEM_STEP;
	  db_private_free_and_init (thread_p, result_path);
	  result_path = (char *) db_private_alloc (thread_p, sizeof (char) * len_result_path);
	  if (result_path == NULL)
	    {
	      goto error;
	    }
	}

      strcpy (result_path, path_tmp);

      /* get the parent node */
      if (qexec_get_tuple_column_value (tuple_rec.tpl, xptr->outptr_list->valptr_cnt - PCOL_PARENTPOS_TUPLE_OFFSET,
					&p_pos_dbval, &tp_Bit_domain) != NO_ERROR)
	{
	  goto error;
	}

      bitval = (QFILE_TUPLE_POSITION *) db_get_bit (&p_pos_dbval, &length);

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
qdata_bit_not_dbval (DB_VALUE * dbval_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
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
qdata_bit_and_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
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
qdata_bit_or_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
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
qdata_bit_xor_dbval (DB_VALUE * dbval1_p, DB_VALUE * dbval2_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
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
		       TP_DOMAIN * domain_p)
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
		    TP_DOMAIN * domain_p)
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
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
		      return ER_QPROC_OVERFLOW_ADDITION;
		    }
		  db_make_int (result_p, (INT32) (bi[0] / bi[1]));
		}
	      else if (type[0] == DB_TYPE_BIGINT)
		{
		  if (OR_CHECK_BIGINT_DIV_OVERFLOW (bi[0], bi[1]))
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
		      return ER_QPROC_OVERFLOW_ADDITION;
		    }
		  db_make_bigint (result_p, bi[0] / bi[1]);
		}
	      else
		{
		  if (OR_CHECK_SHORT_DIV_OVERFLOW (bi[0], bi[1]))
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OVERFLOW_ADDITION, 0);
		      return ER_QPROC_OVERFLOW_ADDITION;
		    }
		  db_make_short (result_p, (INT16) (bi[0] / bi[1]));
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
qdata_list_dbs (THREAD_ENTRY * thread_p, DB_VALUE * result_p, TP_DOMAIN * domain_p)
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
 * qdata_group_concat_first_value() - concatenates the first value
 *   return: NO_ERROR, or ER_code
 *   thread_p(in) :
 *   agg_p(in)	  : GROUP_CONCAT aggregate
 *   dbvalue(in)  : current value
 */
int
qdata_group_concat_first_value (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p, DB_VALUE * dbvalue)
{
  TP_DOMAIN *result_domain;
  DB_TYPE agg_type;
  int max_allowed_size;
  DB_VALUE tmp_val;

  db_make_null (&tmp_val);

  agg_type = DB_VALUE_DOMAIN_TYPE (agg_p->accumulator.value);
  /* init the aggregate value domain */
  if (db_value_domain_init (agg_p->accumulator.value, agg_type, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
    {
      pr_clear_value (dbvalue);
      return ER_FAILED;
    }

  if (db_string_make_empty_typed_string (agg_p->accumulator.value, agg_type, DB_DEFAULT_PRECISION,
					 TP_DOMAIN_CODESET (agg_p->domain), TP_DOMAIN_COLLATION (agg_p->domain))
      != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true)
    {
      agg_p->accumulator.value->domain.general_info.is_null = 0;
    }

  /* concat the first value */
  result_domain = ((TP_DOMAIN_TYPE (agg_p->domain) == agg_type) ? agg_p->domain : NULL);

  max_allowed_size = (int) prm_get_bigint_value (PRM_ID_GROUP_CONCAT_MAX_LEN);

  if (qdata_concatenate_dbval (thread_p, agg_p->accumulator.value, dbvalue, &tmp_val, result_domain, max_allowed_size,
			       "GROUP_CONCAT()") != NO_ERROR)
    {
      pr_clear_value (dbvalue);
      return ER_FAILED;
    }

  /* check for concat success */
  if (!DB_IS_NULL (&tmp_val))
    {
      (void) pr_clear_value (agg_p->accumulator.value);
      pr_clone_value (&tmp_val, agg_p->accumulator.value);
    }

  (void) pr_clear_value (&tmp_val);

  return NO_ERROR;
}

/*
 * qdata_group_concat_value() - concatenates a value
 *   return: NO_ERROR, or ER_code
 *   thread_p(in) :
 *   agg_p(in)	  : GROUP_CONCAT aggregate
 *   dbvalue(in)  : current value
 */
int
qdata_group_concat_value (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p, DB_VALUE * dbvalue)
{
  TP_DOMAIN *result_domain;
  DB_TYPE agg_type;
  int max_allowed_size;
  DB_VALUE tmp_val;

  db_make_null (&tmp_val);

  agg_type = DB_VALUE_DOMAIN_TYPE (agg_p->accumulator.value);

  result_domain = ((TP_DOMAIN_TYPE (agg_p->domain) == agg_type) ? agg_p->domain : NULL);

  max_allowed_size = (int) prm_get_bigint_value (PRM_ID_GROUP_CONCAT_MAX_LEN);

  if (DB_IS_NULL (agg_p->accumulator.value2) && prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true)
    {
      if (db_string_make_empty_typed_string (agg_p->accumulator.value2, agg_type, DB_DEFAULT_PRECISION,
					     TP_DOMAIN_CODESET (agg_p->domain),
					     TP_DOMAIN_COLLATION (agg_p->domain)) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      agg_p->accumulator.value2->domain.general_info.is_null = 0;
    }

  /* add separator if specified (it may be the case for bit string) */
  if (!DB_IS_NULL (agg_p->accumulator.value2))
    {
      if (qdata_concatenate_dbval (thread_p, agg_p->accumulator.value, agg_p->accumulator.value2, &tmp_val,
				   result_domain, max_allowed_size, "GROUP_CONCAT()") != NO_ERROR)
	{
	  return ER_FAILED;
	}

      /* check for concat success */
      if (!DB_IS_NULL (&tmp_val))
	{
	  (void) pr_clear_value (agg_p->accumulator.value);
	  pr_clone_value (&tmp_val, agg_p->accumulator.value);
	}
    }
  else
    {
      assert (agg_type == DB_TYPE_VARBIT || agg_type == DB_TYPE_BIT);
    }

  pr_clear_value (&tmp_val);

  if (qdata_concatenate_dbval (thread_p, agg_p->accumulator.value, dbvalue, &tmp_val, result_domain, max_allowed_size,
			       "GROUP_CONCAT()") != NO_ERROR)
    {
      pr_clear_value (dbvalue);
      return ER_FAILED;
    }

  /* check for concat success */
  if (!DB_IS_NULL (&tmp_val))
    {
      (void) pr_clear_value (agg_p->accumulator.value);
      pr_clone_value (&tmp_val, agg_p->accumulator.value);
    }

  pr_clear_value (&tmp_val);

  return NO_ERROR;
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
qdata_regu_list_to_regu_array (FUNCTION_TYPE * function_p, const int array_size, REGU_VARIABLE * regu_array[],
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
				 OID * obj_oid_p, QFILE_TUPLE tuple)
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
      error_status = fetch_peek_dbval (thread_p, regu_array[i], val_desc_p, NULL, obj_oid_p, tuple, &args[i]);
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
	   QFILE_TUPLE tuple)
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

  error_status = fetch_peek_dbval (thread_p, &function_p->operand->value, val_desc_p, NULL, obj_oid_p, tuple, &index);
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

  error_status = fetch_peek_dbval (thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tuple, &operand_value);
  if (error_status != NO_ERROR)
    {
      goto error_exit;
    }

  /* 
   * operand should already be cast to the right type (CHAR
   * or NCHAR VARYING)
   */
  error_status = pr_clone_value (operand_value, function_p->value);

fast_exit:
  return error_status;

error_exit:
  return error_status;
}

static int
qdata_convert_operands_to_value_and_call (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p,
					  OID * obj_oid_p, QFILE_TUPLE tuple,
					  int (*function_to_call) (DB_VALUE *, DB_VALUE **, int const))
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
      error_status = fetch_peek_dbval (thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tuple, &value);
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

static int
qdata_json_object (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		   QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p, obj_oid_p, tuple, db_json_object);
}

static int
qdata_json_array (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		  QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p, obj_oid_p, tuple, db_json_array);
}

static int
qdata_json_insert (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		   QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p, obj_oid_p, tuple, db_json_insert);
}

static int
qdata_json_replace (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		    QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p, obj_oid_p, tuple, db_json_replace);
}

static int
qdata_json_set (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p, obj_oid_p, tuple, db_json_set);
}

static int
qdata_json_keys (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		 QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p, obj_oid_p, tuple, db_json_keys);
}

static int
qdata_json_remove (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		   QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p, obj_oid_p, tuple, db_json_remove);
}

static int
qdata_json_array_append (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
			 QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p,
						   obj_oid_p, tuple, db_json_array_append);
}

static int
qdata_json_get_all_paths (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
			  QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p,
						   obj_oid_p, tuple, db_json_get_all_paths);
}

static int
qdata_json_merge (THREAD_ENTRY * thread_p, FUNCTION_TYPE * function_p, VAL_DESCR * val_desc_p, OID * obj_oid_p,
		  QFILE_TUPLE tuple)
{
  return qdata_convert_operands_to_value_and_call (thread_p, function_p, val_desc_p, obj_oid_p, tuple, db_json_merge);
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
 * qdata_initialize_analytic_func () -
 *   return: NO_ERROR, or ER_code
 *   func_p(in): Analytic expression node
 *   query_id(in): Associated query id
 *
 */
int
qdata_initialize_analytic_func (THREAD_ENTRY * thread_p, ANALYTIC_TYPE * func_p, QUERY_ID query_id)
{
  func_p->curr_cnt = 0;
  if (db_value_domain_init (func_p->value, DB_VALUE_DOMAIN_TYPE (func_p->value), DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE)
      != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (func_p->function == PT_COUNT_STAR || func_p->function == PT_COUNT || func_p->function == PT_ROW_NUMBER
      || func_p->function == PT_RANK || func_p->function == PT_DENSE_RANK)
    {
      db_make_int (func_p->value, 0);
    }

  db_make_null (&func_p->part_value);

  /* create temporary list file to handle distincts */
  if (func_p->option == Q_DISTINCT)
    {
      QFILE_TUPLE_VALUE_TYPE_LIST type_list;
      QFILE_LIST_ID *list_id_p;

      type_list.type_cnt = 1;
      type_list.domp = (TP_DOMAIN **) db_private_alloc (thread_p, sizeof (TP_DOMAIN *));
      if (type_list.domp == NULL)
	{
	  return ER_FAILED;
	}
      type_list.domp[0] = func_p->operand.domain;

      list_id_p = qfile_open_list (thread_p, &type_list, NULL, query_id, QFILE_FLAG_DISTINCT);
      if (list_id_p == NULL)
	{
	  db_private_free_and_init (thread_p, type_list.domp);
	  return ER_FAILED;
	}

      db_private_free_and_init (thread_p, type_list.domp);

      if (qfile_copy_list_id (func_p->list_id, list_id_p, true) != NO_ERROR)
	{
	  qfile_free_list_id (list_id_p);
	  return ER_FAILED;
	}

      qfile_free_list_id (list_id_p);
    }

  return NO_ERROR;
}

/*
 * qdata_evaluate_analytic_func () -
 *   return: NO_ERROR, or ER_code
 *   func_p(in): Analytic expression node
 *   vd(in): Value descriptor
 *
 */
int
qdata_evaluate_analytic_func (THREAD_ENTRY * thread_p, ANALYTIC_TYPE * func_p, VAL_DESCR * val_desc_p)
{
  DB_VALUE dbval, sqr_val;
  DB_VALUE *opr_dbval_p = NULL;
  PR_TYPE *pr_type_p;
  OR_BUF buf;
  char *disk_repr_p = NULL;
  int dbval_size;
  int copy_opr;
  TP_DOMAIN *tmp_domain_p = NULL;
  DB_TYPE dbval_type;
  double ntile_bucket = 0.0;
  int error = NO_ERROR;
  TP_DOMAIN_STATUS dom_status;
  int coll_id;
  ANALYTIC_PERCENTILE_FUNCTION_INFO *percentile_info_p = NULL;
  DB_VALUE *peek_value_p = NULL;

  db_make_null (&dbval);
  db_make_null (&sqr_val);

  /* fetch operand value, analytic regulator variable should only contain constants */
  if (fetch_copy_dbval (thread_p, &func_p->operand, val_desc_p, NULL, NULL, NULL, &dbval) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if ((func_p->opr_dbtype == DB_TYPE_VARIABLE || TP_DOMAIN_COLLATION_FLAG (func_p->domain) != TP_DOMAIN_COLL_NORMAL)
      && !DB_IS_NULL (&dbval))
    {
      /* set function default domain when late binding */
      switch (func_p->function)
	{
	case PT_COUNT:
	case PT_COUNT_STAR:
	  func_p->domain = tp_domain_resolve_default (DB_TYPE_INTEGER);
	  break;

	case PT_AVG:
	case PT_STDDEV:
	case PT_STDDEV_POP:
	case PT_STDDEV_SAMP:
	case PT_VARIANCE:
	case PT_VAR_POP:
	case PT_VAR_SAMP:
	  func_p->domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);
	  break;

	case PT_SUM:
	  if (TP_IS_NUMERIC_TYPE (DB_VALUE_TYPE (&dbval)))
	    {
	      func_p->domain = tp_domain_resolve_value (&dbval, NULL);
	    }
	  else
	    {
	      func_p->domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);
	    }
	  break;

	default:
	  func_p->domain = tp_domain_resolve_value (&dbval, NULL);
	  break;
	}

      if (func_p->domain == NULL)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      /* coerce operand */
      if (tp_value_coerce (&dbval, &dbval, func_p->domain) != DOMAIN_COMPATIBLE)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      func_p->opr_dbtype = TP_DOMAIN_TYPE (func_p->domain);
      db_value_domain_init (func_p->value, func_p->opr_dbtype, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
    }

  if (DB_IS_NULL (&dbval) && func_p->function != PT_ROW_NUMBER && func_p->function != PT_FIRST_VALUE
      && func_p->function != PT_LAST_VALUE && func_p->function != PT_NTH_VALUE && func_p->function != PT_RANK
      && func_p->function != PT_DENSE_RANK && func_p->function != PT_LEAD && func_p->function != PT_LAG
      && !QPROC_IS_INTERPOLATION_FUNC (func_p))
    {
      if (func_p->function == PT_COUNT || func_p->function == PT_COUNT_STAR)
	{
	  func_p->curr_cnt++;
	}

      if (func_p->function == PT_NTILE)
	{
	  func_p->info.ntile.is_null = true;
	  func_p->info.ntile.bucket_count = 0;
	}
      goto exit;
    }

  if (func_p->option == Q_DISTINCT)
    {
      /* handle distincts by adding to the temp list file */
      dbval_type = DB_VALUE_DOMAIN_TYPE (&dbval);
      pr_type_p = PR_TYPE_FROM_ID (dbval_type);

      if (pr_type_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      dbval_size = pr_data_writeval_disk_size (&dbval);
      if (dbval_size > 0 && (disk_repr_p = (char *) db_private_alloc (thread_p, dbval_size)) != NULL)
	{
	  OR_BUF_INIT (buf, disk_repr_p, dbval_size);
	  error = (*(pr_type_p->data_writeval)) (&buf, &dbval);
	  if (error != NO_ERROR)
	    {
	      /* ER_TF_BUFFER_OVERFLOW means that val_size or packing is bad. */
	      assert (error != ER_TF_BUFFER_OVERFLOW);

	      db_private_free_and_init (thread_p, disk_repr_p);
	      error = ER_FAILED;
	      goto exit;
	    }
	}
      else
	{
	  error = ER_FAILED;
	  goto exit;
	}

      if (qfile_add_item_to_list (thread_p, disk_repr_p, dbval_size, func_p->list_id) != NO_ERROR)
	{
	  db_private_free_and_init (thread_p, disk_repr_p);
	  error = ER_FAILED;
	  goto exit;
	}
      db_private_free_and_init (thread_p, disk_repr_p);

      /* interpolation funcs need to check domain compatibility in the following code */
      if (!QPROC_IS_INTERPOLATION_FUNC (func_p))
	{
	  goto exit;
	}
    }

  copy_opr = false;
  coll_id = func_p->domain->collation_id;
  switch (func_p->function)
    {
    case PT_CUME_DIST:
    case PT_PERCENT_RANK:
      /* these functions do not execute here, just in case */
      pr_clear_value (func_p->value);
      break;

    case PT_NTILE:
      /* output value is not required now */
      db_make_null (func_p->value);

      if (func_p->curr_cnt < 1)
	{
	  /* the operand is the number of buckets and should be constant within the window; we can extract it now for
	   * later use */
	  dom_status = tp_value_coerce (&dbval, &dbval, &tp_Double_domain);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, &dbval, &tp_Double_domain);
	      assert_release (error != NO_ERROR);

	      goto exit;
	    }

	  ntile_bucket = db_get_double (&dbval);

	  /* boundary check */
	  if (ntile_bucket < 1.0 || ntile_bucket > DB_INT32_MAX)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NTILE_INVALID_BUCKET_NUMBER, 0);
	      error = ER_NTILE_INVALID_BUCKET_NUMBER;
	      goto exit;
	    }

	  /* we're sure the operand is not null */
	  func_p->info.ntile.is_null = false;
	  func_p->info.ntile.bucket_count = (int) floor (ntile_bucket);
	}
      break;

    case PT_FIRST_VALUE:
      if ((func_p->ignore_nulls && DB_IS_NULL (func_p->value)) || (func_p->curr_cnt < 1))
	{
	  /* copy value if it's the first value OR if we're ignoring NULLs and we've only encountered NULL values so
	   * far */
	  (void) pr_clear_value (func_p->value);
	  pr_clone_value (&dbval, func_p->value);
	}
      break;

    case PT_LAST_VALUE:
      if (!func_p->ignore_nulls || !DB_IS_NULL (&dbval))
	{
	  (void) pr_clear_value (func_p->value);
	  pr_clone_value (&dbval, func_p->value);
	}
      break;

    case PT_LEAD:
    case PT_LAG:
    case PT_NTH_VALUE:
      /* just copy */
      (void) pr_clear_value (func_p->value);
      pr_clone_value (&dbval, func_p->value);
      break;

    case PT_MIN:
      opr_dbval_p = &dbval;
      if ((func_p->curr_cnt < 1 || DB_IS_NULL (func_p->value))
	  || (*(func_p->domain->type->cmpval)) (func_p->value, &dbval, 1, 1, NULL, coll_id) > 0)
	{
	  copy_opr = true;
	}
      break;

    case PT_MAX:
      opr_dbval_p = &dbval;
      if ((func_p->curr_cnt < 1 || DB_IS_NULL (func_p->value))
	  || (*(func_p->domain->type->cmpval)) (func_p->value, &dbval, 1, 1, NULL, coll_id) < 0)
	{
	  copy_opr = true;
	}
      break;

    case PT_AVG:
    case PT_SUM:
      if (func_p->curr_cnt < 1)
	{
	  opr_dbval_p = &dbval;
	  copy_opr = true;

	  if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (opr_dbval_p)))
	    {
	      /* char types default to double; coerce here so we don't mess up the accumulator when we copy the operand 
	       */
	      if (tp_value_coerce (&dbval, &dbval, func_p->domain) != DOMAIN_COMPATIBLE)
		{
		  error = ER_FAILED;
		  goto exit;
		}
	    }

	  /* this type setting is necessary, it ensures that for the case average handling, which is treated like sum
	   * until final iteration, starts with the initial data type */
	  if (db_value_domain_init (func_p->value, DB_VALUE_DOMAIN_TYPE (opr_dbval_p), DB_DEFAULT_PRECISION,
				    DB_DEFAULT_SCALE) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }
	}
      else
	{
	  TP_DOMAIN *result_domain;
	  DB_TYPE type =
	    (func_p->function ==
	     PT_AVG) ? (DB_TYPE) func_p->value->domain.general_info.type : TP_DOMAIN_TYPE (func_p->domain);

	  result_domain = ((type == DB_TYPE_NUMERIC) ? NULL : func_p->domain);
	  if (qdata_add_dbval (func_p->value, &dbval, func_p->value, result_domain) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }
	  copy_opr = false;
	}
      break;

    case PT_COUNT_STAR:
      break;

    case PT_ROW_NUMBER:
      db_make_int (func_p->out_value, func_p->curr_cnt + 1);
      break;

    case PT_COUNT:
      if (func_p->curr_cnt < 1)
	{
	  db_make_int (func_p->value, 1);
	}
      else
	{
	  db_make_int (func_p->value, db_get_int (func_p->value) + 1);
	}
      break;

    case PT_RANK:
      if (func_p->curr_cnt < 1)
	{
	  db_make_int (func_p->value, 1);
	}
      else
	{
	  if (ANALYTIC_FUNC_IS_FLAGED (func_p, ANALYTIC_KEEP_RANK))
	    {
	      ANALYTIC_FUNC_CLEAR_FLAG (func_p, ANALYTIC_KEEP_RANK);
	    }
	  else
	    {
	      db_make_int (func_p->value, func_p->curr_cnt + 1);
	    }
	}
      break;

    case PT_DENSE_RANK:
      if (func_p->curr_cnt < 1)
	{
	  db_make_int (func_p->value, 1);
	}
      else
	{
	  if (ANALYTIC_FUNC_IS_FLAGED (func_p, ANALYTIC_KEEP_RANK))
	    {
	      ANALYTIC_FUNC_CLEAR_FLAG (func_p, ANALYTIC_KEEP_RANK);
	    }
	  else
	    {
	      db_make_int (func_p->value, db_get_int (func_p->value) + 1);
	    }
	}
      break;

    case PT_STDDEV:
    case PT_STDDEV_POP:
    case PT_STDDEV_SAMP:
    case PT_VARIANCE:
    case PT_VAR_POP:
    case PT_VAR_SAMP:
      copy_opr = false;
      tmp_domain_p = tp_domain_resolve_default (DB_TYPE_DOUBLE);

      if (tp_value_coerce (&dbval, &dbval, tmp_domain_p) != DOMAIN_COMPATIBLE)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      if (func_p->curr_cnt < 1)
	{
	  opr_dbval_p = &dbval;
	  /* func_p->value contains SUM(X) */
	  if (db_value_domain_init (func_p->value, DB_VALUE_DOMAIN_TYPE (opr_dbval_p), DB_DEFAULT_PRECISION,
				    DB_DEFAULT_SCALE) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }

	  /* func_p->value contains SUM(X^2) */
	  if (db_value_domain_init (func_p->value2, DB_VALUE_DOMAIN_TYPE (opr_dbval_p), DB_DEFAULT_PRECISION,
				    DB_DEFAULT_SCALE) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }

	  /* calculate X^2 */
	  if (qdata_multiply_dbval (&dbval, &dbval, &sqr_val, tmp_domain_p) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }

	  (void) pr_clear_value (func_p->value);
	  (void) pr_clear_value (func_p->value2);
	  dbval_type = DB_VALUE_DOMAIN_TYPE (func_p->value);
	  pr_type_p = PR_TYPE_FROM_ID (dbval_type);
	  if (pr_type_p == NULL)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }

	  (*(pr_type_p->setval)) (func_p->value, &dbval, true);
	  (*(pr_type_p->setval)) (func_p->value2, &sqr_val, true);
	}
      else
	{
	  if (qdata_multiply_dbval (&dbval, &dbval, &sqr_val, tmp_domain_p) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }

	  if (qdata_add_dbval (func_p->value, &dbval, func_p->value, tmp_domain_p) != NO_ERROR)
	    {
	      pr_clear_value (&sqr_val);
	      error = ER_FAILED;
	      goto exit;
	    }

	  if (qdata_add_dbval (func_p->value2, &sqr_val, func_p->value2, tmp_domain_p) != NO_ERROR)
	    {
	      pr_clear_value (&sqr_val);
	      error = ER_FAILED;
	      goto exit;
	    }

	  pr_clear_value (&sqr_val);
	}
      break;

    case PT_MEDIAN:
    case PT_PERCENTILE_CONT:
    case PT_PERCENTILE_DISC:
      if (func_p->function == PT_PERCENTILE_CONT || func_p->function == PT_PERCENTILE_DISC)
	{
	  percentile_info_p = &func_p->info.percentile;
	}

      if (func_p->curr_cnt < 1)
	{
	  if (func_p->function == PT_PERCENTILE_CONT || func_p->function == PT_PERCENTILE_DISC)
	    {
	      error =
		fetch_peek_dbval (thread_p, percentile_info_p->percentile_reguvar, NULL, NULL, NULL, NULL,
				  &peek_value_p);
	      if (error != NO_ERROR)
		{
		  assert (er_errid () != NO_ERROR);

		  goto exit;
		}

	      if ((peek_value_p == NULL) || (DB_VALUE_TYPE (peek_value_p) != DB_TYPE_DOUBLE))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
		  error = ER_QPROC_INVALID_DATATYPE;
		  goto exit;
		}

	      percentile_info_p->cur_group_percentile = db_get_double (peek_value_p);
	      if ((percentile_info_p->cur_group_percentile < 0) || (percentile_info_p->cur_group_percentile > 1))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PERCENTILE_FUNC_INVALID_PERCENTILE_RANGE, 1,
			  percentile_info_p->cur_group_percentile);
		  error = ER_PERCENTILE_FUNC_INVALID_PERCENTILE_RANGE;
		  goto exit;
		}
	    }

	  if (func_p->is_first_exec_time)
	    {
	      func_p->is_first_exec_time = false;
	      /* determine domain based on first value */
	      switch (func_p->opr_dbtype)
		{
		case DB_TYPE_SHORT:
		case DB_TYPE_INTEGER:
		case DB_TYPE_BIGINT:
		case DB_TYPE_FLOAT:
		case DB_TYPE_DOUBLE:
		case DB_TYPE_MONETARY:
		case DB_TYPE_NUMERIC:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      if (func_p->is_const_operand || func_p->function == PT_PERCENTILE_DISC)
			{
			  /* percentile_disc returns the same type as operand while median and percentile_cont return
			   * double */
			  func_p->domain = tp_domain_resolve_value (&dbval, NULL);
			  if (func_p->domain == NULL)
			    {
			      error = er_errid ();
			      assert (error != NO_ERROR);

			      return error;
			    }
			}
		      else
			{
			  func_p->domain = tp_domain_resolve_default (DB_TYPE_DOUBLE);
			}
		    }
		  break;

		case DB_TYPE_DATE:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_DATE);
		    }
		  break;

		case DB_TYPE_DATETIME:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_DATETIME);
		    }
		  break;

		case DB_TYPE_DATETIMETZ:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_DATETIMETZ);
		    }
		  break;

		case DB_TYPE_DATETIMELTZ:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_DATETIMELTZ);
		    }
		  break;

		case DB_TYPE_TIMESTAMP:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_TIMESTAMP);
		    }
		  break;

		case DB_TYPE_TIMESTAMPTZ:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_TIMESTAMPTZ);
		    }
		  break;

		case DB_TYPE_TIMESTAMPLTZ:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_TIMESTAMPLTZ);
		    }
		  break;

		case DB_TYPE_TIME:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_TIME);
		    }
		  break;

		case DB_TYPE_TIMETZ:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_TIMETZ);
		    }
		  break;

		case DB_TYPE_TIMELTZ:
		  if (TP_DOMAIN_TYPE (func_p->domain) == DB_TYPE_VARIABLE)
		    {
		      func_p->domain = tp_domain_resolve_default (DB_TYPE_TIMELTZ);
		    }
		  break;

		default:
		  /* try to cast dbval to double, datetime then time */
		  tmp_domain_p = tp_domain_resolve_default (DB_TYPE_DOUBLE);

		  dom_status = tp_value_cast (&dbval, &dbval, tmp_domain_p, false);
		  if (dom_status != DOMAIN_COMPATIBLE)
		    {
		      /* try datetime */
		      tmp_domain_p = tp_domain_resolve_default (DB_TYPE_DATETIME);

		      dom_status = tp_value_cast (&dbval, &dbval, tmp_domain_p, false);
		    }

		  /* try time */
		  if (dom_status != DOMAIN_COMPATIBLE)
		    {
		      tmp_domain_p = tp_domain_resolve_default (DB_TYPE_TIME);

		      dom_status = tp_value_cast (&dbval, &dbval, tmp_domain_p, false);
		    }

		  if (dom_status != DOMAIN_COMPATIBLE)
		    {
		      error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, qdump_function_type_string (func_p->function),
			      "DOUBLE, DATETIME, TIME");
		      goto exit;
		    }

		  /* update domain */
		  func_p->domain = tmp_domain_p;
		}
	    }
	}

      /* percentile value check */
      if (func_p->function == PT_PERCENTILE_CONT || func_p->function == PT_PERCENTILE_DISC)
	{
	  error =
	    fetch_peek_dbval (thread_p, percentile_info_p->percentile_reguvar, NULL, NULL, NULL, NULL, &peek_value_p);
	  if (error != NO_ERROR)
	    {
	      assert (er_errid () != NO_ERROR);

	      goto exit;
	    }

	  if ((peek_value_p == NULL) || (DB_VALUE_TYPE (peek_value_p) != DB_TYPE_DOUBLE)
	      || (db_get_double (peek_value_p) != func_p->info.percentile.cur_group_percentile))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PERCENTILE_FUNC_PERCENTILE_CHANGED_IN_GROUP, 0);
	      error = ER_PERCENTILE_FUNC_PERCENTILE_CHANGED_IN_GROUP;
	      goto exit;
	    }
	}

      /* copy value */
      pr_clear_value (func_p->value);
      error = db_value_coerce (&dbval, func_p->value, func_p->domain);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      error = ER_QPROC_INVALID_XASLNODE;
      goto exit;
    }

  if (copy_opr)
    {
      /* copy resultant operand value to analytic node */
      (void) pr_clear_value (func_p->value);
      dbval_type = DB_VALUE_DOMAIN_TYPE (func_p->value);
      pr_type_p = PR_TYPE_FROM_ID (dbval_type);
      if (pr_type_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      (*(pr_type_p->setval)) (func_p->value, opr_dbval_p, true);
    }

  func_p->curr_cnt++;

exit:
  pr_clear_value (&dbval);

  return error;
}

/*
 * qdata_finalize_analytic_func () -
 *   return: NO_ERROR, or ER_code
 *   func_p(in): Analytic expression node
 *   is_same_group(in): Don't deallocate list file
 *
 */
int
qdata_finalize_analytic_func (THREAD_ENTRY * thread_p, ANALYTIC_TYPE * func_p, bool is_same_group)
{
  DB_VALUE dbval;
  QFILE_LIST_ID *list_id_p;
  char *tuple_p;
  PR_TYPE *pr_type_p;
  OR_BUF buf;
  QFILE_LIST_SCAN_ID scan_id;
  SCAN_CODE scan_code;
  DB_VALUE xavgval, xavg_1val, x2avgval;
  DB_VALUE xavg2val, varval, sqr_val, dval;
  double dtmp;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  TP_DOMAIN *tmp_domain_ptr = NULL;
  int err = NO_ERROR;

  db_make_null (&sqr_val);
  db_make_null (&dbval);
  db_make_null (&xavgval);
  db_make_null (&xavg_1val);
  db_make_null (&x2avgval);
  db_make_null (&xavg2val);
  db_make_null (&varval);
  db_make_null (&dval);

  if (func_p->function == PT_VARIANCE || func_p->function == PT_VAR_POP || func_p->function == PT_VAR_SAMP
      || func_p->function == PT_STDDEV || func_p->function == PT_STDDEV_POP || func_p->function == PT_STDDEV_SAMP)
    {
      tmp_domain_ptr = tp_domain_resolve_default (DB_TYPE_DOUBLE);
    }

  /* set count-star aggregate values */
  if (func_p->function == PT_COUNT_STAR)
    {
      db_make_int (func_p->value, func_p->curr_cnt);
    }

  /* process list file for distinct */
  if (func_p->option == Q_DISTINCT)
    {
      assert (func_p->list_id->sort_list != NULL);

      list_id_p = qfile_sort_list (thread_p, func_p->list_id, NULL, Q_DISTINCT, false);

      /* release the resource to prevent resource leak */
      if (func_p->list_id != list_id_p)
	{
	  qfile_close_list (thread_p, func_p->list_id);
	  qfile_destroy_list (thread_p, func_p->list_id);
	}

      if (!list_id_p)
	{
	  return ER_FAILED;
	}

      func_p->list_id = list_id_p;

      if (func_p->function == PT_COUNT)
	{
	  db_make_int (func_p->value, list_id_p->tuple_cnt);
	}
      else
	{
	  pr_type_p = list_id_p->type_list.domp[0]->type;

	  /* scan list file, accumulating total for sum/avg */
	  if (qfile_open_list_scan (list_id_p, &scan_id) != NO_ERROR)
	    {
	      qfile_close_list (thread_p, list_id_p);
	      qfile_destroy_list (thread_p, list_id_p);
	      return ER_FAILED;
	    }

	  (void) pr_clear_value (func_p->value);

	  db_make_null (func_p->value);

	  /* median and percentile funcs don't need to read all rows */
	  if (list_id_p->tuple_cnt > 0 && QPROC_IS_INTERPOLATION_FUNC (func_p))
	    {
	      err = qdata_evaluate_interpolation_function (thread_p, func_p, &scan_id, true);
	      if (err != NO_ERROR)
		{
		  qfile_close_scan (thread_p, &scan_id);
		  qfile_close_list (thread_p, list_id_p);
		  qfile_destroy_list (thread_p, list_id_p);

		  goto error;
		}
	    }
	  else
	    {
	      while (true)
		{
		  scan_code = qfile_scan_list_next (thread_p, &scan_id, &tuple_record, PEEK);
		  if (scan_code != S_SUCCESS)
		    {
		      break;
		    }

		  tuple_p = ((char *) tuple_record.tpl + QFILE_TUPLE_LENGTH_SIZE);
		  if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_p) == V_UNBOUND)
		    {
		      continue;
		    }

		  or_init (&buf, (char *) tuple_p + QFILE_TUPLE_VALUE_HEADER_SIZE,
			   QFILE_GET_TUPLE_VALUE_LENGTH (tuple_p));
		  if ((*(pr_type_p->data_readval)) (&buf, &dbval, list_id_p->type_list.domp[0], -1, true, NULL, 0) !=
		      NO_ERROR)
		    {
		      qfile_close_scan (thread_p, &scan_id);
		      qfile_close_list (thread_p, list_id_p);
		      qfile_destroy_list (thread_p, list_id_p);
		      return ER_FAILED;
		    }

		  if (func_p->function == PT_VARIANCE || func_p->function == PT_VAR_POP
		      || func_p->function == PT_VAR_SAMP || func_p->function == PT_STDDEV
		      || func_p->function == PT_STDDEV_POP || func_p->function == PT_STDDEV_SAMP)
		    {
		      if (tp_value_coerce (&dbval, &dbval, tmp_domain_ptr) != DOMAIN_COMPATIBLE)
			{
			  (void) pr_clear_value (&dbval);
			  qfile_close_scan (thread_p, &scan_id);
			  qfile_close_list (thread_p, list_id_p);
			  qfile_destroy_list (thread_p, list_id_p);
			  return ER_FAILED;
			}
		    }

		  if (DB_IS_NULL (func_p->value))
		    {
		      /* first iteration: can't add to a null agg_ptr->value */
		      PR_TYPE *tmp_pr_type;
		      DB_TYPE dbval_type = DB_VALUE_DOMAIN_TYPE (&dbval);

		      tmp_pr_type = PR_TYPE_FROM_ID (dbval_type);
		      if (tmp_pr_type == NULL)
			{
			  (void) pr_clear_value (&dbval);
			  qfile_close_scan (thread_p, &scan_id);
			  qfile_close_list (thread_p, list_id_p);
			  qfile_destroy_list (thread_p, list_id_p);
			  return ER_FAILED;
			}

		      if (func_p->function == PT_STDDEV || func_p->function == PT_STDDEV_POP
			  || func_p->function == PT_STDDEV_SAMP || func_p->function == PT_VARIANCE
			  || func_p->function == PT_VAR_POP || func_p->function == PT_VAR_SAMP)
			{
			  if (qdata_multiply_dbval (&dbval, &dbval, &sqr_val, tmp_domain_ptr) != NO_ERROR)
			    {
			      (void) pr_clear_value (&dbval);
			      qfile_close_scan (thread_p, &scan_id);
			      qfile_close_list (thread_p, list_id_p);
			      qfile_destroy_list (thread_p, list_id_p);
			      return ER_FAILED;
			    }

			  (*(tmp_pr_type->setval)) (func_p->value2, &sqr_val, true);
			}

		      (*(tmp_pr_type->setval)) (func_p->value, &dbval, true);
		    }
		  else
		    {
		      TP_DOMAIN *domain_ptr;

		      if (func_p->function == PT_STDDEV || func_p->function == PT_STDDEV_POP
			  || func_p->function == PT_STDDEV_SAMP || func_p->function == PT_VARIANCE
			  || func_p->function == PT_VAR_POP || func_p->function == PT_VAR_SAMP)
			{
			  if (qdata_multiply_dbval (&dbval, &dbval, &sqr_val, tmp_domain_ptr) != NO_ERROR)
			    {
			      (void) pr_clear_value (&dbval);
			      qfile_close_scan (thread_p, &scan_id);
			      qfile_close_list (thread_p, list_id_p);
			      qfile_destroy_list (thread_p, list_id_p);
			      return ER_FAILED;
			    }

			  if (qdata_add_dbval (func_p->value2, &sqr_val, func_p->value2, tmp_domain_ptr) != NO_ERROR)
			    {
			      (void) pr_clear_value (&dbval);
			      pr_clear_value (&sqr_val);
			      qfile_close_scan (thread_p, &scan_id);
			      qfile_close_list (thread_p, list_id_p);
			      qfile_destroy_list (thread_p, list_id_p);
			      return ER_FAILED;
			    }
			}

		      domain_ptr = NOT_NULL_VALUE (tmp_domain_ptr, func_p->domain);
		      if ((func_p->function == PT_AVG) && (dbval.domain.general_info.type == DB_TYPE_NUMERIC))
			{
			  domain_ptr = NULL;
			}

		      if (qdata_add_dbval (func_p->value, &dbval, func_p->value, domain_ptr) != NO_ERROR)
			{
			  (void) pr_clear_value (&dbval);
			  qfile_close_scan (thread_p, &scan_id);
			  qfile_close_list (thread_p, list_id_p);
			  qfile_destroy_list (thread_p, list_id_p);
			  return ER_FAILED;
			}
		    }

		  (void) pr_clear_value (&dbval);
		}		/* while (true) */
	    }

	  qfile_close_scan (thread_p, &scan_id);
	  func_p->curr_cnt = list_id_p->tuple_cnt;
	}
    }

  if (is_same_group)
    {
      /* this is the end of a partition; save accumulator */
      qdata_copy_db_value (&func_p->part_value, func_p->value);
    }

  /* compute averages */
  if (func_p->curr_cnt > 0
      && (func_p->function == PT_AVG || func_p->function == PT_STDDEV || func_p->function == PT_STDDEV_POP
	  || func_p->function == PT_STDDEV_SAMP || func_p->function == PT_VARIANCE || func_p->function == PT_VAR_POP
	  || func_p->function == PT_VAR_SAMP))
    {
      TP_DOMAIN *double_domain_ptr;

      double_domain_ptr = tp_domain_resolve_default (DB_TYPE_DOUBLE);

      /* compute AVG(X) = SUM(X)/COUNT(X) */
      db_make_double (&dbval, func_p->curr_cnt);
      if (qdata_divide_dbval (func_p->value, &dbval, &xavgval, double_domain_ptr) != NO_ERROR)
	{
	  goto error;
	}

      if (func_p->function == PT_AVG)
	{
	  (void) pr_clear_value (func_p->value);
	  if (tp_value_coerce (&xavgval, func_p->value, double_domain_ptr) != DOMAIN_COMPATIBLE)
	    {
	      goto error;
	    }

	  goto exit;
	}

      if (func_p->function == PT_STDDEV_SAMP || func_p->function == PT_VAR_SAMP)
	{
	  /* compute SUM(X^2) / (n-1) */
	  if (func_p->curr_cnt > 1)
	    {
	      db_make_double (&dbval, func_p->curr_cnt - 1);
	    }
	  else
	    {
	      /* when not enough samples, return NULL */
	      (void) pr_clear_value (func_p->value);
	      db_make_null (func_p->value);
	      goto exit;
	    }
	}
      else
	{
	  assert (func_p->function == PT_STDDEV || func_p->function == PT_STDDEV_POP || func_p->function == PT_VARIANCE
		  || func_p->function == PT_VAR_POP);
	  /* compute SUM(X^2) / n */
	  db_make_double (&dbval, func_p->curr_cnt);
	}

      if (qdata_divide_dbval (func_p->value2, &dbval, &x2avgval, double_domain_ptr) != NO_ERROR)
	{
	  goto error;
	}

      /* compute {SUM(X) / (n)} OR {SUM(X) / (n-1)} for xxx_SAMP agg */
      if (qdata_divide_dbval (func_p->value, &dbval, &xavg_1val, double_domain_ptr) != NO_ERROR)
	{
	  goto error;
	}

      /* compute AVG(X) * {SUM(X) / (n)} , AVG(X) * {SUM(X) / (n-1)} for xxx_SAMP agg */
      if (qdata_multiply_dbval (&xavgval, &xavg_1val, &xavg2val, double_domain_ptr) != NO_ERROR)
	{
	  goto error;
	}

      /* compute VAR(X) = SUM(X^2)/(n) - AVG(X) * {SUM(X) / (n)} OR VAR(X) = SUM(X^2)/(n-1) - AVG(X) * {SUM(X) / (n-1)} 
       * for xxx_SAMP aggregates */
      if (qdata_subtract_dbval (&x2avgval, &xavg2val, &varval, double_domain_ptr) != NO_ERROR)
	{
	  goto error;
	}

      if (func_p->function == PT_VARIANCE || func_p->function == PT_VAR_POP || func_p->function == PT_VAR_SAMP
	  || func_p->function == PT_STDDEV || func_p->function == PT_STDDEV_POP || func_p->function == PT_STDDEV_SAMP)
	{
	  pr_clone_value (&varval, func_p->value);
	}

      if (!DB_IS_NULL (&varval)
	  && (func_p->function == PT_STDDEV || func_p->function == PT_STDDEV_POP || func_p->function == PT_STDDEV_SAMP))
	{
	  db_value_domain_init (&dval, DB_TYPE_DOUBLE, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  if (tp_value_coerce (&varval, &dval, double_domain_ptr) != DOMAIN_COMPATIBLE)
	    {
	      goto error;
	    }

	  dtmp = db_get_double (&dval);

	  /* mathematically, dtmp should be zero or positive; however, due to some precision errors, in some cases it
	   * can be a very small negative number of which we cannot extract the square root */
	  dtmp = (dtmp < 0.0f ? 0.0f : dtmp);

	  dtmp = sqrt (dtmp);
	  db_make_double (&dval, dtmp);

	  pr_clone_value (&dval, func_p->value);
	}
    }

exit:
  /* destroy distincts temp list file */
  if (!is_same_group)
    {
      qfile_close_list (thread_p, func_p->list_id);
      qfile_destroy_list (thread_p, func_p->list_id);
    }

  return NO_ERROR;

error:
  qfile_close_list (thread_p, func_p->list_id);
  qfile_destroy_list (thread_p, func_p->list_id);

  return ER_FAILED;
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
qdata_tuple_to_values_array (THREAD_ENTRY * thread_p, QFILE_TUPLE_DESCRIPTOR * tuple, DB_VALUE ** values)
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
qdata_apply_interpolation_function_coercion (DB_VALUE * f_value, TP_DOMAIN ** result_dom, DB_VALUE * result,
					     FUNC_TYPE function)
{
  DB_TYPE type;
  double d_result;
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
	      numeric_coerce_num_to_double (db_locate_numeric (f_value), DB_VALUE_SCALE (f_value), &d_result);
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
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMETZ:
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

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, qdump_function_type_string (function),
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
				     double c_row_num_d, TP_DOMAIN ** result_dom, DB_VALUE * result, FUNC_TYPE function)
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

	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, qdump_function_type_string (function),
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
      numeric_coerce_num_to_double (db_locate_numeric (f_value), DB_VALUE_SCALE (f_value), &d1);
      numeric_coerce_num_to_double (db_locate_numeric (c_value), DB_VALUE_SCALE (c_value), &d2);

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
    case DB_TYPE_TIMELTZ:
    case DB_TYPE_TIMETZ:
      if (type == DB_TYPE_TIMETZ)
	{
	  d1 = (double) (db_get_timetz (f_value)->time);
	  d2 = (double) (db_get_timetz (c_value)->time);
	}
      else
	{
	  d1 = (double) (*db_get_time (f_value));
	  d2 = (double) (*db_get_time (c_value));
	}

      d_result = floor ((c_row_num_d - row_num_d) * d1 + (row_num_d - f_row_num_d) * d2);

      time = (DB_TIME) d_result;

      if (type == DB_TYPE_TIME)
	{
	  db_value_put_encoded_time (result, &time);
	}
      else if (type == DB_TYPE_TIMELTZ)
	{
	  db_make_timeltz (result, &time);
	}
      else
	{
	  DB_TIMETZ ttz1, ttz2;

	  /* if the two timezones are different, we use the first timezone */
	  ttz1.time = time;
	  ttz1.tz_id = db_get_timetz (f_value)->tz_id;

	  error = tz_timetz_fix_zone (&ttz1, &ttz2);
	  if (error != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto end;
	    }

	  db_make_timetz (result, &ttz2);
	}

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
qdata_get_interpolation_function_result (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id, TP_DOMAIN * domain,
					 int pos, double row_num_d, double f_row_num_d, double c_row_num_d,
					 DB_VALUE * result, TP_DOMAIN ** result_dom, FUNC_TYPE function)
{
  int error = NO_ERROR;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
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

  error = fetch_peek_dbval (thread_p, &regu_var, NULL, NULL, NULL, tuple_record.tpl, &f_value);
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
      error = fetch_peek_dbval (thread_p, &regu_var, NULL, NULL, NULL, tuple_record.tpl, &c_value);
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
 * qdata_calculate_aggregate_cume_dist_percent_rank () -
 *   return: NO_ERROR, or ER_code
 *   agg_p(in): aggregate type
 *   val_desc_p(in):
 *
 */
static int
qdata_calculate_aggregate_cume_dist_percent_rank (THREAD_ENTRY * thread_p, AGGREGATE_TYPE * agg_p,
						  VAL_DESCR * val_desc_p)
{
  DB_VALUE *val_node, **val_node_p;
  int *len;
  int i, nloops, cmp;
  REGU_VARIABLE_LIST regu_var_list, regu_var_node, regu_tmp_node;
  AGGREGATE_DIST_PERCENT_INFO *info_p;
  PR_TYPE *pr_type_p;
  SORT_LIST *sort_p;
  SORT_ORDER s_order;
  SORT_NULLS s_nulls;
  DB_DOMAIN *dom;
  HL_HEAPID save_heapid = 0;

  assert (agg_p != NULL && agg_p->sort_list != NULL && agg_p->operand.type == TYPE_REGU_VAR_LIST);

  regu_var_list = agg_p->operand.value.regu_var_list;
  info_p = &agg_p->info.dist_percent;
  assert (regu_var_list != NULL && info_p != NULL);

  sort_p = agg_p->sort_list;
  assert (sort_p != NULL);

  /* for the first time, init */
  if (agg_p->accumulator.curr_cnt == 0)
    {
      /* first split the const list and type list: CUME_DIST and PERCENT_RANK is defined as: CUME_DIST( const_list)
       * WITHIN GROUP (ORDER BY type_list) ...  const list: the hypothetical values for calculation type list: field
       * name given in the ORDER BY clause; All these information is store in the agg_p->operand.value.regu_var_list;
       * First N values are type_list, and the last N values are const_list. */
      assert (info_p->list_len == 0 && info_p->const_array == NULL);

      regu_var_node = regu_tmp_node = regu_var_list;
      len = &info_p->list_len;
      info_p->nlargers = 0;
      nloops = 0;

      /* find the length of the type list and const list */
      while (regu_tmp_node)
	{
	  ++nloops;
	  regu_var_node = regu_var_node->next;
	  regu_tmp_node = regu_tmp_node->next->next;
	}
      *len = nloops;

      /* memory alloc for const array */
      assert (info_p->const_array == NULL);
      info_p->const_array = (DB_VALUE **) db_private_alloc (thread_p, nloops * sizeof (DB_VALUE *));

      if (info_p->const_array == NULL)
	{
	  goto exit_on_error;
	}

      /* now we have found the start of the const list, fetch DB_VALUE from the list into info.dist_percent */
      regu_tmp_node = regu_var_list;
      for (i = 0; i < nloops; i++)
	{
	  val_node_p = &info_p->const_array[i];
	  if (fetch_peek_dbval (thread_p, &regu_var_node->value, val_desc_p, NULL, NULL, NULL, val_node_p) != NO_ERROR)
	    {
	      goto exit_on_error;
	    }

	  /* Note: we must cast the const value to the same domain as the compared field in the order by clause */
	  dom = regu_tmp_node->value.domain;

	  if (REGU_VARIABLE_IS_FLAGED (&regu_var_node->value, REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE))
	    {
	      save_heapid = db_change_private_heap (thread_p, 0);
	    }

	  if (db_value_coerce (*val_node_p, *val_node_p, dom) != NO_ERROR)
	    {
	      if (save_heapid != 0)
		{
		  (void) db_change_private_heap (thread_p, save_heapid);
		  save_heapid = 0;
		}

	      goto exit_on_error;
	    }

	  if (save_heapid != 0)
	    {
	      (void) db_change_private_heap (thread_p, save_heapid);
	      save_heapid = 0;
	    }

	  regu_var_node = regu_var_node->next;
	  regu_tmp_node = regu_tmp_node->next;
	}
    }

  /* comparing the values of type list and const list */
  assert (info_p->list_len != 0 && info_p->const_array != NULL);

  regu_var_node = regu_var_list;
  cmp = 0;
  nloops = info_p->list_len;

  for (i = 0; i < nloops; i++)
    {
      /* Note: To handle 'nulls first/last', we need to compare NULLs values */
      s_order = sort_p->s_order;
      s_nulls = sort_p->s_nulls;
      sort_p = sort_p->next;

      if (fetch_peek_dbval (thread_p, &regu_var_node->value, val_desc_p, NULL, NULL, NULL, &val_node) != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* compare the value and find the order in asc or desc */
      if (DB_IS_NULL (val_node) && DB_IS_NULL (info_p->const_array[i]))
	{
	  /* NULL and NULL comparison */
	  cmp = DB_EQ;
	}
      else if (!DB_IS_NULL (val_node) && DB_IS_NULL (info_p->const_array[i]))
	{
	  /* non-NULL and NULL comparison */
	  if (s_nulls == S_NULLS_LAST)
	    {
	      cmp = DB_LT;
	    }
	  else
	    {
	      cmp = DB_GT;
	    }
	}
      else if (DB_IS_NULL (val_node) && !DB_IS_NULL (info_p->const_array[i]))
	{
	  /* NULL and non-NULL comparison */
	  if (s_nulls == S_NULLS_LAST)
	    {
	      cmp = DB_GT;
	    }
	  else
	    {
	      cmp = DB_LT;
	    }
	}
      else
	{
	  /* non-NULL values comparison */
	  pr_type_p = PR_TYPE_FROM_ID (DB_VALUE_DOMAIN_TYPE (val_node));
	  cmp = (*(pr_type_p->cmpval)) (val_node, info_p->const_array[i], 1, 0, NULL,
					regu_var_node->value.domain->collation_id);

	  assert (cmp != DB_UNK);
	}

      if (cmp != DB_EQ)
	{
	  if (s_order == S_DESC)
	    {
	      /* in a descend order */
	      cmp = -cmp;
	    }
	  break;
	}
      /* equal, compare next value */
      regu_var_node = regu_var_node->next;
    }

  switch (agg_p->function)
    {
    case PT_CUME_DIST:
      if (cmp <= 0)
	{
	  info_p->nlargers++;
	}
      break;
    case PT_PERCENT_RANK:
      if (cmp < 0)
	{
	  info_p->nlargers++;
	}
      break;
    default:
      goto exit_on_error;
    }

  agg_p->accumulator.curr_cnt++;

  return NO_ERROR;

exit_on_error:
  /* error! free const_array */
  if (agg_p->info.dist_percent.const_array != NULL)
    {
      db_private_free_and_init (thread_p, agg_p->info.dist_percent.const_array);
    }
  return ER_FAILED;
}

/*
 * qdata_alloc_agg_hkey () - allocate new hash aggregate key
 *   returns: pointer to new structure or NULL on error
 *   thread_p(in): thread
 *   val_cnt(in): size of key
 *   alloc_vals(in): if true will allocate dbvalues
 */
AGGREGATE_HASH_KEY *
qdata_alloc_agg_hkey (THREAD_ENTRY * thread_p, int val_cnt, bool alloc_vals)
{
  AGGREGATE_HASH_KEY *key;
  int i;

  key = (AGGREGATE_HASH_KEY *) db_private_alloc (thread_p, sizeof (AGGREGATE_HASH_KEY));
  if (key == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (AGGREGATE_HASH_KEY));
      return NULL;
    }

  key->values = (DB_VALUE **) db_private_alloc (thread_p, sizeof (DB_VALUE *) * val_cnt);
  if (key->values == NULL)
    {
      db_private_free (thread_p, key);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE *) * val_cnt);
      return NULL;
    }

  if (alloc_vals)
    {
      for (i = 0; i < val_cnt; i++)
	{
	  key->values[i] = pr_make_value ();
	}
    }

  key->val_count = val_cnt;
  key->free_values = alloc_vals;
  return key;
}

/*
 * qdata_free_agg_hkey () - free hash aggregate key
 *   thread_p(in): thread
 *   key(in): aggregate hash key
 */
void
qdata_free_agg_hkey (THREAD_ENTRY * thread_p, AGGREGATE_HASH_KEY * key)
{
  int i = 0;

  if (key == NULL)
    {
      return;
    }

  if (key->values != NULL)
    {
      if (key->free_values)
	{
	  for (i = 0; i < key->val_count; i++)
	    {
	      if (key->values[i])
		{
		  pr_free_value (key->values[i]);
		}
	    }
	}

      /* free values array */
      db_private_free (thread_p, key->values);
    }

  /* free structure */
  db_private_free (thread_p, key);
}

/*
 * qdata_alloc_agg_hkey () - allocate new hash aggregate key
 *   returns: pointer to new structure or NULL on error
 *   thread_p(in): thread
 */
AGGREGATE_HASH_VALUE *
qdata_alloc_agg_hvalue (THREAD_ENTRY * thread_p, int func_cnt)
{
  AGGREGATE_HASH_VALUE *value;
  int i;

  /* alloc structure */
  value = (AGGREGATE_HASH_VALUE *) db_private_alloc (thread_p, sizeof (AGGREGATE_HASH_VALUE));
  if (value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (AGGREGATE_HASH_VALUE));
      return NULL;
    }

  if (func_cnt > 0)
    {
      value->accumulators =
	(AGGREGATE_ACCUMULATOR *) db_private_alloc (thread_p, sizeof (AGGREGATE_ACCUMULATOR) * func_cnt);
      if (value->accumulators == NULL)
	{
	  db_private_free (thread_p, value);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  sizeof (AGGREGATE_ACCUMULATOR) * func_cnt);
	  return NULL;
	}
    }
  else
    {
      value->accumulators = NULL;
    }

  /* alloc DB_VALUEs */
  value->func_count = func_cnt;
  for (i = 0; i < func_cnt; i++)
    {
      value->accumulators[i].curr_cnt = 0;
      value->accumulators[i].value = pr_make_value ();
      value->accumulators[i].value2 = pr_make_value ();
    }

  /* initialize counter */
  value->tuple_count = 0;

  /* initialize tuple */
  value->first_tuple.size = 0;
  value->first_tuple.tpl = NULL;

  return value;
}

/*
 * qdata_free_agg_hkey () - free hash aggregate key
 *   thread_p(in): thread
 *   key(in): aggregate hash key
 */
void
qdata_free_agg_hvalue (THREAD_ENTRY * thread_p, AGGREGATE_HASH_VALUE * value)
{
  int i = 0;

  if (value == NULL)
    {
      return;
    }

  /* free values */
  if (value->accumulators != NULL)
    {
      for (i = 0; i < value->func_count; i++)
	{
	  if (value->accumulators[i].value != NULL)
	    {
	      pr_free_value (value->accumulators[i].value);
	    }

	  if (value->accumulators[i].value2 != NULL)
	    {
	      pr_free_value (value->accumulators[i].value2);
	    }
	}

      db_private_free (thread_p, value->accumulators);
    }

  /* free tuple */
  value->first_tuple.size = 0;
  if (value->first_tuple.tpl != NULL)
    {
      db_private_free (thread_p, value->first_tuple.tpl);
    }

  /* free structure */
  db_private_free (thread_p, value);
}

/*
 * qdata_get_agg_hkey_size () - get aggregate hash key size
 *   returns: size
 *   key(in): hash key
 */
int
qdata_get_agg_hkey_size (AGGREGATE_HASH_KEY * key)
{
  int i, size = 0;

  for (i = 0; i < key->val_count; i++)
    {
      if (key->values[i] != NULL)
	{
	  size += pr_value_mem_size (key->values[i]);
	}
    }

  return size + sizeof (AGGREGATE_HASH_KEY);
}

/*
 * qdata_get_agg_hvalue_size () - get aggregate hash value size
 *   returns: size
 *   value(in): hash
 *   ret_delta(in): if false return actual size, if true return difference in
 *                  size between previously computed size and current size
 */
int
qdata_get_agg_hvalue_size (AGGREGATE_HASH_VALUE * value, bool ret_delta)
{
  int i, size = 0, old_size = 0;

  if (value->accumulators != NULL)
    {
      for (i = 0; i < value->func_count; i++)
	{
	  if (value->accumulators[i].value != NULL)
	    {
	      size += pr_value_mem_size (value->accumulators[i].value);
	    }
	  if (value->accumulators[i].value2 != NULL)
	    {
	      size += pr_value_mem_size (value->accumulators[i].value2);
	    }
	  size += sizeof (AGGREGATE_ACCUMULATOR);
	}
    }

  size += sizeof (AGGREGATE_HASH_VALUE);
  size += value->first_tuple.size;

  old_size = (ret_delta ? value->curr_size : 0);
  value->curr_size = size;
  size -= old_size;

  return size;
}

/*
 * qdata_free_agg_hentry () - free key-value pair of hash entry
 *   returns: error code or NO_ERROR
 *   key(in): key pointer
 *   data(in): value pointer
 *   args(in): args passed by mht_rem (should be null)
 */
int
qdata_free_agg_hentry (const void *key, void *data, void *args)
{
  AGGREGATE_HASH_KEY *hkey = (AGGREGATE_HASH_KEY *) key;
  AGGREGATE_HASH_VALUE *hvalue = (AGGREGATE_HASH_VALUE *) data;
  THREAD_ENTRY *thread_p = (THREAD_ENTRY *) args;

  /* free key */
  qdata_free_agg_hkey (thread_p, hkey);

  /* free accumulators */
  qdata_free_agg_hvalue (thread_p, hvalue);

  /* all ok */
  return NO_ERROR;
}

/*
 * qdata_hash_agg_hkey () - compute hash of aggregate key
 *   returns: hash value
 *   key(in): key
 *   ht_size(in): hash table size (in buckets)
 */
unsigned int
qdata_hash_agg_hkey (const void *key, unsigned int ht_size)
{
  AGGREGATE_HASH_KEY *ckey = (AGGREGATE_HASH_KEY *) key;
  unsigned int hash_val = 0;
  int i;

  /* build hash value */
  for (i = 0; i < ckey->val_count; i++)
    {
      hash_val = hash_val ^ mht_get_hash_number (ht_size, ckey->values[i]);
    }

  return hash_val;
}

/*
 * qdata_agg_hkey_compare () - compare two aggregate keys
 *   returns: comparison result
 *   key1(in): first key
 *   key2(in): second key
 *   diff_pos(out): if not equal, position of difference, otherwise -1
 */
DB_VALUE_COMPARE_RESULT
qdata_agg_hkey_compare (AGGREGATE_HASH_KEY * ckey1, AGGREGATE_HASH_KEY * ckey2, int *diff_pos)
{
  DB_VALUE_COMPARE_RESULT result;
  int i;

  assert (diff_pos);
  *diff_pos = -1;

  if (ckey1 == ckey2)
    {
      /* same pointer, same values */
      return DB_EQ;
    }

  if (ckey1->val_count != ckey2->val_count)
    {
      /* can't compare keys of different sizes; shouldn't get here */
      assert (false);
      return DB_UNK;
    }

  for (i = 0; i < ckey1->val_count; i++)
    {
      result = tp_value_compare (ckey1->values[i], ckey2->values[i], 0, 1);
      if (result != DB_EQ)
	{
	  *diff_pos = i;
	  return result;
	}
    }

  /* if we got this far, it's equal */
  return DB_EQ;
}

/*
 * qdata_agg_hkey_eq () - check equality of two aggregate keys
 *   returns: true if equal, false otherwise
 *   key1(in): first key
 *   key2(in): second key
 */
int
qdata_agg_hkey_eq (const void *key1, const void *key2)
{
  AGGREGATE_HASH_KEY *ckey1 = (AGGREGATE_HASH_KEY *) key1;
  AGGREGATE_HASH_KEY *ckey2 = (AGGREGATE_HASH_KEY *) key2;
  int decoy;

  /* compare for equality */
  return (qdata_agg_hkey_compare (ckey1, ckey2, &decoy) == DB_EQ);
}

/*
 * qdata_copy_agg_hkey () - deep copy aggregate key
 *   returns: pointer to new aggregate hash key
 *   thread_p(in): thread
 *   key(in): source key
 */
AGGREGATE_HASH_KEY *
qdata_copy_agg_hkey (THREAD_ENTRY * thread_p, AGGREGATE_HASH_KEY * key)
{
  AGGREGATE_HASH_KEY *new_key = NULL;
  int i = 0;

  if (key)
    {
      /* make a copy */
      new_key = qdata_alloc_agg_hkey (thread_p, key->val_count, false);
    }

  if (new_key)
    {
      /* copy values */
      new_key->val_count = key->val_count;
      for (i = 0; i < key->val_count; i++)
	{
	  new_key->values[i] = pr_copy_value (key->values[i]);
	}

      new_key->free_values = true;
    }

  return new_key;
}

/*
 * qdata_load_agg_hvalue_in_agg_list () - load hash value in aggregate list
 *   value(in): aggregate hash value
 *   agg_list(in): aggregate list
 *   copy_vals(in): true for deep copy of DB_VALUES, false for shallow copy
 */
void
qdata_load_agg_hvalue_in_agg_list (AGGREGATE_HASH_VALUE * value, AGGREGATE_TYPE * agg_list, bool copy_vals)
{
  int i = 0;
  DB_TYPE db_type;

  if (value == NULL)
    {
      assert (false);
      return;
    }

  if (value->func_count != 0 && agg_list == NULL)
    {
      assert (false);
      return;
    }

  while (agg_list != NULL)
    {
      if (i >= value->func_count)
	{
	  /* should not get here */
	  assert (false);
	  break;
	}

      if (agg_list->function != PT_GROUPBY_NUM)
	{
	  if (copy_vals)
	    {
	      /* set tuple count */
	      agg_list->accumulator.curr_cnt = value->accumulators[i].curr_cnt;

	      /* copy */
	      (void) pr_clone_value (value->accumulators[i].value, agg_list->accumulator.value);
	      (void) pr_clone_value (value->accumulators[i].value2, agg_list->accumulator.value2);
	    }
	  else
	    {
	      /* set tuple count */
	      agg_list->accumulator.curr_cnt = value->accumulators[i].curr_cnt;

	      /*
	       * shallow, fast copy dbval. This may be unsafe. Internally, value->accumulators[i].value and
	       * agg_list->accumulator.value values keeps the same pointer to a buffer. If a value is cleared, the other
	       * value refer a invalid memory. Probably a safety way would be to use clone.
	       */
	      *(agg_list->accumulator.value) = *(value->accumulators[i].value);
	      *(agg_list->accumulator.value2) = *(value->accumulators[i].value2);

	      /* reset accumulator values. */
	      value->accumulators[i].value->need_clear = false;
	      db_type = DB_VALUE_DOMAIN_TYPE (value->accumulators[i].value);
	      if (db_type == DB_TYPE_VARCHAR || db_type == DB_TYPE_VARNCHAR)
		{
		  value->accumulators[i].value->data.ch.info.compressed_need_clear = false;
		}

	      value->accumulators[i].value2->need_clear = false;
	      db_type = DB_VALUE_DOMAIN_TYPE (value->accumulators[i].value2);
	      if (db_type == DB_TYPE_VARCHAR || db_type == DB_TYPE_VARNCHAR)
		{
		  value->accumulators[i].value2->data.ch.info.compressed_need_clear = false;
		}
	    }
	}

      /* next */
      agg_list = agg_list->next;
      i++;
    }

  assert (i == value->func_count);
}

/*
 * qdata_save_agg_hentry_to_list () - save key/value pair in list file
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread
 *   key(in): group key
 *   value(in): accumulators
 *   temp_dbval_array(in): array of temporary values used for holding counters
 *   list_id(in): target list file
 */
int
qdata_save_agg_hentry_to_list (THREAD_ENTRY * thread_p, AGGREGATE_HASH_KEY * key, AGGREGATE_HASH_VALUE * value,
			       DB_VALUE * temp_dbval_array, QFILE_LIST_ID * list_id)
{
  DB_VALUE tuple_count;
  int tuple_size = QFILE_TUPLE_LENGTH_SIZE;
  int col = 0, i;

  /* build tuple descriptor */
  for (i = 0; i < key->val_count; i++)
    {
      list_id->tpl_descr.f_valp[col++] = key->values[i];
      tuple_size += qdata_get_tuple_value_size_from_dbval (key->values[i]);
    }

  for (i = 0; i < value->func_count; i++)
    {
      list_id->tpl_descr.f_valp[col++] = value->accumulators[i].value;
      list_id->tpl_descr.f_valp[col++] = value->accumulators[i].value2;

      db_make_int (&temp_dbval_array[i], value->accumulators[i].curr_cnt);
      list_id->tpl_descr.f_valp[col++] = &temp_dbval_array[i];

      tuple_size += qdata_get_tuple_value_size_from_dbval (value->accumulators[i].value);
      tuple_size += qdata_get_tuple_value_size_from_dbval (value->accumulators[i].value2);
      tuple_size += qdata_get_tuple_value_size_from_dbval (&temp_dbval_array[i]);
    }

  db_make_int (&tuple_count, value->tuple_count);
  list_id->tpl_descr.f_valp[col++] = &tuple_count;
  tuple_size += qdata_get_tuple_value_size_from_dbval (&tuple_count);

  /* add to list file */
  list_id->tpl_descr.tpl_size = tuple_size;
  qfile_generate_tuple_into_list (thread_p, list_id, T_NORMAL);

  /* all ok */
  return NO_ERROR;
}

/*
 * qdata_load_agg_hentry_from_tuple () - load key/value pair from list file
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread
 *   tuple(in): tuple to load from
 *   key(out): group key
 *   value(out): accumulators
 *   list_id(in): list file
 *   key_dom(in): key domains
 *   acc_dom(in): accumulator domains
 */
int
qdata_load_agg_hentry_from_tuple (THREAD_ENTRY * thread_p, QFILE_TUPLE tuple, AGGREGATE_HASH_KEY * key,
				  AGGREGATE_HASH_VALUE * value, TP_DOMAIN ** key_dom,
				  AGGREGATE_ACCUMULATOR_DOMAIN ** acc_dom)
{
  QFILE_TUPLE_VALUE_FLAG flag;
  DB_VALUE int_val;
  OR_BUF iterator, buf;
  int i, rc;

  /* initialize buffer */
  db_make_int (&int_val, 0);
  OR_BUF_INIT (iterator, tuple, QFILE_GET_TUPLE_LENGTH (tuple));
  rc = or_advance (&iterator, QFILE_TUPLE_LENGTH_SIZE);
  if (rc != NO_ERROR)
    {
      return rc;
    }

  /* read key */
  for (i = 0; i < key->val_count; i++)
    {
      rc = qfile_locate_tuple_next_value (&iterator, &buf, &flag);
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      (void) pr_clear_value (key->values[i]);
      if (flag == V_BOUND)
	{
	  (key_dom[i]->type->data_readval) (&buf, key->values[i], key_dom[i], -1, true, NULL, 0);
	}
      else
	{
	  db_make_null (key->values[i]);
	}
    }

  /* read value */
  for (i = 0; i < value->func_count; i++)
    {
      /* read value */
      rc = qfile_locate_tuple_next_value (&iterator, &buf, &flag);
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      (void) pr_clear_value (value->accumulators[i].value);
      if (flag == V_BOUND)
	{
	  (acc_dom[i]->value_dom->type->data_readval) (&buf, value->accumulators[i].value, acc_dom[i]->value_dom, -1,
						       true, NULL, 0);
	}
      else
	{
	  db_make_null (value->accumulators[i].value);
	}

      /* read value2 */
      rc = qfile_locate_tuple_next_value (&iterator, &buf, &flag);
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      (void) pr_clear_value (value->accumulators[i].value2);
      if (flag == V_BOUND)
	{
	  (acc_dom[i]->value2_dom->type->data_readval) (&buf, value->accumulators[i].value2, acc_dom[i]->value2_dom, -1,
							true, NULL, 0);
	}
      else
	{
	  db_make_null (value->accumulators[i].value2);
	}

      /* read tuple count */
      rc = qfile_locate_tuple_next_value (&iterator, &buf, &flag);
      if (rc != NO_ERROR)
	{
	  return rc;
	}

      if (flag == V_BOUND)
	{
	  (tp_Integer_domain.type->data_readval) (&buf, &int_val, &tp_Integer_domain, -1, true, NULL, 0);
	  value->accumulators[i].curr_cnt = int_val.data.i;
	}
      else
	{
	  /* should not happen */
	  return ER_FAILED;
	}
    }

  /* read tuple count */
  rc = qfile_locate_tuple_next_value (&iterator, &buf, &flag);
  if (rc != NO_ERROR)
    {
      return rc;
    }

  if (flag == V_BOUND)
    {
      (tp_Integer_domain.type->data_readval) (&buf, &int_val, &tp_Integer_domain, -1, true, NULL, 0);
      value->tuple_count = int_val.data.i;
    }
  else
    {
      /* should not happen */
      return ER_FAILED;
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qdata_load_agg_hentry_from_list () - load key/value pair from list file
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread
 *   list_scan_id(in): list scan
 *   key(out): group key
 *   value(out): accumulators
 *   key_dom(in): key domains
 *   acc_dom(in): accumulator domains
 */
SCAN_CODE
qdata_load_agg_hentry_from_list (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * list_scan_id, AGGREGATE_HASH_KEY * key,
				 AGGREGATE_HASH_VALUE * value, TP_DOMAIN ** key_dom,
				 AGGREGATE_ACCUMULATOR_DOMAIN ** acc_dom)
{
  SCAN_CODE sc;
  QFILE_TUPLE_RECORD tuple_rec;

  sc = qfile_scan_list_next (thread_p, list_scan_id, &tuple_rec, PEEK);
  if (sc == S_SUCCESS)
    {
      if (qdata_load_agg_hentry_from_tuple (thread_p, tuple_rec.tpl, key, value, key_dom, acc_dom) != NO_ERROR)
	{
	  return S_ERROR;
	}
    }

  return sc;
}

/*
 * qdata_save_agg_htable_to_list () - save aggregate hash table to list file
 *   returns: error code or NO_ERROR
 *   thread_p(in): thread
 *   hash_table(in): take a wild guess
 *   tuple_list_id(in): list file containing unsorted tuples
 *   partial_list_id(in): list file containing partial accumulators
 *   temp_dbval_array(in): array of temporary values used for holding counters
 *
 * NOTE: This function will clear the hash table!
 */
int
qdata_save_agg_htable_to_list (THREAD_ENTRY * thread_p, MHT_TABLE * hash_table, QFILE_LIST_ID * tuple_list_id,
			       QFILE_LIST_ID * partial_list_id, DB_VALUE * temp_dbval_array)
{
  AGGREGATE_HASH_KEY *key = NULL;
  AGGREGATE_HASH_VALUE *value = NULL;
  HENTRY_PTR head;
  int rc;

  /* check nulls */
  if (hash_table == NULL || tuple_list_id == NULL || partial_list_id == NULL)
    {
      return ER_FAILED;
    }

  head = hash_table->act_head;
  while (head != NULL)
    {
      key = (AGGREGATE_HASH_KEY *) head->key;
      value = (AGGREGATE_HASH_VALUE *) head->data;

      /* dump first tuple to unsorted list */
      if (value->first_tuple.tpl != NULL)
	{
	  rc = qfile_add_tuple_to_list (thread_p, tuple_list_id, value->first_tuple.tpl);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	}

      if (value->tuple_count > 0)
	{
	  /* dump accumulators to partial list */
	  rc = qdata_save_agg_hentry_to_list (thread_p, key, value, temp_dbval_array, partial_list_id);
	  if (rc != NO_ERROR)
	    {
	      return rc;
	    }
	}

      /* next */
      head = head->act_next;
    }

  /* clear hash table; memory will no longer be used */
  rc = mht_clear (hash_table, qdata_free_agg_hentry, (void *) thread_p);
  if (rc != NO_ERROR)
    {
      return rc;
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * qdata_update_agg_interpolation_func_value_and_domain () -
 *   return: NO_ERROR, or error code
 *   agg_p(in): aggregate type
 *   val(in):
 *
 */
static int
qdata_update_agg_interpolation_func_value_and_domain (AGGREGATE_TYPE * agg_p, DB_VALUE * dbval)
{
  int error = NO_ERROR;
  DB_TYPE dbval_type;

  assert (dbval != NULL && agg_p != NULL && QPROC_IS_INTERPOLATION_FUNC (agg_p) && agg_p->sort_list != NULL
	  && agg_p->list_id != NULL && agg_p->list_id->type_list.type_cnt == 1);

  if (DB_IS_NULL (dbval))
    {
      goto end;
    }

  dbval_type = TP_DOMAIN_TYPE (agg_p->domain);
  if (dbval_type == DB_TYPE_VARIABLE || TP_DOMAIN_COLLATION_FLAG (agg_p->domain) != TP_DOMAIN_COLL_NORMAL)
    {
      dbval_type = DB_VALUE_DOMAIN_TYPE (dbval);
      agg_p->domain = tp_domain_resolve_default (dbval_type);
    }

  if (!TP_IS_DATE_OR_TIME_TYPE (dbval_type)
      && ((agg_p->function == PT_PERCENTILE_DISC && !TP_IS_NUMERIC_TYPE (dbval_type))
	  || (agg_p->function != PT_PERCENTILE_DISC && dbval_type != DB_TYPE_DOUBLE)))
    {
      error = qdata_update_interpolation_func_value_and_domain (dbval, dbval, &agg_p->domain);
      if (error != NO_ERROR)
	{
	  assert (error == ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, qdump_function_type_string (agg_p->function),
		  "DOUBLE, DATETIME, TIME");
	  goto end;
	}
    }
  else
    {
      dbval_type = DB_VALUE_DOMAIN_TYPE (dbval);
      if (dbval_type != TP_DOMAIN_TYPE (agg_p->domain))
	{
	  /* cast */
	  error = db_value_coerce (dbval, dbval, agg_p->domain);
	  if (error != NO_ERROR)
	    {
	      goto end;
	    }
	}
    }

  /* set list_id domain, if it's not set */
  if (TP_DOMAIN_TYPE (agg_p->list_id->type_list.domp[0]) != TP_DOMAIN_TYPE (agg_p->domain))
    {
      agg_p->list_id->type_list.domp[0] = agg_p->domain;
      agg_p->sort_list->pos_descr.dom = agg_p->domain;
    }

end:

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

  *domain = tmp_domain;

end:

  return error;
}

/*
 * qdata_evaluate_interpolation_function () -
 * return : error code or NO_ERROR
 * thread_p (in)    : thread entry
 * func_p (in)       :
 *
 * NOTE: scan_id is release at the caller
 */
static int
qdata_evaluate_interpolation_function (THREAD_ENTRY * thread_p, void *func_p, QFILE_LIST_SCAN_ID * scan_id,
				       bool is_analytic)
{
  int error = NO_ERROR;
  int tuple_count;
  double row_num_d, f_row_num_d, c_row_num_d, percentile_d;
  FUNC_TYPE function;
  AGGREGATE_TYPE *agg_p = NULL;
  ANALYTIC_TYPE *ana_p = NULL;
  double cur_group_percentile;

  assert (func_p != NULL && scan_id != NULL && scan_id->status == S_OPENED);

  if (is_analytic)
    {
      ana_p = (ANALYTIC_TYPE *) func_p;
      assert (QPROC_IS_INTERPOLATION_FUNC (ana_p));

      function = ana_p->function;
      cur_group_percentile = ana_p->info.percentile.cur_group_percentile;
    }
  else
    {
      agg_p = (AGGREGATE_TYPE *) func_p;
      assert (QPROC_IS_INTERPOLATION_FUNC (agg_p));

      function = agg_p->function;
      cur_group_percentile = agg_p->info.percentile.cur_group_percentile;
    }

  tuple_count = scan_id->list_id.tuple_cnt;
  if (tuple_count < 1)
    {
      return NO_ERROR;
    }

  if (function == PT_MEDIAN)
    {
      percentile_d = 0.5;
    }
  else
    {
      percentile_d = cur_group_percentile;

      if (function == PT_PERCENTILE_DISC)
	{
	  percentile_d = ceil (percentile_d * tuple_count) / tuple_count;
	}
    }

  row_num_d = ((double) (tuple_count - 1)) * percentile_d;
  f_row_num_d = floor (row_num_d);

  if (function == PT_PERCENTILE_DISC)
    {
      c_row_num_d = f_row_num_d;
    }
  else
    {
      c_row_num_d = ceil (row_num_d);
    }

  if (is_analytic)
    {
      error =
	qdata_get_interpolation_function_result (thread_p, scan_id, scan_id->list_id.type_list.domp[0], 0, row_num_d,
						 f_row_num_d, c_row_num_d, ana_p->value, &ana_p->domain,
						 ana_p->function);
    }
  else
    {
      error =
	qdata_get_interpolation_function_result (thread_p, scan_id, scan_id->list_id.type_list.domp[0], 0, row_num_d,
						 f_row_num_d, c_row_num_d, agg_p->accumulator.value, &agg_p->domain,
						 agg_p->function);
    }

  if (error == NO_ERROR)
    {
      if (is_analytic)
	{
	  if (TP_DOMAIN_TYPE (ana_p->domain) != ana_p->opr_dbtype)
	    {
	      ana_p->opr_dbtype = TP_DOMAIN_TYPE (ana_p->domain);
	    }
	}
      else if (TP_DOMAIN_TYPE (agg_p->domain) != agg_p->opr_dbtype)
	{
	  agg_p->opr_dbtype = TP_DOMAIN_TYPE (agg_p->domain);
	}
    }

  return error;
}
