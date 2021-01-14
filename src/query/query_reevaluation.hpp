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
// Predicate re-evaluation - part of MVCC read committed re-evaluation of modified object
//

#ifndef _QUERY_REEVALUATION_HPP_
#define _QUERY_REEVALUATION_HPP_

#include "dbtype_def.h"                 // OID, DB_LOGICAL

#include "query_evaluator.h"            // FILTER_INFO, QPROC_QUALIFICATION, scan_attrs

// forward definitions
struct db_value;
struct lc_copy_area;
struct heap_cache_attrinfo;
struct recdes;
struct scan_id_struct;
struct val_descr;

namespace cubxasl
{
  struct pred_expr;
}

/* type of reevaluation */
enum mvcc_reev_data_type
{
  REEV_DATA_UPDDEL = 0,
  REEV_DATA_SCAN
};
typedef enum mvcc_reev_data_type MVCC_REEV_DATA_TYPE;

namespace cubquery
{
  /* describes an assignment used in MVCC reevaluation */
  struct update_mvcc_reev_assignment
  {
    int att_id;			/* index in the class attributes array */
    db_value *constant;		/* constant to be assigned to an attribute or NULL */
    regu_variable_node *regu_right;	/* regu variable for right side of an assignment */
    update_mvcc_reev_assignment *next;	/* link to the next assignment */
  };

  /* class info for UPDATE/DELETE MVCC condition reevaluation */
  struct upddel_mvcc_cond_reeval
  {
    int class_index;		/* index of class in select list */
    OID cls_oid;			/* OID of class */
    OID *inst_oid;		/* OID of instance involved in condition */
    filter_info data_filter;	/* data filter */
    filter_info key_filter;	/* key_filter */
    filter_info range_filter;	/* range filter */
    QPROC_QUALIFICATION qualification;	/* see QPROC_QUALIFICATION; used for both input and output parameter */
    regu_variable_list_node *rest_regu_list;	/* regulator variable list */
    scan_attrs *rest_attrs;	/* attribute info for attribute that is not involved in current filter */
    upddel_mvcc_cond_reeval *next;	/* next upddel_mvcc_cond_reeval structure that will be processed on
					   * reevaluation */

    void init (scan_id_struct &sid);
  };

  /* data for MVCC condition reevaluation */
  struct mvcc_update_reev_data
  {
    upddel_mvcc_cond_reeval *mvcc_cond_reev_list;	/* list of classes that are referenced in condition */

    /* information for class that is currently updated/deleted */
    upddel_mvcc_cond_reeval *curr_upddel;	/* pointer to the reevaluation data for class that is currently updated/
					   * deleted or NULL if it is not involved in reevaluation */
    int curr_extra_assign_cnt;	/* length of curr_extra_assign_reev array */
    upddel_mvcc_cond_reeval **curr_extra_assign_reev;	/* classes involved in the right side of assignments and are
							   * not part of conditions to be reevaluated */
    update_mvcc_reev_assignment *curr_assigns;	/* list of assignments to the attributes of this class */
    heap_cache_attrinfo *curr_attrinfo;	/* attribute info for performing assignments */

    cubxasl::pred_expr *cons_pred;
    lc_copy_area *copyarea;	/* used to build the tuple to be stored to disk after reevaluation */
    val_descr *vd;		/* values descriptor */
    recdes *new_recdes;		/* record descriptor after assignment reevaluation */
  };

  /* Structure used in condition reevaluation at SELECT */
  struct mvcc_scan_reev_data
  {
    filter_info *range_filter;	/* filter for range predicate. Used only at index scan */
    filter_info *key_filter;	/* key filter */
    filter_info *data_filter;	/* data filter */

    QPROC_QUALIFICATION *qualification;	/* address of a variable that contains qualification value */

    void set_filters (upddel_mvcc_cond_reeval &ureev);
  };

  /* Used in condition reevaluation for UPDATE/DELETE */
  struct mvcc_reev_data
  {
    MVCC_REEV_DATA_TYPE type;
    union
    {
      mvcc_update_reev_data *upddel_reev_data;	/* data for reevaluation at UPDATE/DELETE */
      mvcc_scan_reev_data *select_reev_data;	/* data for reevaluation at SELECT */
    };
    DB_LOGICAL filter_result;	/* the result of reevaluation if successful */

    void set_update_reevaluation (mvcc_update_reev_data &urd);
    void set_scan_reevaluation (mvcc_scan_reev_data &scan);
  };
} // namespace cubquery

using UPDATE_MVCC_REEV_ASSIGNMENT = cubquery::update_mvcc_reev_assignment;
using UPDDEL_MVCC_COND_REEVAL = cubquery::upddel_mvcc_cond_reeval;
using MVCC_UPDDEL_REEV_DATA = cubquery::mvcc_update_reev_data;
using MVCC_SCAN_REEV_DATA = cubquery::mvcc_scan_reev_data;
using MVCC_REEV_DATA = cubquery::mvcc_reev_data;

#endif // _QUERY_REEVALUATION_HPP_
