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
 * log_writeset.c - writeset collection and global commit history (writeset PoC)
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "log_writeset.h"

#include "error_manager.h"
#include "log_impl.h"
#include "object_representation.h"
#include "porting.h"

#include "memory_wrapper.hpp"	// XXX: SHOULD BE THE LAST INCLUDE HEADER

#if defined(SERVER_MODE) || defined(SA_MODE)

/* FNV-1a 64-bit constants */
#define LOG_WRITESET_FNV_OFFSET_BASIS ((UINT64) 0xcbf29ce484222325ULL)
#define LOG_WRITESET_FNV_PRIME        ((UINT64) 0x00000100000001b3ULL)

LOG_WRITESET_HISTORY log_Writeset_history;

/* commit-order baseline: the most recent commit LSA published to the history.
 * Guarded by log_Writeset_history.latch (advanced monotonically in commit_flush). */
static LOG_LSA log_Writeset_prev_commit_lsa;

static UINT64 log_writeset_fnv1a (const OID * class_oid, const char *packed, int len);

/*
 * log_writeset_fnv1a - FNV-1a 64-bit hash over class_oid (8 bytes) + packed key
 */
static UINT64
log_writeset_fnv1a (const OID * class_oid, const char *packed, int len)
{
  UINT64 hash = LOG_WRITESET_FNV_OFFSET_BASIS;
  unsigned char oid_bytes[8];
  int i;

  /* class_oid: 8 bytes = pageid (4) + slotid (2) + volid (2) */
  memcpy (&oid_bytes[0], &class_oid->pageid, sizeof (class_oid->pageid));
  memcpy (&oid_bytes[4], &class_oid->slotid, sizeof (class_oid->slotid));
  memcpy (&oid_bytes[6], &class_oid->volid, sizeof (class_oid->volid));

  for (i = 0; i < 8; i++)
    {
      hash ^= (UINT64) oid_bytes[i];
      hash *= LOG_WRITESET_FNV_PRIME;
    }

  for (i = 0; i < len; i++)
    {
      hash ^= (UINT64) (unsigned char) packed[i];
      hash *= LOG_WRITESET_FNV_PRIME;
    }

  return hash;
}

/*
 * log_writeset_history_initialize - allocate and initialize the global commit history
 *
 * return: NO_ERROR, or ER_OUT_OF_VIRTUAL_MEMORY on allocation failure
 */
int
log_writeset_history_initialize (void)
{
  log_Writeset_history.map.clear ();
  LSA_SET_NULL (&log_Writeset_history.history_start);
  pthread_mutex_init (&log_Writeset_history.latch, NULL);

  LSA_SET_NULL (&log_Writeset_prev_commit_lsa);

  return NO_ERROR;
}

/*
 * log_writeset_history_finalize - free the global commit history
 */
void
log_writeset_history_finalize (void)
{
  log_Writeset_history.map.clear ();
  LSA_SET_NULL (&log_Writeset_history.history_start);
  pthread_mutex_destroy (&log_Writeset_history.latch);

  LSA_SET_NULL (&log_Writeset_prev_commit_lsa);
}

/*
 * log_writeset_add_key - add a distinct writeset key hash to the transaction
 *
 *   tdes(in/out): transaction descriptor
 *   class_oid(in): class OID of the modified instance
 *   packed(in): packed primary key image
 *   len(in): length of the packed image
 *
 * return: NO_ERROR, or ER_OUT_OF_VIRTUAL_MEMORY on allocation failure
 *
 * Note: On per-tx limit overflow the collected writeset is dropped and the
 *       transaction is marked to be committed in commit order (ws_overflow).
 */
int
log_writeset_add_key (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const OID * class_oid, const char *packed, int len)
{
  LOG_WRITESET_HASH hash;

  if (tdes == NULL || class_oid == NULL)
    {
      return NO_ERROR;
    }

  if (tdes->ws_overflow)
    {
      /* already dropped this transaction's writeset */
      return NO_ERROR;
    }

  /* NOTE: ws_overflow 는 단순 메모리 캡이 아니라 정합성 안전장치다. 수집이 부분적으로만 되면
   * writeset 이 불완전 -> 트랜잭션이 실제보다 "독립"으로 보여 슬레이브 게이트가 잘못 병렬화
   * -> 같은 행 순서 붕괴. 게다가 호출부는 반환값을 (void) 로 무시하므로 조용히 틀린다. 그래서
   * per-tx 한도 초과 시 부분 writeset 을 남기지 않고 통째로 버린 뒤 commit-order 로 격하한다
   * (= MySQL has_missing_keys). (PoC 단순화 대상 아님 - 제거 금지.) */
  if (tdes->ws_hashes.size () >= LOG_WRITESET_TX_LIMIT)
    {
      /* per-tx limit reached: drop writeset, degrade to commit order */
      tdes->ws_overflow = true;
      tdes->ws_hashes.clear ();
      tdes->ws_hashes.shrink_to_fit ();
      return NO_ERROR;
    }

  hash = log_writeset_fnv1a (class_oid, packed, len);
  tdes->ws_hashes.push_back (hash);

  return NO_ERROR;
}

/*
 * log_writeset_add_dbvalue - pack a DB_VALUE primary key and add its hash
 *
 *   tdes(in/out): transaction descriptor
 *   class_oid(in): class OID of the modified instance
 *   pk(in): primary key value
 *
 * return: NO_ERROR, or an error code on failure
 */
int
log_writeset_add_dbvalue (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const OID * class_oid, DB_VALUE * pk)
{
  char *buf = NULL;
  char *ptr;
  int buf_len;
  int packed_len = 0;
  int error;

  if (tdes == NULL || class_oid == NULL || pk == NULL)
    {
      return NO_ERROR;
    }

  if (tdes->ws_overflow)
    {
      return NO_ERROR;
    }

  buf_len = OR_VALUE_ALIGNED_SIZE (pk);
  if (buf_len <= 0)
    {
      return NO_ERROR;
    }

  buf = (char *) malloc ((size_t) buf_len);
  if (buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) buf_len);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  ptr = or_pack_mem_value (buf, pk, &packed_len);
  if (ptr == NULL)
    {
      free_and_init (buf);
      return NO_ERROR;
    }

  error = log_writeset_add_key (thread_p, tdes, class_oid, buf, packed_len);

  free_and_init (buf);

  return error;
}

/*
 * log_writeset_commit_probe - compute this transaction's dependency label
 *
 *   ws_parent_out(out): dependency_seq = min (prev_commit_lsa, writeset parent)
 *
 * Note: Called BEFORE the commit LSA is assigned (outside prior_lsa_mutex), under
 *       the dedicated history latch. The writeset parent is the newest commit LSA
 *       among the keys this transaction touched, floored at history_start. Taking
 *       the min with the commit-order baseline lets an independent transaction
 *       (whose parent stays at history_start) gate through immediately, while a
 *       same-key successor inherits its predecessor's commit LSA. An overflowed
 *       (or empty) writeset degrades to the commit-order baseline.
 */
void
log_writeset_commit_probe (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * ws_parent_out)
{
  LOG_LSA ws_parent;

  pthread_mutex_lock (&log_Writeset_history.latch);

  if (tdes != NULL && tdes->ws_overflow)
    {
      /* no writeset: fall back to commit order (wait for everything before us) */
      LSA_COPY (ws_parent_out, &log_Writeset_prev_commit_lsa);
      pthread_mutex_unlock (&log_Writeset_history.latch);
      return;
    }

  LSA_COPY (&ws_parent, &log_Writeset_history.history_start);

  if (tdes != NULL)
    {
      for (LOG_WRITESET_HASH h : tdes->ws_hashes)
	{
	  auto it = log_Writeset_history.map.find (h);

	  if (it != log_Writeset_history.map.end () && LSA_GT (&it->second, &ws_parent))
	    {
	      LSA_COPY (&ws_parent, &it->second);
	    }
	}
    }

  /* dependency_seq = min (prev_commit_lsa, ws_parent); NULL acts as the smallest LSA */
  if (LSA_ISNULL (&log_Writeset_prev_commit_lsa) || LSA_LT (&ws_parent, &log_Writeset_prev_commit_lsa))
    {
      LSA_COPY (ws_parent_out, &ws_parent);
    }
  else
    {
      LSA_COPY (ws_parent_out, &log_Writeset_prev_commit_lsa);
    }

  pthread_mutex_unlock (&log_Writeset_history.latch);
}

/*
 * log_writeset_commit_flush - publish this commit's keys into the global history
 *
 *   commit_lsa(in): the assigned commit LSA of this transaction
 *
 * Note: Called AFTER the commit LSA is assigned and BEFORE the transaction's row
 *       locks are released. Row X-locks serialize same-key transactions, so a
 *       later same-key transaction cannot probe until this flush has published the
 *       key. Runs under the dedicated history latch, never on the abort path.
 */
void
log_writeset_commit_flush (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const LOG_LSA * commit_lsa)
{
  if (tdes == NULL || commit_lsa == NULL || LSA_ISNULL (commit_lsa))
    {
      return;
    }

  pthread_mutex_lock (&log_Writeset_history.latch);

  /* advance the commit-order baseline monotonically (flush order may differ from
   * commit order once we run outside prior_lsa_mutex) */
  if (LSA_ISNULL (&log_Writeset_prev_commit_lsa) || LSA_GT (commit_lsa, &log_Writeset_prev_commit_lsa))
    {
      LSA_COPY (&log_Writeset_prev_commit_lsa, commit_lsa);
    }

  if (!tdes->ws_overflow && !tdes->ws_hashes.empty ())
    {
      /* CAP 초과하면 히스토리를 통째로 비우고 보수적 floor 를 이 commit LSA 로 올린다
       * (= MySQL m_writeset_history.clear() + m_writeset_history_start = seq). */
      if (log_Writeset_history.map.size () + tdes->ws_hashes.size () > (size_t) LOG_WRITESET_HISTORY_CAP)
	{
	  log_Writeset_history.map.clear ();
	  LSA_COPY (&log_Writeset_history.history_start, commit_lsa);
	}

      /* 이 트랜잭션의 키들에 현재 commit LSA 를 기록(신규 삽입 또는 갱신). */
      for (LOG_WRITESET_HASH h : tdes->ws_hashes)
	{
	  log_Writeset_history.map[h] = *commit_lsa;
	}
    }

  pthread_mutex_unlock (&log_Writeset_history.latch);
}

#endif /* SERVER_MODE || SA_MODE */
