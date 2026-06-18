/*
 *
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
 * px_scan_pre_execution_info.cpp
 */

#include "px_scan_pre_execution_info.hpp"

#include "object_primitive.h"
#include "xasl.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace parallel_scan
{
  pre_execution_info::pre_execution_info()
    :  m_scan_infos ()
    , m_precomp_vals ()
  {
  }

  pre_execution_info::~pre_execution_info()
  {
    /* CBRD-26931: each precomputed scalar value was cloned in via pr_clone_value -- clear exactly once. */
    for (auto &entry : m_precomp_vals)
      {
	pr_clear_value (&entry.second);
      }
  }

  void pre_execution_info::capture_pre_execution_info (xasl_node *head)
  {
    xasl_node *xptr = head;
    ACCESS_SPEC_TYPE *specp;
    scan_info scan_info;
    for (; xptr; xptr = xptr->scan_ptr)
      {
	specp = xptr->curr_spec? xptr->curr_spec : xptr->spec_list;
	if (specp->curent != NULL)
	  {
	    COPY_OID (&scan_info.oid, &specp->curent->oid);
	    HFID_COPY (&scan_info.hfid, &specp->curent->hfid);
	    if (specp->access == ACCESS_METHOD_INDEX)
	      {
		BTID_COPY (&scan_info.btid, &specp->curent->btid);
	      }
	  }
	else
	  {
	    COPY_OID (&scan_info.oid, &ACCESS_SPEC_CLS_OID (specp));
	    HFID_COPY (&scan_info.hfid, &ACCESS_SPEC_HFID (specp));
	    if (specp->access == ACCESS_METHOD_INDEX)
	      {
		BTID_COPY (&scan_info.btid, &specp->btid);
	      }
	  }

	scan_info.target_type = specp->type;
	scan_info.access_method = specp->access;
	if (specp->type == TARGET_LIST)
	  {
	    scan_info.list_id = ACCESS_SPEC_LIST_ID (specp);
	  }
	else
	  {
	    scan_info.list_id = NULL;
	  }
	scan_info.status = specp->s_id.status;
	scan_info.qualified_block = specp->s_id.qualified_block;
	m_scan_infos[xptr->header.id] = scan_info;
      }
  }

  /* CBRD-26931: snapshot the main-thread-precomputed value of every uncorrelated single-value (scalar)
   * subquery (one carrying precomp_owner_regu) in the consuming node's aptr lists (across the scan_ptr chain),
   * keyed by the subquery's header.id. Called on the main thread before worker tasks start. Idempotent
   * (emplace skips an existing key). */
  void pre_execution_info::capture_precomp_vals (xasl_node *head)
  {
    for (xasl_node *xptr = head; xptr != NULL; xptr = xptr->scan_ptr)
      {
	for (xasl_node *subq = xptr->aptr_list; subq != NULL; subq = subq->next)
	  {
	    if (subq->precomp_owner_regu == NULL)
	      {
		continue;
	      }
	    if (subq->single_tuple == NULL || subq->single_tuple->valp == NULL
		|| subq->single_tuple->valp->val == NULL)
	      {
		continue;
	      }
	    if (m_precomp_vals.find (subq->header.id) != m_precomp_vals.end ())
	      {
		continue;
	      }
	    DB_VALUE v;
	    pr_clone_value (subq->single_tuple->valp->val, &v);
	    m_precomp_vals.emplace (subq->header.id, v);
	  }
      }
  }

  const DB_VALUE *pre_execution_info::find_precomp_val (XASL_NODE_ID subquery_id) const
  {
    auto it = m_precomp_vals.find (subquery_id);
    if (it == m_precomp_vals.end ())
      {
	return NULL;
      }
    return &it->second;
  }

  void pre_execution_info::record_pre_execution_info (XASL_NODE_ID xasl_id, xasl_node *xptr)
  {
    std::lock_guard<std::mutex> lock (m_mutex);
    scan_info &scan_info = m_scan_infos[xasl_id];
    scan_info.status = xptr->curr_spec->s_id.status;
    scan_info.qualified_block = xptr->curr_spec->s_id.qualified_block;
  }
  void pre_execution_info::apply_pre_execution_info (xasl_node *xasl)
  {
    xasl_node *xptr;
    ACCESS_SPEC_TYPE *specp;
    std::lock_guard<std::mutex> lock (m_mutex);
    for (xptr = xasl; xptr != NULL; xptr = xptr->scan_ptr)
      {
	scan_info &scan_info = m_scan_infos[xptr->header.id];
	specp = xptr->curr_spec? xptr->curr_spec : xptr->spec_list;
	specp->s_id.qualified_block = scan_info.qualified_block;
	specp->s_id.status = S_ENDED;
	specp->s_id.position = S_AFTER;
      }
  }
}
