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
 * writeset_history_internal.hpp - history state and order-module primitives
 */

#ifndef _WRITESET_HISTORY_INTERNAL_HPP_
#define _WRITESET_HISTORY_INTERNAL_HPP_

#ident "$Id$"

#include "writeset_history.hpp"
#include "writeset_history_map.hpp"
#include "writeset_types.hpp"

#include <pthread.h>

typedef struct log_tdes LOG_TDES;

typedef struct wset_history LOG_WSET_HISTORY;
struct wset_history
{
  wset_history_map map;
  LOG_LSA history_start;
  LOG_LSA prev_commit_lsa;
  pthread_rwlock_t history_lock;
  pthread_mutex_t seq_lock;
};

extern LOG_WSET_HISTORY wset_History;

extern void wset_history_lock_for_probe (LOG_LSA *history_start, LOG_LSA *prev_commit_lsa);
extern void wset_history_unlock_after_probe (void);
extern bool wset_history_probe_key (LOG_WSET_HASH hash, LOG_WSET_KIND kind, LOG_WSET_SLOTS &slots,
				    LOG_LSA &candidate, bool &candidate_is_ref);
extern void wset_history_get_commit_order_dependency (LOG_LSA *dependency_out);
extern void wset_history_record_commit (bool commit_order, const LOG_LSA *commit_lsa);
extern void wset_history_publish_keys (LOG_TDES *tdes, size_t begin, size_t end, const LOG_LSA *commit_lsa);
extern void wset_history_advance_start (const LOG_LSA *lsa);

#endif /* _WRITESET_HISTORY_INTERNAL_HPP_ */
