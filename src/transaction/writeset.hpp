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
 * writeset.hpp - writeset collection and global commit history for parallel applylogdb
 */

#ifndef _WRITESET_HPP_
#define _WRITESET_HPP_

#ident "$Id$"

#include "storage_common.h"	/* OID */
#include "log_lsa.hpp"		/* LOG_LSA */
#include "dbtype.h"		/* DB_VALUE */
#include "thread_compat.hpp"	/* THREAD_ENTRY */
#include "writeset_history_map.hpp"

#include <pthread.h>

/* forward declaration of transaction descriptor (defined in log_impl.h) */
typedef struct log_tdes LOG_TDES;

/* forward declaration of value domain (defined in object_domain.h) */
struct tp_domain;

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

typedef enum
{
  WSET_FALLBACK_TRANSACTION_LIMIT = 0,
  WSET_FALLBACK_STATEMENT_REPLICATION = 1
} WSET_FALLBACK_REASON;

extern void wset_history_initialize (void);
extern void wset_history_finalize (void);
extern int wset_add_write_key (LOG_TDES *tdes, const OID *class_oid, const VFID *index_vfid,
			       DB_VALUE *key);
extern int wset_add_ref_key (LOG_TDES *tdes, const OID *ref_class_oid, const VFID *index_vfid,
			     DB_VALUE *fk_value, struct tp_domain *parent_pk_domain);
extern bool wset_trace_enabled (int level);
extern void wset_trace_key_source (LOG_TDES *tdes, const char *operation, const char *image,
				   LOG_WSET_KIND kind, const OID *table_oid, const OID *row_oid,
				   const char *table_name, const char *constraint_name, const OID *key_class_oid,
				   const VFID *key_index_vfid);
extern void wset_fallback_to_commit_order (LOG_TDES *tdes, WSET_FALLBACK_REASON reason);
extern void wset_find_dependency_from_history (LOG_TDES *tdes, LOG_LSA *wset_parent_out);
extern void wset_publish_commit_to_history (LOG_TDES *tdes, const LOG_LSA *commit_lsa);

#endif /* _WRITESET_HPP_ */
