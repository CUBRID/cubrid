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
// xasl_aggregate.hpp - XASL structures used for aggregation
//

#ifndef _XASL_AGGREGATE_HPP_
#define _XASL_AGGREGATE_HPP_


#include "dbtype_def.h"
#include "query_sum_accumulator.h"
#include "storage_common.h"
#if defined (SERVER_MODE) || defined (SA_MODE)
#include "domain_rules.h"
#endif

// forward definitions
struct qfile_list_id;
struct regu_variable_list_node;
class regu_variable_node;
struct sort_list;
typedef struct sort_list SORT_LIST;   // todo - rename sort_list member.
struct tp_domain;

namespace cubxasl
{
  struct aggregate_percentile_info
  {
    double cur_group_percentile;	/* current percentile value */
    regu_variable_node *percentile_reguvar;
  };

#if defined (SERVER_MODE) || defined (SA_MODE)

  struct aggregate_dist_percent_info
  {
    DB_VALUE **const_array;
    int list_len;
    int nlargers;
  };
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

  union aggregate_specific_function_info
  {
    aggregate_percentile_info percentile;	/* PERCENTILE_CONT and PERCENTILE_DISC */
#if defined (SERVER_MODE) || defined (SA_MODE)
    aggregate_dist_percent_info dist_percent;	/* CUME_DIST and PERCENT_RANK */
#endif				/* defined (SERVER_MODE) || defined (SA_MODE) */
  };

  struct aggregate_accumulator
  {
    db_value *value;		/* value of the aggregate */
    db_value *value2;		/* for GROUP_CONCAT, STTDEV and VARIANCE */
    INT64 curr_cnt;			/* current number of items */

    bool clear_value_at_clone_decache;	/* true, if need to clear value at clone decache */
    bool clear_value2_at_clone_decache;	/* true, if need to clear value2 at clone decache */
    SUM_ACC sum_acc;	        /* word accumulator for NUMERIC SUM/AVG */
    int shared_from;		/* 1 + index of the accumulator owner; 0 = no sharing (zero-init safe) */
  };

#if defined (SERVER_MODE) || defined (SA_MODE)

  /* An aggregate's or an analytic function's accumulator domains, which the compiler plans (D-83:
   * domain_plan_aggregate_accumulator, domain_plan_analytic_accumulator) and unpack looks the converters of up; the
   * partitions and the rows read them. */
  struct aggregate_accumulator_domain
  {
    tp_domain *value_dom;		/* domain of value; an analytic SUM / AVG: the sum's domain, which the first
				 * value is converted into and each addition's result is coerced to, NULL for a sum
				 * kept floating (an AVG over a NUMERIC argument, a SUM whose domain is NUMERIC) */
    tp_domain *value2_dom;	/* domain of value2 */
    DOMAIN_OPERAND_COERCION operand_coercion;	/* SUM / AVG: the operand coercion of value + a value, set with
						 * value_dom; STDDEV / VARIANCE, NTILE: [1] the converter of a value
						 * into DOUBLE; MEDIAN / PERCENTILE: [1] into the function's domain;
						 * GROUP_CONCAT: [1] a value's cast into the function's domain;
						 * LEAD / LAG / NTH_VALUE: [0] the offset's into INTEGER (NTH_VALUE:
						 * DOUBLE), [1] the default's into the function's domain */
    bool accumulator_takes;	/* SUM / AVG: the resident accumulator takes the argument's type
				 * (SUM_ACC_IS_AGG_SUPPORTED_TYPE, an analytic's SUM_ACC_IS_ANALYTIC_SUPPORTED_TYPE) -
				 * its typed sum is the sum; else the values are added into value_dom (and a DISTINCT
				 * list's values always) */
    TP_VALUE_CONVERTER first_conv;	/* the converter of a value copied into the accumulator into value_dom: MIN /
					 * MAX's every new extreme, SUM / AVG's first value (tp_value_cast_force);
					 * NULL when it is of that type already */
    TP_VALUE_CONVERTER final_conv;	/* an aggregate SUM: the converter of the sum in value_dom into the function's
					 * domain at the group's end (tp_value_cast_force) */
    DOMAIN_OPERAND_COERCION distinct_coercion;	/* an aggregate SUM / AVG: the operand coercion of a DISTINCT
						 * list's second value added to its first, which is held in the
						 * argument's type until then (two FLOATs add as FLOAT); a later
						 * value adds with operand_coercion */
    DOMAIN_OPERAND_COERCION divide_coercion;	/* an AVG: the operand coercion of sum / count at the group's or
						 * partition's end, over the sum's type - an aggregate's value_dom,
						 * an analytic's accumulator's for a type it takes, value_dom's or
						 * the argument's otherwise */
  };
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

  struct aggregate_list_node
  {
    aggregate_list_node *next;		/* next aggregate node */
    tp_domain *domain;		/* domain of the result */
    FUNC_CODE function;		/* aggregate function name */
    QUERY_OPTIONS option;		/* DISTINCT/ALL option */
    DB_TYPE opr_dbtype;		/* Operand values data type */
    regu_variable_list_node *operands;	/* list of operands (one operand per function argument) */
    qfile_list_id *list_id;	/* used for distinct handling */
    BTID btid;
    SORT_LIST *sort_list;		/* for sorting elements before aggregation; used by GROUP_CONCAT */
    aggregate_specific_function_info info;	/* variables for specific functions */
    aggregate_accumulator accumulator;	/* holds runtime values, only for evaluation */
#if defined (SERVER_MODE) || defined (SA_MODE)
    aggregate_accumulator_domain accumulator_domain;	/* CBRD-27583 (4.3, D-83): the compiler's plan from the
							 * node's domain and its operand's
							 * (domain_plan_aggregate_accumulator), its converters looked
							 * up at unpack */
    /* CBRD-27583 (D-94): a MIN / MAX's comparison of two accumulator values (value_dom with itself), by which a
     * partitioned class's extremes merge (qdata_evaluate_aggregate_hierarchy), decided at unpack; NULL for another
     * function and where value_dom leaves the comparison to the values */
    const DOMAIN_COMPARE *merge_compare;
    /* compiled operand-evaluation program covering the WHOLE aggregate list; kept on the
     * list HEAD node only.  Server-side runtime state -- never serialized.  See
     * expr_compile.h; built lazily on the first evaluated row when the bound host
     * variable types are known.
     * Concurrency contract: these fields (and the program they point to) are written with
     * plain, non-atomic stores.  That is safe only because an XASL clone is checked out to
     * exactly one executing thread at a time (the xcache clone mutex publishes the stores
     * when the clone changes hands).  Nothing here tolerates two threads sharing one clone
     * -- do not add such a caller without making this state per-thread or synchronized. */
    void *operand_prog;		/* EXPR_PROG *, head node only */
    int *operand_prog_idx;	/* program root index per operand ordinal or -1; head only */
    int operand_prog_state;	/* 0 = untried, 1 = active, 2 = declined, 3 = nothing to compute (the
				 * fast path peeks the plain operands in place); head only */
    int operand_prog_base;	/* THIS node's first ordinal in operand_prog_idx, or -1 */
    void *operand_prog_share_spec;	/* head only: the node's only heap scan (ACCESS_SPEC_TYPE *) whose
					 * compiled data filter may serve operand values, or NULL
					 * (qexec_set_expr_share_spec ()); runtime only */
    unsigned long long operand_prog_link_stamp;	/* head only: the execution (query id) whose first
						 * evaluation derived the cell-based accumulator links
						 * (qdata_link_shared_accumulators_by_cell ()) */
#endif				/* defined (SERVER_MODE) || defined (SA_MODE) */
    struct
    {
      bool agg_optimized;	/* true, if the aggregate is optimized */
      bool min_max_optimized;	/* true, if the aggregate is min/max optimized */
      bool part_key_descending;	/* true, if the aggregate is part key descending */
      bool dummy; /* for alignment, free space for future use */
    } flag;
    int is_ended;		/* true, if the aggregate is ended */
  };
};

// legacy aliases
using AGGREGATE_SPECIFIC_FUNCTION_INFO = cubxasl::aggregate_specific_function_info;
using AGGREGATE_PERCENTILE_INFO = cubxasl::aggregate_percentile_info;
using AGGREGATE_ACCUMULATOR = cubxasl::aggregate_accumulator;
using AGGREGATE_TYPE = cubxasl::aggregate_list_node;

#if defined (SERVER_MODE) || defined (SA_MODE)
using AGGREGATE_DIST_PERCENT_INFO = cubxasl::aggregate_dist_percent_info;
using AGGREGATE_ACCUMULATOR_DOMAIN = cubxasl::aggregate_accumulator_domain;
#endif // server or SA mode

#endif // _XASL_AGGREGATE_HPP_
