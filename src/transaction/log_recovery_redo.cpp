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

#include "log_recovery_redo.hpp"

#if !defined(NDEBUG)
vpid_lsa_consistency_check log_Gl_recovery_redo_consistency_check;
#endif

void
vpid_lsa_consistency_check::check (const vpid &a_vpid, const log_lsa &a_log_lsa)
{
#if !defined(NDEBUG)
  std::lock_guard<std::mutex> lck (mtx);
  const vpid_key_t key {a_vpid.volid, a_vpid.pageid};
  const auto map_it =  consistency_check_map.find (key);
  if (map_it != consistency_check_map.cend())
    {
      assert ((*map_it).second < a_log_lsa);
    }
  consistency_check_map.emplace (key, a_log_lsa);
#endif
}

void
vpid_lsa_consistency_check::cleanup ()
{
#if !defined(NDEBUG)
  std::lock_guard<std::mutex> lck (mtx);
  consistency_check_map.clear ();
#endif
}
