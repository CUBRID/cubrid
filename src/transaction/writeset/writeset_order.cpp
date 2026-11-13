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

/* Probe only while keys are stable and before acquiring prior_lsa_mutex. The history read lock prevents a
 * map swap, while TBB accessors protect individual slots from concurrent publishers. */
void
wset_order_probe_keys (LOG_TDES *tdes, size_t begin, wset_dependency_candidate *result)
{
  LOG_LSA history_start;
  LOG_LSA prev_commit;
  bool key_conflict = false;

  assert (tdes != NULL);
  assert (result != NULL);
  assert (begin <= tdes->wset_hashes.size ());

  LSA_SET_NULL (&result->lsa);
  result->from_ref = false;
  if (tdes->wset_overflow)
    {
      return;
    }

  wset_history_lock_for_probe (&history_start, &prev_commit);
  LSA_COPY (&result->lsa, &history_start);
  for (size_t index = begin; index < tdes->wset_hashes.size (); index++)
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
                  LSA_AS_ARGS (&slots.write_lsa), LSA_AS_ARGS (&slots.ref_lsa), LSA_AS_ARGS (&candidate),
                  candidate_is_ref ? "REF_SLOT" : "WRITE_SLOT");
      if (!LSA_ISNULL (&candidate) && LSA_GT (&candidate, &result->lsa))
        {
          LSA_COPY (&result->lsa, &candidate);
          result->from_ref = candidate_is_ref;
          key_conflict = true;
        }
    }
  wset_history_unlock_after_probe ();
  if (key_conflict)
    {
      wset_stat_count_conflict (result->from_ref);
    }
}

/* Resolve in log order. No history reader lock or per-key work is allowed here: a clear or barrier after the
 * probe can only raise the dependency. Preserve the existing REF-slot rule and WRITE-slot upper bound. */
void
wset_order_resolve_dependency (LOG_TDES *tdes, const wset_dependency_candidate *candidate, LOG_LSA *dependency_out)
{
  LOG_LSA history_start;
  LOG_LSA prev_commit;

  assert (tdes != NULL);
  assert (candidate != NULL);
  assert (dependency_out != NULL);

  wset_history_get_boundaries (&history_start, &prev_commit);
  if (tdes->wset_overflow)
    {
      LSA_COPY (dependency_out, &prev_commit);
    }
  else
    {
      LSA_COPY (dependency_out, &candidate->lsa);
      if (!candidate->from_ref && !LSA_ISNULL (&prev_commit) && LSA_GT (dependency_out, &prev_commit))
        {
          LSA_COPY (dependency_out, &prev_commit);
        }
    }

  if (!LSA_ISNULL (&history_start)
      && (LSA_ISNULL (dependency_out) || LSA_LT (dependency_out, &history_start)))
    {
      LSA_COPY (dependency_out, &history_start);
    }

  if (tdes->wset_overflow)
    {
      WSET_TRACE (1, "DEPENDENCY trid=%d mode=COMMIT_ORDER dependency=%lld|%d\n",
                  tdes->trid, LSA_AS_ARGS (dependency_out));
    }
  else
    {
      WSET_TRACE (1,
                  "DEPENDENCY trid=%d source=%s history_start=%lld|%d prev_commit=%lld|%d dependency=%lld|%d\n",
                  tdes->trid, candidate->from_ref ? "REF_SLOT" : "WRITE_BOUND",
                  LSA_AS_ARGS (&history_start), LSA_AS_ARGS (&prev_commit), LSA_AS_ARGS (dependency_out));
    }
}

static void
wset_order_find_dependency_in_range (LOG_TDES *tdes, size_t begin, LOG_LSA *dependency_out)
{
  wset_dependency_candidate candidate;

  wset_order_probe_keys (tdes, begin, &candidate);
  wset_order_resolve_dependency (tdes, &candidate, dependency_out);
}

void
wset_order_find_dependency (LOG_TDES *tdes, LOG_LSA *dependency_out)
{
  assert (tdes != NULL);
  assert (dependency_out != NULL);

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
	      tdes->trid, LSA_AS_ARGS (commit_lsa),
	      LSA_AS_ARGS (&tdes->wset_dependency_lsa),
	      tdes->wset_hashes.size (), tdes->wset_overflow ? 1 : 0);

  wset_stat_count_commit (tdes->wset_overflow, tdes->wset_fallback_reason);
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
	      LSA_AS_ARGS (dependency_out));
}

/* Make every later apply unit wait at least for this log coordinate. */
void
wset_order_advance_barrier (const LOG_LSA *lsa, const char *reason)
{
  assert (lsa != NULL);
  assert (!LSA_ISNULL (lsa));

  wset_history_advance_start (lsa);
  WSET_TRACE (1, "HISTORY_START raise reason=%s history_start=%lld|%d\n", reason,
	      LSA_AS_ARGS (lsa));
}

/* An independent replication unit (serial update, INCR/DECR that commits on its own) is ordered by the keys it
 * collected, the tail of the key list from begin, like a commit. */
void
wset_order_find_sysop_key_dependency (LOG_TDES *tdes, size_t begin, LOG_LSA *dependency_out)
{
  assert (tdes != NULL);
  assert (dependency_out != NULL);
  assert (!tdes->wset_overflow);

  wset_order_find_dependency_in_range (tdes, begin, dependency_out);
}

/* Called in log order, inside prior_lsa_mutex, for a SYSOP_END ordered by its keys. The commit-order baseline moves
 * to it as for a commit: a later label lowered to the baseline, and a later commit-order label, then still waits for
 * it, although the history floor is not raised. */
void
wset_order_record_sysop (const LOG_LSA *sysop_lsa)
{
  assert (sysop_lsa != NULL);
  wset_history_record_commit (false, sysop_lsa);
}

/* Publish the keys of an independent replication unit at its SYSOP_END, before the operation releases the row lock
 * (instant lock) or the page latch (serial) that held them. */
void
wset_order_publish_sysop_keys (LOG_TDES *tdes, size_t begin, const LOG_LSA *sysop_lsa)
{
  assert (tdes != NULL);
  assert (sysop_lsa != NULL);
  assert (!LSA_ISNULL (sysop_lsa));
  assert (begin <= tdes->wset_hashes.size ());

  WSET_TRACE (1, "SYSOP_PUBLISH trid=%d sysop_lsa=%lld|%d dependency=%lld|%d entries=%zu\n",
	      tdes->trid, LSA_AS_ARGS (sysop_lsa), LSA_AS_ARGS (&tdes->wset_dependency_lsa),
	      tdes->wset_hashes.size () - begin);

  wset_history_publish_keys (tdes, begin, tdes->wset_hashes.size (), sysop_lsa);
}

#endif /* SERVER_MODE || SA_MODE */
