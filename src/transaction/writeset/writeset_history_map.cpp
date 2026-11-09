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
 * writeset_history_map.cpp - concurrent key history map for writeset dependencies
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "writeset_history_map.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * probe - read the history of one key and choose the commit it must wait for
 *
 *   return: false if the key has no history
 *   hash(in): key hash
 *   kind(in): WRITE or REF
 *   slots(out): both slots of the key, for the trace
 *   candidate(out): the commit to wait for
 *   candidate_is_ref(out): true if the candidate came from ref_seq
 *
 * Note: A REF waits for write_seq, the last transaction that changed the referenced key. A WRITE waits for the
 *       later of write_seq and ref_seq, because a later transaction may have referenced the key's previous state.
 */
bool
wset_history_map::probe (LOG_WSET_HASH hash, LOG_WSET_KIND kind, LOG_WSET_SLOTS &slots, LOG_LSA &candidate,
			 bool &candidate_is_ref) const
{
  map_type::const_accessor acc;

  if (!m_map.find (acc, hash))
    {
      return false;
    }

  LSA_COPY (&slots.write_seq, &acc->second.write_seq);
  LSA_COPY (&slots.ref_seq, &acc->second.ref_seq);

  assert (kind == LOG_WSET_KIND_WRITE || kind == LOG_WSET_KIND_REF);
  candidate_is_ref = (kind == LOG_WSET_KIND_WRITE && LSA_GT (&slots.ref_seq, &slots.write_seq));
  LSA_COPY (&candidate, candidate_is_ref ? &slots.ref_seq : &slots.write_seq);

  return true;
}

void
wset_history_map::publish (LOG_WSET_HASH hash, LOG_WSET_KIND kind, const LOG_LSA &commit_lsa)
{
  map_type::accessor acc;

  if (m_map.insert (acc, hash))
    {
      LSA_SET_NULL (&acc->second.write_seq);
      LSA_SET_NULL (&acc->second.ref_seq);
    }

  if (kind == LOG_WSET_KIND_WRITE)
    {
      if (LSA_GT (&commit_lsa, &acc->second.write_seq))
	{
	  LSA_COPY (&acc->second.write_seq, &commit_lsa);
	}
      return;
    }

  if (kind == LOG_WSET_KIND_REF)
    {
      if (LSA_GT (&commit_lsa, &acc->second.ref_seq))
	{
	  LSA_COPY (&acc->second.ref_seq, &commit_lsa);
	}
      return;
    }

  assert (false);
}

std::size_t
wset_history_map::size () const
{
  return m_map.size ();
}

void
wset_history_map::clear ()
{
  m_map.clear ();
}
