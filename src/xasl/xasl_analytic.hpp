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

//
// xasl_analytic - defines XASL structures for analytics
//

#ifndef _XASL_ANALYTIC_HPP_
#define _XASL_ANALYTIC_HPP_

#include "dbtype_def.h"
#include "regu_var.h"             // regu_variable_node
#include "storage_common.h"       // FUNC_TYPE, QUERY_OPTIONS

// forward definitions
struct qfile_list_id;
struct sort_list;
typedef struct sort_list SORT_LIST;   // todo - rename sort_list member.
struct tp_domain;

namespace cubxasl
{
  struct analytic_ntile_function_info
  {
    bool is_null;			/* is function result NULL? */
    int bucket_count;		/* number of required buckets */
  };

  struct analytic_cume_percent_function_info
  {
    int last_pos;			/* record the current position of the rows that are no larger than the current row */
    double last_res;		/* record the last result */
  };

  struct analytic_percentile_function_info
  {
    double cur_group_percentile;	/* current percentile value */
    regu_variable_node *percentile_reguvar;	/* percentile value of the new tuple if this is not the same as
                                                 * cur_gourp_percentile, an error is raised. */
  };

  union analytic_function_info
  {
    analytic_ntile_function_info ntile;
    analytic_percentile_function_info percentile;
    analytic_cume_percent_function_info cume_percent;
  };

  struct analytic_list_node
  {
    analytic_list_node *next;		/* next analytic node */

    /* constant fields, XASL serialized */
    FUNC_TYPE function;		/* analytic function type */
    QUERY_OPTIONS option;		/* DISTINCT/ALL option */
    tp_domain *domain;		/* domain of the result */
    tp_domain *original_domain;	/* domain of the result */

    DB_TYPE opr_dbtype;		/* operand data type */
    DB_TYPE original_opr_dbtype;	/* original operand data type */
    regu_variable_node operand;	/* operand */

    int flag;			/* flags */
    int sort_prefix_size;		/* number of PARTITION BY cols in sort list */
    int sort_list_size;		/* the total size of the sort list */
    int offset_idx;		/* index of offset value in select list (for LEAD/LAG/NTH_value functions) */
    int default_idx;		/* index of default value in select list (for LEAD/LAG functions) */
    bool from_last;		/* begin at the last or first row */
    bool ignore_nulls;		/* ignore or respect NULL values */
    bool is_const_operand;	/* is the operand a constant or a host var for MEDIAN function */

    /* runtime values */
    analytic_function_info info;	/* custom function runtime values */
    qfile_list_id *list_id;	/* used for distinct handling */
    db_value *value;		/* value of the aggregate */
    db_value *value2;		/* for STTDEV and VARIANCE */
    db_value *out_value;		/* DB_VALUE used for output */
    db_value part_value;		/* partition temporary accumulator */
    int curr_cnt;			/* current number of items */
    bool is_first_exec_time;	/* the fist time to be executed */

    void init ();
  };

  struct analytic_eval_type
  {
    analytic_eval_type *next;	/* next eval group */
    analytic_list_node *head;		/* analytic type list */
    SORT_LIST *sort_list;		/* partition sort */

    analytic_eval_type () = default;
  };
} // namespace cubxasl

using ANALYTIC_NTILE_FUNCTION_INFO = cubxasl::analytic_ntile_function_info;
using ANALYTIC_CUME_PERCENT_FUNCTION_INFO = cubxasl::analytic_cume_percent_function_info;
using ANALYTIC_PERCENTILE_FUNCTION_INFO = cubxasl::analytic_percentile_function_info;
using ANALYTIC_FUNCTION_INFO = cubxasl::analytic_function_info;
using ANALYTIC_TYPE = cubxasl::analytic_list_node;
using ANALYTIC_EVAL_TYPE = cubxasl::analytic_eval_type;

#endif // _XASL_ANALYTIC_HPP_
