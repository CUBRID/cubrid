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
 * subquery_cache.h - Correlated Scalar Subquery Result Cache.
 */

#ifndef _SUBQUERY_CACHE_H_
#define _SUBQUERY_CACHE_H_

#ident "$Id$"

typedef union sq_regu_value SQ_REGU_VALUE;
typedef struct sq_key SQ_KEY;
typedef struct sq_val SQ_VAL;

extern int sq_cache_initialize (THREAD_ENTRY * thread_p, xasl_node * xasl);
extern int sq_put (THREAD_ENTRY * thread_p, SQ_KEY * key, xasl_node * xasl, REGU_VARIABLE * result);
extern bool sq_get (THREAD_ENTRY * thread_p, SQ_KEY * key, xasl_node * xasl, REGU_VARIABLE * regu_var);
extern void sq_cache_destroy (xasl_node * xasl);
extern int sq_check_enable (THREAD_ENTRY * thread_p, xasl_node * xasl);
extern SQ_KEY *sq_make_key (THREAD_ENTRY * thread_p, xasl_node * xasl);
extern void sq_free_key (SQ_KEY * key);

#define SQ_CACHE_MIN_HIT_RATIO 9	/* it means 90% */

#define SQ_TYPE_XASL 0
#define SQ_TYPE_PRED 1
#define SQ_TYPE_REGU_VAR 2
#define SQ_TYPE_DBVAL 3


union sq_regu_value
{
  /* fields used by both XASL interpreter and regulator */
  DB_VALUE *dbvalptr;		/* for constant values */
  QFILE_SORTED_LIST_ID *srlist_id;	/* sorted list identifier for subquery results */
};

struct sq_key
{
  DB_VALUE *pred_set;
};

struct sq_val
{
  SQ_REGU_VALUE val;
  REGU_DATATYPE t;
};

#endif /* _SUBQUERY_CACHE_H_ */
