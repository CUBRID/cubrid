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

#include "query_reevaluation.hpp"

#include "scan_manager.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubquery
{
  void
  mvcc_reev_data::set_update_reevaluation (mvcc_update_reev_data &urd)
  {
    type = REEV_DATA_UPDDEL;
    upddel_reev_data = &urd;
    filter_result = V_TRUE;
  }

  void
  mvcc_reev_data::set_scan_reevaluation (mvcc_scan_reev_data &scan)
  {
    type = REEV_DATA_SCAN;
    select_reev_data = &scan;
    filter_result = V_TRUE;
  }

  void
  upddel_mvcc_cond_reeval::init (scan_id_struct &sid)
  {
    switch (sid.type)
      {
      case S_HEAP_SCAN:
	// no range & key
	range_filter = filter_info ();
	key_filter = filter_info ();
	scan_init_filter_info (&data_filter, &sid.s.hsid.scan_pred, &sid.s.hsid.pred_attrs, sid.val_list, sid.vd,
			       &sid.s.hsid.cls_oid, 0, NULL, NULL, NULL);
	rest_attrs = &sid.s.hsid.rest_attrs;
	rest_regu_list = sid.s.hsid.rest_regu_list;
	qualification = sid.qualification;
	break;

      case S_INDX_SCAN:
	scan_init_filter_info (&range_filter, &sid.s.isid.range_pred, &sid.s.isid.range_attrs, sid.val_list,
			       sid.vd, &sid.s.isid.cls_oid, 0, NULL, &sid.s.isid.num_vstr, sid.s.isid.vstr_ids);
	scan_init_filter_info (&key_filter, &sid.s.isid.key_pred, &sid.s.isid.key_attrs, sid.val_list, sid.vd,
			       &sid.s.isid.cls_oid, sid.s.isid.bt_num_attrs, sid.s.isid.bt_attr_ids,
			       &sid.s.isid.num_vstr, sid.s.isid.vstr_ids);
	scan_init_filter_info (&data_filter, &sid.s.isid.scan_pred, &sid.s.isid.pred_attrs, sid.val_list, sid.vd,
			       &sid.s.isid.cls_oid, 0, NULL, NULL, NULL);
	rest_attrs = &sid.s.isid.rest_attrs;
	rest_regu_list = sid.s.isid.rest_regu_list;
	qualification = sid.qualification;
	break;

      default:
	break;
      }
  }

  void
  mvcc_scan_reev_data::set_filters (upddel_mvcc_cond_reeval &ureev)
  {
    if (ureev.range_filter.scan_pred != NULL && ureev.range_filter.scan_pred->regu_list != NULL)
      {
	range_filter = &ureev.range_filter;
      }
    else
      {
	range_filter = NULL;
      }
    if (ureev.key_filter.scan_pred != NULL && ureev.key_filter.scan_pred->regu_list != NULL)
      {
	key_filter = &ureev.key_filter;
      }
    else
      {
	key_filter = NULL;
      }
    if (ureev.data_filter.scan_pred != NULL && ureev.data_filter.scan_pred->regu_list != NULL)
      {
	data_filter = &ureev.data_filter;
      }
    else
      {
	data_filter = NULL;
      }
  }
} // namespace cubquery
