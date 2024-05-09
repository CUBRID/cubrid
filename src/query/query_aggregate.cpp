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
// query_aggregate - implementation of aggregate functions execution during queries
//

#include "query_aggregate.hpp"

#include "arithmetic.h"
#include "btree.h"                          // btree_find_min_or_max_key, btree_get_unique_statistics_for_count
#include "db_json.hpp"
#include "dbtype.h"
#include "fetch.h"
#include "list_file.h"
#include "memory_alloc.h"
#include "memory_hash.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_opfunc.h"
#include "regu_var.hpp"
#include "string_opfunc.h"
#include "xasl.h"                           // QPROC_IS_INTERPOLATION_FUNC
#include "xasl_aggregate.hpp"
#include "statistics.h"

#include <cmath>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

using namespace cubquery;

//
// static functions declarations
//
static int qdata_aggregate_value_to_accumulator (cubthread::entry *thread_p, cubxasl::aggregate_accumulator *acc,
    cubxasl::aggregate_accumulator_domain *domain, FUNC_CODE func_type,
    tp_domain *func_domain, db_value *value, bool is_acc_to_acc);
static int qdata_aggregate_multiple_values_to_accumulator (cubthread::entry *thread_p,
    cubxasl::aggregate_accumulator *acc,
    cubxasl::aggregate_accumulator_domain *domain,
    FUNC_CODE func_type, tp_domain *func_domain,
    std::vector<DB_VALUE> &db_values);
static int qdata_process_distinct_or_sort (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_p,
    QUERY_ID query_id);
static int qdata_calculate_aggregate_cume_dist_percent_rank (cubthread::entry *thread_p,
    cubxasl::aggregate_list_node *agg_p,
    VAL_DESCR *val_desc_p);
static int qdata_update_agg_interpolation_func_value_and_domain (cubxasl::aggregate_list_node *agg_p, DB_VALUE *val);
static int qdata_aggregate_interpolation (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_p,
    QFILE_LIST_SCAN_ID *scan_id);

static int qdata_group_concat_first_value (THREAD_ENTRY *thread_p, AGGREGATE_TYPE *agg_p, DB_VALUE *dbvalue);
static int qdata_group_concat_value (THREAD_ENTRY *thread_p, AGGREGATE_TYPE *agg_p, DB_VALUE *dbvalue);

//
// implementation
//

static int
qdata_process_distinct_or_sort (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_p, QUERY_ID query_id)
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

  type_list.domp[0] = agg_p->operands->value.domain;
  /* if the agg has ORDER BY force setting 'QFILE_FLAG_ALL' : in this case, no additional SORT_LIST will be created,
   * but the one in the aggregate_list_node structure will be used */
  if (agg_p->sort_list != NULL)
    {
      ls_flag = QFILE_FLAG_ALL;
    }
  list_id_p = qfile_open_list (thread_p, &type_list, NULL, query_id, ls_flag, NULL);

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
qdata_initialize_aggregate_list (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_list_p,
				 QUERY_ID query_id)
{
  cubxasl::aggregate_list_node *agg_p;

  for (agg_p = agg_list_p; agg_p != NULL; agg_p = agg_p->next)
    {

      /* the value of groupby_num() remains unchanged; it will be changed while evaluating groupby_num predicates
       * against each group at 'xs_eval_grbynum_pred()' */
      if (agg_p->function == PT_GROUPBY_NUM)
	{
	  /* nothing to do with groupby_num() */
	  continue;
	}

      /* CAUTION : if modify initializing ACC's value then should change qdata_alloc_agg_hvalue() */
      agg_p->accumulator.curr_cnt = 0;
      if (db_value_domain_init (agg_p->accumulator.value, DB_VALUE_DOMAIN_TYPE (agg_p->accumulator.value),
				DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      /* This set is made, because if class is empty, aggregate results should return NULL, except count(*) and count */
      if (agg_p->function == PT_COUNT_STAR || agg_p->function == PT_COUNT)
	{
	  db_make_bigint (agg_p->accumulator.value, 0);
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
qdata_aggregate_accumulator_to_accumulator (cubthread::entry *thread_p, cubxasl::aggregate_accumulator *acc,
    cubxasl::aggregate_accumulator_domain *acc_dom, FUNC_CODE func_type,
    tp_domain *func_domain, cubxasl::aggregate_accumulator *new_acc)
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
      // these functions only affect acc.value and new_acc can be treated as an ordinary value
      error = qdata_aggregate_value_to_accumulator (thread_p, acc, acc_dom, func_type, func_domain, new_acc->value, true);
      break;

    // for these two situations we just need to merge
    case PT_JSON_ARRAYAGG:
    case PT_JSON_OBJECTAGG:
      error = db_evaluate_json_merge_preserve (new_acc->value, &acc->value, 1);
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
	  double_domain->type->setval (acc->value, new_acc->value, true);
	  double_domain->type->setval (acc->value2, new_acc->value2, true);
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
 *   value_next(int): value of the second argument; used only for JSON_OBJECTAGG
 */
static int
qdata_aggregate_value_to_accumulator (cubthread::entry *thread_p, cubxasl::aggregate_accumulator *acc,
				      cubxasl::aggregate_accumulator_domain *domain, FUNC_CODE func_type,
				      tp_domain *func_domain, db_value *value, bool is_acc_to_acc)
{
  DB_VALUE squared;
  bool copy_operator = false;
  int coll_id = -1;

  if (DB_IS_NULL (value))
    {
      return NO_ERROR;
    }

  if (domain != NULL && domain->value_dom != NULL)
    {
      coll_id = domain->value_dom->collation_id;
    }

  /* aggregate new value */
  switch (func_type)
    {
    case PT_MIN:
      if (coll_id == -1)
	{
	  return ER_FAILED;
	}
      if (acc->curr_cnt < 1 || domain->value_dom->type->cmpval (acc->value, value, 1, 1, NULL, coll_id) > 0)
	{
	  /* we have new minimum */
	  copy_operator = true;
	}
      break;

    case PT_MAX:
      if (coll_id == -1)
	{
	  return ER_FAILED;
	}
      if (acc->curr_cnt < 1 || domain->value_dom->type->cmpval (acc->value, value, 1, 1, NULL, coll_id) < 0)
	{
	  /* we have new maximum */
	  copy_operator = true;
	}
      break;

    case PT_COUNT:
      if (is_acc_to_acc)
	{
	  /* from qdata_aggregate_accumulator_to_accumulator (). value param is number of count */
	  db_make_bigint (acc->value, db_get_bigint (acc->value) + db_get_bigint (value));
	}
      else
	{
	  /* from qdata_aggregate_multiple_values_to_accumulator(). value param is value of column */
	  if (acc->curr_cnt < 1)
	    {
	      /* first value */
	      db_make_bigint (acc->value, (INT64) 1);
	    }
	  else
	    {
	      /* increment */
	      db_make_bigint (acc->value, (INT64) 1 + db_get_bigint (acc->value));
	    }
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
	  domain->value_dom->type->setval (acc->value, value, true);
	  domain->value2_dom->type->setval (acc->value2, &squared, true);
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

    case PT_JSON_ARRAYAGG:
      if (db_accumulate_json_arrayagg (value, acc->value) != NO_ERROR)
	{
	  return ER_FAILED;
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

static int
qdata_aggregate_multiple_values_to_accumulator (cubthread::entry *thread_p, cubxasl::aggregate_accumulator *acc,
    cubxasl::aggregate_accumulator_domain *domain, FUNC_CODE func_type,
    tp_domain *func_domain, std::vector<DB_VALUE> &db_values)
{
  // we have only one argument so aggregate only the first db_value
  if (db_values.size () == 1)
    {
      return qdata_aggregate_value_to_accumulator (thread_p, acc, domain, func_type, func_domain, &db_values[0], false);
    }

  // maybe this condition will be changed in the future based on the future arguments conditions
  for (DB_VALUE &db_value : db_values)
    {
      if (DB_IS_NULL (&db_value))
	{
	  return NO_ERROR;
	}
    }

  switch (func_type)
    {
    case PT_JSON_OBJECTAGG:
      if (db_accumulate_json_objectagg (&db_values[0], &db_values[1], acc->value) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return ER_FAILED;
    }

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
qdata_evaluate_aggregate_list (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_list_p,
			       val_descr *val_desc_p, cubxasl::aggregate_accumulator *alt_acc_list)
{
  cubxasl::aggregate_list_node *agg_p;
  cubxasl::aggregate_accumulator *accumulator;
  DB_VALUE *percentile_val = NULL;
  PR_TYPE *pr_type_p;
  DB_TYPE dbval_type;
  OR_BUF buf;
  char *disk_repr_p = NULL;
  int dbval_size, i, error;
  cubxasl::aggregate_percentile_info *percentile = NULL;
  DB_VALUE *db_value_p = NULL;

  for (agg_p = agg_list_p, i = 0; agg_p != NULL; agg_p = agg_p->next, i++)
    {
      std::vector<DB_VALUE> db_values;

      /* determine accumulator */
      accumulator = (alt_acc_list != NULL ? &alt_acc_list[i] : &agg_p->accumulator);

      if (agg_p->flag_agg_optimize)
	{
	  continue;
	}

      if (agg_p->function == PT_COUNT_STAR)
	{
	  /* increment and continue */
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

      /* fetch operands value. aggregate regulator variable should only contain constants */
      REGU_VARIABLE_LIST operand = NULL;
      for (operand = agg_p->operands; operand != NULL; operand = operand->next)
	{
	  // create an empty value
	  db_values.emplace_back ();

	  // fetch it
	  if (fetch_copy_dbval (thread_p, &operand->value, val_desc_p, NULL, NULL, NULL,
				&db_values.back ()) != NO_ERROR)
	    {
	      pr_clear_value_vector (db_values);
	      return ER_FAILED;
	    }
	}

      /*
       * eliminate null values
       * consider only the first argument, because for the rest will depend on the function
       */
      db_value_p = &db_values[0];
      if (DB_IS_NULL (db_value_p))
	{
	  /*
	   * for JSON_ARRAYAGG we need to include also NULL values in the result set
	   * so we need to construct a NULL JSON value
	   */
	  if (agg_p->function == PT_JSON_ARRAYAGG)
	    {
	      // this creates a new JSON_DOC with the type DB_JSON_NULL
	      db_make_json (db_value_p, db_json_allocate_doc (), true);
	    }
	  /*
	   * for JSON_OBJECTAGG we need to include keep track of key-value pairs
	   * the key can not be NULL so this will throw an error
	   * the value can be NULL and we will wrap this into a JSON with DB_JSON_NULL type in the next statement
	   */
	  else if (agg_p->function == PT_JSON_OBJECTAGG)
	    {
	      pr_clear_value_vector (db_values);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_OBJECT_NAME_IS_NULL, 0);
	      return ER_JSON_OBJECT_NAME_IS_NULL;
	    }
	  else
	    {
	      pr_clear_value_vector (db_values);
	      continue;
	    }
	}

      /*
       * for JSON_OBJECTAGG, we wrap the second argument with a null JSON only if the value is NULL
       */
      if (agg_p->function == PT_JSON_OBJECTAGG)
	{
	  if (DB_IS_NULL (&db_values[1]))
	    {
	      db_make_json (&db_values[1], db_json_allocate_doc (), true);
	    }
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
	      assert (!DB_IS_NULL (db_value_p));

	      error = qdata_update_agg_interpolation_func_value_and_domain (agg_p, db_value_p);
	      if (error != NO_ERROR)
		{
		  pr_clear_value_vector (db_values);
		  return ER_FAILED;
		}
	    }

	  dbval_type = DB_VALUE_DOMAIN_TYPE (db_value_p);
	  pr_type_p = pr_type_from_id (dbval_type);

	  if (pr_type_p == NULL)
	    {
	      pr_clear_value_vector (db_values);
	      return ER_FAILED;
	    }

	  dbval_size = pr_data_writeval_disk_size (db_value_p);
	  if (dbval_size > 0 && (disk_repr_p = (char *) db_private_alloc (thread_p, dbval_size)) != NULL)
	    {
	      or_init (&buf, disk_repr_p, dbval_size);
	      error = pr_type_p->data_writeval (&buf, db_value_p);
	      if (error != NO_ERROR)
		{
		  /* ER_TF_BUFFER_OVERFLOW means that val_size or packing is bad. */
		  assert (error != ER_TF_BUFFER_OVERFLOW);

		  db_private_free_and_init (thread_p, disk_repr_p);
		  pr_clear_value_vector (db_values);
		  return ER_FAILED;
		}
	    }
	  else
	    {
	      pr_clear_value_vector (db_values);
	      return ER_FAILED;
	    }

	  if (qfile_add_item_to_list (thread_p, disk_repr_p, dbval_size, agg_p->list_id) != NO_ERROR)
	    {
	      db_private_free_and_init (thread_p, disk_repr_p);
	      pr_clear_value_vector (db_values);
	      return ER_FAILED;
	    }

	  db_private_free_and_init (thread_p, disk_repr_p);
	  pr_clear_value_vector (db_values);

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
		      break;
		    default:
		      assert (agg_p->operands->value.type == TYPE_CONSTANT ||
			      agg_p->operands->value.type == TYPE_DBVAL);

		      /* try to cast dbval to double, datetime then time */
		      tmp_domain_p = tp_domain_resolve_default (DB_TYPE_DOUBLE);

		      status = tp_value_cast (db_value_p, db_value_p, tmp_domain_p, false);
		      if (status != DOMAIN_COMPATIBLE)
			{
			  /* try datetime */
			  tmp_domain_p = tp_domain_resolve_default (DB_TYPE_DATETIME);

			  status = tp_value_cast (db_value_p, db_value_p, tmp_domain_p, false);
			}

		      /* try time */
		      if (status != DOMAIN_COMPATIBLE)
			{
			  tmp_domain_p = tp_domain_resolve_default (DB_TYPE_TIME);

			  status = tp_value_cast (db_value_p, db_value_p, tmp_domain_p, false);
			}

		      if (status != DOMAIN_COMPATIBLE)
			{
			  error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
				  fcode_get_uppercase_name (agg_p->function), "DOUBLE, DATETIME, TIME");

			  pr_clear_value_vector (db_values);
			  return error;
			}

		      /* update domain */
		      agg_p->domain = tmp_domain_p;
		    }

		  pr_clear_value (agg_p->accumulator.value);
		  error = pr_clone_value (db_value_p, agg_p->accumulator.value);
		  if (error != NO_ERROR)
		    {
		      pr_clear_value_vector (db_values);
		      return error;
		    }
		}
	    }

	  /* clear value */
	  pr_clear_value_vector (db_values);

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
	      error = qdata_group_concat_first_value (thread_p, agg_p, db_value_p);
	    }
	  else
	    {
	      error = qdata_group_concat_value (thread_p, agg_p, db_value_p);
	    }

	  /* increment tuple count */
	  agg_p->accumulator.curr_cnt++;

	  /* clear value */
	  pr_clear_value_vector (db_values);

	  /* check error */
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      else
	{
	  /* aggregate value */
	  error = qdata_aggregate_multiple_values_to_accumulator (thread_p, accumulator, &agg_p->accumulator_domain,
		  agg_p->function, agg_p->domain, db_values);

	  /* increment tuple count */
	  accumulator->curr_cnt++;

	  /* clear values */
	  pr_clear_value_vector (db_values);

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
qdata_evaluate_aggregate_optimize (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_p, HFID *hfid_p,
				   OID *super_oid)
{
  long long oid_count = 0, null_count = 0, key_count = 0;
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
	  db_make_bigint (agg_p->accumulator.value, oid_count - null_count);
	}
      else
	{
	  db_make_bigint (agg_p->accumulator.value, key_count);
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
qdata_evaluate_aggregate_hierarchy (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_p, HFID *root_hfid,
				    BTID *root_btid, hierarchy_aggregate_helper *helper)
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
qdata_finalize_aggregate_list (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_list_p,
			       bool keep_list_file, sampling_info *sampling)
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
  int sampling_weight = 1;
  int adjust_sam_weight = 1;

  db_make_null (&sqr_val);
  db_make_null (&dbval);
  db_make_null (&xavgval);
  db_make_null (&xavg_1val);
  db_make_null (&x2avgval);
  db_make_null (&xavg2val);
  db_make_null (&varval);
  db_make_null (&dval);

  /* check sampling scan */
  if (sampling)
    {
      assert (sampling->weight > 0);
      sampling_weight = sampling->weight;
    }

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
	  db_make_bigint (agg_p->accumulator.value, agg_p->accumulator.curr_cnt * sampling_weight);
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
		  ASSERT_ERROR_AND_SET (error);
		  goto exit;
		}

	      if (list_id_p == NULL)
		{
		  if (!er_has_error ())
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		    }

		  ASSERT_ERROR_AND_SET (error);
		  goto exit;
		}

	      agg_p->list_id = list_id_p;

	      if (agg_p->function == PT_COUNT)
		{
		  adjust_sam_weight = stats_adjust_sampling_weight (list_id_p->tuple_cnt, sampling_weight);
		  db_make_bigint (agg_p->accumulator.value, list_id_p->tuple_cnt * adjust_sam_weight);
		}
	      else
		{
		  pr_type_p = list_id_p->type_list.domp[0]->type;

		  /* scan list file, accumulating total for sum/avg */
		  error = qfile_open_list_scan (list_id_p, &scan_id);
		  if (error != NO_ERROR)
		    {
		      ASSERT_ERROR ();
		      qfile_close_list (thread_p, list_id_p);
		      qfile_destroy_list (thread_p, list_id_p);
		      goto exit;
		    }

		  /* median and percentile funcs don't need to read all rows */
		  if (list_id_p->tuple_cnt > 0 && QPROC_IS_INTERPOLATION_FUNC (agg_p))
		    {
		      error = qdata_aggregate_interpolation (thread_p, agg_p, &scan_id);
		      if (error != NO_ERROR)
			{
			  ASSERT_ERROR ();
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
			      ASSERT_ERROR_AND_SET (error);
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
			  error = pr_type_p->data_readval (&buf, &dbval, list_id_p->type_list.domp[0], -1, true, NULL,
							   0);
			  if (error != NO_ERROR)
			    {
			      ASSERT_ERROR ();
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
				  ASSERT_ERROR_AND_SET (error);
				  (void) pr_clear_value (&dbval);
				  qfile_close_scan (thread_p, &scan_id);
				  qfile_close_list (thread_p, list_id_p);
				  qfile_destroy_list (thread_p, list_id_p);
				  goto exit;
				}
			    }

			  if (DB_IS_NULL (agg_p->accumulator.value))
			    {
			      /* first iteration: can't add to a null agg_ptr->value */
			      PR_TYPE *tmp_pr_type;
			      DB_TYPE dbval_type = DB_VALUE_DOMAIN_TYPE (&dbval);

			      tmp_pr_type = pr_type_from_id (dbval_type);
			      if (tmp_pr_type == NULL)
				{
				  ASSERT_ERROR_AND_SET (error);
				  (void) pr_clear_value (&dbval);
				  qfile_close_scan (thread_p, &scan_id);
				  qfile_close_list (thread_p, list_id_p);
				  qfile_destroy_list (thread_p, list_id_p);
				  goto exit;
				}

			      if (agg_p->function == PT_STDDEV || agg_p->function == PT_VARIANCE
				  || agg_p->function == PT_STDDEV_POP || agg_p->function == PT_VAR_POP
				  || agg_p->function == PT_STDDEV_SAMP || agg_p->function == PT_VAR_SAMP)
				{
				  error = qdata_multiply_dbval (&dbval, &dbval, &sqr_val, tmp_domain_ptr);
				  if (error != NO_ERROR)
				    {
				      ASSERT_ERROR ();
				      (void) pr_clear_value (&dbval);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }

				  if (tmp_pr_type->setval (agg_p->accumulator.value2, &sqr_val, true) != NO_ERROR)
				    {
				      assert (false);
				      error = ER_FAILED;
				      goto exit;
				    }
				}
			      if (agg_p->function == PT_GROUP_CONCAT)
				{
				  error = qdata_group_concat_first_value (thread_p, agg_p, &dbval);
				  if (error != NO_ERROR)
				    {
				      ASSERT_ERROR ();
				      (void) pr_clear_value (&dbval);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }
				}
			      else
				{
				  if (tmp_pr_type->setval (agg_p->accumulator.value, &dbval, true) != NO_ERROR)
				    {
				      assert (false);
				      error = ER_FAILED;
				      goto exit;
				    }
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
				      ASSERT_ERROR ();
				      (void) pr_clear_value (&dbval);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }

				  error = qdata_add_dbval (agg_p->accumulator.value2, &sqr_val,
							   agg_p->accumulator.value2, tmp_domain_ptr);
				  if (error != NO_ERROR)
				    {
				      ASSERT_ERROR ();
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
				      ASSERT_ERROR ();
				      (void) pr_clear_value (&dbval);
				      qfile_close_scan (thread_p, &scan_id);
				      qfile_close_list (thread_p, list_id_p);
				      qfile_destroy_list (thread_p, list_id_p);
				      goto exit;
				    }
				}
			      else
				{

				  TP_DOMAIN *domain_ptr =
					  tmp_domain_ptr != NULL ? tmp_domain_ptr : agg_p->accumulator_domain.value_dom;
				  /* accumulator domain should be used instead of agg_p->domain for SUM/AVG evaluation
				   * at the end cast the result to agg_p->domain */
				  if ((agg_p->function == PT_AVG)
				      && (dbval.domain.general_info.type == DB_TYPE_NUMERIC))
				    {
				      domain_ptr = NULL;
				    }

				  error = qdata_add_dbval (agg_p->accumulator.value, &dbval,
							   agg_p->accumulator.value, domain_ptr);
				  if (error != NO_ERROR)
				    {
				      ASSERT_ERROR ();
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
	      ASSERT_ERROR ();
	      goto exit;
	    }

	  if (agg_p->function == PT_AVG)
	    {
	      if (tp_value_coerce (&xavgval, agg_p->accumulator.value, double_domain_ptr) != DOMAIN_COMPATIBLE)
		{
		  ASSERT_ERROR_AND_SET (error);
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
	      ASSERT_ERROR ();
	      goto exit;
	    }

	  /* compute {SUM(X) / (n)} OR {SUM(X) / (n-1)} for xxx_SAMP agg */
	  error = qdata_divide_dbval (agg_p->accumulator.value, &dbval, &xavg_1val, double_domain_ptr);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }

	  /* compute AVG(X) * {SUM(X) / (n)} , AVG(X) * {SUM(X) / (n-1)} for xxx_SAMP agg */
	  error = qdata_multiply_dbval (&xavgval, &xavg_1val, &xavg2val, double_domain_ptr);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }

	  /* compute VAR(X) = SUM(X^2)/(n) - AVG(X) * {SUM(X) / (n)} OR VAR(X) = SUM(X^2)/(n-1) - AVG(X) * {SUM(X) /
	   * (n-1)} for xxx_SAMP aggregates */
	  error = qdata_subtract_dbval (&x2avgval, &xavg2val, &varval, double_domain_ptr);
	  if (error != NO_ERROR)
	    {
	      ASSERT_ERROR ();
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
		  ASSERT_ERROR_AND_SET (error);
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
	      ASSERT_ERROR ();
	      return ER_FAILED;
	    }
	}
    }

exit:
  if (error != NO_ERROR)
    {
      // make sure all list ids are cleared
      for (agg_p = agg_list_p; agg_p != NULL; agg_p = agg_p->next)
	{
	  qfile_close_list (thread_p, agg_p->list_id);
	  qfile_destroy_list (thread_p, agg_p->list_id);
	}
    }

  (void) pr_clear_value (&dbval);

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
qdata_calculate_aggregate_cume_dist_percent_rank (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_p,
    VAL_DESCR *val_desc_p)
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

  assert (agg_p != NULL && agg_p->sort_list != NULL && agg_p->operands->value.type == TYPE_REGU_VAR_LIST);

  regu_var_list = agg_p->operands->value.value.regu_var_list;
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
	  pr_type_p = pr_type_from_id (DB_VALUE_DOMAIN_TYPE (val_node));
	  cmp = pr_type_p->cmpval (val_node, info_p->const_array[i], 1, 0, NULL,
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
aggregate_hash_key *
qdata_alloc_agg_hkey (cubthread::entry *thread_p, int val_cnt, bool alloc_vals)
{
  aggregate_hash_key *key;
  int i;

  key = (aggregate_hash_key *) db_private_alloc (thread_p, sizeof (aggregate_hash_key));
  if (key == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (aggregate_hash_key));
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
qdata_free_agg_hkey (cubthread::entry *thread_p, aggregate_hash_key *key)
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
aggregate_hash_value *
qdata_alloc_agg_hvalue (cubthread::entry *thread_p, int func_cnt, cubxasl::aggregate_list_node *g_agg_list)
{
  aggregate_hash_value *value;
  int i;
  cubxasl::aggregate_list_node *agg_p;

  /* alloc structure */
  value = (aggregate_hash_value *) db_private_alloc (thread_p, sizeof (aggregate_hash_value));
  if (value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (aggregate_hash_value));
      return NULL;
    }

  if (func_cnt > 0)
    {
      value->accumulators =
	      (cubxasl::aggregate_accumulator *) db_private_alloc (thread_p,
		  sizeof (cubxasl::aggregate_accumulator) * func_cnt);
      if (value->accumulators == NULL)
	{
	  db_private_free (thread_p, value);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  sizeof (cubxasl::aggregate_accumulator) * func_cnt);
	  return NULL;
	}
    }
  else
    {
      value->accumulators = NULL;
    }

  value->func_count = func_cnt;
  /* alloc DB_VALUEs */
  for (i = 0; i < func_cnt; i++)
    {
      value->accumulators[i].value = pr_make_value ();
      value->accumulators[i].value2 = pr_make_value ();
    }
  /* initialize accumulators.value */
  for (i = 0, agg_p = g_agg_list; agg_p != NULL; agg_p = agg_p->next, i++)
    {
      /* CAUTION : if modify initializing ACC's value then should change qdata_initialize_aggregate_list() */
      if (agg_p->function == PT_GROUPBY_NUM)
	{
	  /* nothing to do with groupby_num() */
	  continue;
	}
      value->accumulators[i].curr_cnt = 0;

      /* This set is made, because if class is empty, aggregate results should return NULL, except count(*) and count */
      if (agg_p->function == PT_COUNT_STAR || agg_p->function == PT_COUNT)
	{
	  db_make_bigint (value->accumulators[i].value, 0);
	}
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
qdata_free_agg_hvalue (cubthread::entry *thread_p, aggregate_hash_value *value)
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
qdata_get_agg_hkey_size (aggregate_hash_key *key)
{
  int i, size = 0;

  for (i = 0; i < key->val_count; i++)
    {
      if (key->values[i] != NULL)
	{
	  size += pr_value_mem_size (key->values[i]);
	}
    }

  return size + sizeof (aggregate_hash_key);
}

/*
 * qdata_get_agg_hvalue_size () - get aggregate hash value size
 *   returns: size
 *   value(in): hash
 *   ret_delta(in): if false return actual size, if true return difference in
 *                  size between previously computed size and current size
 */
int
qdata_get_agg_hvalue_size (aggregate_hash_value *value, bool ret_delta)
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
	  size += sizeof (cubxasl::aggregate_accumulator);
	}
    }

  size += sizeof (aggregate_hash_value);
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
  aggregate_hash_key *hkey = (aggregate_hash_key *) key;
  aggregate_hash_value *hvalue = (aggregate_hash_value *) data;
  cubthread::entry *thread_p = (cubthread::entry *) args;

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
  aggregate_hash_key *ckey = (aggregate_hash_key *) key;
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
qdata_agg_hkey_compare (aggregate_hash_key *ckey1, aggregate_hash_key *ckey2, int *diff_pos)
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
  aggregate_hash_key *ckey1 = (aggregate_hash_key *) key1;
  aggregate_hash_key *ckey2 = (aggregate_hash_key *) key2;
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
aggregate_hash_key *
qdata_copy_agg_hkey (cubthread::entry *thread_p, aggregate_hash_key *key)
{
  aggregate_hash_key *new_key = NULL;
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
qdata_load_agg_hvalue_in_agg_list (aggregate_hash_value *value, cubxasl::aggregate_list_node *agg_list, bool copy_vals)
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
	      * (agg_list->accumulator.value) = * (value->accumulators[i].value);
	      * (agg_list->accumulator.value2) = * (value->accumulators[i].value2);

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
qdata_save_agg_hentry_to_list (cubthread::entry *thread_p, aggregate_hash_key *key, aggregate_hash_value *value,
			       DB_VALUE *temp_dbval_array, qfile_list_id *list_id)
{
  DB_VALUE tuple_count;
  int tuple_size = QFILE_TUPLE_LENGTH_SIZE;
  int col = 0, i;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  int error = NO_ERROR;

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

  list_id->tpl_descr.tpl_size = tuple_size;
  /* add to list file */
  if (tuple_size <= QFILE_MAX_TUPLE_SIZE_IN_PAGE)
    {
      qfile_generate_tuple_into_list (thread_p, list_id, T_NORMAL);
    }
  else
    {
      error = qfile_copy_tuple_descr_to_tuple (thread_p, &list_id->tpl_descr, &tplrec);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
      error = qfile_add_tuple_to_list (thread_p, list_id, tplrec.tpl);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

cleanup:
  if (tplrec.tpl != NULL)
    {
      db_private_free (thread_p, tplrec.tpl);
    }

  /* all ok */
  return error;
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
qdata_load_agg_hentry_from_tuple (cubthread::entry *thread_p, QFILE_TUPLE tuple, aggregate_hash_key *key,
				  aggregate_hash_value *value, tp_domain **key_dom,
				  cubxasl::aggregate_accumulator_domain **acc_dom)
{
  QFILE_TUPLE_VALUE_FLAG flag;
  DB_VALUE int_val;
  OR_BUF iterator, buf;
  int i, rc;

  /* initialize buffer */
  db_make_int (&int_val, 0);
  or_init (&iterator, tuple, QFILE_GET_TUPLE_LENGTH (tuple));
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
	  key_dom[i]->type->data_readval (&buf, key->values[i], key_dom[i], -1, true, NULL, 0);
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
	  acc_dom[i]->value_dom->type->data_readval (&buf, value->accumulators[i].value, acc_dom[i]->value_dom, -1,
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
	  acc_dom[i]->value2_dom->type->data_readval (&buf, value->accumulators[i].value2, acc_dom[i]->value2_dom, -1,
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
	  tp_Integer_domain.type->data_readval (&buf, &int_val, &tp_Integer_domain, -1, true, NULL, 0);
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
      tp_Integer_domain.type->data_readval (&buf, &int_val, &tp_Integer_domain, -1, true, NULL, 0);
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
qdata_load_agg_hentry_from_list (cubthread::entry *thread_p, qfile_list_scan_id *list_scan_id, aggregate_hash_key *key,
				 aggregate_hash_value *value, tp_domain **key_dom,
				 cubxasl::aggregate_accumulator_domain **acc_dom)
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
qdata_save_agg_htable_to_list (cubthread::entry *thread_p, mht_table *hash_table, qfile_list_id *tuple_list_id,
			       qfile_list_id *partial_list_id, db_value *temp_dbval_array)
{
  aggregate_hash_key *key = NULL;
  aggregate_hash_value *value = NULL;
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
      key = (aggregate_hash_key *) head->key;
      value = (aggregate_hash_value *) head->data;

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
qdata_update_agg_interpolation_func_value_and_domain (cubxasl::aggregate_list_node *agg_p, DB_VALUE *dbval)
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

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2, fcode_get_uppercase_name (agg_p->function),
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
 * qdata_group_concat_first_value() - concatenates the first value
 *   return: NO_ERROR, or ER_code
 *   thread_p(in) :
 *   agg_p(in)	  : GROUP_CONCAT aggregate
 *   dbvalue(in)  : current value
 */
int
qdata_group_concat_first_value (THREAD_ENTRY *thread_p, AGGREGATE_TYPE *agg_p, DB_VALUE *dbvalue)
{
  TP_DOMAIN *result_domain;
  DB_TYPE agg_type;
  int max_allowed_size;
  DB_VALUE tmp_val;
  int error_code = NO_ERROR;

  db_make_null (&tmp_val);

  agg_type = DB_VALUE_DOMAIN_TYPE (agg_p->accumulator.value);
  /* init the aggregate value domain */
  error_code = db_value_domain_init (agg_p->accumulator.value, agg_type, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pr_clear_value (dbvalue);
      return error_code;
    }

  error_code = db_string_make_empty_typed_string (agg_p->accumulator.value, agg_type, DB_DEFAULT_PRECISION,
	       TP_DOMAIN_CODESET (agg_p->domain),
	       TP_DOMAIN_COLLATION (agg_p->domain));
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == true)
    {
      agg_p->accumulator.value->domain.general_info.is_null = 0;
    }

  /* concat the first value */
  result_domain = ((TP_DOMAIN_TYPE (agg_p->domain) == agg_type) ? agg_p->domain : NULL);

  max_allowed_size = (int) prm_get_bigint_value (PRM_ID_GROUP_CONCAT_MAX_LEN);

  error_code = qdata_concatenate_dbval (thread_p, agg_p->accumulator.value, dbvalue, &tmp_val, result_domain,
					max_allowed_size, "GROUP_CONCAT()");
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pr_clear_value (dbvalue);
      return error_code;
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
qdata_group_concat_value (THREAD_ENTRY *thread_p, AGGREGATE_TYPE *agg_p, DB_VALUE *dbvalue)
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

static int
qdata_aggregate_interpolation (cubthread::entry *thread_p, cubxasl::aggregate_list_node *agg_p,
			       QFILE_LIST_SCAN_ID *scan_id)
{
  int error = NO_ERROR;
  INT64 tuple_count;
  double row_num_d, f_row_num_d, c_row_num_d, percentile_d;
  FUNC_CODE function;
  double cur_group_percentile;

  assert (agg_p != NULL && scan_id != NULL && scan_id->status == S_OPENED);
  assert (QPROC_IS_INTERPOLATION_FUNC (agg_p));

  function = agg_p->function;
  cur_group_percentile = agg_p->info.percentile.cur_group_percentile;

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
	      f_row_num_d, c_row_num_d, agg_p->accumulator.value, &agg_p->domain,
	      agg_p->function);

  if (error == NO_ERROR)
    {
      agg_p->opr_dbtype = TP_DOMAIN_TYPE (agg_p->domain);
    }

  return error;
}
