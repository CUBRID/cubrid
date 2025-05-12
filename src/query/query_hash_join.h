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
 * query_hash_join.h
 */

#ifndef _QUERY_HASH_JOIN_H_
#define _QUERY_HASH_JOIN_H_

#include "query_hash_scan.h"	/* HASH_METHOD */
#if defined(WINDOWS)
#include <winsock2.h>		/* struct timeval */
#else /* WINDOWS */
#include <sys/time.h>		/* struct timeval */
#endif /* WINDOWS */
#include "system.h"		/* UINT32, UINT64 */
#if defined (SERVER_MODE) || defined (SA_MODE)
#include "thread_entry.hpp"	/* THREAD_ENTRY */
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

/*
 * Debug Macros
 */

#define HASHJOIN_PROFILE_TIME 0
#define HASHJOIN_DUMP_PARTITION 0
#define HASHJOIN_DUMP_HASH_TABLE 0
#define HASHJOIN_DUMP_BUILD 0
#define HASHJOIN_DUMP_PROBE 0

/*
 * Forward Declarations
 */

struct xasl_node;
typedef struct xasl_node XASL_NODE;

struct tp_domain;
typedef struct tp_domain TP_DOMAIN;

/*
 * Struct & Typedef Definitions
 */

typedef struct hashjoin_input
{
  XASL_NODE *xasl;

  /* For evaluating during-join predicates. */
  REGU_VARIABLE_LIST regu_list_pred;
} HASHJOIN_INPUT;

typedef struct hashjoin_input_domain_info
{
  TP_DOMAIN **domains;
  int *value_indexes;
} HASHJOIN_INPUT_DOMAIN_INFO;

typedef struct hashjoin_domain_info
{
  HASHJOIN_INPUT_DOMAIN_INFO outer;
  HASHJOIN_INPUT_DOMAIN_INFO inner;

  /* Common domains of build and probe inputs. */
  TP_DOMAIN **coerce_domains;

  /* Whether to use the coerce domain. */
  bool need_coerce_domains;
} HASHJOIN_DOMAIN_INFO;

#if defined (SERVER_MODE) || defined (SA_MODE)

typedef struct hashjoin_common_stats
{
  struct timeval elapsed_time;
  struct timeval time;
  UINT64 fetches;
  UINT64 fetch_time;
  UINT64 ioreads;
  UINT64 part_rows;
  UINT64 readkeys;
  UINT64 rows;
  UINT64 max_collisions;
  double skew;
} HASHJOIN_INPUT_STATS;
#define HASHJOIN_INPUT_STATS_INITIALIZER { { 0 }, { 0 }, 0, 0, 0, 0, 0, 0, 0, 0 }

#if HASHJOIN_PROFILE_TIME
typedef struct hashjoin_profile_stats
{
  struct
  {
    struct timeval fetch;	/* hjoin_fetch_key */
    struct timeval hash;	/* qdata_hash_scan_key */
    struct timeval insert;	/* hjoin_build_key */
  } build;

  struct
  {
    struct timeval fetch;	/* hjoin_fetch_key */
    struct timeval hash;	/* qdata_hash_scan_key */
    struct timeval search;	/* hjoin_probe_key */
    struct timeval match;	/* hjoin_fetch_key */
    struct timeval add;		/* hjoin_merge_tuple_to_list_id */
  } probe;
} HASHJOIN_PROFILE_STATS;
#endif /* HASHJOIN_PROFILE_TIME */

typedef struct hashjoin_stats
{
  HASH_METHOD hash_method;
  bool is_build_outer;

  HASHJOIN_INPUT_STATS outer;
  HASHJOIN_INPUT_STATS inner;
  HASHJOIN_INPUT_STATS build;
  HASHJOIN_INPUT_STATS probe;

#if HASHJOIN_PROFILE_TIME
  HASHJOIN_PROFILE_STATS profile;
#endif				/* HASHJOIN_PROFILE_TIME */
} HASHJOIN_STATS;

typedef struct hashjoin_stats_group
{
  HASHJOIN_STATS stats;
  HASHJOIN_STATS *context_stats;
  int context_cnt;
} HASHJOIN_STATS_GROUP;

/*
 * Function Declarations
 */

int qexec_hash_join (THREAD_ENTRY * thread_p, XASL_NODE * xasl, QUERY_ID query_id, VAL_DESCR * vd);

#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

#endif /* _QUERY_HASH_JOIN_H_ */
