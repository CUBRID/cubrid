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

#ifndef _PARTITION_H_
#define _PARTITION_H_

#include "thread.h"
#include "query_executor.h"

/* object for caching objects used in multi row modify statements for each
 * partition
 */
typedef struct pruning_scan_cache PRUNING_SCAN_CACHE;
struct pruning_scan_cache
{
  HEAP_SCANCACHE scan_cache;	/* cached partition heap info */
  bool is_scan_cache_started;	/* true if cache has been started */
  FUNC_PRED_UNPACK_INFO *func_index_pred;	/* an array of function indexes
						 * cached for repeated evaluation */
  int n_indexes;		/* number of indexes */
};

/* linked list for caching PRUNING_SCAN_CACHE objects in the pruning context
 */
typedef struct scancache_list SCANCACHE_LIST;
struct scancache_list
{
  SCANCACHE_LIST *next;
  PRUNING_SCAN_CACHE scan_cache;
};

typedef struct pruning_context PRUNING_CONTEXT;
struct pruning_context
{
  THREAD_ENTRY *thread_p;	/* current thread */
  OID root_oid;			/* OID of the root class */
  REPR_ID root_repr_id;		/* root class representation id */
  ACCESS_SPEC_TYPE *spec;	/* class to prune */
  VAL_DESCR *vd;		/* value descriptor */
  DB_PARTITION_TYPE partition_type;	/* hash, range, list */

  OR_PARTITION *partitions;	/* partitions array */
  OR_PARTITION *selected_partition;	/* if a request was made on a
					 * partition rather than on the root
					 * class, this holds the partition
					 * info */
  SCANCACHE_LIST *scan_cache_list;	/* caches for partitions affected
					 * by the query using this context */
  int count;			/* number of partitions */

  void *fp_cache_context;	/* unpacking info */
  FUNC_PRED *partition_pred;	/* partition predicate */
  int attr_position;		/* attribute position in index key */
  ATTR_ID attr_id;		/* id of the attribute which defines the
				 * partitions */
  HEAP_CACHE_ATTRINFO attr_info;	/* attribute info cache for the partition
					 * expression */
  int error_code;		/* error encountered during pruning */

  int pruning_type;		/* The type of the class for which this
				 * context was loaded. DB_PARTITIONED_CLASS,
				 * DB_PARTITION_CLASS
				 */
  bool is_attr_info_inited;
  bool is_from_cache;		/* true if this context is cached */
};

extern void partition_init_pruning_context (PRUNING_CONTEXT * pinfo);

extern int partition_load_pruning_context (THREAD_ENTRY * thread_p,
					   const OID * class_oid,
					   int pruning_type,
					   PRUNING_CONTEXT * pinfo);

extern void partition_clear_pruning_context (PRUNING_CONTEXT * pinfo);

extern int partition_cache_init (THREAD_ENTRY * thread_p);

extern void partition_cache_finalize (THREAD_ENTRY * thread_p);

extern void partition_decache_class (THREAD_ENTRY * thread_p,
				     const OID * class_oid);

extern PRUNING_SCAN_CACHE *partition_get_scancache (PRUNING_CONTEXT *
						    pcontext,
						    const OID *
						    partition_oid);

extern PRUNING_SCAN_CACHE *partition_new_scancache (PRUNING_CONTEXT *
						    pcontext);

extern int partition_prune_spec (THREAD_ENTRY * thread_p, VAL_DESCR * vd,
				 ACCESS_SPEC_TYPE * access_spec);

extern int partition_prune_insert (THREAD_ENTRY * thread_p,
				   const OID * class_oid, RECDES * recdes,
				   HEAP_SCANCACHE * scan_cache,
				   PRUNING_CONTEXT * pcontext, int op_type,
				   OID * pruned_class_oid, HFID * pruned_hfid,
				   OID * superclass_oid);

extern int partition_prune_update (THREAD_ENTRY * thread_p,
				   const OID * class_oid, RECDES * recdes,
				   PRUNING_CONTEXT * pcontext,
				   int pruning_type, OID * pruned_class_oid,
				   HFID * pruned_hfid, OID * superclass_oid);

extern int partition_prune_unique_btid (PRUNING_CONTEXT * pcontext,
					DB_VALUE * key, OID * class_oid,
					HFID * class_hfid, BTID * btid,
					bool * is_global_btid);

extern int partition_get_partition_oids (THREAD_ENTRY * thread_p,
					 const OID * class_oid,
					 OID ** partition_oids, int *count);

extern int partition_load_aggregate_helper (PRUNING_CONTEXT * pcontext,
					    ACCESS_SPEC_TYPE * spec,
					    int pruned_count,
					    BTID * root_btid,
					    HIERARCHY_AGGREGATE_HELPER *
					    helper);
extern int partition_is_global_index (THREAD_ENTRY * thread_p,
				      PRUNING_CONTEXT * contextp,
				      OID * class_oid, BTID * btid,
				      BTREE_TYPE * btree_typep,
				      int *is_global_index);
extern int partition_find_root_class_oid (THREAD_ENTRY * thread_p,
					  const OID * class_oid,
					  OID * super_oid);
#endif /* _PARTITION_H_ */
