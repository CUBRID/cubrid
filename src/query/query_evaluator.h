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
 * Predicate evaluation
 */

#ifndef _QUERY_EVALUATOR_H_
#define _QUERY_EVALUATOR_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "heap_file.h"
#if defined(WINDOWS)
#include "porting.h"
#endif /* ! WINDOWS */
#include "thread_compat.hpp"

#include <assert.h>
#if !defined (WINDOWS)
#include <stdlib.h>
#endif // not WINDOWS

// forward definitions
struct pred_expr;
typedef struct pred_expr PRED_EXPR;
struct regu_variable_list_node;
struct regu_variable_node;
struct val_descr;
typedef struct val_descr VAL_DESCR;
struct val_list_node;

typedef DB_LOGICAL (*PR_EVAL_FNC) (THREAD_ENTRY * thread_p, pred_expr *, val_descr *, OID *);

typedef enum
{
  QPROC_QUALIFIED = 0,		/* fetch a qualified item; default */
  QPROC_NOT_QUALIFIED,		/* fetch a not-qualified item */
  QPROC_QUALIFIED_OR_NOT	/* fetch either a qualified or not-qualified item */
} QPROC_QUALIFICATION;

#define QPROC_ANALYTIC_IS_OFFSET_FUNCTION(func_p) \
    (((func_p) != NULL) \
    && (((func_p)->function == PT_LEAD) \
        || ((func_p)->function == PT_LAG) \
        || ((func_p)->function == PT_NTH_VALUE)))

#define ANALYTIC_ADVANCE_RANK 1	/* advance rank */
#define ANALYTIC_KEEP_RANK    2	/* keep current rank */

#define ANALYTIC_FUNC_IS_FLAGED(x, f)        ((x)->flag & (int) (f))
#define ANALYTIC_FUNC_SET_FLAG(x, f)         (x)->flag |= (int) (f)
#define ANALYTIC_FUNC_CLEAR_FLAG(x, f)       (x)->flag &= (int) ~(f)

/*
 * typedefs related to the predicate expression structure
 */

#ifdef V_FALSE
#undef V_FALSE
#endif
#ifdef V_TRUE
#undef V_TRUE
#endif

/* predicates information of scan */
typedef struct scan_pred SCAN_PRED;
struct scan_pred
{
  regu_variable_list_node *regu_list;	/* regu list for predicates (or filters) */
  PRED_EXPR *pred_expr;		/* predicate expressions */
  PR_EVAL_FNC pr_eval_fnc;	/* predicate evaluation function */
};

/* attributes information of scan */
typedef struct scan_attrs SCAN_ATTRS;
struct scan_attrs
{
  ATTR_ID *attr_ids;		/* array of attributes id */
  heap_cache_attrinfo *attr_cache;	/* attributes access cache */
  int num_attrs;		/* number of attributes */
};

/* informations that are need for applying filter (predicate) */
typedef struct filter_info FILTER_INFO;
struct filter_info
{
  /* filter information */
  SCAN_PRED *scan_pred;		/* predicates of the filter */
  SCAN_ATTRS *scan_attrs;	/* attributes scanning info */
  val_list_node *val_list;	/* value list */
  VAL_DESCR *val_descr;		/* value descriptor */

  /* class information */
  OID *class_oid;		/* class OID */

  /* index information */
  ATTR_ID *btree_attr_ids;	/* attribute id array of the index key */
  int *num_vstr_ptr;		/* number pointer of variable string attrs */
  ATTR_ID *vstr_ids;		/* attribute id array of variable string */
  int btree_num_attrs;		/* number of attributes of the index key */
  int func_idx_col_id;		/* function expression column position, if this is a function index */
};

/************************************************************************/
/* Re-evaluation                                                        */
/************************************************************************/

/* describes an assignment used in MVCC reevaluation */
typedef struct update_mvcc_reev_assignment UPDATE_MVCC_REEV_ASSIGNMENT;
struct update_mvcc_reev_assignment
{
  int att_id;			/* index in the class attributes array */
  DB_VALUE *constant;		/* constant to be assigned to an attribute or NULL */
  regu_variable_node *regu_right;	/* regu variable for right side of an assignment */
  UPDATE_MVCC_REEV_ASSIGNMENT *next;	/* link to the next assignment */
};

/* class info for UPDATE/DELETE MVCC condition reevaluation */
typedef struct upddel_mvcc_cond_reeval UPDDEL_MVCC_COND_REEVAL;
struct upddel_mvcc_cond_reeval
{
  int class_index;		/* index of class in select list */
  OID cls_oid;			/* OID of class */
  OID *inst_oid;		/* OID of instance involved in condition */
  FILTER_INFO data_filter;	/* data filter */
  FILTER_INFO key_filter;	/* key_filter */
  FILTER_INFO range_filter;	/* range filter */
  QPROC_QUALIFICATION qualification;	/* see QPROC_QUALIFICATION; used for both input and output parameter */
  regu_variable_list_node *rest_regu_list;	/* regulator variable list */
  SCAN_ATTRS *rest_attrs;	/* attribute info for attribute that is not involved in current filter */
  UPDDEL_MVCC_COND_REEVAL *next;	/* next upddel_mvcc_cond_reeval structure that will be processed on
					 * reevaluation */
};

/* type of reevaluation */
enum mvcc_reev_data_type
{
  REEV_DATA_UPDDEL = 0,
  REEV_DATA_SCAN
};
typedef enum mvcc_reev_data_type MVCC_REEV_DATA_TYPE;

/* data for MVCC condition reevaluation */
typedef struct mvcc_update_reev_data MVCC_UPDDEL_REEV_DATA;
struct mvcc_update_reev_data
{
  UPDDEL_MVCC_COND_REEVAL *mvcc_cond_reev_list;	/* list of classes that are referenced in condition */

  /* information for class that is currently updated/deleted */
  UPDDEL_MVCC_COND_REEVAL *curr_upddel;	/* pointer to the reevaluation data for class that is currently updated/
					 * deleted or NULL if it is not involved in reevaluation */
  int curr_extra_assign_cnt;	/* length of curr_extra_assign_reev array */
  UPDDEL_MVCC_COND_REEVAL **curr_extra_assign_reev;	/* classes involved in the right side of assignments and are
							 * not part of conditions to be reevaluated */
  UPDATE_MVCC_REEV_ASSIGNMENT *curr_assigns;	/* list of assignments to the attributes of this class */
  HEAP_CACHE_ATTRINFO *curr_attrinfo;	/* attribute info for performing assignments */

  pred_expr *cons_pred;
  LC_COPYAREA *copyarea;	/* used to build the tuple to be stored to disk after reevaluation */
  val_descr *vd;		/* values descriptor */
  RECDES *new_recdes;		/* record descriptor after assignment reevaluation */
};

/* Structure used in condition reevaluation at SELECT */
typedef struct mvcc_scan_reev_data MVCC_SCAN_REEV_DATA;
struct mvcc_scan_reev_data
{
  FILTER_INFO *range_filter;	/* filter for range predicate. Used only at index scan */
  FILTER_INFO *key_filter;	/* key filter */
  FILTER_INFO *data_filter;	/* data filter */

  QPROC_QUALIFICATION *qualification;	/* address of a variable that contains qualification value */
};

/* Used in condition reevaluation for UPDATE/DELETE */
typedef struct mvcc_reev_data MVCC_REEV_DATA;
struct mvcc_reev_data
{
  MVCC_REEV_DATA_TYPE type;
  union
  {
    MVCC_UPDDEL_REEV_DATA *upddel_reev_data;	/* data for reevaluation at UPDATE/DELETE */
    MVCC_SCAN_REEV_DATA *select_reev_data;	/* data for reevaluation at SELECT */
  };
  DB_LOGICAL filter_result;	/* the result of reevaluation if successful */
};

#define INIT_FILTER_INFO_FOR_SCAN_REEV(p_scan_id, p_range_filter, p_key_filter, p_data_filter) \
  do \
    { \
      assert ((p_scan_id) != NULL); \
      if ((p_scan_id)->type == S_INDX_SCAN) \
	{ \
	  INDX_SCAN_ID * p_idx_scan_id = &(p_scan_id)->s.isid; \
	  if ((FILTER_INFO *) (p_range_filter) != NULL)  \
	    { \
	      scan_init_filter_info ((p_range_filter), \
				     &(p_idx_scan_id)->range_pred, \
				     &(p_idx_scan_id)->range_attrs, \
				     (p_scan_id)->val_list, (p_scan_id)->vd, \
				     &(p_idx_scan_id)->cls_oid, 0, NULL, \
				     &(p_idx_scan_id)->num_vstr, \
				     (p_idx_scan_id)->vstr_ids); \
	    } \
	  if ((FILTER_INFO *) (p_key_filter) != NULL) \
	    { \
	      scan_init_filter_info ((p_key_filter), &(p_idx_scan_id)->key_pred, \
				     &(p_idx_scan_id)->key_attrs, \
				     (p_scan_id)->val_list, (p_scan_id)->vd, \
				     &(p_idx_scan_id)->cls_oid, \
				     (p_idx_scan_id)->bt_num_attrs, \
				     (p_idx_scan_id)->bt_attr_ids, \
				     &(p_idx_scan_id)->num_vstr, \
				     (p_idx_scan_id)->vstr_ids); \
	    } \
	  if ((FILTER_INFO *) (p_data_filter) != NULL) \
	    { \
	      scan_init_filter_info ((p_data_filter), \
				     &(p_idx_scan_id)->scan_pred, \
				     &(p_idx_scan_id)->pred_attrs, \
				     (p_scan_id)->val_list, \
				     (p_scan_id)->vd, &(p_idx_scan_id)->cls_oid, \
				      0, NULL, NULL, NULL); \
	    } \
	} \
      else if ((p_scan_id)->type == S_HEAP_SCAN) \
	{ \
	  HEAP_SCAN_ID * p_heap_scan_id = &(p_scan_id)->s.hsid; \
	  if ((FILTER_INFO *) (p_data_filter) != NULL) \
	    { \
	      scan_init_filter_info ((p_data_filter), \
				     &(p_heap_scan_id)->scan_pred, \
				     &(p_heap_scan_id)->pred_attrs, \
				     (p_scan_id)->val_list, \
				     (p_scan_id)->vd, \
				     &(p_heap_scan_id)->cls_oid, \
				     0, NULL, NULL, NULL); \
	    } \
	  if ((FILTER_INFO *) (p_range_filter) != NULL) \
	    { \
	      memset (p_range_filter, 0, sizeof (FILTER_INFO)); \
	    } \
	  if ((FILTER_INFO *) (p_key_filter) != NULL) \
	    { \
	      memset (p_key_filter, 0, sizeof (FILTER_INFO)); \
	    } \
	} \
    } \
  while (0)

#define INIT_SCAN_REEV_DATA(p_mvcc_sel_reev_data, p_range_filter, p_key_filter, p_data_filter, p_qualification) \
  do \
    { \
      assert ((p_mvcc_sel_reev_data) != NULL); \
      if (((FILTER_INFO *) (p_range_filter) != NULL) && ((p_range_filter)->scan_pred != NULL) \
          && ((p_range_filter)->scan_pred->regu_list != NULL)) \
	{ \
	  (p_mvcc_sel_reev_data)->range_filter = (p_range_filter); \
	} \
      else \
	{ \
	  (p_mvcc_sel_reev_data)->range_filter = NULL; \
	} \
      if (((FILTER_INFO *) (p_key_filter) != NULL) && ((p_key_filter)->scan_pred != NULL) \
          && ((p_key_filter)->scan_pred->regu_list != NULL)) \
	{ \
	  (p_mvcc_sel_reev_data)->key_filter = (p_key_filter); \
	} \
      else \
	{ \
	  (p_mvcc_sel_reev_data)->key_filter = NULL; \
	} \
      if (((FILTER_INFO *) (p_data_filter) != NULL) && ((p_data_filter)->scan_pred != NULL) \
	  && ((p_data_filter)->scan_pred->regu_list != NULL)) \
	{ \
	  (p_mvcc_sel_reev_data)->data_filter = (p_data_filter); \
	} \
      else \
	{ \
	  (p_mvcc_sel_reev_data)->data_filter = NULL; \
	} \
      (p_mvcc_sel_reev_data)->qualification = (p_qualification); \
    } \
  while (0)

#define SET_MVCC_SELECT_REEV_DATA(p_mvcc_reev_data, p_mvcc_sel_reev_data, reev_filter_result) \
  do \
    { \
      assert ((p_mvcc_reev_data) != NULL); \
      (p_mvcc_reev_data)->type = REEV_DATA_SCAN; \
      (p_mvcc_reev_data)->select_reev_data = (p_mvcc_sel_reev_data); \
      (p_mvcc_reev_data)->filter_result = (reev_filter_result); \
    } \
  while (0)

#define SET_MVCC_UPDATE_REEV_DATA(p_mvcc_reev_data, p_mvcc_upddel_reev_data, reev_filter_result) \
  do \
    { \
      (p_mvcc_reev_data)->type = REEV_DATA_UPDDEL; \
      (p_mvcc_reev_data)->upddel_reev_data = (p_mvcc_upddel_reev_data); \
      (p_mvcc_reev_data)->filter_result = (reev_filter_result); \
    } \
  while (0)

extern DB_LOGICAL eval_pred (THREAD_ENTRY * thread_p, pred_expr * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp0 (THREAD_ENTRY * thread_p, pred_expr * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp1 (THREAD_ENTRY * thread_p, pred_expr * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp2 (THREAD_ENTRY * thread_p, pred_expr * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp3 (THREAD_ENTRY * thread_p, pred_expr * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_alsm4 (THREAD_ENTRY * thread_p, pred_expr * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_alsm5 (THREAD_ENTRY * thread_p, pred_expr * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_like6 (THREAD_ENTRY * thread_p, pred_expr * pr, val_descr * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_rlike7 (THREAD_ENTRY * thread_p, pred_expr * pr, val_descr * vd, OID * obj_oid);
extern PR_EVAL_FNC eval_fnc (THREAD_ENTRY * thread_p, pred_expr * pr, DB_TYPE * single_node_type);
extern DB_LOGICAL eval_data_filter (THREAD_ENTRY * thread_p, OID * oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache,
				    FILTER_INFO * filter);
extern DB_LOGICAL eval_key_filter (THREAD_ENTRY * thread_p, DB_VALUE * value, FILTER_INFO * filter);
extern DB_LOGICAL update_logical_result (THREAD_ENTRY * thread_p, DB_LOGICAL ev_res, int *qualification,
					 FILTER_INFO * key_filter, RECDES * recdes, const OID * oid);

#endif /* _QUERY_EVALUATOR_H_ */
