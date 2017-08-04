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
 * recovery.c -
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "recovery.h"
#include "log_manager.h"
#include "replication.h"
#include "btree.h"
#include "btree_load.h"
#include "system_catalog.h"
#include "disk_manager.h"
#include "extendible_hash.h"
#include "file_manager.h"
#include "overflow_file.h"
#include "boot_sr.h"
#include "locator_sr.h"

/*
 *
 *    		 THE ARRAY OF SERVER RECOVERY FUNCTIONS
 *
 * Note: When adding new entries, be sure to add the an entry to print it as
 * a string in rv_rcvindex_string().
 */
struct rvfun RV_fun[] = {
  {RVDK_NEWVOL,
   "RVDK_NEWVOL",
   NULL,
   disk_rv_redo_dboutside_newvol,
   NULL,
   disk_rv_dump_hdr},
  {RVDK_FORMAT,
   "RVDK_FORMAT",
   disk_rv_undo_format,
   disk_rv_redo_format,
   log_rv_dump_char,
   disk_rv_dump_hdr},
  {RVDK_INITMAP,
   "RVDK_INITMAP",
   NULL,
   disk_rv_redo_init_map,
   NULL,
   disk_rv_dump_init_map},
  {RVDK_CHANGE_CREATION,
   "RVDK_CHANGE_CREATION",
   disk_rv_undoredo_set_creation_time,
   disk_rv_undoredo_set_creation_time,
   disk_rv_dump_set_creation_time,
   disk_rv_dump_set_creation_time},
  {RVDK_RESET_BOOT_HFID,
   "RVDK_RESET_BOOT_HFID",
   disk_rv_undoredo_set_boot_hfid,
   disk_rv_undoredo_set_boot_hfid,
   disk_rv_dump_set_boot_hfid,
   disk_rv_dump_set_boot_hfid},
  {RVDK_LINK_PERM_VOLEXT,
   "RVDK_LINK_PERM_VOLEXT",
   disk_rv_undoredo_link,
   disk_rv_undoredo_link,
   disk_rv_dump_link,
   disk_rv_dump_link},
  {RVDK_INIT_PAGES,
   "RVDK_INIT_PAGES",
   NULL,
   disk_rv_redo_dboutside_init_pages,
   NULL,
   disk_rv_dump_init_pages},
  {RVDK_RESERVE_SECTORS,
   "RVDK_RESERVE_SECTORS",
   disk_rv_unreserve_sectors,
   disk_rv_reserve_sectors,
   NULL,
   NULL},
  {RVDK_UNRESERVE_SECTORS,
   "RVDK_UNRESERVE_SECTORS",
   disk_rv_reserve_sectors,
   disk_rv_unreserve_sectors,
   NULL,
   NULL},
  {RVDK_VOLHEAD_EXPAND,
   "RVDK_VOLHEAD_EXPAND",
   NULL,
   disk_rv_volhead_extend_redo,
   NULL,
   NULL},

  {RVFL_DESTROY,
   "RVFL_DESTROY",
   file_rv_destroy,
   file_rv_destroy,
   NULL,
   NULL},
  {RVFL_EXPAND,
   "RVFL_EXPAND",
   file_rv_perm_expand_undo,
   file_rv_perm_expand_redo,
   NULL,
   NULL},
  {RVFL_ALLOC,
   "RVFL_ALLOC",
   file_rv_dealloc_on_undo,
   NULL,
   file_rv_dump_vfid_and_vpid,
   NULL},
  {RVFL_DEALLOC,
   "RVFL_DEALLOC",
   NULL,
   file_rv_dealloc_on_postpone,	/* For postpone */
   NULL,
   file_rv_dump_vfid_and_vpid},
  {RVFL_FHEAD_ALLOC,
   "RVFL_FHEAD_ALLOC",
   file_rv_fhead_dealloc,
   file_rv_fhead_alloc,
   NULL,
   NULL},
  {RVFL_FHEAD_DEALLOC,
   "RVFL_FHEAD_DEALLOC",
   file_rv_fhead_alloc,
   file_rv_fhead_dealloc,
   NULL,
   NULL},
  {RVFL_FHEAD_SET_LAST_USER_PAGE_FTAB,
   "RVFL_FHEAD_SET_LAST_USER_PAGE_FTAB",
   file_rv_fhead_set_last_user_page_ftab,
   file_rv_fhead_set_last_user_page_ftab,
   NULL,
   NULL},
  {RVFL_FHEAD_MARK_DELETE,
   "RVFL_FHEAD_MARK_DELETE",
   file_rv_header_update_mark_deleted,
   file_rv_header_update_mark_deleted,
   NULL,
   NULL},
  {RVFL_FHEAD_STICKY_PAGE,
   "RVFL_FHEAD_STICKY_PAGE",
   file_rv_fhead_sticky_page,
   file_rv_fhead_sticky_page,
   NULL,
   NULL},
  {RVFL_USER_PAGE_MARK_DELETE,
   "RVFL_USER_PAGE_MARK_DELETE",
   file_rv_user_page_unmark_delete_logical,
   file_rv_user_page_mark_delete,
   file_rv_dump_vfid_and_vpid,
   NULL},
  {RVFL_USER_PAGE_MARK_DELETE_COMPENSATE,
   "RVFL_USER_PAGE_MARK_DELETE_COMPENSATE",
   file_rv_user_page_unmark_delete_physical,
   NULL,
   NULL,
   NULL},
  {RVFL_FILEDESC_UPD,
   "RVFL_FILEDESC_UPD",
   log_rv_copy_char,
   log_rv_copy_char,
   log_rv_dump_char,
   log_rv_dump_char},
  {RVFL_PARTSECT_ALLOC,
   "RVFL_PARTSECT_ALLOC",
   file_rv_partsect_clear,
   file_rv_partsect_set,
   NULL,
   NULL},
  {RVFL_PARTSECT_DEALLOC,
   "RVFL_PARTSECT_DEALLOC",
   file_rv_partsect_set,
   file_rv_partsect_clear,
   NULL,
   NULL},
  {RVFL_EXTDATA_SET_NEXT,
   "RVFL_EXTDATA_SET_NEXT",
   file_rv_extdata_set_next,
   file_rv_extdata_set_next,
   file_rv_dump_extdata_set_next,
   file_rv_dump_extdata_set_next},
  {RVFL_EXTDATA_ADD,
   "RVFL_EXTDATA_ADD",
   file_rv_extdata_remove,
   file_rv_extdata_add,
   file_rv_dump_extdata_remove,
   file_rv_dump_extdata_add},
  {RVFL_EXTDATA_REMOVE,
   "RVFL_EXTDATA_REMOVE",
   file_rv_extdata_add,
   file_rv_extdata_remove,
   file_rv_dump_extdata_add,
   file_rv_dump_extdata_remove},
  {RVFL_EXTDATA_MERGE,
   "RVFL_EXTDATA_MERGE",
   file_rv_extdata_merge,
   file_rv_extdata_merge,
   NULL,
   NULL},
  {RVFL_EXTDATA_UPDATE_ITEM,
   "RVFL_EXTDATA_UPDATE_ITEM",
   log_rv_copy_char,
   log_rv_copy_char,
   NULL,
   NULL},
  {RVFL_TRACKER_HEAP_MARK_DELETED,	/* used for compensate and run postpone */
   "RVFL_TRACKER_HEAP_MARK_DELETED",
   file_rv_tracker_mark_heap_deleted_compensate_or_run_postpone,
   file_rv_tracker_mark_heap_deleted_compensate_or_run_postpone,
   NULL,
   NULL},
  {RVFL_TRACKER_HEAP_REUSE,
   "RVFL_TRACKER_HEAP_REUSE",
   heap_rv_mark_deleted_on_undo,
   file_rv_tracker_reuse_heap,
   NULL,
   NULL},
  {RVFL_TRACKER_UNREGISTER,
   "RVFL_TRACKER_UNREGISTER",
   file_rv_tracker_unregister_undo,
   NULL,
   NULL,
   NULL},
  {RVFL_FHEAD_CONVERT_FTAB_TO_USER,
   "RVFL_FHEAD_CONVERT_FTAB_TO_USER",
   file_rv_fhead_convert_user_to_ftab_page,
   file_rv_fhead_convert_ftab_to_user_page,
   NULL,
   NULL},

  {RVHF_CREATE_HEADER,
   "RVHF_CREATE_HEADER",
   NULL,
   heap_rv_redo_newpage,
   NULL,
   heap_rv_dump_statistics},
  {RVHF_NEWPAGE,
   "RVHF_NEWPAGE",
   NULL,
   heap_rv_redo_newpage,
   NULL,
   heap_rv_dump_chain},
  {RVHF_STATS,
   "RVHF_STATS",
   heap_rv_undoredo_pagehdr,
   heap_rv_undoredo_pagehdr,
   heap_rv_dump_statistics,
   heap_rv_dump_statistics},
  {RVHF_CHAIN,
   "RVHF_CHAIN",
   heap_rv_undoredo_pagehdr,
   heap_rv_undoredo_pagehdr,
   heap_rv_dump_chain,
   heap_rv_dump_chain},
  {RVHF_INSERT,
   "RVHF_INSERT",
   heap_rv_undo_insert,
   heap_rv_redo_insert,
   log_rv_dump_hexa,
   log_rv_dump_hexa},
  {RVHF_DELETE,
   "RVHF_DELETE",
   heap_rv_undo_delete,
   heap_rv_redo_delete,
   log_rv_dump_hexa,
   log_rv_dump_hexa},
  {RVHF_UPDATE,
   "RVHF_UPDATE",
   heap_rv_undo_update,
   heap_rv_redo_update,
   log_rv_dump_hexa,
   log_rv_dump_hexa},
  {RVHF_REUSE_PAGE,
   "RVHF_REUSE_PAGE",
   NULL,
   heap_rv_redo_reuse_page,
   NULL,
   heap_rv_dump_reuse_page},
  {RVHF_REUSE_PAGE_REUSE_OID,
   "RVHF_REUSE_PAGE_REUSE_OID",
   NULL,
   heap_rv_redo_reuse_page_reuse_oid,
   NULL,
   heap_rv_dump_reuse_page},
  {RVHF_MARK_REUSABLE_SLOT,
   "RVHF_MARK_REUSABLE_SLOT",
   NULL,
   heap_rv_redo_mark_reusable_slot,
   NULL,
   log_rv_dump_hexa},
  {RVHF_MVCC_INSERT,
   "RVHF_MVCC_INSERT",
   heap_rv_undo_insert,
   heap_rv_mvcc_redo_insert,
   log_rv_dump_hexa,
   log_rv_dump_hexa},
  {RVHF_MVCC_DELETE_REC_HOME,
   "RVHF_MVCC_DELETE_REC_HOME",
   heap_rv_mvcc_undo_delete,
   heap_rv_mvcc_redo_delete_home,
   NULL,
   NULL},
  {RVHF_MVCC_DELETE_OVERFLOW,
   "RVHF_MVCC_DELETE_OVERFLOW",
   heap_rv_mvcc_undo_delete_overflow,
   heap_rv_mvcc_redo_delete_overflow,
   NULL,
   NULL},
  {RVHF_MVCC_DELETE_REC_NEWHOME,
   "RVHF_MVCC_DELETE_REC_NEWHOME",
   heap_rv_mvcc_undo_delete,
   heap_rv_mvcc_redo_delete_newhome,
   NULL,
   NULL},
  {RVHF_MVCC_DELETE_MODIFY_HOME,
   "RVHF_MVCC_DELETE_MODIFY_HOME",
   heap_rv_undo_update,
   heap_rv_redo_update_and_update_chain,
   NULL,
   NULL},
  {RVHF_MVCC_NO_MODIFY_HOME,
   "RVHF_MVCC_NO_MODIFY_HOME",
   heap_rv_nop,
   heap_rv_update_chain_after_mvcc_op,
   NULL,
   NULL},
  {RVHF_UPDATE_NOTIFY_VACUUM,
   "RVHF_UPDATE_NOTIFY_VACUUM",
   heap_rv_undo_update,
   heap_rv_redo_update_and_update_chain,
   log_rv_dump_hexa,
   log_rv_dump_hexa},
  {RVHF_INSERT_NEWHOME,
   "RVHF_INSERT_NEWHOME",
   heap_rv_undo_insert,
   heap_rv_redo_insert,
   log_rv_dump_hexa,
   log_rv_dump_hexa},
  {RVHF_MVCC_REDISTRIBUTE,
   "RVHF_MVCC_REDISTRIBUTE",
   heap_rv_undo_insert,
   heap_rv_mvcc_redo_redistribute,
   log_rv_dump_hexa,
   log_rv_dump_hexa},
  {RVHF_MVCC_UPDATE_OVERFLOW,
   "RVHF_MVCC_UPDATE_OVERFLOW",
   heap_rv_undo_ovf_update,
   NULL,
   NULL,
   NULL},
  {RVHF_MARK_DELETED,
   "RVHF_MARK_DELETED",
   NULL,
   heap_rv_mark_deleted_on_postpone,
   NULL,
   NULL},

  {RVOVF_NEWPAGE_INSERT,	/* required for HA */
   "RVOVF_NEWPAGE_INSERT",
   NULL,
   overflow_rv_newpage_insert_redo,
   NULL,
   log_rv_dump_hexa},
  {RVOVF_NEWPAGE_LINK,
   "RVOVF_NEWPAGE_LINK",
   overflow_rv_newpage_link_undo,
   overflow_rv_link,
   overflow_rv_link_dump,
   overflow_rv_link_dump},
  {RVOVF_PAGE_UPDATE,
   "RVOVF_PAGE_UPDATE",
   log_rv_copy_char,
   overflow_rv_page_update_redo,
   overflow_rv_page_dump,
   overflow_rv_page_dump},
  {RVOVF_CHANGE_LINK,
   "RVOVF_CHANGE_LINK",
   overflow_rv_link,
   overflow_rv_link,
   overflow_rv_link_dump,
   overflow_rv_link_dump},

  {RVEH_REPLACE,
   "RVEH_REPLACE",
   log_rv_copy_char,
   log_rv_copy_char,
   NULL,
   NULL},
  {RVEH_INSERT,
   "RVEH_INSERT",
   ehash_rv_insert_undo,
   ehash_rv_insert_redo,
   NULL,
   NULL},
  {RVEH_DELETE,
   "RVEH_DELETE",
   ehash_rv_delete_undo,
   ehash_rv_delete_redo,
   NULL,
   NULL},
  {RVEH_INIT_BUCKET,
   "RVEH_INIT_BUCKET",
   NULL,
   ehash_rv_init_bucket_redo,
   NULL,
   NULL},
  {RVEH_CONNECT_BUCKET,
   "RVEH_CONNECT_BUCKET",
   log_rv_copy_char,
   ehash_rv_connect_bucket_redo,
   NULL,
   NULL},
  {RVEH_INC_COUNTER,
   "RVEH_INC_COUNTER",
   ehash_rv_increment,
   ehash_rv_increment,
   NULL,
   NULL},
  {RVEH_INIT_DIR,
   "RVEH_INIT_DIR",
   NULL,
   ehash_rv_init_dir_redo,
   NULL,
   NULL},
  {RVEH_INIT_NEW_DIR_PAGE,
   "RVEH_INIT_NEW_DIR_PAGE",
   NULL,
   ehash_rv_init_dir_new_page_redo,
   NULL,
   NULL},

  {RVBT_NDHEADER_UPD,
   "RVBT_NDHEADER_UPD",
   btree_rv_nodehdr_undoredo_update,
   btree_rv_nodehdr_undoredo_update,
   btree_rv_nodehdr_dump,
   btree_rv_nodehdr_dump},
  {RVBT_NDHEADER_INS,
   "RVBT_NDHEADER_INS",
   btree_rv_nodehdr_undo_insert,
   btree_rv_nodehdr_redo_insert,
   btree_rv_nodehdr_dump,
   btree_rv_nodehdr_dump},
  {RVBT_NDRECORD_UPD,
   "RVBT_NDRECORD_UPD",
   btree_rv_noderec_undoredo_update,
   btree_rv_noderec_undoredo_update,
   btree_rv_noderec_dump,
   btree_rv_noderec_dump},
  {RVBT_NDRECORD_INS,
   "RVBT_NDRECORD_INS",
   btree_rv_noderec_undo_insert,
   btree_rv_noderec_redo_insert,
   btree_rv_noderec_dump_slot_id,
   btree_rv_noderec_dump},
  {RVBT_NDRECORD_DEL,
   "RVBT_NDRECORD_DEL",
   btree_rv_noderec_redo_insert,
   btree_rv_noderec_undo_insert,
   btree_rv_noderec_dump,
   btree_rv_noderec_dump_slot_id},
  {RVBT_DEL_PGRECORDS,
   "RVBT_DEL_PGRECORDS",
   btree_rv_pagerec_insert,
   btree_rv_pagerec_delete,
   NULL,
   NULL},
  {RVBT_GET_NEWPAGE,
   "RVBT_GET_NEWPAGE",
   NULL,
   btree_rv_newpage_redo_init,
   NULL,
   NULL},
  {RVBT_COPYPAGE,
   "RVBT_COPYPAGE",
   btree_rv_undoredo_copy_page,
   btree_rv_undoredo_copy_page,
   NULL,
   NULL},
  {RVBT_ROOTHEADER_UPD,
   "RVBT_ROOTHEADER_UPD",
   btree_rv_roothdr_undo_update,
   btree_rv_nodehdr_undoredo_update,
   btree_rv_roothdr_dump,
   btree_rv_nodehdr_dump},
  {RVBT_UPDATE_OVFID,
   "RVBT_UPDATE_OVFID",
   btree_rv_ovfid_undoredo_update,
   btree_rv_ovfid_undoredo_update,
   btree_rv_ovfid_dump,
   btree_rv_ovfid_dump},
  {RVBT_INS_PGRECORDS,
   "RVBT_INS_PGRECORDS",
   btree_rv_pagerec_delete,
   btree_rv_pagerec_insert,
   NULL,
   NULL},
  {RVBT_MVCC_DELETE_OBJECT,
   "RVBT_MVCC_DELETE_OBJECT",
   btree_rv_keyval_undo_insert_mvcc_delid,
   btree_rv_redo_record_modify,
   btree_rv_keyval_dump,
   log_rv_dump_hexa},
  {RVBT_MVCC_INCREMENTS_UPD,
   "RVBT_MVCC_INCREMENTS_UPD",
   btree_rv_update_tran_stats,
   NULL,
   NULL, NULL},
  {RVBT_MVCC_NOTIFY_VACUUM,
   "RVBT_MVCC_NOTIFY_VACUUM",
   btree_rv_nop,
   btree_rv_nop,
   btree_rv_keyval_dump,
   NULL},
  {RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT,
   "RVBT_LOG_GLOBAL_UNIQUE_STATS_COMMIT",
   btree_rv_undo_global_unique_stats_commit,
   btree_rv_redo_global_unique_stats_commit,
   NULL,
   NULL},
  {RVBT_DELETE_OBJECT_PHYSICAL,
   "RVBT_DELETE_OBJECT_PHYSICAL",
   btree_rv_keyval_undo_delete,
   btree_rv_redo_record_modify,
   btree_rv_keyval_dump,
   log_rv_dump_hexa},
  {RVBT_NON_MVCC_INSERT_OBJECT,
   "RVBT_NON_MVCC_INSERT_OBJECT",
   btree_rv_keyval_undo_insert,
   btree_rv_redo_record_modify,
   btree_rv_keyval_dump,
   log_rv_dump_hexa},
  {RVBT_MVCC_INSERT_OBJECT,
   "RVBT_MVCC_INSERT_OBJECT",
   btree_rv_keyval_undo_insert,
   btree_rv_redo_record_modify,
   btree_rv_keyval_dump,
   log_rv_dump_hexa},
  {RVBT_MVCC_INSERT_OBJECT_UNQ,
   "RVBT_MVCC_INSERT_OBJECT_UNQ",
   btree_rv_keyval_undo_insert_unique,
   btree_rv_redo_record_modify,
   btree_rv_keyval_dump,
   log_rv_dump_hexa},
  {RVBT_RECORD_MODIFY_UNDOREDO,
   "RVBT_RECORD_MODIFY_UNDOREDO",
   btree_rv_undo_record_modify,
   btree_rv_redo_record_modify,
   log_rv_dump_hexa,
   log_rv_dump_hexa},
  {RVBT_RECORD_MODIFY_NO_UNDO,
   "RVBT_RECORD_MODIFY_NO_UNDO",
   NULL,
   btree_rv_redo_record_modify,
   NULL,
   log_rv_dump_hexa},
  {RVBT_RECORD_MODIFY_COMPENSATE,
   "RVBT_RECORD_MODIFY_COMPENSATE",
   btree_rv_redo_record_modify,
   NULL,
   log_rv_dump_hexa,
   NULL},
  {RVBT_REMOVE_UNIQUE_STATS,
   "RVBT_REMOVE_UNIQUE_STATS",
   btree_rv_remove_unique_stats,
   btree_rv_remove_unique_stats,
   NULL,
   NULL},
  {RVBT_DELETE_OBJECT_POSTPONE,
   "RVBT_DELETE_OBJECT_POSTPONE",
   NULL,
   btree_rv_remove_marked_for_delete,
   NULL,
   btree_rv_keyval_dump},
  {RVBT_MARK_DELETED,
   "RVBT_MARK_DELETED",
   btree_rv_keyval_undo_insert_mvcc_delid,
   btree_rv_redo_record_modify,
   btree_rv_keyval_dump,
   log_rv_dump_hexa},
  {RVBT_MARK_DEALLOC_PAGE,
   "RVBT_MARK_DEALLOC_PAGE",
   btree_rv_undo_mark_dealloc_page,
   NULL,
   NULL,
   NULL},

  {RVCT_NEWPAGE,
   "RVCT_NEWPAGE",
   NULL,
   catalog_rv_new_page_redo,
   NULL,
   NULL},
  {RVCT_INSERT,
   "RVCT_INSERT",
   catalog_rv_insert_undo,
   catalog_rv_insert_redo,
   NULL,
   NULL},
  {RVCT_DELETE,
   "RVCT_DELETE",
   catalog_rv_delete_undo,
   catalog_rv_delete_redo,
   NULL,
   NULL},
  {RVCT_UPDATE,
   "RVCT_UPDATE",
   catalog_rv_update,
   catalog_rv_update,
   NULL,
   NULL},
  {RVCT_NEW_OVFPAGE_LOGICAL_UNDO,
   "RVCT_NEW_OVFPAGE_LOGICAL_UNDO",
   catalog_rv_ovf_page_logical_insert_undo,
   NULL,
   NULL,
   NULL},

  {RVLOG_OUTSIDE_LOGICAL_REDO_NOOP,
   "RVLOG_OUTSIDE_LOGICAL_REDO_NOOP",
   NULL,
   log_rv_outside_noop_redo,
   NULL,
   NULL},

  {RVREPL_DATA_INSERT,
   "RVREPL_DATA_INSERT",
   NULL,
   NULL,
   NULL,
   repl_data_insert_log_dump},
  {RVREPL_DATA_UPDATE,
   "RVREPL_DATA_UPDATE",
   NULL,
   NULL,
   NULL,
   repl_data_insert_log_dump},
  {RVREPL_DATA_DELETE,
   "RVREPL_DATA_DELETE",
   NULL,
   NULL,
   NULL,
   repl_data_insert_log_dump},
  {RVREPL_STATEMENT,
   "RVREPL_STATEMENT",
   NULL,
   NULL,
   NULL,
   repl_schema_log_dump},
  {RVREPL_DATA_UPDATE_START,
   "RVREPL_DATA_UPDATE_START",
   NULL,
   NULL,
   NULL,
   repl_data_insert_log_dump},
  {RVREPL_DATA_UPDATE_END,
   "RVREPL_DATA_UPDATE_END",
   NULL,
   NULL,
   NULL,
   repl_data_insert_log_dump},

  {RVVAC_COMPLETE,
   "RVVAC_COMPLETE",
   NULL,
   vacuum_rv_redo_vacuum_complete,
   NULL, NULL},
  {RVVAC_START_JOB,
   "RVVAC_START_JOB",
   NULL,
   vacuum_rv_redo_start_job,
   NULL,
   NULL},
  {RVVAC_DATA_APPEND_BLOCKS,
   "RVVAC_DATA_APPEND_BLOCKS",
   NULL,
   vacuum_rv_redo_append_data,
   NULL,
   vacuum_rv_redo_append_data_dump},
  {RVVAC_DATA_INIT_NEW_PAGE,
   "RVVAC_DATA_INIT_NEW_PAGE",
   NULL,
   vacuum_rv_redo_initialize_data_page,
   NULL,
   NULL},
  {RVVAC_DATA_SET_LINK,
   "RVVAC_DATA_SET_LINK",
   vacuum_rv_undoredo_data_set_link,
   vacuum_rv_undoredo_data_set_link,
   vacuum_rv_undoredo_data_set_link_dump,
   vacuum_rv_undoredo_data_set_link_dump},
  {RVVAC_DATA_FINISHED_BLOCKS,
   "RVVAC_DATA_FINISHED_BLOCKS",
   NULL,
   vacuum_rv_redo_data_finished,
   NULL,
   vacuum_rv_redo_data_finished_dump},
  {RVVAC_NOTIFY_DROPPED_FILE,
   "RVVAC_NOTIFY_DROPPED_FILE",
   vacuum_rv_notify_dropped_file,
   vacuum_rv_notify_dropped_file,
   NULL,
   NULL},
  {RVVAC_DROPPED_FILE_CLEANUP,
   "RVVAC_DROPPED_FILE_CLEANUP",
   NULL,
   vacuum_rv_redo_cleanup_dropped_files,
   NULL,
   NULL},
  {RVVAC_DROPPED_FILE_NEXT_PAGE,
   "RVVAC_DROPPED_FILE_NEXT_PAGE",
   vacuum_rv_set_next_page_dropped_files,
   vacuum_rv_set_next_page_dropped_files,
   NULL,
   NULL},
  {RVVAC_DROPPED_FILE_ADD,
   "RVVAC_DROPPED_FILE_ADD",
   vacuum_rv_undo_add_dropped_file,
   vacuum_rv_redo_add_dropped_file,
   NULL,
   NULL},
  {RVVAC_DROPPED_FILE_REPLACE,
   "RVVAC_DROPPED_FILE_REPLACE",
   vacuum_rv_replace_dropped_file,
   vacuum_rv_replace_dropped_file,
   NULL,
   NULL},
  {RVVAC_HEAP_RECORD_VACUUM,
   "RVVAC_HEAP_RECORD_VACUUM",
   vacuum_rv_undo_vacuum_heap_record,
   vacuum_rv_redo_vacuum_heap_record,
   NULL,
   log_rv_dump_hexa},
  {RVVAC_HEAP_PAGE_VACUUM,
   "RVVAC_HEAP_PAGE_VACUUM",
   NULL,
   vacuum_rv_redo_vacuum_heap_page,
   NULL,
   log_rv_dump_hexa},
  {RVVAC_REMOVE_OVF_INSID,
   "RVVAC_REMOVE_OVF_INSID",
   NULL,
   vacuum_rv_redo_remove_ovf_insid,
   NULL,
   NULL},

  {RVES_NOTIFY_VACUUM,
   "RVES_NOTIFY_VACUUM",
   es_rv_nop,
   es_rv_nop,
   NULL, NULL},

  {RVLOC_CLASSNAME_DUMMY,
   "RVLOC_CLASSNAME_DUMMY",
   NULL,
   locator_rv_redo_rename,
   NULL,
   NULL},

  {RVPGBUF_FLUSH_PAGE,
   "RVPGBUF_FLUSH_PAGE",
   pgbuf_rv_flush_page,
   pgbuf_rv_flush_page,
   pgbuf_rv_flush_page_dump,
   pgbuf_rv_flush_page_dump},
  {RVPGBUF_NEW_PAGE,
   "RVPGBUF_NEW_PAGE",
   pgbuf_rv_new_page_undo,
   pgbuf_rv_new_page_redo,
   NULL,
   NULL},
  {RVPGBUF_DEALLOC,
   "RVPGBUF_DEALLOC",
   pgbuf_rv_dealloc_undo,
   pgbuf_rv_dealloc_redo,
   NULL,
   NULL},
  {RVPGBUF_COMPENSATE_DEALLOC,
   "RVPGBUF_COMPENSATE_DEALLOC",
   pgbuf_rv_new_page_redo,
   NULL,
   NULL,
   NULL},

};

/*
 * rv_rcvindex_string - RETURN STRING ASSOCIATED WITH GIVEN LOG_RCVINDEX
 *
 * return:
 *
 *   rcvindex(in): Numeric recovery index
 *
 * NOTE: Return a string corresponding to the associated recovery
 *              index identifier.
 */
const char *
rv_rcvindex_string (LOG_RCVINDEX rcvindex)
{
  return RV_fun[rcvindex].recv_string;
}

#if !defined (NDEBUG)
/*
 * rv_check_rvfuns - CHECK ORDERING OF RECOVERY FUNCTIONS
 *
 * return:
 *
 * NOTE:Check the ordering of recovery functions.
 *              This is a debugging function.
 */
void
rv_check_rvfuns (void)
{
  unsigned int i, num_indices;

  num_indices = DIM (RV_fun);

  for (i = 0; i < num_indices; i++)
    if (RV_fun[i].recv_index != i)
      {
	er_log_debug (ARG_FILE_LINE,
		      "log_check_rvfuns: *** SYSTEM ERROR *** Bad compilation... Recovery function %d is out of"
		      " sequence in index %d of recovery function array\n", RV_fun[i].recv_index, i);
	er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	assert (false);
	break;
      }

}
#endif /* !NDEBUG */
