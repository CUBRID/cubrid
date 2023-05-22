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
 * xasl_regu_alloc - allocate/initialize XASL structures (for XASL generation)
 */

#include "xasl_regu_alloc.hpp"

#include "dbtype.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "query_list.h"
#include "regu_var.hpp"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "xasl_predicate.hpp"

#include <cstring>

static void regu_xasl_proc_init (xasl_node &node, PROC_TYPE type);
static void regu_spec_target_init (access_spec_node &spec, TARGET_TYPE type);

xasl_node *
regu_xasl_node_alloc (PROC_TYPE type)
{
  xasl_node *xasl = NULL;
  regu_alloc (xasl);
  if (xasl == NULL)
    {
      return NULL;
    }
  regu_xasl_proc_init (*xasl, type);
  return xasl;
}

void
regu_init (xasl_node &node)
{
  std::memset (&node, 0, sizeof (node));

  node.option = Q_ALL;
  node.iscan_oid_order = prm_get_bool_value (PRM_ID_BT_INDEX_SCAN_OID_ORDER);
  node.scan_op_type = S_SELECT;

  regu_alloc (node.list_id);
}

void
regu_xasl_proc_init (xasl_node &node, PROC_TYPE type)
{
  node.type = type;
  switch (type)
    {
    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
      node.option = Q_DISTINCT;
      break;

    case OBJFETCH_PROC:
      break;

    case BUILDLIST_PROC:
      break;

    case BUILDVALUE_PROC:
      break;

    case MERGELIST_PROC:
      break;

    case SCAN_PROC:
      break;

    case UPDATE_PROC:
      break;

    case DELETE_PROC:
      break;

    case INSERT_PROC:
      break;

    case CONNECTBY_PROC:
      /* allocate CONNECT BY internal list files */
      regu_alloc (node.proc.connect_by.input_list_id);
      regu_alloc (node.proc.connect_by.start_with_list_id);
      break;

    case DO_PROC:
      break;

    case MERGE_PROC:
      node.proc.merge.update_xasl = NULL;
      node.proc.merge.insert_xasl = NULL;
      break;

    case CTE_PROC:
      node.proc.cte.recursive_part = NULL;
      node.proc.cte.non_recursive_part = NULL;
      break;

    default:
      /* BUILD_SCHEMA_PROC */
      break;
    }
}

access_spec_node *
regu_spec_alloc (TARGET_TYPE type)
{
  access_spec_node *ptr = NULL;

  regu_alloc (ptr);
  if (ptr == NULL)
    {
      return NULL;
    }
  regu_spec_target_init (*ptr, type);
  return ptr;
}

void
regu_init (access_spec_node &spec)
{
  spec.access = ACCESS_METHOD_SEQUENTIAL;
  spec.indexptr = NULL;
  spec.where_key = NULL;
  spec.where_pred = NULL;
  spec.where_range = NULL;
  spec.single_fetch = (QPROC_SINGLE_FETCH) false;
  spec.s_dbval = NULL;
  spec.next = NULL;
  spec.flags = ACCESS_SPEC_FLAG_NONE;
}

static void
regu_spec_target_init (access_spec_node &spec, TARGET_TYPE type)
{
  spec.type = type;

  switch (type)
    {
    case TARGET_CLASS:
    case TARGET_CLASS_ATTR:
      spec.s.cls_node.cls_regu_list_key = NULL;
      spec.s.cls_node.cls_regu_list_pred = NULL;
      spec.s.cls_node.cls_regu_list_rest = NULL;
      spec.s.cls_node.cls_regu_list_range = NULL;
      ACCESS_SPEC_HFID (&spec).vfid.fileid = NULL_FILEID;
      ACCESS_SPEC_HFID (&spec).vfid.volid = NULL_VOLID;
      ACCESS_SPEC_HFID (&spec).hpgid = NULL_PAGEID;
      regu_init (ACCESS_SPEC_CLS_OID (&spec));
      spec.s.cls_node.attrids_range = NULL;
      spec.s.cls_node.cache_range = NULL;
      spec.s.cls_node.num_attrs_range = 0;
      break;
    case TARGET_LIST:
      spec.s.list_node.list_regu_list_pred = NULL;
      spec.s.list_node.list_regu_list_rest = NULL;
      spec.s.list_node.list_regu_list_build = NULL;
      spec.s.list_node.list_regu_list_probe = NULL;
      spec.s.list_node.hash_list_scan_yn = 1;
      ACCESS_SPEC_XASL_NODE (&spec) = NULL;
      break;
    case TARGET_SHOWSTMT:
      spec.s.showstmt_node.show_type = SHOWSTMT_NULL;
      spec.s.showstmt_node.arg_list = NULL;
      break;
    case TARGET_SET:
      ACCESS_SPEC_SET_REGU_LIST (&spec) = NULL;
      ACCESS_SPEC_SET_PTR (&spec) = NULL;
      break;
    case TARGET_METHOD:
      ACCESS_SPEC_METHOD_REGU_LIST (&spec) = NULL;
      ACCESS_SPEC_XASL_NODE (&spec) = NULL;
      ACCESS_SPEC_METHOD_SIG_LIST (&spec) = NULL;
      break;
    case TARGET_JSON_TABLE:
      ACCESS_SPEC_JSON_TABLE_REGU_VAR (&spec) = NULL;
      ACCESS_SPEC_JSON_TABLE_ROOT_NODE (&spec) = NULL;
      ACCESS_SPEC_JSON_TABLE_M_NODE_COUNT (&spec) = 0;
      break;
    case TARGET_DBLINK:
      break;
    default:
      // do nothing
      break;
    }
}

void
regu_init (indx_info &ii)
{
  OID_SET_NULL (&ii.class_oid);
  ii.coverage = 0;
  ii.cov_list_id = NULL;
  ii.range_type = R_KEY;
  ii.key_info.key_cnt = 0;
  ii.key_info.key_ranges = NULL;
  ii.key_info.is_constant = false;
  ii.key_info.key_limit_reset = false;
  ii.key_info.is_user_given_keylimit = false;
  ii.key_info.key_limit_l = NULL;
  ii.key_info.key_limit_u = NULL;
  ii.orderby_desc = 0;
  ii.groupby_desc = 0;
  ii.use_desc_index = 0;
  ii.orderby_skip = 0;
  ii.groupby_skip = 0;
  ii.use_iss = false;
  ii.iss_range.range = NA_NA;
  ii.iss_range.key1 = NULL;
  ii.iss_range.key2 = NULL;
}

void
regu_init (key_range &kr)
{
  kr.range = NA_NA;
  kr.key1 = NULL;
  kr.key2 = NULL;
}

void
regu_init (sort_list &sl)
{
  sl.next = NULL;
  sl.pos_descr.pos_no = 0;
  sl.pos_descr.dom = &tp_Integer_domain;
  sl.s_order = S_ASC;
  sl.s_nulls = S_NULLS_FIRST;
}

void
regu_init (qfile_list_id &list_id)
{
  QFILE_CLEAR_LIST_ID (&list_id);
}

void
regu_init (cubxasl::pred_expr &pr)
{
  pr.type = T_NOT_TERM;
  pr.pe.m_not_term = NULL;
}

void
regu_init (arith_list_node &arith)
{
  arith.domain = NULL;
  arith.value = NULL;
  arith.opcode = T_ADD;
  arith.leftptr = NULL;
  arith.rightptr = NULL;
  arith.thirdptr = NULL;
  arith.misc_operand = LEADING;
  arith.rand_seed = NULL;

  regu_alloc (arith.value);
}

void
regu_init (function_node &fnode)
{
  fnode.value = NULL;
  fnode.ftype = (FUNC_TYPE) 0;
  fnode.operand = NULL;
  fnode.tmp_obj = NULL;

  regu_alloc (fnode.value);
}

void
regu_init (cubxasl::aggregate_list_node &agg)
{
  agg.next = NULL;
  agg.accumulator.value = NULL;
  agg.accumulator.value2 = NULL;
  agg.accumulator.curr_cnt = 0;
  agg.function = (FUNC_TYPE) 0;
  agg.option = (QUERY_OPTIONS) 0;
  agg.operands = NULL;
  agg.list_id = NULL;
  agg.sort_list = NULL;
  std::memset (&agg.info, 0, sizeof (AGGREGATE_SPECIFIC_FUNCTION_INFO));

  regu_alloc (agg.accumulator.value);
  regu_alloc (agg.accumulator.value2);
  regu_alloc (agg.list_id);
}

void
regu_init (cubxasl::analytic_list_node &ana)
{
  ana.next = NULL;
  ana.value = NULL;
  ana.value2 = NULL;
  ana.out_value = NULL;
  ana.offset_idx = 0;
  ana.default_idx = 0;
  ana.curr_cnt = 0;
  ana.sort_prefix_size = 0;
  ana.sort_list_size = 0;
  ana.function = (FUNC_TYPE) 0;
  regu_init (ana.operand);
  ana.opr_dbtype = DB_TYPE_NULL;
  ana.flag = 0;
  ana.from_last = false;
  ana.ignore_nulls = false;
  ana.is_const_operand = false;

  regu_alloc (ana.list_id);
  regu_alloc (ana.value2);
}

void
regu_init (regu_variable_node &regu)
{
  regu.type = TYPE_POS_VALUE;
  regu.flags = 0;
  regu.value.val_pos = 0;
  regu.vfetch_to = NULL;
  regu.domain = NULL;
  regu.xasl = NULL;
}

void
regu_init (regu_variable_list_node &regu_varlist)
{
  regu_varlist.next = NULL;
  regu_init (regu_varlist.value);
}

void
regu_init (tp_domain &dom)
{
  tp_domain_init (&dom, DB_TYPE_INTEGER);
}

void
regu_init (OID &oid)
{
  OID_SET_NULL (&oid);
}

void
regu_init (HFID &hfid)
{
  HFID_SET_NULL (&hfid);
}

void
regu_init (upddel_class_info &upddel)
{
  upddel.att_id = NULL;
  upddel.class_hfid = NULL;
  upddel.class_oid = NULL;
  upddel.has_uniques = 0;
  upddel.num_subclasses = 0;
  upddel.num_attrs = 0;
  upddel.needs_pruning = DB_NOT_PARTITIONED_CLASS;
  upddel.num_lob_attrs = NULL;
  upddel.lob_attr_ids = NULL;
  upddel.num_extra_assign_reev = 0;
  upddel.mvcc_extra_assign_reev = NULL;
}

void
regu_init (update_assignment &assign)
{
  assign.att_idx = -1;
  assign.cls_idx = -1;
  assign.constant = NULL;
  assign.regu_var = NULL;
}

void
regu_init (selupd_list &selupd)
{
  selupd.next = NULL;
  regu_init (selupd.class_oid);
  selupd.class_hfid.vfid.fileid = NULL_FILEID;
  selupd.class_hfid.vfid.volid = NULL_VOLID;
  selupd.class_hfid.hpgid = NULL_PAGEID;
  selupd.select_list_size = 0;
  selupd.select_list = NULL;
}

void
regu_dbval_type_init (db_value *ptr, DB_TYPE type)
{
  if (db_value_domain_init (ptr, type, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
    {
      assert (false);
    }
}

void
regu_init (db_value &dbval)
{
  regu_dbval_type_init (&dbval, DB_TYPE_NULL);
}

int *
regu_int_array_alloc (int size)
{
  int *ret_array = NULL;
  regu_array_alloc (&ret_array, (size_t) size);
  return ret_array;
}

OID *
regu_oid_array_alloc (int size)
{
  OID *ret_array = NULL;
  regu_array_alloc (&ret_array, (size_t) size);
  return ret_array;
}
