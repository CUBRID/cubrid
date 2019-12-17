/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
