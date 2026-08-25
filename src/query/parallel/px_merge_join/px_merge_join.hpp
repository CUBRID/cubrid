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
 * px_merge_join.hpp - parallel range-partitioned merge of a merge join's sorted inputs (CBRD-27307)
 */

#ifndef _PX_MERGE_JOIN_HPP_
#define _PX_MERGE_JOIN_HPP_

#if !defined (SERVER_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) */

#include "query_list.h"
#include "thread_compat.hpp"

namespace parallel_query
{
  namespace merge_join
  {
    /* Attempts the parallel range-partitioned merge (JOIN_INNER only). executed comes back false
     * (with NO_ERROR) whenever the gate, the partitioning, or the worker reservation says no —
     * the caller must then run the serial qexec_merge_list. On executed == true, *result_list_id
     * holds the merged list, tuple-for-tuple identical to the serial merge output. */
    int try_parallel_merge (THREAD_ENTRY *thread_p, QFILE_LIST_ID *outer_list_id, QFILE_LIST_ID *inner_list_id,
			    QFILE_LIST_MERGE_INFO *merge_infop, int ls_flag, QFILE_LIST_ID **result_list_id,
			    bool &executed);
  } /* namespace merge_join */
} /* namespace parallel_query */

#endif /* _PX_MERGE_JOIN_HPP_ */
