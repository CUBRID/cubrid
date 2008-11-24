/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
#include "thread_impl.h"

extern bool locator_Dont_check_foreign_key;

extern EHID *locator_initialize (THREAD_ENTRY * thread_p,
				 EHID * classname_table);
extern void locator_finalize (THREAD_ENTRY * thread_p);
#if defined(SERVER_MODE)
extern LC_FIND_CLASSNAME
xlocator_reserve_class_name (THREAD_ENTRY * thread_p, const char *classname,
			     const OID * class_oid);
extern LC_FIND_CLASSNAME xlocator_delete_class_name (THREAD_ENTRY * thread_p,
						     const char *classname);
extern LC_FIND_CLASSNAME xlocator_rename_class_name (THREAD_ENTRY * thread_p,
						     const char *oldname,
						     const char *newname,
						     const OID * class_oid);
extern LC_FIND_CLASSNAME xlocator_find_class_oid (THREAD_ENTRY * thread_p,
						  const char *classname,
						  OID * class_oid, LOCK lock);
#endif /* SERVER_MODE */
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
#if defined(SERVER_MODE)
extern int xlocator_assign_oid (THREAD_ENTRY * thread_p, const HFID * hfid,
				OID * perm_oid, int expected_length,
				OID * class_oid, const char *classname);
extern int xlocator_fetch (THREAD_ENTRY * thrd,
			   OID * oid, int chn, LOCK lock,
			   OID * class_oid, int class_chn,
			   int prefetching, LC_COPYAREA ** fetch_area);
extern int xlocator_get_class (THREAD_ENTRY * thread_p, OID * class_oid,
			       int class_chn, const OID * oid, LOCK lock,
			       int prefetching, LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_all (THREAD_ENTRY * thread_p, const HFID * hfid,
			       LOCK * lock, OID * class_oid, int *nobjects,
			       int *nfetched, OID * last_oid,
			       LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_lockset (THREAD_ENTRY * thread_p,
				   LC_LOCKSET * lockset,
				   LC_COPYAREA ** fetch_area);
extern int xlocator_fetch_all_reference_lockset (THREAD_ENTRY * thread_p,
						 OID * oid, int chn,
						 OID * class_oid,
						 int class_chn, LOCK lock,
						 int quit_on_errors,
						 int prune_level,
						 LC_LOCKSET ** lockset,
						 LC_COPYAREA ** fetch_area);
#endif /* SERVER_MODE */

extern int
xlc_fetch_allrefslockset (OID * oid, int chn, OID * class_oid, int class_chn,
			  LOCK lock, int quit_on_errors, int prune_level,
			  LC_LOCKSET ** lockset, LC_COPYAREA ** fetch_area);
#if defined(SERVER_MODE)
extern LC_FIND_CLASSNAME
xlocator_find_lockhint_class_oids (THREAD_ENTRY * thread_p, int num_classes,
				   const char **many_classnames,
				   LOCK * many_locks,
				   int *many_need_subclasses,
				   OID * guessed_class_oids,
				   int *guessed_class_chns,
				   int quit_on_errors, LC_LOCKHINT ** hlock,
				   LC_COPYAREA ** fetch_area);
extern int xlocator_does_exist (THREAD_ENTRY * thread_p, OID * oid, int chn,
				LOCK lock, OID * class_oid, int class_chn,
				int need_fetching, int prefetching,
				LC_COPYAREA ** fetch_area);
extern int xlocator_force (THREAD_ENTRY * thread_p, LC_COPYAREA * copy_area);
#endif /* SERVER_MODE */
extern int
locator_start_force_scan_cache (THREAD_ENTRY * thread_p,
				HEAP_SCANCACHE * scan_cache,
				const HFID * hfid, const OID * class_oid,
				int op_type);
extern void locator_end_force_scan_cache (THREAD_ENTRY * thread_p,
					  HEAP_SCANCACHE * scan_cache);
extern int locator_attribute_info_force (THREAD_ENTRY * thread_p, HFID * hfid,
					 OID * oid,
					 HEAP_CACHE_ATTRINFO * attr_info,
					 ATTR_ID * att_id, int n_att_id,
					 LC_COPYAREA_OPERATION operation,
					 int op_type,
					 HEAP_SCANCACHE * scan_cache,
					 int *force_count, bool not_check_fk);
extern int locator_other_insert_delete (THREAD_ENTRY * thread_p, HFID * hfid,
					OID * oid, HFID * newhfid,
					OID * newoid,
					HEAP_CACHE_ATTRINFO * attr_info,
					HEAP_SCANCACHE * scan_cache,
					int *force_count, OID * prev_oid,
					REPR_ID * new_reprid);
#if defined(SERVER_MODE)
extern bool xlocator_notify_isolation_incons (THREAD_ENTRY * thread_p,
					      LC_COPYAREA ** synch_area);
#endif /* SERVER_MODE */
extern DISK_ISVALID locator_check_all_entries_of_all_btrees (THREAD_ENTRY *
							     thread_p,
							     bool repair);
extern DISK_ISVALID locator_check_btree_entries (THREAD_ENTRY * thread_p,
						 BTID * btid, HFID * hfid,
						 OID * class_oid,
						 int n_attr_ids,
						 ATTR_ID * attr_ids,
						 bool repair);
extern int locator_add_or_remove_index (THREAD_ENTRY * thread_p,
					RECDES * recdes, OID * inst_oid,
					OID * class_oid, int is_insert,
					int op_type,
					HEAP_SCANCACHE * scan_cache,
					bool datayn, bool replyn,
					HFID * hfid);
extern int locator_update_index (THREAD_ENTRY * thread_p, RECDES * new_recdes,
				 RECDES * old_recdes, ATTR_ID * att_id,
				 int n_att_id, OID * inst_oid,
				 OID * class_oid, int op_type,
				 HEAP_SCANCACHE * scan_cache,
				 bool data_update, bool replyn);
#if defined(SERVER_MODE)
extern int xlocator_remove_class_from_index (THREAD_ENTRY * thread_p,
					     OID * oid, BTID * btid,
					     HFID * hfid);
#endif /* SERVER_MODE */

#if defined(SERVER_MODE)
extern int xlocator_assign_oid_batch (THREAD_ENTRY * thread_p,
				      LC_OIDSET * oidset);
extern int xlocator_fetch_lockhint_classes (THREAD_ENTRY * thread_p,
					    LC_LOCKHINT * lockhint,
					    LC_COPYAREA ** fetch_area);
extern int xrepl_set_info (THREAD_ENTRY * thread_p, REPL_INFO * repl_info);
extern LOG_LSA *xrepl_log_get_append_lsa (void);
extern int
xlocator_build_fk_object_cache (THREAD_ENTRY * thread_p, OID * cls_oid,
				HFID * hfid, TP_DOMAIN * key_type,
				int n_attrs, int *attr_ids, OID * pk_cls_oid,
				BTID * pk_btid, int cache_attr_id,
				char *fk_name);
#endif /* SERVER_MODE */

#endif /* _LOCATOR_SR_H_ */
