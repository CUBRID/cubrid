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
 * locator_sr.h - Server transaction locator (interface)
 */

#ifndef _LOCATOR_SR_H_
#define _LOCATOR_SR_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "config.h"
#include "disk_manager.h"
#include "error_manager.h"
#include "extendible_hash.h"
#include "heap_file.h"
#include "locator.h"
#include "lock_manager.h"
#include "oid.h"
#include "partition_sr.h"
#include "query_evaluator.h"
#include "replication.h"
#include "storage_common.h"
#include "thread_compat.hpp"

enum
{
  LOB_FLAG_EXCLUDE_LOB,
  LOB_FLAG_INCLUDE_LOB
};

extern bool locator_Dont_check_foreign_key;

extern int locator_initialize (THREAD_ENTRY * thread_p);
extern void locator_finalize (THREAD_ENTRY * thread_p);
extern int locator_drop_transient_class_name_entries (THREAD_ENTRY * thread_p, LOG_LSA * savep_lsa);
extern int locator_savepoint_transient_class_name_entries (THREAD_ENTRY * thread_p, LOG_LSA * savep_lsa);
extern DISK_ISVALID locator_check_class_names (THREAD_ENTRY * thread_p);
extern void locator_dump_class_names (THREAD_ENTRY * thread_p, FILE * out_fp);

extern int xlc_fetch_allrefslockset (OID * oid, int chn, OID * class_oid, int class_chn, LOCK lock, int quit_on_errors,
				     int prune_level, LC_LOCKSET ** lockset, LC_COPYAREA ** fetch_area);
extern int locator_start_force_scan_cache (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache, const HFID * hfid,
					   const OID * class_oid, int op_type);
extern void locator_end_force_scan_cache (THREAD_ENTRY * thread_p, HEAP_SCANCACHE * scan_cache);
extern int locator_attribute_info_force (THREAD_ENTRY * thread_p, const HFID * hfid, OID * oid,
					 HEAP_CACHE_ATTRINFO * attr_info, ATTR_ID * att_id, int n_att_id,
					 LC_COPYAREA_OPERATION operation, int op_type, HEAP_SCANCACHE * scan_cache,
					 int *force_count, bool not_check_fk, REPL_INFO_TYPE repl_info,
					 int pruning_type, PRUNING_CONTEXT * pcontext,
					 FUNC_PRED_UNPACK_INFO * func_preds, MVCC_REEV_DATA * mvcc_reev_data,
					 UPDATE_INPLACE_STYLE force_update_inplace, RECDES * rec_descriptor,
					 bool need_locking);
extern LC_COPYAREA *locator_allocate_copy_area_by_attr_info (THREAD_ENTRY * thread_p, HEAP_CACHE_ATTRINFO * attr_info,
							     RECDES * old_recdes, RECDES * new_recdes,
							     const int copyarea_length_hint, int lob_create_flag);
extern int locator_other_insert_delete (THREAD_ENTRY * thread_p, HFID * hfid, OID * oid, BTID * btid,
					bool btid_dup_key_locked, HFID * newhfid, OID * newoid,
					HEAP_CACHE_ATTRINFO * attr_info, HEAP_SCANCACHE * scan_cache, int *force_count,
					OID * prev_oid, REPR_ID * new_reprid);
extern DISK_ISVALID locator_check_class (THREAD_ENTRY * thtread_p, OID * class_oid, RECDES * peek, HFID * class_hfid,
					 BTID * index_btid, bool repair);
extern DISK_ISVALID locator_check_by_class_oid (THREAD_ENTRY * thread_p, OID * cls_oid, HFID * hfid, BTID * index_btid,
						bool repair);
extern DISK_ISVALID locator_check_all_entries_of_all_btrees (THREAD_ENTRY * thread_p, bool repair);
extern DISK_ISVALID locator_check_btree_entries (THREAD_ENTRY * thread_p, BTID * btid, HFID * hfid, OID * class_oid,
						 int n_attr_ids, ATTR_ID * attr_ids, int *atts_prefix_length,
						 const char *btname, bool repair);
extern int locator_delete_force (THREAD_ENTRY * thread_p, HFID * hfid, OID * oid, int has_index, int op_type,
				 HEAP_SCANCACHE * scan_cache, int *force_count, MVCC_REEV_DATA * mvcc_reev_data,
				 bool need_locking);
extern int locator_add_or_remove_index (THREAD_ENTRY * thread_p, RECDES * recdes, OID * inst_oid, OID * class_oid,
					int is_insert, int op_type, HEAP_SCANCACHE * scan_cache, bool datayn,
					bool replyn, HFID * hfid, FUNC_PRED_UNPACK_INFO * func_preds);
extern int locator_update_index (THREAD_ENTRY * thread_p, RECDES * new_recdes, RECDES * old_recdes, ATTR_ID * att_id,
				 int n_att_id, OID * oid, OID * class_oid, int op_type,
				 HEAP_SCANCACHE * scan_cache, REPL_INFO * repl_info);
extern int locator_delete_lob_force (THREAD_ENTRY * thread_p, OID * class_oid, OID * oid, RECDES * recdes);
extern PRUNING_SCAN_CACHE *locator_get_partition_scancache (PRUNING_CONTEXT * pcontext, const OID * class_oid,
							    const HFID * hfid, int op_type, bool has_function_index);
extern int xlocator_redistribute_partition_data (THREAD_ENTRY * thread_p, OID * class_oid, int no_oids, OID * oid_list);

extern int locator_rv_redo_rename (THREAD_ENTRY * thread_p, LOG_RCV * rcv);

extern SCAN_CODE locator_lock_and_get_object (THREAD_ENTRY * thread_p, const OID * oid, OID * class_oid,
					      RECDES * recdes, HEAP_SCANCACHE * scan_cache, LOCK lock, int ispeeking,
					      int old_chn, NON_EXISTENT_HANDLING non_ex_handling_type);
extern SCAN_CODE locator_lock_and_get_object_with_evaluation (THREAD_ENTRY * thread_p, OID * oid, OID * class_oid,
							      RECDES * recdes, HEAP_SCANCACHE * scan_cache,
							      int ispeeking, int old_chn,
							      MVCC_REEV_DATA * mvcc_reev_data,
							      NON_EXISTENT_HANDLING non_ex_handling_type);
extern SCAN_CODE locator_get_object (THREAD_ENTRY * thread_p, const OID * oid, OID * class_oid, RECDES * recdes,
				     HEAP_SCANCACHE * scan_cache, SCAN_OPERATION_TYPE op_type, LOCK lock_mode,
				     int ispeeking, int chn);
extern SCAN_OPERATION_TYPE locator_decide_operation_type (LOCK lock_mode, LC_FETCH_VERSION_TYPE fetch_version_type);
extern LOCK locator_get_lock_mode_from_op_type (SCAN_OPERATION_TYPE op_type);
#endif /* _LOCATOR_SR_H_ */
