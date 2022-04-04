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
#include "storage_common.h"

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
  };

#if defined (SERVER_MODE) || defined (SA_MODE)

  struct aggregate_accumulator_domain
  {
    tp_domain *value_dom;		/* domain of value */
    tp_domain *value2_dom;	/* domain of value2 */
  };
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

  struct aggregate_list_node
  {
    aggregate_list_node *next;		/* next aggregate node */
    tp_domain *domain;		/* domain of the result */
    tp_domain *original_domain;	/* original domain of the result */
    FUNC_TYPE function;		/* aggregate function name */
    QUERY_OPTIONS option;		/* DISTINCT/ALL option */
    DB_TYPE opr_dbtype;		/* Operand values data type */
    DB_TYPE original_opr_dbtype;	/* Original operand values data type */
    regu_variable_list_node *operands;	/* list of operands (one operand per function argument) */
    qfile_list_id *list_id;	/* used for distinct handling */
    int flag_agg_optimize;
    BTID btid;
    SORT_LIST *sort_list;		/* for sorting elements before aggregation; used by GROUP_CONCAT */
    aggregate_specific_function_info info;	/* variables for specific functions */
    aggregate_accumulator accumulator;	/* holds runtime values, only for evaluation */
#if defined (SERVER_MODE) || defined (SA_MODE)
    aggregate_accumulator_domain accumulator_domain;	/* holds domain info on accumulator */
#endif				/* defined (SERVER_MODE) || defined (SA_MODE) */
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
