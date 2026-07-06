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
 * log_writeset.h - writeset collection and global commit history (writeset PoC)
 */

#ifndef _LOG_WRITESET_H_
#define _LOG_WRITESET_H_

#ident "$Id$"

#include "storage_common.h"	/* OID */
#include "log_lsa.hpp"		/* LOG_LSA */
#include "dbtype.h"		/* DB_VALUE */
#include "thread_compat.hpp"	/* THREAD_ENTRY */

#include <pthread.h>

/* forward declaration of transaction descriptor (defined in log_impl.h) */
typedef struct log_tdes LOG_TDES;

/* FNV-1a hash over class_oid (8 bytes) + packed primary key image. */
#ifndef _LOG_WRITESET_HASH_DEFINED_
#define _LOG_WRITESET_HASH_DEFINED_
typedef UINT64 LOG_WRITESET_HASH;
#endif /* _LOG_WRITESET_HASH_DEFINED_ */

/* per-transaction distinct-key limit and global history capacity (shared bound) */
#define LOG_WRITESET_TX_LIMIT     110000
#define LOG_WRITESET_HISTORY_CAP  110000

/* hash key source: (class_oid, packed PK image) - the exact bytes the slave rehydrates */
typedef struct log_writeset_key LOG_WRITESET_KEY;
struct log_writeset_key
{
  OID class_oid;
  int packed_len;
  char *packed_key;
};

/* global commit history: open-addressed hash of recently committed writeset keys */
typedef struct log_writeset_history LOG_WRITESET_HISTORY;
struct log_writeset_history
{
  LOG_WRITESET_HASH *keys;	/* open-addressed key slots, sized capacity */
  LOG_LSA *lsas;		/* parallel commit LSA slots, sized capacity */
  int capacity;			/* number of slots (power of two) */
  int count;			/* number of occupied slots */
  LOG_LSA history_start;	/* conservative parent LSA for keys evicted by clear */
  pthread_mutex_t latch;	/* protects the whole structure */
};

extern LOG_WRITESET_HISTORY log_Writeset_history;

extern int log_writeset_history_initialize (void);
extern void log_writeset_history_finalize (void);
extern int log_writeset_add_key (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const OID * class_oid,
				 const char *packed, int len);
extern int log_writeset_add_dbvalue (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const OID * class_oid, DB_VALUE * pk);
extern void log_writeset_commit_probe (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * ws_parent_out);
extern void log_writeset_commit_flush (THREAD_ENTRY * thread_p, LOG_TDES * tdes, const LOG_LSA * commit_lsa);

#endif /* _LOG_WRITESET_H_ */
