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

#include "config.h"

#include "error_manager.h"
#include "oid.h"
#include "storage_common.h"
#include "locator.h"
#include "heap_file.h"
#include "lock_manager.h"
#include "extendible_hash.h"

#include "disk_manager.h"
#include "replication.h"
#include "thread.h"
#include "partition.h"

enum
{
  LOB_FLAG_EXCLUDE_LOB,
  LOB_FLAG_INCLUDE_LOB
};

extern bool locator_Dont_check_foreign_key;

extern EHID *locator_initialize (THREAD_ENTRY * thread_p,
				 EHID * classname_table);
extern void locator_finalize (THREAD_ENTRY * thread_p);
extern int
locator_drop_transient_class_name_entries (THREAD_ENTRY * thread_p,
					   int tran_index,
					   LOG_LSA * savep_lsa);
extern int locator_savepoint_transient_class_name_entries (THREAD_ENTRY *
							   thread_p,
							   int tran_index,
							   LOG_LSA *
							   savep_lsa);
extern DISK_ISVALID locator_check_class_names (THREAD_ENTRY * thread_p);
extern void locator_dump_class_names (THREAD_ENTRY * thread_p, FILE * out_fp);

extern int
xlc_fetch_allrefslockset (OID * oid, int chn, OID * class_oid, int class_chn,
			  LOCK lock, int quit_on_errors, int prune_level,
			  LC_LOCKSET ** lockset, LC_COPYAREA ** fetch_area);
extern int
locator_start_force_scan_cache (THREAD_ENTRY * thread_p,
				HEAP_SCANCACHE * scan_cache,
				const HFID * hfid, const OID * class_oid,
				int op_type);
extern void locator_end_force_scan_cache (THREAD_ENTRY * thread_p,
					  HEAP_SCANCACHE * scan_cache);
extern int locator_attribute_info_force (THREAD_ENTRY * thread_p,
					 const HFID * hfid, OID * oid,
					 BTID * btid,
					 bool btid_duplicate_key_locked,
					 HEAP_CACHE_ATTRINFO * attr_info,
					 ATTR_ID * att_id, int n_att_id,
					 LC_COPYAREA_OPERATION operation,
					 int op_type,
					 HEAP_SCANCACHE * scan_cache,
					 int *force_count, bool not_check_fk,
					 REPL_INFO_TYPE repl_info,
					 PRUNING_CONTEXT * pcontext,
					 FUNC_PRED_UNPACK_INFO * func_preds);
extern LC_COPYAREA *locator_allocate_copy_area_by_attr_info (THREAD_ENTRY *
							     thread_p,
							     HEAP_CACHE_ATTRINFO
							     * attr_info,
							     RECDES *
							     old_recdes,
							     RECDES *
							     new_recdes,
							     const int
							     copyarea_length_hint,
							     int
							     lob_create_flag);
extern int locator_other_insert_delete (THREAD_ENTRY * thread_p, HFID * hfid,
					OID * oid, BTID * btid,
					bool btid_dup_key_locked,
					HFID * newhfid, OID * newoid,
					HEAP_CACHE_ATTRINFO * attr_info,
					HEAP_SCANCACHE * scan_cache,
					int *force_count, OID * prev_oid,
					REPR_ID * new_reprid);
extern DISK_ISVALID locator_check_class (THREAD_ENTRY * thtread_p,
					 OID * class_oid, RECDES * peek,
					 HFID * class_hfid, bool repair);
extern DISK_ISVALID locator_check_by_class_oid (THREAD_ENTRY * thread_p,
						OID * cls_oid, HFID * hfid,
						bool repair);
extern DISK_ISVALID locator_check_all_entries_of_all_btrees (THREAD_ENTRY *
							     thread_p,
							     bool repair);
extern DISK_ISVALID locator_check_btree_entries (THREAD_ENTRY * thread_p,
						 BTID * btid, HFID * hfid,
						 OID * class_oid,
						 int n_attr_ids,
						 ATTR_ID * attr_ids,
						 int *atts_prefix_length,
						 const char *btname,
						 bool repair);
extern int locator_delete_force (THREAD_ENTRY * thread_p, HFID * hfid,
				 OID * oid, BTID * search_btid,
				 bool duplicate_key_locked,
				 int has_index, int op_type,
				 HEAP_SCANCACHE * scan_cache,
				 int *force_count);
extern int locator_add_or_remove_index (THREAD_ENTRY * thread_p,
					RECDES * recdes, OID * inst_oid,
					OID * class_oid, BTID * search_btid,
					bool duplicate_key_locked,
					int is_insert, int op_type,
					HEAP_SCANCACHE * scan_cache,
					bool datayn, bool replyn,
					HFID * hfid,
					FUNC_PRED_UNPACK_INFO * func_preds);
extern int locator_update_index (THREAD_ENTRY * thread_p, RECDES * new_recdes,
				 RECDES * old_recdes, ATTR_ID * att_id,
				 int n_att_id, OID * inst_oid,
				 OID * class_oid, BTID * search_btid,
				 bool duplicate_key_locked,
				 int op_type, HEAP_SCANCACHE * scan_cache,
				 bool data_update, bool replyn,
				 REPL_INFO_TYPE repl_info);
extern int locator_delete_lob_force (THREAD_ENTRY * thread_p, OID * class_oid,
				     OID * oid, RECDES * recdes);
extern PRUNING_SCAN_CACHE *locator_get_partition_scancache (PRUNING_CONTEXT *
							    pcontext,
							    const OID *
							    class_oid,
							    const HFID * hfid,
							    int op_type,
							    bool
							    has_function_index);
#endif /* _LOCATOR_SR_H_ */
