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

#include <unordered_map>

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

/* global commit history (MySQL rpl_trx_tracking 방식): writeset 키 해시 -> 최신 커밋 LSA 표준
 * 해시맵. 손수 만든 오픈 어드레싱 대신 std::unordered_map 이 성장/적재율을 알아서 관리하고,
 * CAP 초과 시 통째로 clear + history_start 상향(= MySQL m_writeset_history.clear()). */
typedef struct log_writeset_history LOG_WRITESET_HISTORY;
struct log_writeset_history
{
  std::unordered_map < LOG_WRITESET_HASH, LOG_LSA > map;	/* 키 해시 -> 최신 커밋 LSA */
  LOG_LSA history_start;	/* clear 로 evict 된 키의 보수적 부모 LSA */
  pthread_mutex_t latch;	/* 전체 보호 */
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
