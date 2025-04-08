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

#define HASH_JOIN_PROFILE_TIME 0
#define HASH_JOIN_DUMP_HASH_TABLE 0
#define HASH_JOIN_DUMP_PROBE 0
#define HASH_JOIN_DUMP_BUILD 0
#define HASH_JOIN_DUMP_PARTITION 0

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

typedef struct hashjoin_input HASHJOIN_INPUT;
typedef struct hashjoin_input HJ_INPUT;
struct hashjoin_input
{
  XASL_NODE *xasl;
  REGU_VARIABLE_LIST regu_list_pred;
};

typedef struct hashjoin_input_domain_info HASHJOIN_INPUT_DOMAIN_INFO;
typedef struct hashjoin_input_domain_info HJ_INPUT_DOMAIN_INFO;
struct hashjoin_input_domain_info
{
  TP_DOMAIN **domains;
  int *value_indexes;
};

typedef struct hashjoin_domain_info HASHJOIN_DOMAIN_INFO;
typedef struct hashjoin_domain_info HJ_DOMAIN_INFO;
struct hashjoin_domain_info
{
  HJ_INPUT_DOMAIN_INFO outer;
  HJ_INPUT_DOMAIN_INFO inner;

  /* The common domains between the domains of values used in the build and probe inputs. */
  TP_DOMAIN **coerce_domains;

  /* Whether there is a need to use the coerce domain. */
  bool need_coerce_domains;
};

#if defined (SERVER_MODE) || defined (SA_MODE)

typedef struct hashjoin_stats HASHJOIN_STATS;
typedef struct hashjoin_stats HJ_STATS;
struct hashjoin_stats
{
  HASH_METHOD hash_method;
  bool is_build_outer;

  struct
  {
    struct timeval elapsed_time;
    UINT64 fetches;
    UINT64 fetch_time;
    UINT64 ioreads;
  } part;

  struct
  {
    struct timeval elapsed_time;
    struct timeval build_time;
    UINT64 fetches;
    UINT64 fetch_time;
    UINT64 ioreads;
    UINT64 rows;

#if HASH_JOIN_PROFILE_TIME
    struct
    {
      struct timeval fetch;	/* qexec_hash_join_fetch_key */
      struct timeval hash;	/* qdata_hash_scan_key */
      struct timeval insert;	/* qexec_hash_join_build_key */
    } profile;
#endif
  } build;

  struct
  {
    struct timeval elapsed_time;
    struct timeval probe_time;
    UINT64 fetches;
    UINT64 fetch_time;
    UINT64 ioreads;
    UINT64 readkeys;
    UINT64 rows;
    UINT32 max_collisions;

#if HASH_JOIN_PROFILE_TIME
    struct
    {
      struct timeval fetch;	/* qexec_hash_join_fetch_key */
      struct timeval hash;	/* qdata_hash_scan_key */
      struct timeval search;	/* qexec_hash_join_probe_key */
      struct timeval match;	/* qexec_hash_join_fetch_key */
      struct timeval add;	/* qexec_hash_join_merge_tuple_to_list_id */
    } profile;
#endif
  } probe;
};

typedef struct hashjoin_stats_group HASHJOIN_STATS_GROUP;
typedef struct hashjoin_stats_group HJ_STATS_GROUP;
struct hashjoin_stats_group
{
  HJ_STATS stats;
  HJ_STATS *context_stats;
  int context_cnt;
};

/*
 * Function Declarations
 */

int qexec_hash_join (THREAD_ENTRY *thread_p, XASL_NODE *xasl, QUERY_ID query_id, VAL_DESCR *vd);

#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

#endif /* _QUERY_HASH_JOIN_H_ */
