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

//
// query_analytic - implementation of analytic query execution
//

#include "query_analytic.hpp"

#include "dbtype.h"
#include "fetch.h"
#include "list_file.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_opfunc.h"
#include "xasl.h"                           // QPROC_IS_INTERPOLATION_FUNC
#include "xasl_analytic.hpp"

#include <cmath>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static int qdata_analytic_interpolation (cubthread::entry *thread_p, cubxasl::analytic_list_node *ana_p,
    QFILE_LIST_SCAN_ID *scan_id);

/*
 * qdata_initialize_analytic_func () -
 *   return: NO_ERROR, or ER_code
 *   func_p(in): Analytic expression node
 *   query_id(in): Associated query id
 *
 */
int
qdata_initialize_analytic_func (cubthread::entry *thread_p, ANALYTIC_TYPE *func_p, QUERY_ID query_id)
{
  func_p->curr_cnt = 0;
  if (db_value_domain_init (func_p->value, DB_VALUE_DOMAIN_TYPE (func_p->value), DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE)
      != NO_ERROR)
    {
      return ER_FAILED;
    }

  const FUNC_CODE fcode = func_p->function;
  if (fcode == PT_COUNT_STAR || fcode == PT_COUNT)
    {
      db_make_bigint (func_p->value, 0);
    }
  else if (fcode == PT_ROW_NUMBER || fcode == PT_RANK || fcode == PT_DENSE_RANK)
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

      list_id_p = qfile_open_list (thread_p, &type_list, NULL, query_id, QFILE_FLAG_DISTINCT, NULL);
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
qdata_evaluate_analytic_func (cubthread::entry *thread_p, ANALYTIC_TYPE *func_p, VAL_DESCR *val_desc_p)
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
	  func_p->domain = tp_domain_resolve_default (DB_TYPE_BIGINT);
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
      pr_type_p = pr_type_from_id (dbval_type);

      if (pr_type_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      dbval_size = pr_data_writeval_disk_size (&dbval);
      if (dbval_size > 0 && (disk_repr_p = (char *) db_private_alloc (thread_p, dbval_size)) != NULL)
	{
	  or_init (&buf, disk_repr_p, dbval_size);
	  error = pr_type_p->data_writeval (&buf, &dbval);
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

	  int ntile_bucket = (int) floor (db_get_double (&dbval));

	  /* boundary check */
	  if (ntile_bucket < 1 || ntile_bucket > DB_INT32_MAX)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_NTILE_INVALID_BUCKET_NUMBER, 0);
	      error = ER_NTILE_INVALID_BUCKET_NUMBER;
	      goto exit;
	    }

	  /* we're sure the operand is not null */
	  func_p->info.ntile.is_null = false;
	  func_p->info.ntile.bucket_count = ntile_bucket;
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
	  || func_p->domain->type->cmpval (func_p->value, &dbval, 1, 1, NULL, coll_id) > 0)
	{
	  copy_opr = true;
	}
      break;

    case PT_MAX:
      opr_dbval_p = &dbval;
      if ((func_p->curr_cnt < 1 || DB_IS_NULL (func_p->value))
	  || func_p->domain->type->cmpval (func_p->value, &dbval, 1, 1, NULL, coll_id) < 0)
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
	  db_make_bigint (func_p->value, 1);
	}
      else
	{
	  db_make_bigint (func_p->value, db_get_bigint (func_p->value) + 1);
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
	  pr_type_p = pr_type_from_id (dbval_type);
	  if (pr_type_p == NULL)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }

	  pr_type_p->setval (func_p->value, &dbval, true);
	  pr_type_p->setval (func_p->value2, &sqr_val, true);
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
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, fcode_get_uppercase_name (func_p->function),
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
      pr_type_p = pr_type_from_id (dbval_type);
      if (pr_type_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      pr_type_p->setval (func_p->value, opr_dbval_p, true);
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
qdata_finalize_analytic_func (cubthread::entry *thread_p, ANALYTIC_TYPE *func_p, bool is_same_group)
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
      db_make_bigint (func_p->value, (INT64) func_p->curr_cnt);
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
	  db_make_bigint (func_p->value, list_id_p->tuple_cnt);
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
	      err = qdata_analytic_interpolation (thread_p, func_p, &scan_id);
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
		  if (pr_type_p->data_readval (&buf, &dbval, list_id_p->type_list.domp[0], -1, true, NULL, 0) !=
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

		      tmp_pr_type = pr_type_from_id (dbval_type);
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

			  tmp_pr_type->setval (func_p->value2, &sqr_val, true);
			}

		      tmp_pr_type->setval (func_p->value, &dbval, true);
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

		      domain_ptr = tmp_domain_ptr != NULL ? tmp_domain_ptr : func_p->domain;
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
  /* destroy distinct temp list file */
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

static int
qdata_analytic_interpolation (cubthread::entry *thread_p, cubxasl::analytic_list_node *ana_p,
			      QFILE_LIST_SCAN_ID *scan_id)
{
  int error = NO_ERROR;
  INT64 tuple_count;
  double row_num_d, f_row_num_d, c_row_num_d, percentile_d;
  FUNC_CODE function;
  double cur_group_percentile;

  assert (ana_p != NULL && scan_id != NULL && scan_id->status == S_OPENED);
  assert (QPROC_IS_INTERPOLATION_FUNC (ana_p));

  function = ana_p->function;
  cur_group_percentile = ana_p->info.percentile.cur_group_percentile;

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

  error =
	  qdata_get_interpolation_function_result (thread_p, scan_id, scan_id->list_id.type_list.domp[0], 0, row_num_d,
	      f_row_num_d, c_row_num_d, ana_p->value, &ana_p->domain,
	      ana_p->function);

  if (error == NO_ERROR)
    {
      ana_p->opr_dbtype = TP_DOMAIN_TYPE (ana_p->domain);
    }

  return error;
}
