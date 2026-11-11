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
 * writeset_order.cpp - transaction ordering with writeset history
 */

#ident "$Id$"

#include "config.h"

#include "writeset.hpp"
#include "writeset_history_internal.hpp"
#include "writeset_order.hpp"
#include "writeset_stat.hpp"
#include "writeset_trace.hpp"

#include "log_impl.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(SERVER_MODE) || defined(SA_MODE)

static void wset_order_find_dependency_in_range (LOG_TDES *tdes, size_t begin, LOG_LSA *dependency_out);

static void
wset_order_find_dependency_in_range (LOG_TDES *tdes, size_t begin, LOG_LSA *dependency_out)
{
  LOG_LSA dependency;
  LOG_LSA prev_commit;
  LOG_LSA history_start;
  bool dependency_from_ref = false;
  bool key_conflict = false;
  const size_t end = tdes->wset_hashes.size ();

  wset_history_lock_for_probe (&history_start, &prev_commit);
  LSA_COPY (&dependency, &history_start);

  for (size_t index = begin; index < end; index++)
    {
      const LOG_WSET_ENTRY &entry = tdes->wset_hashes[index];
      LOG_WSET_SLOTS slots;
      LOG_LSA candidate;
      bool candidate_is_ref;

      if (!wset_history_probe_key (entry.hash, entry.kind, slots, candidate, candidate_is_ref))
	{
	  WSET_TRACE (3, "PROBE trid=%d kind=%s hash=%016llx result=MISS\n",
		      tdes->trid, wset_trace_kind_name (entry.kind), (unsigned long long) entry.hash);
	  continue;
	}

      WSET_TRACE (3,
		  "PROBE trid=%d kind=%s hash=%016llx write_lsa=%lld|%d ref_lsa=%lld|%d candidate=%lld|%d source=%s\n",
		  tdes->trid, wset_trace_kind_name (entry.kind), (unsigned long long) entry.hash,
		  (long long) slots.write_lsa.pageid, (int) slots.write_lsa.offset,
		  (long long) slots.ref_lsa.pageid, (int) slots.ref_lsa.offset,
		  (long long) candidate.pageid, (int) candidate.offset,
		  candidate_is_ref ? "REF_SLOT" : "WRITE_SLOT");

      if (LSA_ISNULL (&candidate) || !LSA_GT (&candidate, &dependency))
	{
	  continue;
	}

      LSA_COPY (&dependency, &candidate);
      dependency_from_ref = candidate_is_ref;
      key_conflict = true;
    }

  wset_history_unlock_after_probe ();

  if (key_conflict)
    {
      wset_stat_count_conflict (dependency_from_ref);
    }

  if (dependency_from_ref)
    {
      LSA_COPY (dependency_out, &dependency);
      WSET_TRACE (1,
		  "DEPENDENCY trid=%d source=REF_SLOT history_start=%lld|%d prev_commit=%lld|%d dependency=%lld|%d\n",
		  tdes->trid, (long long) history_start.pageid, (int) history_start.offset,
		  (long long) prev_commit.pageid, (int) prev_commit.offset,
		  (long long) dependency_out->pageid, (int) dependency_out->offset);
      return;
    }

  if (LSA_ISNULL (&prev_commit) || LSA_LT (&dependency, &prev_commit))
    {
      LSA_COPY (dependency_out, &dependency);
    }
  else
    {
      LSA_COPY (dependency_out, &prev_commit);
    }

  if (!LSA_ISNULL (&history_start)
      && (LSA_ISNULL (dependency_out) || LSA_LT (dependency_out, &history_start)))
    {
      LSA_COPY (dependency_out, &history_start);
    }

  WSET_TRACE (1,
	      "DEPENDENCY trid=%d source=WRITE_BOUND history_start=%lld|%d prev_commit=%lld|%d dependency=%lld|%d\n",
	      tdes->trid, (long long) history_start.pageid, (int) history_start.offset,
	      (long long) prev_commit.pageid, (int) prev_commit.offset,
	      (long long) dependency_out->pageid, (int) dependency_out->offset);
}

void
wset_order_find_dependency (LOG_TDES *tdes, LOG_LSA *dependency_out)
{
  assert (tdes != NULL);
  assert (dependency_out != NULL);

  if (tdes->wset_overflow)
    {
      wset_history_get_commit_order_dependency (dependency_out);
      WSET_TRACE (1, "DEPENDENCY trid=%d mode=COMMIT_ORDER dependency=%lld|%d\n",
		  tdes->trid, (long long) dependency_out->pageid, (int) dependency_out->offset);
      return;
    }

  wset_order_find_dependency_in_range (tdes, 0, dependency_out);
}

/* Record the commit-order baseline while prior_lsa_mutex still fixes log order. */
void
wset_order_record_commit (LOG_TDES *tdes, const LOG_LSA *commit_lsa)
{
  assert (tdes != NULL);
  wset_history_record_commit (tdes->wset_overflow, commit_lsa);
}

/* Publish committed keys before the transaction releases its row locks. */
void
wset_order_publish_commit_keys (LOG_TDES *tdes, const LOG_LSA *commit_lsa)
{
  assert (tdes != NULL);
  assert (commit_lsa != NULL);
  assert (!LSA_ISNULL (commit_lsa));

  WSET_TRACE (1,
	      "COMMIT trid=%d commit_lsa=%lld|%d dependency=%lld|%d entries=%zu fallback=%d\n",
	      tdes->trid, (long long) commit_lsa->pageid, (int) commit_lsa->offset,
	      (long long) tdes->wset_dependency_lsa.pageid, (int) tdes->wset_dependency_lsa.offset,
	      tdes->wset_hashes.size (), tdes->wset_overflow ? 1 : 0);

  wset_stat_count_commit (tdes->wset_overflow, (WSET_FALLBACK_REASON) tdes->wset_fallback_reason);
  wset_history_publish_keys (tdes, 0, tdes->wset_hashes.size (), commit_lsa);
}

/* A replicated SYSOP has no published keys, so it uses the commit-order boundary. */
void
wset_order_find_sysop_dependency (LOG_TDES *tdes, LOG_LSA *dependency_out)
{
  assert (tdes != NULL);
  assert (dependency_out != NULL);

  wset_history_get_commit_order_dependency (dependency_out);
  WSET_TRACE (1, "DEPENDENCY trid=%d mode=SYSOP_COMMIT_ORDER dependency=%lld|%d\n", tdes->trid,
	      (long long) dependency_out->pageid, (int) dependency_out->offset);
}

/* Make every later apply unit wait at least for this log coordinate. */
void
wset_order_advance_barrier (const LOG_LSA *lsa, const char *reason)
{
  assert (lsa != NULL);
  assert (!LSA_ISNULL (lsa));

  wset_history_advance_start (lsa);
  WSET_TRACE (1, "HISTORY_START raise reason=%s history_start=%lld|%d\n", reason,
	      (long long) lsa->pageid, (int) lsa->offset);
}

#endif /* SERVER_MODE || SA_MODE */
