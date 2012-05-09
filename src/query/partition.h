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
 * Partition pruning on server
 */

#ifndef _PARTITION_SR_H_
#define _PARTITION_SR_H_

#include "thread.h"
#include "query_executor.h"

extern int partition_cache_init (THREAD_ENTRY * thread_p);

extern void partition_cache_finalize (THREAD_ENTRY * thread_p);

extern void partition_decache_class (THREAD_ENTRY * thread_p,
				     const OID * class_oid);

extern int partition_prune_spec (THREAD_ENTRY * thread_p, VAL_DESCR * vd,
				 ACCESS_SPEC_TYPE * access_spec);

extern int partition_prune_insert (THREAD_ENTRY * thread_p,
				   const OID * class_oid, RECDES * recdes,
				   HEAP_SCANCACHE * scan_cache,
				   OID * pruned_class_oid,
				   HFID * pruned_hfid);

extern int partition_prune_update (THREAD_ENTRY * thread_p,
				   const OID * class_oid, RECDES * recdes,
				   OID * pruned_class_oid,
				   HFID * pruned_hfid);

extern int partition_get_partitions (THREAD_ENTRY * thread_p,
				     const OID * root_oid,
				     OR_PARTITION ** partitions,
				     int *parts_count);

#endif /* _PARTITION_SR_H_ */
