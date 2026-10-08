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
#include "qfile_tuple_layout.h"

#include "dbtype.h"
#include "fetch.h"
#include "list_file.h"
#include "numeric_opfunc.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_opfunc.h"
#include "xasl.h"                           // QPROC_IS_INTERPOLATION_FUNC
#include "xasl_analytic.hpp"

#include <cmath>
#include "perf_monitor.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static int qdata_analytic_interpolation (cubthread::entry *thread_p, const VAL_DESCR *vd,
    cubxasl::analytic_list_node *ana_p, QFILE_LIST_SCAN_ID *scan_id);

/*
 * qdata_analytic_is_plain_sum_avg () - is this a plain SUM/AVG the peek path may take?
 *   return: true if the fast path in qdata_evaluate_analytic_func () is allowed
 *
 * This is the per-query half of the test; the caller checks the per-row state
 * (curr_cnt / is_active). The aggregate path uses the same split;
 * see qdata_agg_is_plain_sum_avg () in query_aggregate.cpp.
 *
 * DB_TYPE_VARIABLE and non-normal collations stay on the general path because
 * they require in-place coercion of the fetched value. DISTINCT uses its own
 * list file and is also excluded.
 */
static inline bool
qdata_analytic_is_plain_sum_avg (const ANALYTIC_TYPE *func_p, const VAL_DESCR *val_desc_p)
{
  if ((func_p->function != PT_SUM && func_p->function != PT_AVG) || func_p->option == Q_DISTINCT)
    {
      return false;
    }
  if (qexec_node_operand_type (val_desc_p, func_p->opr_dbtype, func_p->plan_item) == DB_TYPE_VARIABLE
      || TP_DOMAIN_COLLATION_FLAG (qexec_get_node_domain (val_desc_p, func_p->domain, func_p->plan_item))
      != TP_DOMAIN_COLL_NORMAL)
    {
      /* resolve_domains resolved the domain before the first row, so the domain does not block the fast path. The
       * first non-NULL value still takes the general path (curr_cnt < 1, sum_acc inactive), which applies that
       * resolution before the accumulator is activated (a resolution over a session variable read too); a
       * resolution without a value leaves only NULLs. */
      return qexec_resolved_domain (val_desc_p, func_p->plan_item) != NULL;
    }
  return true;
}

/*
 * qdata_initialize_analytic_func () -
 *   return: NO_ERROR, or ER_code
 *   func_p(in): Analytic expression node
 *   query_id(in): Associated query id
 *   vd(in): Value descriptor
 *
 */
int
qdata_initialize_analytic_func (cubthread::entry *thread_p, ANALYTIC_TYPE *func_p, QUERY_ID query_id,
				const VAL_DESCR *vd)
{
  func_p->curr_cnt = 0;
  func_p->sum_acc.is_active = false;
  /* the value's type: the function's domain in this execution, which the setup settled before the scan
   * (qexec_setup_analytic_domains; a function the compiler left variable has resolve_domains' type, which the first
   * binding used to give the value), else the value's own; a copy into the value takes this type (a *variable* value
   * would drop what MIN / MAX copy) */
  const TP_DOMAIN *domain = qexec_get_node_domain (vd, func_p->domain, func_p->plan_item);
  const DB_TYPE value_type = domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE
			     && TP_DOMAIN_TYPE (domain) != DB_TYPE_NULL
			     ? TP_DOMAIN_TYPE (domain) : DB_VALUE_DOMAIN_TYPE (func_p->value);
  if (db_value_domain_init (func_p->value, value_type, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  const FUNC_CODE fcode = func_p->function;
  if (fcode == PT_SUM || fcode == PT_AVG)
    {
      /* a value a SUM / AVG adds after the first takes the addition's operand coercion for its type - a string into
       * the sum the first value became - resolved here from the function's domain and its argument's in this
       * execution, and kept in the execution state (qexec_accumulator_domain) */
      DOMAIN_OPERAND_COERCION *coercion = &qexec_accumulator_domain (vd, func_p->plan_item)->operand_coercion;
      *coercion = DOMAIN_OPERAND_COERCION ();
      const TP_DOMAIN *argument = qexec_value_domain (vd, &func_p->operand);
      const TP_DOMAIN *function = qexec_resolved_domain (vd, func_p->plan_item);
      if (function == NULL)
	{
	  function = qexec_get_node_domain (vd, func_p->domain, func_p->plan_item);
	}
      if (argument != NULL && function != NULL && TP_DOMAIN_TYPE (argument) != DB_TYPE_VARIABLE
	  && TP_DOMAIN_TYPE (argument) != DB_TYPE_NULL && TP_DOMAIN_TYPE (function) != DB_TYPE_VARIABLE)
	{
	  /* the first value becomes the function's domain when it is a string, and keeps its own type otherwise */
	  const TP_DOMAIN *sum = TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (argument)) ? function : argument;
	  const DOMAIN_OPERAND operands[2] =
	  {
	    {sum, TP_DOMAIN_TYPE (sum), -1, false}, {argument, TP_DOMAIN_TYPE (argument), -1, false}
	  };
	  domain_resolve_operand_coercion (T_ADD, operands, coercion);
	}
    }
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
      type_list.domp[0] = qexec_get_node_domain (vd, func_p->operand.domain, func_p->operand.plan_item);
      if (TP_DOMAIN_TYPE (type_list.domp[0]) == DB_TYPE_VARIABLE)
	{
	  /* the values are written converted to the function's domain, which the setup settled; a *variable* readval
	   * would silently drop them */
	  type_list.domp[0] = qexec_get_node_domain (vd, func_p->domain, func_p->plan_item);
	}

      list_id_p = qfile_open_list (thread_p, &type_list, NULL, query_id, QFILE_FLAG_DISTINCT, NULL);
      if (list_id_p == NULL)
	{
	  db_private_free_and_init (thread_p, type_list.domp);
	  return ER_FAILED;
	}

      db_private_free_and_init (thread_p, type_list.domp);

      if (qfile_copy_list_id (func_p->list_id, list_id_p, true, QFILE_PROHIBIT_DEPENDENT) != NO_ERROR)
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
  const PR_TYPE *pr_type_p;
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

  /* Fast path for a plain SUM/AVG over one operand: peek the operand and add
   * it directly to the accumulator. The first value, a restored partial, and
   * NULL stay on the general path, which owns the fetched value and clears it
   * afterward. The row's own state is tested first. */
  if (func_p->curr_cnt >= 1 && func_p->sum_acc.is_active && qdata_analytic_is_plain_sum_avg (func_p, val_desc_p))
    {
      DB_VALUE *peek_operand_p = NULL;

      if (fetch_peek_dbval (thread_p, &func_p->operand, val_desc_p, NULL, NULL, NULL, &peek_operand_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      if (!DB_IS_NULL (peek_operand_p)
	  && func_p->sum_acc.sum_type == sum_acc_analytic_sum_type_for (DB_VALUE_DOMAIN_TYPE (peek_operand_p)))
	{
	  if (qdata_sum_acc_add_dbv (&func_p->sum_acc, peek_operand_p) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  func_p->curr_cnt++;
	  return NO_ERROR;
	}
    }

  /* the function's domain and operand type in this execution, settled before the scan (qexec_setup_analytic_domains);
   * the fast path above needs neither, and fetching its operand takes none of the function's */
  TP_DOMAIN *domain = qexec_get_node_domain (val_desc_p, func_p->domain, func_p->plan_item);
  DB_TYPE opr_type = qexec_node_operand_type (val_desc_p, func_p->opr_dbtype, func_p->plan_item);

  /* fetch operand value, analytic regulator variable should only contain constants */
  if (fetch_copy_dbval (thread_p, &func_p->operand, val_desc_p, NULL, NULL, NULL, &dbval) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* no value is converted at the function's binding: the setup settled its domains before the scan
   * (qexec_setup_analytic_domains), and each function converts the values it reads where it always did - a string into
   * a SUM / AVG, every value of a MEDIAN / PERCENTILE, a value of a DISTINCT list. The operand keeps its own type
   * otherwise, as it did under develop's first binding, which typed the function by the value and converted nothing. */

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
      /* later rows may have different types because only the first row is coerced.
       * coerce all values to the list domain for consistent duplicate elimination and finalize
       * (a conversion to the function's domain, not a resolution) */
      if (TP_DOMAIN_TYPE (func_p->list_id->type_list.domp[0]) != DB_TYPE_VARIABLE
	  && DB_VALUE_DOMAIN_TYPE (&dbval) != TP_DOMAIN_TYPE (func_p->list_id->type_list.domp[0]))
	{
	  /* a value the list's domain does not take is the row's error, -181 (ER_FAILED alone left no error and no row) */
	  DB_VALUE coerced;
	  db_make_null (&coerced);
	  dom_status = tp_value_coerce (&dbval, &coerced, func_p->list_id->type_list.domp[0]);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, &dbval, func_p->list_id->type_list.domp[0]);
	      pr_clear_value (&coerced);
	      goto exit;
	    }
	  pr_clear_value (&dbval);
	  dbval = coerced;
	}

      /* handle distincts by adding to the temp list file (the assembler encodes for the list's column layout) */
      {
	DB_VALUE *distinct_val_p = &dbval;

	if (qfile_add_values_tuple_to_list (thread_p, func_p->list_id, &distinct_val_p, 1) != NO_ERROR)
	  {
	    error = ER_FAILED;
	    goto exit;
	  }
      }

      /* interpolation funcs need to check domain compatibility in the following code */
      if (!QPROC_IS_INTERPOLATION_FUNC (func_p))
	{
	  goto exit;
	}
    }

  copy_opr = false;
  coll_id = domain->collation_id;
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
	  || domain->type->cmpval (func_p->value, &dbval, 1, 1, NULL, coll_id) > 0)
	{
	  copy_opr = true;
	}
      break;

    case PT_MAX:
      opr_dbval_p = &dbval;
      if ((func_p->curr_cnt < 1 || DB_IS_NULL (func_p->value))
	  || domain->type->cmpval (func_p->value, &dbval, 1, 1, NULL, coll_id) < 0)
	{
	  copy_opr = true;
	}
      break;

    case PT_AVG:
    case PT_SUM:
    {
      /* An operand resolved by late binding (opr_dbtype == DB_TYPE_VARIABLE) is
       * coerced in place only on the first row. Repeat the coercion on later rows
       * so the value matches the accumulation type, as the legacy add does
       * implicitly on every row.
       */
      if (func_p->sum_acc.is_active
	  && func_p->sum_acc.sum_type != sum_acc_analytic_sum_type_for (DB_VALUE_DOMAIN_TYPE (&dbval)))
	{
	  dom_status = tp_value_coerce (&dbval, &dbval, domain);
	  if (dom_status != DOMAIN_COMPATIBLE)
	    {
	      error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, &dbval, domain);
	      goto exit;
	    }
	}

      /* whether the accumulator takes this value's type */
      bool use_sum_acc = SUM_ACC_IS_ANALYTIC_SUPPORTED_TYPE (DB_VALUE_DOMAIN_TYPE (&dbval));

      if (func_p->curr_cnt < 1)
	{
	  opr_dbval_p = &dbval;
	  copy_opr = true;

	  if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (opr_dbval_p)))
	    {
	      /* char types default to double; coerce here so we don't mess up the accumulator when we copy the operand.
	       * A string the function's domain does not take ('10:00:00' as a DOUBLE) is the row's error, -181, as the
	       * aggregate path raises it (qdata_aggregate_value_to_accumulator); ER_FAILED alone left no error and no
	       * row. */
	      DB_VALUE coerced;
	      db_make_null (&coerced);
	      dom_status = tp_value_coerce (&dbval, &coerced, domain);
	      if (dom_status != DOMAIN_COMPATIBLE)
		{
		  error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, &dbval, domain);
		  pr_clear_value (&coerced);
		  goto exit;
		}
	      pr_clear_value (&dbval);
	      dbval = coerced;
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
      else if (!use_sum_acc)
	{
	  TP_DOMAIN *result_domain;
	  DB_TYPE type =
		  (func_p->function ==
		   PT_AVG) ? (DB_TYPE) func_p->value->domain.general_info.type : TP_DOMAIN_TYPE (domain);

	  if (func_p->sum_acc.is_active)
	    {
	      /* guard: an unsupported type must not arrive while the accumulator is active */
	      assert (false);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	      error = ER_FAILED;
	      goto exit;
	    }

	  result_domain = ((type == DB_TYPE_NUMERIC) ? NULL : domain);
	  /* after the operand coercion the partition resolved for a value */
	  const DOMAIN_OPERAND_COERCION *coercion =
		  &qexec_accumulator_domain (val_desc_p, func_p->plan_item)->operand_coercion;
	  if (qdata_coerce_arith_operands (T_ADD, coercion->conv, coercion->operand_domain, func_p->value,
					   &dbval, func_p->value, result_domain) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto exit;
	    }
	  copy_opr = false;
	}

      /* Supported types accumulate through the accumulator, the first value
       * included: mid-partition snapshots overwrite func_p->value, so it cannot
       * park the first value the way the aggregate path does. */
      if (use_sum_acc
	  && qdata_sum_acc_accumulate (&func_p->sum_acc, func_p->curr_cnt < 1, func_p->value,
				       &dbval) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto exit;
	}
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

      {
	/* a value DOUBLE does not take is the row's error, -181, as the aggregate STDDEV / VARIANCE raise it */
	DB_VALUE coerced;
	db_make_null (&coerced);
	dom_status = tp_value_coerce (&dbval, &coerced, tmp_domain_p);
	if (dom_status != DOMAIN_COMPATIBLE)
	  {
	    error = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, &dbval, tmp_domain_p);
	    pr_clear_value (&coerced);
	    goto exit;
	  }
	pr_clear_value (&dbval);
	dbval = coerced;
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
	      /* the execution's descriptor: a constant ratio reads resolve_domains' value */
	      error =
		      fetch_peek_dbval (thread_p, percentile_info_p->percentile_reguvar, val_desc_p, NULL, NULL, NULL,
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
	}

      /* percentile value check */
      if (func_p->function == PT_PERCENTILE_CONT || func_p->function == PT_PERCENTILE_DISC)
	{
	  error =
		  fetch_peek_dbval (thread_p, percentile_info_p->percentile_reguvar, val_desc_p, NULL, NULL, NULL,
				    &peek_value_p);
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

      /* copy value, converted to the function's domain, which the setup settled (qexec_setup_analytic_domains) */
      pr_clear_value (func_p->value);
      error = db_value_coerce (&dbval, func_p->value, domain);
      if (error != NO_ERROR)
	{
	  if (!TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (&dbval))
	      || (func_p->curr_cnt < 1
		  && (func_p->plan_item == NULL || ! (func_p->plan_item->flags & DOMAIN_PLAN_VALUE_ARGUMENT))))
	    {
	      /* the function's error, as the cast of the first value reported it: a value of a type the function cannot
	       * take (a bit string, a collection), or the first value of a string column or expression (a date string
	       * under the compiled DOUBLE); a later string that does not convert fails as the conversion does, and a
	       * value argument (a literal, a bind, a session variable read) keeps the conversion's error */
	      er_clear ();
	      error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, fcode_get_uppercase_name (func_p->function),
		      TP_DOMAIN_TYPE (domain) == DB_TYPE_TIME ? "DOUBLE, DATETIME, TIME" : "DOUBLE");
	    }
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
qdata_finalize_analytic_func (cubthread::entry *thread_p, ANALYTIC_TYPE *func_p, bool is_same_group,
			      const VAL_DESCR *vd)
{
  DB_VALUE dbval;
  QFILE_LIST_ID *list_id_p;
  QFILE_LIST_SCAN_ID scan_id;
  SCAN_CODE scan_code;
  DB_VALUE xavgval, xavg_1val, x2avgval;
  DB_VALUE xavg2val, varval, sqr_val, dval;
  double dtmp;
  QFILE_TUPLE_RECORD tuple_record = QFILE_TUPLE_RECORD_INITIALIZER;
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
	      err = qdata_analytic_interpolation (thread_p, vd, func_p, &scan_id);
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

		  {
		    bool is_null;

		    if (qfile_slot_read_column_value (&tuple_record, 0, list_id_p->type_list.domp[0], &dbval, true, &is_null)
			!= NO_ERROR)
		      {
			qfile_close_scan (thread_p, &scan_id);
			qfile_close_list (thread_p, list_id_p);
			qfile_destroy_list (thread_p, list_id_p);
			return ER_FAILED;
		      }
		    if (is_null)
		      {
			continue;
		      }
		  }

		  {
		    /* the distinct list keeps the argument's type; a string it holds is read as the function's domain
		     * (DOUBLE for SUM / AVG), as the first value of the function without DISTINCT is
		     * (qdata_evaluate_analytic_func) - the value list the finalization writes is laid out by that domain,
		     * and the typed addition takes no string. A string the domain does not take is -181. */
		    TP_DOMAIN *function_domain = tmp_domain_ptr != NULL ? tmp_domain_ptr
		      : qexec_get_node_domain (vd, func_p->domain, func_p->plan_item);
		    if (TP_IS_CHAR_TYPE (DB_VALUE_DOMAIN_TYPE (&dbval)) && function_domain != NULL
			&& TP_IS_NUMERIC_TYPE (TP_DOMAIN_TYPE (function_domain)))
		      {
			DB_VALUE coerced;
			db_make_null (&coerced);
			TP_DOMAIN_STATUS dom_status = tp_value_coerce (&dbval, &coerced, function_domain);
			if (dom_status != DOMAIN_COMPATIBLE)
			  {
			    err = tp_domain_status_er_set (dom_status, ARG_FILE_LINE, &dbval, function_domain);
			    pr_clear_value (&coerced);
			    (void) pr_clear_value (&dbval);
			    qfile_close_scan (thread_p, &scan_id);
			    qfile_close_list (thread_p, list_id_p);
			    qfile_destroy_list (thread_p, list_id_p);
			    goto error;
			  }
			(void) pr_clear_value (&dbval);
			dbval = coerced;
		      }
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
		      const PR_TYPE *tmp_pr_type;
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

		      domain_ptr = tmp_domain_ptr != NULL ? tmp_domain_ptr
				   : qexec_get_node_domain (vd, func_p->domain, func_p->plan_item);
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

  /* Emit a rounded snapshot. The accumulator stays active for cumulative evaluation. */
  if ((func_p->function == PT_SUM || func_p->function == PT_AVG) && func_p->sum_acc.is_active)
    {
      if (qdata_sum_acc_snapshot (&func_p->sum_acc, func_p->value) != NO_ERROR)
	{
	  goto error;
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
qdata_analytic_interpolation (cubthread::entry *thread_p, const VAL_DESCR *vd, cubxasl::analytic_list_node *ana_p,
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

  /* the function takes the domain the interpolation gives, and its type as the operand type, as its execution
   * domains */
  TP_DOMAIN *domain = qexec_get_node_domain (vd, ana_p->domain, ana_p->plan_item);
  error =
	  qdata_get_interpolation_function_result (thread_p, scan_id, scan_id->list_id.type_list.domp[0], 0, row_num_d,
	      f_row_num_d, c_row_num_d, ana_p->value, &domain,
	      ana_p->function);

  if (error == NO_ERROR)
    {
      qexec_set_node_domain (vd, ana_p->plan_item, ana_p->domain, domain);
      qexec_take_operand_type (vd, ana_p->plan_item, ana_p->opr_dbtype, TP_DOMAIN_TYPE (domain));
    }

  return error;
}
