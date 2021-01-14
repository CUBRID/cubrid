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
 * Filter predicate cache.
 */

#ifndef _FILTER_PRED_CACHE_H_
#define _FILTER_PRED_CACHE_H_

#ident "$Id$"

#include "storage_common.h"
#include "thread_compat.hpp"

#include <cstdio>

// forward definitions
struct or_predicate;
struct pred_expr_with_context;

extern int fpcache_initialize (THREAD_ENTRY * thread_p);
extern void fpcache_finalize (THREAD_ENTRY * thread_p);
extern int fpcache_claim (THREAD_ENTRY * thread_p, BTID * btid, or_predicate * or_pred,
			  pred_expr_with_context ** pred_expr);
extern int fpcache_retire (THREAD_ENTRY * thread_p, OID * class_oid, BTID * btid, pred_expr_with_context * filter_pred);
extern void fpcache_remove_by_class (THREAD_ENTRY * thread_p, const OID * class_oid);
extern void fpcache_drop_all (THREAD_ENTRY * thread_p);
extern void fpcache_dump (THREAD_ENTRY * thread_p, FILE * fp);

#endif /* _XASL_CACHE_H_ */
