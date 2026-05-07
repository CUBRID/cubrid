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
 * px_scan_join_info.hpp
 */

#ifndef _PX_SCAN_JOIN_INFO_HPP_
#define _PX_SCAN_JOIN_INFO_HPP_

#include "storage_common.h"
#include "xasl.h"

#include <map>

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

  using XASL_ID = int;

  class join_info
  {
    public:
      join_info();
      ~join_info() = default;

      void capture_join_info (xasl_node *head);
      scan_info get_scan_info (XASL_ID xasl_id)
      {
	return m_scan_infos[xasl_id];
      }
      void record_join_info (XASL_ID xasl_id, xasl_node *xptr);
      void apply_join_info (xasl_node *xptr);

    private:
      std::mutex m_mutex;
      std::map <XASL_ID, scan_info> m_scan_infos;
  };
}

#endif /* _PX_SCAN_JOIN_INFO_HPP_ */
