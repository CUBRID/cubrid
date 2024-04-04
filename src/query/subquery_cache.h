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

extern int sq_cache_initialize (xasl_node * xasl);
extern int sq_put (xasl_node * xasl, REGU_VARIABLE * result);
extern bool sq_get (xasl_node * xasl, REGU_VARIABLE * retp);
extern void sq_cache_destroy (xasl_node * xasl);
extern int sq_check_enable (xasl_node * xasl);

#define SQ_CACHE_MIN_HIT_RATIO 9	/* it means 90% */

#define SQ_TYPE_XASL 0
#define SQ_TYPE_PRED 1
#define SQ_TYPE_REGU_VAR 2
#define SQ_TYPE_DBVAL 3

#endif /* _SUBQUERY_CACHE_H_ */
