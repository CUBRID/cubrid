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
 * writeset_history.cpp - global writeset commit history
 */

#ident "$Id$"

#include "config.h"

#include <atomic>
#include <new>

#include "writeset.hpp"
#include "writeset_config.hpp"
#include "writeset_history_internal.hpp"
#include "writeset_stat.hpp"
#include "writeset_trace.hpp"

#include "log_impl.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(SERVER_MODE) || defined(SA_MODE)

LOG_WSET_HISTORY wset_History;

static std::atomic<INT64> wset_Publish_reserved { 0 };
static bool wset_History_initialized = false;

static void wset_history_clear_locked (const LOG_LSA *clear_floor, LOG_LSA *history_start_out,
                                       wset_history_map &retired_map);
static bool wset_history_has_capacity (size_t publish_count);
static bool wset_history_reserve_capacity (size_t publish_count);
static void wset_history_release_capacity (size_t publish_count);
static bool wset_history_publish_entries (LOG_TDES *tdes, size_t begin, size_t end,
    const LOG_LSA *commit_lsa);
static void wset_history_clear_for_commit_order (LOG_TDES *tdes, const LOG_LSA *commit_lsa);
static void wset_history_publish_with_capacity (LOG_TDES *tdes, size_t begin, size_t end,
    const LOG_LSA *commit_lsa);

void
wset_history_initialize (void)
{
  if (wset_History_initialized)
    {
      return;
    }

  pthread_rwlock_init (&wset_History.history_lock, NULL);
  pthread_mutex_init (&wset_History.seq_lock, NULL);

  wset_History.map.clear ();
  LSA_SET_NULL (&wset_History.history_start);
  LSA_SET_NULL (&wset_History.prev_commit_lsa);
  wset_Publish_reserved.store (0, std::memory_order_relaxed);
  wset_config_initialize ();

  wset_History_initialized = true;
  wset_stat_start ();

  WSET_TRACE (1, "INITIALIZE history_size=%lld trace_level=%d\n",
	      (long long) wset_config_get_key_limit (), wset_config_get_trace_level ());
}

/* Finalize after transaction processing has stopped. */
void
wset_history_finalize (void)
{
  if (!wset_History_initialized)
    {
      return;
    }

  wset_History.map.clear ();
  LSA_SET_NULL (&wset_History.history_start);
  LSA_SET_NULL (&wset_History.prev_commit_lsa);

  pthread_mutex_destroy (&wset_History.seq_lock);
  pthread_rwlock_destroy (&wset_History.history_lock);

  wset_History_initialized = false;
  wset_config_finalize ();
}

/* Hold a stable history generation while writeset_order probes all transaction keys. */
void
wset_history_lock_for_probe (LOG_LSA *history_start, LOG_LSA *prev_commit_lsa)
{
  assert (history_start != NULL);
  assert (prev_commit_lsa != NULL);

  /* A clear must wait for readers without holding seq_lock, which committers acquire under the log mutex. */
  pthread_rwlock_rdlock (&wset_History.history_lock);
  wset_history_get_boundaries (history_start, prev_commit_lsa);
}

void
wset_history_unlock_after_probe (void)
{
  pthread_rwlock_unlock (&wset_History.history_lock);
}

/* Read only the ordering boundaries; safe inside prior_lsa_mutex without waiting for history readers. */
void
wset_history_get_boundaries (LOG_LSA *history_start, LOG_LSA *prev_commit_lsa)
{
  assert (history_start != NULL);
  assert (prev_commit_lsa != NULL);

  pthread_mutex_lock (&wset_History.seq_lock);
  LSA_COPY (history_start, &wset_History.history_start);
  LSA_COPY (prev_commit_lsa, &wset_History.prev_commit_lsa);
  pthread_mutex_unlock (&wset_History.seq_lock);
}

bool
wset_history_probe_key (LOG_WSET_HASH hash, LOG_WSET_KIND kind, LOG_WSET_SLOTS &slots,
			LOG_LSA &candidate, bool &candidate_is_ref)
{
  return wset_History.map.probe (hash, kind, slots, candidate, candidate_is_ref);
}

void
wset_history_get_commit_order_dependency (LOG_LSA *dependency_out)
{
  assert (dependency_out != NULL);

  pthread_mutex_lock (&wset_History.seq_lock);
  LSA_COPY (dependency_out, &wset_History.prev_commit_lsa);
  if (!LSA_ISNULL (&wset_History.history_start)
      && (LSA_ISNULL (dependency_out) || LSA_LT (dependency_out, &wset_History.history_start)))
    {
      LSA_COPY (dependency_out, &wset_History.history_start);
    }
  pthread_mutex_unlock (&wset_History.seq_lock);
}

/* Called in log order; a commit-order transaction also raises the history floor. */
void
wset_history_record_commit (bool commit_order, const LOG_LSA *commit_lsa)
{
  assert (commit_lsa != NULL);
  assert (!LSA_ISNULL (commit_lsa));

  pthread_mutex_lock (&wset_History.seq_lock);
  if (LSA_ISNULL (&wset_History.prev_commit_lsa) || LSA_GT (commit_lsa, &wset_History.prev_commit_lsa))
    {
      LSA_COPY (&wset_History.prev_commit_lsa, commit_lsa);
    }
  if (commit_order
      && (LSA_ISNULL (&wset_History.history_start) || LSA_GT (commit_lsa, &wset_History.history_start)))
    {
      LSA_COPY (&wset_History.history_start, commit_lsa);
    }
  pthread_mutex_unlock (&wset_History.seq_lock);
}

static bool
wset_history_publish_entries (LOG_TDES *tdes, size_t begin, size_t end, const LOG_LSA *commit_lsa)
{
  for (size_t index = begin; index < end; index++)
    {
      const LOG_WSET_ENTRY &entry = tdes->wset_hashes[index];
      try
	{
	  wset_History.map.publish (entry.hash, entry.kind, *commit_lsa);
	}
      catch (const std::bad_alloc &)
	{
	  return false;
	}

      WSET_TRACE (3, "PUBLISH trid=%d kind=%s hash=%016llx commit_lsa=%lld|%d\n",
		  tdes->trid, wset_trace_kind_name (entry.kind), (unsigned long long) entry.hash,
		  LSA_AS_ARGS (commit_lsa));
      assert (entry.kind == LOG_WSET_KIND_WRITE || entry.kind == LOG_WSET_KIND_REF);
      wset_stat_count_publish (entry.kind == LOG_WSET_KIND_REF);
    }

  return true;
}

static void
wset_history_clear_locked (const LOG_LSA *clear_floor, LOG_LSA *history_start_out, wset_history_map &retired_map)
{
  assert (clear_floor != NULL);
  assert (!LSA_ISNULL (clear_floor));

  if (LSA_ISNULL (&wset_History.history_start) || LSA_GT (clear_floor, &wset_History.history_start))
    {
      LSA_COPY (&wset_History.history_start, clear_floor);
    }

  assert (retired_map.size () == 0);
  wset_History.map.swap (retired_map);
  wset_stat_count_history_clear ();
  LSA_COPY (history_start_out, &wset_History.history_start);
}

static bool
wset_history_has_capacity (size_t publish_count)
{
  return (INT64) (wset_History.map.size () + publish_count) <= wset_config_get_key_limit ();
}

static bool
wset_history_reserve_capacity (size_t publish_count)
{
  INT64 reserved = wset_Publish_reserved.fetch_add ((INT64) publish_count,
		   std::memory_order_acq_rel) + (INT64) publish_count;

  if ((INT64) wset_History.map.size () + reserved <= wset_config_get_key_limit ())
    {
      return true;
    }

  wset_Publish_reserved.fetch_sub ((INT64) publish_count, std::memory_order_acq_rel);
  return false;
}

static void
wset_history_release_capacity (size_t publish_count)
{
  wset_Publish_reserved.fetch_sub ((INT64) publish_count, std::memory_order_acq_rel);
}

static void
wset_history_clear_for_commit_order (LOG_TDES *tdes, const LOG_LSA *commit_lsa)
{
  LOG_LSA history_start;
  size_t old_size;
  wset_history_map retired_map;

  pthread_rwlock_wrlock (&wset_History.history_lock);
  pthread_mutex_lock (&wset_History.seq_lock);
  old_size = wset_History.map.size ();
  wset_history_clear_locked (&wset_History.prev_commit_lsa, &history_start, retired_map);
  pthread_mutex_unlock (&wset_History.seq_lock);
  pthread_rwlock_unlock (&wset_History.history_lock);

  WSET_TRACE (1,
              "CLEAR trid=%d reason=COMMIT_ORDER_FALLBACK old_size=%zu new_size=0 "
              "commit_lsa=%lld|%d history_start=%lld|%d\n",
              tdes->trid, old_size, LSA_AS_ARGS (commit_lsa), LSA_AS_ARGS (&history_start));
  /* retired_map releases the detached entries after both locks have been dropped. */
}

static void
wset_history_publish_with_capacity (LOG_TDES *tdes, size_t begin, size_t end, const LOG_LSA *commit_lsa)
{
  const size_t publish_count = end - begin;
  LOG_LSA history_start;

  for (;;)
    {
      bool published = true;

      pthread_rwlock_rdlock (&wset_History.history_lock);
      if (wset_history_reserve_capacity (publish_count))
        {
          published = wset_history_publish_entries (tdes, begin, end, commit_lsa);
          wset_history_release_capacity (publish_count);
          pthread_rwlock_unlock (&wset_History.history_lock);
          if (published)
            {
              return;
            }
        }
      else
        {
          pthread_rwlock_unlock (&wset_History.history_lock);
        }

      wset_history_map retired_map;
      pthread_rwlock_wrlock (&wset_History.history_lock);
      if (published && wset_history_has_capacity (publish_count))
        {
          /* Another publisher cleared the map while we waited. Retry with a shared reservation; never publish
           * a large writeset with seq_lock or the exclusive history lock held. */
          pthread_rwlock_unlock (&wset_History.history_lock);
          continue;
        }

      size_t old_size = wset_History.map.size ();
      const char *clear_reason = published ? "HISTORY_CAPACITY" : "OUT_OF_MEMORY";
      pthread_mutex_lock (&wset_History.seq_lock);
      wset_history_clear_locked (&wset_History.prev_commit_lsa, &history_start, retired_map);
      pthread_mutex_unlock (&wset_History.seq_lock);
      pthread_rwlock_unlock (&wset_History.history_lock);

      if (published)
        {
          wset_stat_count_clear_capacity ();
        }
      WSET_TRACE (1,
                  "CLEAR trid=%d reason=%s old_size=%zu publish=%zu new_size=0 "
                  "commit_lsa=%lld|%d history_start=%lld|%d\n",
                  tdes->trid, clear_reason, old_size, publish_count, LSA_AS_ARGS (commit_lsa),
                  LSA_AS_ARGS (&history_start));
      /* The floor covers this commit and every entry detached with the old map. Re-publishing this writeset
       * would add no ordering information. retired_map is destroyed only after both locks are released. */
      return;
    }
}

void
wset_history_publish_keys (LOG_TDES *tdes, size_t begin, size_t end, const LOG_LSA *commit_lsa)
{
  assert (tdes != NULL);
  assert (commit_lsa != NULL);
  assert (!LSA_ISNULL (commit_lsa));
  assert (begin <= end && end <= tdes->wset_hashes.size ());

  if (tdes->wset_overflow)
    {
      wset_history_clear_for_commit_order (tdes, commit_lsa);
      return;
    }

  if (begin == end)
    {
      return;
    }

  wset_history_publish_with_capacity (tdes, begin, end, commit_lsa);
}

/* Preserve ordering when no per-key history represents a preceding apply unit. */
void
wset_history_advance_start (const LOG_LSA *lsa)
{
  assert (lsa != NULL);
  assert (!LSA_ISNULL (lsa));

  pthread_mutex_lock (&wset_History.seq_lock);
  if (LSA_ISNULL (&wset_History.history_start) || LSA_GT (lsa, &wset_History.history_start))
    {
      LSA_COPY (&wset_History.history_start, lsa);
    }
  pthread_mutex_unlock (&wset_History.seq_lock);
}

void
wset_history_get_usage (INT64 *history_size, INT64 *entry_count)
{
  assert (history_size != NULL);
  assert (entry_count != NULL);

  *history_size = wset_config_get_key_limit ();
  *entry_count = 0;

  if (wset_History_initialized)
    {
      pthread_rwlock_rdlock (&wset_History.history_lock);
      *entry_count = (INT64) wset_History.map.size ();
      pthread_rwlock_unlock (&wset_History.history_lock);
    }
}

#endif /* SERVER_MODE || SA_MODE */
