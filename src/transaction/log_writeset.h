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

/* FNV-1a hash of ONE key = class_oid (8 bytes) + its packed key value. Computed once per key, so a
 * modified row yields a separate hash for the PK and for each UNIQUE key (all pushed into ws_hashes). */
#ifndef _LOG_WRITESET_HASH_DEFINED_
#define _LOG_WRITESET_HASH_DEFINED_
typedef UINT64 LOG_WRITESET_HASH;
#endif /* _LOG_WRITESET_HASH_DEFINED_ */

/* per-transaction distinct-key limit and global history capacity.
 *
 * [최종(운영) 목표값] MySQL 방식(하나의 큰 history)을 따라 두 상수 모두 1000만(10,000,000)으로 갈 예정이다.
 *   아래 #define 값은 그 최종값이 아니라 PoC 검증용으로 낮춘 값이다 — "10개 트랜잭션 × 트랜잭션당 10만건"
 *   시나리오를 기준으로, 그 워크로드에서 병렬이 성립하는 선에 맞춰 정했다(선정 사유는 아래에 그대로 유지).
 *
 * writeset PoC 검증용 상수 (2026-07-08 원인분석 리포트 참조). 두 값은 독립이며 서로 다른 병목이다.
 * 검증 워크로드: 10테이블 각각 10만행을 단일 트랜잭션으로 INSERT, 이후 동일 구조로 UPDATE(전행).
 *
 *   [TX_LIMIT = 25만] per-tx distinct-key 상한. ws_hashes.size() 가 이 값 이상이면 그 트랜잭션의
 *     writeset 을 통째 버리고 commit-order 로 격하한다(ws_overflow). UPDATE 는 행당 old+new = 2 해시라
 *     10만행 = 20만 해시. 11만이면 매 UPDATE 가 overflow -> dependency_seq = prev_commit(직전 커밋)로
 *     격하되고, prev_commit 은 커밋마다 전진하므로 U(n) 이 U(n-1) 에 사슬로 묶여 슬레이브가 계단식
 *     직렬 적용을 한다. 25만으로 올려 20만 을 수용 -> overflow 탈출 -> UPDATE 도 병렬.
 *     (INSERT 는 행당 1 해시 = 10만 < 25만, 원래 overflow 없음.)
 *
 *   [HISTORY_CAP = 200만] 전역 히스토리 상한. commit_flush 의 `map.size()+ws_hashes.size() > CAP`
 *     이면 map.clear() + history_start 점프(베이스라인 재설정) -> 이후 독립 tx 도 그 커밋에 묶인다.
 *     이 워크로드 피크: INSERT 후 map=100만(10테이블x10만). UPDATE 는 PK 불변이라 old/new 해시가
 *     INSERT 와 동일 -> map[h] 를 "갱신(overwrite)"만 하고 신규 추가 0 -> map 은 100만에서 불변.
 *     따라서 per-flush 체크 = 100만 + 20만 = 120만 < 200만 -> clear/재설정 미발생. 각 UPDATE 의 probe
 *     는 자기 행의 INSERT commit 을 읽어 dependency_seq = 자기 INSERT(이미 적용됨)로 잡히므로 배리어
 *     없이 완전 병렬. 메모리 ~110MB.
 *
 * 범용 해법 아님(운영은 워크로드 변경행수 기반 적응적 사이징 필요). MySQL 은 이 둘을 한 상수
 * (binlog_transaction_dependency_history_size)로 공유하나(add_write_set 의 per-tx 체크와
 * get_dependency 의 history 체크가 같은 값), 여기선 분리해 per-tx 만 키워 UPDATE 를 병렬화한다. */
#define LOG_WRITESET_TX_LIMIT     250000
#define LOG_WRITESET_HISTORY_CAP  2000000

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
