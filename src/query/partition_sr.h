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
 * Partition pruning on server
 */

#ifndef _PARTITION_SR_H_
#define _PARTITION_SR_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "heap_file.h"
#include "thread_compat.hpp"

// forward definition
struct access_spec_node;
struct func_pred;
struct func_pred_unpack_info;
struct val_descr;
struct xasl_unpack_info;

// *INDENT-OFF*
namespace cubquery
{
  struct hierarchy_aggregate_helper;
}
using HIERARCHY_AGGREGATE_HELPER = cubquery::hierarchy_aggregate_helper;
// *INDENT-ON*

/* object for caching objects used in multi row modify statements for each partition */
typedef struct pruning_scan_cache PRUNING_SCAN_CACHE;
struct pruning_scan_cache
{
  HEAP_SCANCACHE scan_cache;	/* cached partition heap info */
  bool is_scan_cache_started;	/* true if cache has been started */
  func_pred_unpack_info *func_index_pred;	/* an array of function indexes cached for repeated evaluation */
  int n_indexes;		/* number of indexes */
};

/* linked list for caching PRUNING_SCAN_CACHE objects in the pruning context */
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
  access_spec_node *spec;	/* class to prune */
  val_descr *vd;		/* value descriptor */
  DB_PARTITION_TYPE partition_type;	/* hash, range, list */

  OR_PARTITION *partitions;	/* partitions array */
  OR_PARTITION *selected_partition;	/* if a request was made on a partition rather than on the root class, this
					 * holds the partition info */
  SCANCACHE_LIST *scan_cache_list;	/* caches for partitions affected by the query using this context */
  int count;			/* number of partitions */

  xasl_unpack_info *fp_cache_context;	/* unpacking info */
  func_pred *partition_pred;	/* partition predicate */
  int attr_position;		/* attribute position in index key */
  ATTR_ID attr_id;		/* id of the attribute which defines the partitions */
  HEAP_CACHE_ATTRINFO attr_info;	/* attribute info cache for the partition expression */
  int error_code;		/* error encountered during pruning */

  int pruning_type;		/* The type of the class for which this context was loaded. DB_PARTITIONED_CLASS,
				 * DB_PARTITION_CLASS */
  bool is_attr_info_inited;
  bool is_from_cache;		/* true if this context is cached */
};

#if defined(SUPPORT_KEY_DUP_LEVEL_CARDINALITY_IGNORE)
class CResvBtidMap
{
private:
  THREAD_ENTRY * m_thread;
  BTID *m_pbtid;
  int *m_ppos;
  int m_alloc_sz;
  int m_used_cnt;

public:
    CResvBtidMap (THREAD_ENTRY * thread_p);
   ~CResvBtidMap ();

  void clear ();
  bool add (BTID * btid, int pos);
  int find (BTID * btid);
};
#endif

extern void partition_init_pruning_context (PRUNING_CONTEXT * pinfo);

extern int partition_load_pruning_context (THREAD_ENTRY * thread_p, const OID * class_oid, int pruning_type,
					   PRUNING_CONTEXT * pinfo);

extern void partition_clear_pruning_context (PRUNING_CONTEXT * pinfo);

extern int partition_cache_init (THREAD_ENTRY * thread_p);

extern void partition_cache_finalize (THREAD_ENTRY * thread_p);

extern void partition_decache_class (THREAD_ENTRY * thread_p, const OID * class_oid);

extern PRUNING_SCAN_CACHE *partition_get_scancache (PRUNING_CONTEXT * pcontext, const OID * partition_oid);

extern PRUNING_SCAN_CACHE *partition_new_scancache (PRUNING_CONTEXT * pcontext);

extern int partition_prune_spec (THREAD_ENTRY * thread_p, val_descr * vd, access_spec_node * access_spec);

extern int partition_prune_insert (THREAD_ENTRY * thread_p, const OID * class_oid, RECDES * recdes,
				   HEAP_SCANCACHE * scan_cache, PRUNING_CONTEXT * pcontext, int op_type,
				   OID * pruned_class_oid, HFID * pruned_hfid, OID * superclass_oid);

extern int partition_prune_update (THREAD_ENTRY * thread_p, const OID * class_oid, RECDES * recdes,
				   PRUNING_CONTEXT * pcontext, int pruning_type, OID * pruned_class_oid,
				   HFID * pruned_hfid, OID * superclass_oid);

extern int partition_prune_unique_btid (PRUNING_CONTEXT * pcontext, DB_VALUE * key, OID * class_oid, HFID * class_hfid,
					BTID * btid);

extern int partition_get_partition_oids (THREAD_ENTRY * thread_p, const OID * class_oid, OID ** partition_oids,
					 int *count
#if defined(SUPPORT_KEY_DUP_LEVEL_CARDINALITY_IGNORE)
					 , CResvBtidMap * btid_pos_map
#endif
  );

extern int partition_load_aggregate_helper (PRUNING_CONTEXT * pcontext, access_spec_node * spec, int pruned_count,
					    BTID * root_btid, HIERARCHY_AGGREGATE_HELPER * helper);
#if 0
extern int partition_is_global_index (THREAD_ENTRY * thread_p, PRUNING_CONTEXT * contextp, OID * class_oid, BTID * btid,
				      BTREE_TYPE * btree_typep, int *is_global_index);
#endif

extern int partition_find_root_class_oid (THREAD_ENTRY * thread_p, const OID * class_oid, OID * super_oid);

extern int partition_prune_partition_index (PRUNING_CONTEXT * pcontext, DB_VALUE * key, OID * class_oid,
					    BTID * btid, int *position);

#endif /* _PARTITION_SR_H_ */
