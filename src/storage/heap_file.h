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
 * heap_file.h: Heap file object manager (at Server)
 */

#ifndef _HEAP_FILE_H_
#define _HEAP_FILE_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "storage_common.h"
#include "locator.h"
#include "file_manager.h"
#include "disk_manager.h"
#include "slotted_page.h"
#include "oid.h"
#include "object_representation_sr.h"
#include "thread.h"
#include "system_catalog.h"

#define HFID_EQ(hfid_ptr1, hfid_ptr2) \
  ((hfid_ptr1) == (hfid_ptr2) || \
   ((hfid_ptr1)->hpgid == (hfid_ptr2)->hpgid && \
    VFID_EQ(&((hfid_ptr1)->vfid), &((hfid_ptr2)->vfid))))

#define HEAP_SET_RECORD(recdes, record_area_size, record_length, record_type, \
			record_data)	\
  do  {	\
  (recdes)->area_size = record_area_size;  \
  (recdes)->length    = record_length;  \
  (recdes)->type      = record_type; \
  (recdes)->data      = (char *)record_data; \
  }while(0)

#define HEAP_HEADER_AND_CHAIN_SLOTID  0	/* Slot for chain and header */

#define HEAP_MAX_ALIGN INT_ALIGNMENT	/* maximum alignment for heap record */

/*
 * Heap scan structures
 */

/* Heap MVCC delete informations. This structure keep informations about
 * the current row. 
 */
typedef struct heap_mvcc_delete_info HEAP_MVCC_DELETE_INFO;
struct heap_mvcc_delete_info
{
  MVCCID row_delid;		/* row delete id */
  OID next_row_version;		/* next row version */
  MVCC_SATISFIES_DELETE_RESULT satisfies_delete_result;	/* can delete row? */
};

typedef struct heap_bestspace HEAP_BESTSPACE;
struct heap_bestspace
{
  VPID vpid;			/* Vpid of one of the best pages */
  int freespace;		/* Estimated free space in this page */
};


typedef struct heap_scancache HEAP_SCANCACHE;
struct heap_scancache
{				/* Define a scan over the whole heap file  */
  int debug_initpattern;	/* A pattern which indicates that the
				 * structure has been initialized
				 */
  HFID hfid;			/* Heap file of scan                   */
  OID class_oid;		/* Class oid of scanned instances       */
  int unfill_space;		/* Stop inserting when page has run
				 * below this value. Remaining space
				 * is left for updates. This is used only
				 * for insertions.
				 */
  LOCK page_latch;		/* Indicates the latch/lock to be acquired
				 * on heap pages. Its value may be
				 * NULL_LOCK when it is secure to skip
				 * lock on heap pages. For example, the class
				 * of the heap has been locked with either
				 * S_LOCK, SIX_LOCK, or X_LOCK
				 */
  int cache_last_fix_page;	/* Indicates if page buffers and memory
				 * are cached (left fixed)
				 */
  PAGE_PTR pgptr;		/* Page pointer to last left fixed page */
  char *area;			/* Pointer to last left fixed memory
				 * allocated
				 */
  int area_size;		/* Size of allocated area               */
  int known_nrecs;		/* Cached number of records (known)     */
  int known_nbest;		/* Cached number of best pages (Known)  */
  int known_nother_best;	/* Cached number of other best pages    */
  VPID collect_nxvpid;		/* Next page where statistics are used  */
  int collect_npages;		/* Total number of pages that were
				 * scanned
				 */
  int collect_nrecs;		/* Total num of rec on scanned pages    */
  float collect_recs_sumlen;	/* Total len of recs on scanned pages   */
  int collect_nother_best;	/* Total num of best pages not collected
				 * in collect_best array
				 */
  int collect_nbest;		/* Total of best pages that were found
				 * by scanning the heap. This is a hint
				 */
  int collect_maxbest;		/* Maximum num of elements in collect_best
				 * array
				 */
  HEAP_BESTSPACE *collect_best;	/* Best pages that were found during the
				 * scan
				 */
  int num_btids;		/* Total number of indexes defined
				 * on the scanning class
				 */
  BTREE_UNIQUE_STATS *index_stat_info;	/* unique-related stat info
					 * <btid,num_nulls,num_keys,num_oids>
					 */
  int scanid_bit;
  FILE_TYPE file_type;		/* The file type of the heap file being
				 * scanned. Can be FILE_HEAP or
				 * FILE_HEAP_REUSE_SLOTS
				 */
  FILE_IS_NEW_FILE is_new_file;	/* check for bestspace cache
				 * and logging of new file
				 */
  MVCC_SNAPSHOT *mvcc_snapshot;	/* mvcc snapshot */
};

typedef struct heap_scanrange HEAP_SCANRANGE;
struct heap_scanrange
{				/* Define a scanrange over a set of objects resident on the
				 * same page. It can be used for evaluation of nested joins
				 */
  OID first_oid;		/* First OID in scan range object                */
  OID last_oid;			/* Last OID in scan range object                 */
  HEAP_SCANCACHE scan_cache;	/* Current cached information from previous scan */
};

typedef enum
{
  HEAP_READ_ATTRVALUE,
  HEAP_WRITTEN_ATTRVALUE,
  HEAP_UNINIT_ATTRVALUE,
  HEAP_WRITTEN_LOB_ATTRVALUE
} HEAP_ATTRVALUE_STATE;

typedef enum
{
  HEAP_INSTANCE_ATTR,
  HEAP_SHARED_ATTR,
  HEAP_CLASS_ATTR
} HEAP_ATTR_TYPE;

typedef struct heap_attrvalue HEAP_ATTRVALUE;
struct heap_attrvalue
{
  ATTR_ID attrid;		/* attribute identifier                       */
  HEAP_ATTRVALUE_STATE state;	/* State of the attribute value. Either of
				 * has been read, has been updated, or is
				 * unitialized
				 */
  int do_increment;
  HEAP_ATTR_TYPE attr_type;	/* Instance, class, or shared attribute */
  OR_ATTRIBUTE *last_attrepr;	/* Used for default values                    */
  OR_ATTRIBUTE *read_attrepr;	/* Pointer to a desired attribute information */
  DB_VALUE dbvalue;		/* DB values of the attribute in memory       */
};

typedef struct heap_cache_attrinfo HEAP_CACHE_ATTRINFO;
struct heap_cache_attrinfo
{
  OID class_oid;		/* Class object identifier               */
  int last_cacheindex;		/* An index identifier when the
				 * last_classrepr was obtained from the
				 * classrepr cache. Otherwise, -1
				 */
  int read_cacheindex;		/* An index identifier when the
				 * read_classrepr was obtained from the
				 * classrepr cache. Otherwise, -1
				 */
  OR_CLASSREP *last_classrepr;	/* Currently cached catalog attribute
				 * info.
				 */
  OR_CLASSREP *read_classrepr;	/* Currently cached catalog attribute
				 * info.
				 */
  OID inst_oid;			/* Instance Object identifier            */
  int inst_chn;			/* Current chn of instance object        */
  int num_values;		/* Number of desired attribute values    */
  HEAP_ATTRVALUE *values;	/* Value for the attributes              */
};

typedef struct function_index_info FUNCTION_INDEX_INFO;
struct function_index_info
{
  char *expr_stream;
  int expr_stream_size;
  int col_id;
  int attr_index_start;
  void *expr;
};

typedef struct func_pred_unpack_info FUNC_PRED_UNPACK_INFO;
struct func_pred_unpack_info
{
  void *func_pred;
  void *unpack_info;
};

#if 0				/* TODO: check not use - ksseo */
typedef struct heap_spacecache HEAP_SPACECACHE;
struct heap_spacecache
{				/* Define an alter space cache for heap file  */

  float remain_sumlen;		/* Total new length of records that it
				 * is predicted for the rest of space
				 * cache. If it is unknown -1 is stored.
				 * This value is used to estimate the
				 * number of pages to allocate at a
				 * particular time in space cache.
				 * If the value is < pagesize, only one
				 * page at a time is allocated.
				 */
};
#endif

typedef struct heap_idx_elements_info HEAP_IDX_ELEMENTS_INFO;
struct heap_idx_elements_info
{
  int num_btids;		/* class has # of btids          */
  int has_single_col;		/* class has single column index */
  int has_multi_col;		/* class has multi-column index  */
};

typedef enum heap_update_style HEAP_UPDATE_STYLE;
enum heap_update_style
{
  HEAP_UPDATE_MVCC_STYLE = 0,	/* MVCC update style */
  HEAP_UPDATE_IN_PLACE = 1	/* non-MVCC in-place update style */
};

enum
{ END_SCAN, CONTINUE_SCAN };

enum
{ DONT_NEED_SNAPSHOT, NEED_SNAPSHOT };

extern int heap_classrepr_decache (THREAD_ENTRY * thread_p,
				   const OID * class_oid);
#ifdef DEBUG_CLASSREPR_CACHE
extern int heap_classrepr_dump_anyfixed (void);
#endif /* DEBUG_CLASSREPR_CACHE */
extern int heap_manager_initialize (void);
extern int heap_manager_finalize (void);
extern int heap_assign_address (THREAD_ENTRY * thread_p, const HFID * hfid,
				OID * oid, int expected_length);
extern int heap_assign_address_with_class_oid (THREAD_ENTRY * thread_p,
					       const HFID * hfid,
					       OID * class_oid, OID * oid,
					       int expected_length);
extern OID *heap_insert (THREAD_ENTRY * thread_p, const HFID * hfid,
			 OID * class_oid, OID * oid, RECDES * recdes,
			 HEAP_SCANCACHE * scan_cache);
extern const OID *heap_update (THREAD_ENTRY * thread_p, const HFID * hfid,
			       const OID * class_oid, const OID * oid,
			       RECDES * recdes, OID * new_oid,
			       bool * old, HEAP_SCANCACHE * scan_cache,
			       HEAP_UPDATE_STYLE update_in_place);
extern const OID *heap_mvcc_update_to_row_version (THREAD_ENTRY * thread_p,
						   const HFID * hfid,
						   const OID * class_oid,
						   const OID * oid,
						   OID * new_oid,
						   HEAP_SCANCACHE *
						   scan_cache);
extern const OID *heap_delete (THREAD_ENTRY * thread_p, const HFID * hfid,
			       const OID * class_oid, const OID * oid,
			       HEAP_SCANCACHE * scan_cache);
extern const OID *heap_ovf_delete (THREAD_ENTRY * thread_p, const HFID * hfid,
				   const OID * ovf_oid);
extern void heap_flush (THREAD_ENTRY * thread_p, const OID * oid);
extern int xheap_reclaim_addresses (THREAD_ENTRY * thread_p,
				    const HFID * hfid);
extern int heap_scancache_start (THREAD_ENTRY * thread_p,
				 HEAP_SCANCACHE * scan_cache,
				 const HFID * hfid, const OID * class_oid,
				 int cache_last_fix_page, int is_indexscan,
				 MVCC_SNAPSHOT * mvcc_snapshot);
extern int heap_scancache_start_modify (THREAD_ENTRY * thread_p,
					HEAP_SCANCACHE * scan_cache,
					const HFID * hfid,
					const OID * class_oid, int op_type,
					MVCC_SNAPSHOT * mvcc_snapshot);
extern int heap_scancache_quick_start (HEAP_SCANCACHE * scan_cache);
extern int heap_scancache_quick_start_modify (HEAP_SCANCACHE * scan_cache);
extern int heap_scancache_end (THREAD_ENTRY * thread_p,
			       HEAP_SCANCACHE * scan_cache);
extern int heap_scancache_end_when_scan_will_resume (THREAD_ENTRY * thread_p,
						     HEAP_SCANCACHE *
						     scan_cache);
extern void heap_scancache_end_modify (THREAD_ENTRY * thread_p,
				       HEAP_SCANCACHE * scan_cache);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int heap_hint_expected_num_objects (THREAD_ENTRY * thread_p,
					   HEAP_SCANCACHE * scan_cache,
					   int nobjs, int avg_objsize);
extern int heap_get_chn (THREAD_ENTRY * thread_p, const OID * oid);
#endif
extern SCAN_CODE heap_get (THREAD_ENTRY * thread_p, const OID * oid,
			   RECDES * recdes, HEAP_SCANCACHE * scan_cache,
			   int ispeeking, int chn);
extern SCAN_CODE heap_get_last (THREAD_ENTRY * thread_p, OID * oid,
				RECDES * recdes, HEAP_SCANCACHE * scan_cache,
				int ispeeking, int old_chn,
				OID * updated_oid);
extern SCAN_CODE heap_get_with_class_oid (THREAD_ENTRY * thread_p,
					  OID * class_oid, const OID * oid,
					  RECDES * recdes,
					  HEAP_SCANCACHE * scan_cache,
					  int ispeeking);
extern SCAN_CODE heap_next (THREAD_ENTRY * thread_p, const HFID * hfid,
			    OID * class_oid, OID * next_oid, RECDES * recdes,
			    HEAP_SCANCACHE * scan_cache, int ispeeking);
extern SCAN_CODE heap_next_record_info (THREAD_ENTRY * thread_p,
					const HFID * hfid, OID * class_oid,
					OID * next_oid, RECDES * recdes,
					HEAP_SCANCACHE * scan_cache,
					int ispeeking,
					DB_VALUE ** cache_recordinfo);
extern SCAN_CODE heap_prev (THREAD_ENTRY * thread_p, const HFID * hfid,
			    OID * class_oid, OID * prev_oid, RECDES * recdes,
			    HEAP_SCANCACHE * scan_cache, int ispeeking);
extern SCAN_CODE heap_prev_record_info (THREAD_ENTRY * thread_p,
					const HFID * hfid, OID * class_oid,
					OID * next_oid, RECDES * recdes,
					HEAP_SCANCACHE * scan_cache,
					int ispeeking,
					DB_VALUE ** cache_recordinfo);
extern SCAN_CODE heap_first (THREAD_ENTRY * thread_p, const HFID * hfid,
			     OID * class_oid, OID * oid, RECDES * recdes,
			     HEAP_SCANCACHE * scan_cache, int ispeeking);
extern SCAN_CODE heap_last (THREAD_ENTRY * thread_p, const HFID * hfid,
			    OID * class_oid, OID * oid, RECDES * recdes,
			    HEAP_SCANCACHE * scan_cache, int ispeeking);
extern int heap_get_alloc (THREAD_ENTRY * thread_p, const OID * oid,
			   RECDES * recdes);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int heap_cmp (THREAD_ENTRY * thread_p, const OID * oid,
		     RECDES * recdes);
#endif
extern int heap_scanrange_start (THREAD_ENTRY * thread_p,
				 HEAP_SCANRANGE * scan_range,
				 const HFID * hfid, const OID * class_oid,
				 MVCC_SNAPSHOT * mvcc_snapshot);
extern void heap_scanrange_end (THREAD_ENTRY * thread_p,
				HEAP_SCANRANGE * scan_range);
extern SCAN_CODE heap_scanrange_to_following (THREAD_ENTRY * thread_p,
					      HEAP_SCANRANGE * scan_range,
					      OID * start_oid);
extern SCAN_CODE heap_scanrange_to_prior (THREAD_ENTRY * thread_p,
					  HEAP_SCANRANGE * scan_range,
					  OID * last_oid);
extern SCAN_CODE heap_scanrange_next (THREAD_ENTRY * thread_p, OID * next_oid,
				      RECDES * recdes,
				      HEAP_SCANRANGE * scan_range,
				      int ispeeking);
extern SCAN_CODE heap_scanrange_prev (THREAD_ENTRY * thread_p, OID * prev_oid,
				      RECDES * recdes,
				      HEAP_SCANRANGE * scan_range,
				      int ispeeking);
extern SCAN_CODE heap_scanrange_first (THREAD_ENTRY * thread_p,
				       OID * first_oid, RECDES * recdes,
				       HEAP_SCANRANGE * scan_range,
				       int ispeeking);
extern SCAN_CODE heap_scanrange_last (THREAD_ENTRY * thread_p, OID * last_oid,
				      RECDES * recdes,
				      HEAP_SCANRANGE * scan_range,
				      int ispeeking);

extern bool heap_does_exist (THREAD_ENTRY * thread_p, OID * class_oid,
			     const OID * oid);
extern int heap_get_num_objects (THREAD_ENTRY * thread_p, const HFID * hfid,
				 int *npages, int *nobjs, int *avg_length);

extern int heap_estimate (THREAD_ENTRY * thread_p, const HFID * hfid,
			  int *npages, int *nobjs, int *avg_length);
extern int heap_estimate_num_objects (THREAD_ENTRY * thread_p,
				      const HFID * hfid);
extern INT32 heap_estimate_num_pages_needed (THREAD_ENTRY * thread_p,
					     int total_nobjs,
					     int avg_obj_size, int num_attrs,
					     int num_var_attrs);

extern OID *heap_get_class_oid (THREAD_ENTRY * thread_p, OID * class_oid,
				const OID * oid, bool need_snapshot);
extern char *heap_get_class_name (THREAD_ENTRY * thread_p,
				  const OID * class_oid);
extern char *heap_get_class_name_alloc_if_diff (THREAD_ENTRY * thread_p,
						const OID * class_oid,
						char *guess_classname);
extern char *heap_get_class_name_of_instance (THREAD_ENTRY * thread_p,
					      const OID * inst_oid);
extern int heap_get_class_partitions (THREAD_ENTRY * thread_p,
				      const OID * class_oid,
				      OR_PARTITION ** parts,
				      int *parts_count);
extern void heap_clear_partition_info (THREAD_ENTRY * thread_p,
				       OR_PARTITION * parts, int parts_count);
extern int heap_get_class_supers (THREAD_ENTRY * thread_p,
				  const OID * class_oid, OID ** super_oids,
				  int *count);
#if defined (ENABLE_UNUSED_FUNCTION)
extern char *heap_get_class_name_with_is_class (THREAD_ENTRY * thread_p,
						const OID * oid,
						int *isclass);
#endif
extern int heap_attrinfo_start (THREAD_ENTRY * thread_p,
				const OID * class_oid,
				int requested_num_attrs,
				const ATTR_ID * attrid,
				HEAP_CACHE_ATTRINFO * attr_info);
extern void heap_attrinfo_end (THREAD_ENTRY * thread_p,
			       HEAP_CACHE_ATTRINFO * attr_info);
extern int heap_attrinfo_clear_dbvalues (HEAP_CACHE_ATTRINFO * attr_info);
extern int heap_attrinfo_read_dbvalues (THREAD_ENTRY * thread_p,
					const OID * inst_oid, RECDES * recdes,
					HEAP_SCANCACHE * scan_cache,
					HEAP_CACHE_ATTRINFO * attr_info);
extern int heap_attrinfo_read_dbvalues_without_oid (THREAD_ENTRY * thread_p,
						    RECDES * recdes,
						    HEAP_CACHE_ATTRINFO *
						    attr_info);
extern int heap_attrinfo_delete_lob (THREAD_ENTRY * thread_p, RECDES * recdes,
				     HEAP_CACHE_ATTRINFO * attr_info);
extern DB_VALUE *heap_attrinfo_access (ATTR_ID attrid,
				       HEAP_CACHE_ATTRINFO * attr_info);
extern int heap_attrinfo_set (const OID * inst_oid, ATTR_ID attrid,
			      DB_VALUE * attr_val,
			      HEAP_CACHE_ATTRINFO * attr_info);
extern SCAN_CODE heap_attrinfo_transform_to_disk (THREAD_ENTRY * thread_p,
						  HEAP_CACHE_ATTRINFO *
						  attr_info,
						  RECDES * old_recdes,
						  RECDES * new_recdes);
extern SCAN_CODE
heap_attrinfo_transform_to_disk_except_lob (THREAD_ENTRY * thread_p,
					    HEAP_CACHE_ATTRINFO * attr_info,
					    RECDES * old_recdes,
					    RECDES * new_recdes);

extern DB_VALUE *heap_attrinfo_generate_key (THREAD_ENTRY * thread_p,
					     int n_atts, int *att_ids,
					     int *atts_prefix_length,
					     HEAP_CACHE_ATTRINFO * attr_info,
					     RECDES * recdes,
					     DB_VALUE * dbvalue, char *buf,
					     FUNCTION_INDEX_INFO *
					     func_index_info);
extern int heap_attrinfo_start_with_index (THREAD_ENTRY * thread_p,
					   OID * class_oid,
					   RECDES * class_recdes,
					   HEAP_CACHE_ATTRINFO * attr_info,
					   HEAP_IDX_ELEMENTS_INFO * idx_info);
extern int heap_attrinfo_start_with_btid (THREAD_ENTRY * thread_p,
					  OID * class_oid, BTID * btid,
					  HEAP_CACHE_ATTRINFO * attr_info);

#if defined (ENABLE_UNUSED_FUNCTION)
extern DB_VALUE *heap_attrvalue_get_index (int value_index,
					   ATTR_ID * attrid, int *n_btids,
					   BTID ** btids,
					   HEAP_CACHE_ATTRINFO *
					   idx_attrinfo);
#endif
extern HEAP_ATTRVALUE *heap_attrvalue_locate (ATTR_ID attrid,
					      HEAP_CACHE_ATTRINFO *
					      attr_info);
extern OR_ATTRIBUTE *heap_locate_last_attrepr (ATTR_ID attrid,
					       HEAP_CACHE_ATTRINFO *
					       attr_info);
extern DB_VALUE *heap_attrvalue_get_key (THREAD_ENTRY * thread_p,
					 int btid_index,
					 HEAP_CACHE_ATTRINFO * idx_attrinfo,
					 RECDES * recdes, BTID * btid,
					 DB_VALUE * db_value, char *buf,
					 FUNC_PRED_UNPACK_INFO *
					 func_indx_preds);

extern BTID *heap_indexinfo_get_btid (int btid_index,
				      HEAP_CACHE_ATTRINFO * attrinfo);
extern int heap_indexinfo_get_num_attrs (int btid_index,
					 HEAP_CACHE_ATTRINFO * attrinfo);
extern int heap_indexinfo_get_attrids (int btid_index,
				       HEAP_CACHE_ATTRINFO * attrinfo,
				       ATTR_ID * attrids);
extern int heap_indexinfo_get_attrs_prefix_length (int btid_index,
						   HEAP_CACHE_ATTRINFO *
						   attrinfo,
						   int *attrs_prefix_length,
						   int
						   len_attrs_prefix_length);
extern int heap_get_index_with_name (THREAD_ENTRY * thread_p,
				     OID * class_oid, const char *index_name,
				     BTID * btid);
extern int heap_get_indexinfo_of_btid (THREAD_ENTRY * thread_p,
				       OID * class_oid, BTID * btid,
				       BTREE_TYPE * type, int *num_attrs,
				       ATTR_ID ** attr_ids,
				       int **attrs_prefix_length,
				       char **btnamepp,
				       int *func_index_col_id);
extern int heap_get_referenced_by (THREAD_ENTRY * thread_p, OID * class_oid,
				   const OID * obj_oid, RECDES * obj,
				   int *max_oid_cnt, OID ** oid_list);

extern int heap_prefetch (THREAD_ENTRY * thread_p, OID * class_oid,
			  const OID * oid, LC_COPYAREA_DESC * prefetch);
extern DISK_ISVALID heap_check_all_pages (THREAD_ENTRY * thread_p,
					  HFID * hfid);
extern DISK_ISVALID heap_check_heap_file (THREAD_ENTRY * thread_p,
					  HFID * hfid);
extern DISK_ISVALID heap_check_all_heaps (THREAD_ENTRY * thread_p);

extern int heap_chnguess_get (THREAD_ENTRY * thread_p, const OID * oid,
			      int tran_index);
extern int heap_chnguess_put (THREAD_ENTRY * thread_p, const OID * oid,
			      int tran_index, int chn);
extern void heap_chnguess_clear (THREAD_ENTRY * thread_p, int tran_index);

/* Misc */
extern int xheap_get_class_num_objects_pages (THREAD_ENTRY * thread_p,
					      const HFID * hfid,
					      int approximation, int *nobjs,
					      int *npages);

extern int xheap_has_instance (THREAD_ENTRY * thread_p, const HFID * hfid,
			       OID * class_oid, int has_visible_instance);

extern int heap_init_func_pred_unpack_info (THREAD_ENTRY * thread_p,
					    HEAP_CACHE_ATTRINFO * attr_info,
					    const OID * class_oid,
					    FUNC_PRED_UNPACK_INFO **
					    func_indx_preds);

extern void heap_free_func_pred_unpack_info (THREAD_ENTRY * thread_p,
					     int n_indexes,
					     FUNC_PRED_UNPACK_INFO *
					     func_indx_preds,
					     int *attr_info_started);

/* auto-increment */
extern int heap_set_autoincrement_value (THREAD_ENTRY * thread_p,
					 HEAP_CACHE_ATTRINFO * attr_info,
					 HEAP_SCANCACHE * scan_cache,
					 int *is_set);

extern void heap_dump (THREAD_ENTRY * thread_p, FILE * fp, HFID * hfid,
		       bool dump_records);
extern void heap_dump_all (THREAD_ENTRY * thread_p, FILE * fp,
			   bool dump_records);
extern void heap_attrinfo_dump (THREAD_ENTRY * thread_p, FILE * fp,
				HEAP_CACHE_ATTRINFO * attr_info,
				bool dump_schema);
#if defined (CUBRID_DEBUG)
extern void heap_chnguess_dump (FILE * fp);
#endif /* CUBRID_DEBUG */
extern void heap_dump_all_capacities (THREAD_ENTRY * thread_p, FILE * fp);

/* partition-support */
extern OR_CLASSREP *heap_classrepr_get (THREAD_ENTRY * thread_p,
					OID * class_oid,
					RECDES * class_recdes, REPR_ID reprid,
					int *idx_incache,
					bool use_last_reprid);
extern int heap_classrepr_free (OR_CLASSREP * classrep, int *idx_incache);
extern REPR_ID heap_get_class_repr_id (THREAD_ENTRY * thread_p,
				       OID * class_oid);
extern int heap_classrepr_find_index_id (OR_CLASSREP * classrepr,
					 BTID * btid);
extern int heap_attrinfo_set_uninitialized_global (THREAD_ENTRY * thread_p,
						   OID * inst_oid,
						   RECDES * recdes,
						   HEAP_CACHE_ATTRINFO *
						   attr_info);

/* Recovery functions */
extern int heap_rv_redo_newpage (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_redo_newpage_reuse_oid (THREAD_ENTRY * thread_p,
					   LOG_RCV * rcv);
extern int heap_rv_undoredo_pagehdr (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void heap_rv_dump_statistics (FILE * fp, int ignore_length,
				     void *data);
extern void heap_rv_dump_chain (FILE * fp, int ignore_length, void *data);
extern int heap_rv_undo_insert (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_redo_insert (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_mvcc_redo_insert (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_undo_delete (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_redo_delete (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_mvcc_undo_delete (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_mvcc_redo_delete (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_redo_delete_newhome (THREAD_ENTRY * thread_p,
					LOG_RCV * rcv);
extern int heap_rv_redo_mark_reusable_slot (THREAD_ENTRY * thread_p,
					    LOG_RCV * rcv);
extern int heap_rv_undoredo_update (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_undoredo_update_type (THREAD_ENTRY * thread_p,
					 LOG_RCV * rcv);
extern int heap_rv_redo_reuse_page (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern int heap_rv_redo_reuse_page_reuse_oid (THREAD_ENTRY * thread_p,
					      LOG_RCV * rcv);
extern void heap_rv_dump_reuse_page (FILE * fp, int ignore_length,
				     void *data);

extern int heap_get_hfid_from_class_oid (THREAD_ENTRY * thread_p,
					 const OID * class_oid, HFID * hfid);
extern int heap_compact_pages (THREAD_ENTRY * thread_p, OID * class_oid);

extern void heap_classrepr_dump_all (THREAD_ENTRY * thread_p, FILE * fp,
				     OID * class_oid);

extern int heap_get_btid_from_index_name (THREAD_ENTRY * thread_p,
					  const OID * p_class_oid,
					  const char *index_name,
					  BTID * p_found_btid);

extern int heap_object_upgrade_domain (THREAD_ENTRY * thread_p,
				       HEAP_SCANCACHE * upd_scancache,
				       HEAP_CACHE_ATTRINFO * attr_info,
				       OID * oid, const ATTR_ID att_id);

extern int heap_header_capacity_start_scan (THREAD_ENTRY * thread_p,
					    int show_type,
					    DB_VALUE ** arg_values,
					    int arg_cnt, void **ptr);
extern SCAN_CODE heap_header_next_scan (THREAD_ENTRY * thread_p, int cursor,
					DB_VALUE ** out_values, int out_cnt,
					void *ptr);
extern SCAN_CODE heap_capacity_next_scan (THREAD_ENTRY * thread_p, int cursor,
					  DB_VALUE ** out_values, int out_cnt,
					  void *ptr);
extern int heap_header_capacity_end_scan (THREAD_ENTRY * thread_p,
					  void **ptr);

extern SCAN_CODE heap_page_prev (THREAD_ENTRY * thread_p,
				 const OID * class_oid,
				 const HFID * hfid,
				 VPID * prev_vpid,
				 DB_VALUE ** cache_pageinfo);
extern SCAN_CODE heap_page_next (THREAD_ENTRY * thread_p,
				 const OID * class_oid,
				 const HFID * hfid,
				 VPID * next_vpid,
				 DB_VALUE ** cache_pageinfo);
extern int heap_vpid_next (const HFID * hfid, PAGE_PTR pgptr,
			   VPID * next_vpid);
extern int heap_vpid_prev (const HFID * hfid, PAGE_PTR pgptr,
			   VPID * prev_vpid);

extern int heap_get_pages_for_mvcc_chain_read (THREAD_ENTRY * thread_p,
					       const OID * oid,
					       PAGE_PTR * pgptr,
					       PAGE_PTR * forward_pgptr,
					       OID * forward_oid,
					       bool * ignore_record);
extern SCAN_CODE heap_get_mvcc_data (THREAD_ENTRY * thread_p, const OID * oid,
				     MVCC_REC_HEADER * mvcc_rec_header,
				     PAGE_PTR page_ptr,
				     PAGE_PTR ovf_first_page,
				     MVCC_SNAPSHOT * mvcc_snapshot);
extern int heap_mvcc_lock_chain_for_delete (THREAD_ENTRY * thread_p,
					    const HFID * hfid,
					    OID * oid, OID * class_oid,
					    HEAP_SCANCACHE * scan_p);
extern int heap_get_mvcc_rec_header_from_overflow (PAGE_PTR ovf_page,
						   MVCC_REC_HEADER *
						   mvcc_header,
						   RECDES * peek_recdes);
extern int heap_set_mvcc_rec_header_on_overflow (PAGE_PTR ovf_page,
						 MVCC_REC_HEADER *
						 mvcc_header);
extern OID *heap_get_serial_class_oid (THREAD_ENTRY * thread_p);
extern SCAN_CODE heap_mvcc_get_version_for_delete (THREAD_ENTRY * thread_p,
						   OID * oid, OID * class_oid,
						   RECDES * recdes,
						   HEAP_SCANCACHE *
						   scan_cache, int ispeeking,
						   void *mvcc_reev_data);
extern bool heap_is_mvcc_disabled_for_class (const OID * class_oid);
extern int heap_rv_mvcc_redo_delete_relocated (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern int heap_rv_mvcc_undo_delete_relocated (THREAD_ENTRY * thread_p,
					       LOG_RCV * rcv);
extern int heap_rv_mvcc_redo_delete_relocation (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv);
extern int heap_rv_mvcc_undo_delete_relocation (THREAD_ENTRY * thread_p,
						LOG_RCV * rcv);

extern bool heap_is_big_length (int length);
extern int heap_get_class_oid_from_page (THREAD_ENTRY * thread_p,
					 PAGE_PTR page_p, OID * class_oid);
extern bool heap_attrinfo_check_unique_index (THREAD_ENTRY * thread_p,
					      HEAP_CACHE_ATTRINFO * attr_info,
					      ATTR_ID * att_id, int n_att_id);
#endif /* _HEAP_FILE_H_ */
