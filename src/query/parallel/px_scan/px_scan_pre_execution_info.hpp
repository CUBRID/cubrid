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
 * px_scan_pre_execution_info.hpp
 */

#ifndef _PX_SCAN_PRE_EXECUTION_INFO_HPP_
#define _PX_SCAN_PRE_EXECUTION_INFO_HPP_

#include "dbtype_def.h"
#include "storage_common.h"
#include "xasl.h"

#include <map>
#include <vector>

struct xasl_node;

namespace parallel_scan
{
  struct scan_info
  {
    OID oid;
    HFID hfid;
    BTID btid;
    QFILE_LIST_ID *list_id;
    TARGET_TYPE target_type;
    ACCESS_METHOD access_method;
    /* status / qualified_block: mutex-guarded. */
    SCAN_STATUS status;
    bool qualified_block;
  };

  /* renamed from XASL_ID to avoid colliding with the global XASL_ID (sha1 struct). */
  using XASL_NODE_ID = int;

  /* the CTEs a statement runs once before their readers: those in the aptr lists of its blocks, of the blocks those
   * hold and of the CTE bodies, never under a correlated subquery, which runs again per outer row */
  void collect_run_once_ctes (xasl_node *xasl, std::vector<xasl_node *> &ctes);

  class pre_execution_info
  {
    public:
      pre_execution_info();
      ~pre_execution_info();

      void capture_pre_execution_info (xasl_node *head);
      scan_info get_scan_info (XASL_NODE_ID xasl_id)
      {
	return m_scan_infos[xasl_id];
      }
      void record_pre_execution_info (XASL_NODE_ID xasl_id, xasl_node *xptr);
      void apply_pre_execution_info (xasl_node *xptr);

      /* precomputed scalar values, keyed by subquery xasl header.id; never operator[] (default-inserts NULL on miss) -- find/at/emplace only. */
      const DB_VALUE *find_precomp_val (XASL_NODE_ID subquery_id) const;
      void capture_precomp_vals (xasl_node *head);

      /* the result lists of the CTEs the leader ran before the scan, keyed by the header.id of the CTE and of its
       * non-recursive part, the nodes their readers' list specs name; find only, as for the precomputed values */
      QFILE_LIST_ID *find_cte_list (XASL_NODE_ID xasl_id) const;
      void capture_cte_lists (xasl_node *root);

    private:
      std::mutex m_mutex;
      std::map <XASL_NODE_ID, scan_info> m_scan_infos;
      std::map <XASL_NODE_ID, DB_VALUE> m_precomp_vals;
      std::map <XASL_NODE_ID, QFILE_LIST_ID *> m_cte_lists;
  };
}

#endif /* _PX_SCAN_PRE_EXECUTION_INFO_HPP_ */
