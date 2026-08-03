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

//
// log_prior_inflight - the in-flight window over the prior list
//
//    A bounded, LSA-ordered ring of the MVCC undo nodes that are staged in the prior list but not
//    copied into the log page buffer yet. A reader chasing a previous version can take the undo image
//    straight from the staged node instead of forcing a drain to get the record onto a log page first.
//
//    See log_prior_inflight.cpp for the register/retire/lookup protocol and its memory ordering.
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

/* The window lives exactly as long as the log page buffer pool: its epoch reclamation table sits on the
 * process-wide transaction system, which is torn down after the log module. Called under LOG_CS write
 * mode from logpb_initialize_pool () / logpb_finalize_pool (); both idempotent. While the window is down,
 * registration is skipped and readers drain. */
void log_prior_inflight_initialize ();
void log_prior_inflight_finalize ();

/* Is this record type one the window keeps? Only what prev_version_lsa can point at is worth staging. */
inline bool
log_prior_inflight_is_registrable (LOG_RECTYPE rectype)
{
  return rectype == LOG_MVCC_UNDO_DATA || rectype == LOG_MVCC_UNDOREDO_DATA
	 || rectype == LOG_MVCC_DIFF_UNDOREDO_DATA;
}

/* Is this node currently in the window? A registered node is freed through epoch reclamation instead of
 * by the drain, so a reader holding it cannot have it freed underneath. */
inline bool
log_prior_inflight_is_registered (const LOG_PRIOR_NODE *node)
{
  return node->inflight_holder != NULL;
}

/* Append path, under prior_lsa_mutex. Publishes the node at start_lsa. Silently does nothing when the
 * ring is full or the holder allocation fails; a reader that wants an unregistered node drains. */
void log_prior_inflight_register (const LOG_LSA &start_lsa, LOG_PRIOR_NODE *node);

/* Drain, in the same LSA order register () used. Unlinks the node and hands it to epoch reclamation.
 * Must only be called for a node log_prior_inflight_is_registered () holds for. */
void log_prior_inflight_retire (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node);

/* Reader. Returns the staged node at lsa, pinned, or NULL when the window does not have it. A non-NULL
 * result must be released with log_prior_inflight_unpin (). */
LOG_PRIOR_NODE *log_prior_inflight_pin_lookup (THREAD_ENTRY *thread_p, const LOG_LSA &lsa);
void log_prior_inflight_unpin (THREAD_ENTRY *thread_p);

/* Nodes handed to epoch reclamation but not freed yet. Exposed as a statdump gauge: a value that stays
 * high means reclamation is not keeping up with the drain. */
INT64 log_prior_inflight_backlog ();

#endif // !_LOG_PRIOR_INFLIGHT_HPP_
