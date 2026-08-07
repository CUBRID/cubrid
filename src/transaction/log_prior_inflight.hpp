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

//
// log_prior_inflight - the in-flight window over the prior list
//
//    A bounded, LSA-ordered ring of the MVCC undo nodes staged in the prior list but not copied into the
//    log page buffer yet. A reader chasing a previous version reads the undo image from the staged node
//    instead of forcing a drain first.
//
//    The protocol and its memory ordering are in log_prior_inflight.cpp.
//

#ifndef _LOG_PRIOR_INFLIGHT_HPP_
#define _LOG_PRIOR_INFLIGHT_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif

#include "log_append.hpp"
#include "log_lsa.hpp"
#include "log_record.hpp"
#include "thread_compat.hpp"

/* Lifetime of the log page buffer pool. Called under LOG_CS write mode from logpb_initialize_pool () /
 * logpb_finalize_pool (), both idempotent. While down, registration is skipped and readers drain. */
void log_prior_inflight_initialize ();
void log_prior_inflight_finalize ();

/* Only what prev_version_lsa can point at is worth staging. */
inline bool
log_prior_inflight_is_registrable (LOG_RECTYPE rectype)
{
  return rectype == LOG_MVCC_UNDO_DATA || rectype == LOG_MVCC_UNDOREDO_DATA
	 || rectype == LOG_MVCC_DIFF_UNDOREDO_DATA;
}

/* A registered node is freed by epoch reclamation, not by the drain. */
inline bool
log_prior_inflight_is_registered (const LOG_PRIOR_NODE *node)
{
  return node->inflight_holder != NULL;
}

/* Append path, under prior_lsa_mutex. Does nothing when the ring is full or the holder allocation fails;
 * a reader that wants an unregistered node drains. */
void log_prior_inflight_register (const LOG_LSA &start_lsa, LOG_PRIOR_NODE *node);

/* Drain, in the LSA order register () used. Only for a node log_prior_inflight_is_registered () holds for. */
void log_prior_inflight_retire (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node);

/* Reader. Returns the staged node at lsa, pinned, or NULL. A non-NULL result must be unpinned. */
LOG_PRIOR_NODE *log_prior_inflight_pin_lookup (THREAD_ENTRY *thread_p, const LOG_LSA &lsa);
void log_prior_inflight_unpin (THREAD_ENTRY *thread_p);

#endif // !_LOG_PRIOR_INFLIGHT_HPP_
