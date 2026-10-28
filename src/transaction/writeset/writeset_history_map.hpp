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
 * writeset_history_map.hpp - concurrent key history map for writeset dependencies
 */

#ifndef _WRITESET_HISTORY_MAP_HPP_
#define _WRITESET_HISTORY_MAP_HPP_

#ident "$Id$"

#include "log_lsa.hpp"
#include "writeset_types.hpp"

#include "tbb/concurrent_hash_map.h"

#include <cstddef>

/* Per-key history entry, split into WRITE and REF commit slots. */
typedef struct wset_slots LOG_WSET_SLOTS;
struct wset_slots
{
  LOG_LSA write_seq;
  LOG_LSA ref_seq;
};

/* Global history of the writeset: for each key hash, the commit LSA of the last transaction that wrote the key
 * (write_seq) and of the last one that referenced it (ref_seq). The TBB map locks one key at a time, so
 * find and publish of different keys run in parallel; clear is not safe against them, so the caller holds
 * history_lock of LOG_WSET_HISTORY, shared for find and publish and exclusive for clear. */
class wset_history_map
{
  public:
    bool find (LOG_WSET_HASH hash, LOG_WSET_SLOTS &slots) const;
    void publish (LOG_WSET_HASH hash, LOG_WSET_KIND kind, const LOG_LSA &commit_lsa);
    std::size_t size () const;
    void clear ();

  private:
    using map_type = tbb::concurrent_hash_map<LOG_WSET_HASH, LOG_WSET_SLOTS>;
    map_type m_map;
};

#endif /* _WRITESET_HISTORY_MAP_HPP_ */
