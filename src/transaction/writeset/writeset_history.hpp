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
 * writeset_history.hpp - global commit history of the writeset module
 *
 * Internal to the writeset module: included by writeset.cpp and the writeset unit tests only. Server modules
 * call the functions in writeset.hpp and never touch the history directly.
 */

#ifndef _WRITESET_HISTORY_HPP_
#define _WRITESET_HISTORY_HPP_

#ident "$Id$"

#include "writeset.hpp"
#include "writeset_history_map.hpp"

#include <pthread.h>

/* global commit history: writeset key hash -> WRITE/REF commit slots. Per-key synchronization is
 * hidden by wset_history_map; the history lock is held shared for per-key access and exclusive only
 * for the rare whole-map clear. */
typedef struct wset_history LOG_WSET_HISTORY;
struct wset_history
{
  wset_history_map map;		/* key hash -> WRITE/REF slots */
  LOG_LSA history_start;		/* conservative parent LSA of keys evicted by a full-history clear */
  LOG_LSA prev_commit_lsa;	/* commit-order baseline, advanced monotonically */
  pthread_rwlock_t history_lock;	/* shared: per-key probe/publish; exclusive: whole-map clear */
  pthread_mutex_t seq_lock;	/* guards prev_commit_lsa read-modify-write */
  INT64 history_size;		/* cached ha_writeset_history_size; per-tx and map capacity */
};

extern LOG_WSET_HISTORY wset_History;

#endif /* _WRITESET_HISTORY_HPP_ */
