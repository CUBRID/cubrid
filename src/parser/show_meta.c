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
 * show_meta.c -  show statement infos, including column defination, semantic check.
 */

#ident "$Id$"


#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

#include "show_meta.h"
#include "error_manager.h"
#include "parser.h"
#include "schema_manager.h"
#include "dbtype.h"
#include "error_code.h"
#include "db.h"

enum
{
  ARG_REQUIRED = false,		/* argument is required */
  ARG_OPTIONAL = true		/* argument is optional */
};

enum
{
  ORDER_DESC = false,		/* order by descending */
  ORDER_ASC = true		/* order by ascending */
};

typedef enum
{
  SHOW_ONLY,
  SHOW_ALL
} SHOW_ONLY_ALL;

static bool show_Inited = false;
static SHOWSTMT_METADATA *show_Metas[SHOWSTMT_END];

static int init_db_attribute_list (SHOWSTMT_METADATA * md);
static void free_db_attribute_list (SHOWSTMT_METADATA * md);

/* check functions */
static PT_NODE *pt_check_access_status (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_check_table_in_show_heap (PARSER_CONTEXT * parser, PT_NODE * node);
static PT_NODE *pt_check_show_index (PARSER_CONTEXT * parser, PT_NODE * node);

/* meta functions */
static SHOWSTMT_METADATA *metadata_of_volume_header (void);
static SHOWSTMT_METADATA *metadata_of_access_status (void);
static SHOWSTMT_METADATA *metadata_of_active_log_header (void);
static SHOWSTMT_METADATA *metadata_of_archive_log_header (void);
static SHOWSTMT_METADATA *metadata_of_slotted_page_header (void);
static SHOWSTMT_METADATA *metadata_of_slotted_page_slots (void);
static SHOWSTMT_METADATA *metadata_of_heap_header (SHOW_ONLY_ALL flag);
static SHOWSTMT_METADATA *metadata_of_heap_capacity (SHOW_ONLY_ALL flag);
static SHOWSTMT_METADATA *metadata_of_index_header (SHOW_ONLY_ALL flag);
static SHOWSTMT_METADATA *metadata_of_index_capacity (SHOW_ONLY_ALL flag);
static SHOWSTMT_METADATA *metadata_of_global_critical_sections (void);
static SHOWSTMT_METADATA *metadata_of_job_queues (void);
static SHOWSTMT_METADATA *metadata_of_timezones (void);
static SHOWSTMT_METADATA *metadata_of_full_timezones (void);
static SHOWSTMT_METADATA *metadata_of_tran_tables (void);
static SHOWSTMT_METADATA *metadata_of_threads (void);

static SHOWSTMT_METADATA *
metadata_of_volume_header (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Volume_id", "int"},
    {"Magic_symbol", "varchar(100)"},
    {"Io_page_size", "short"},
    {"Purpose", "varchar(24)"},
    {"Type", "varchar(24)"},
    {"Sector_size_in_pages", "int"},
    {"Num_total_sectors", "int"},
    {"Num_free_sectors", "int"},
    {"Num_max_sectors", "int"},
    {"Hint_alloc_sector", "int"},
    {"Sector_alloc_table_size_in_pages", "int"},
    {"Sector_alloc_table_first_page", "int"},
    {"Last_system_page", "int"},
    {"Creation_time", "datetime"},
    {"Db_charset", "int"},
    {"Checkpoint_lsa", "varchar(64)"},
    {"Boot_hfid", "varchar(64)"},
    {"Full_name", "varchar(255)"},
    {"Next_volume_id", "int"},
    {"Next_vol_full_name", "varchar(255)"},
    {"Remarks", "varchar(64)"}
  };

  static const SHOWSTMT_NAMED_ARG args[] = {
    {NULL, AVT_INTEGER, ARG_REQUIRED}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_VOLUME_HEADER, "show volume header of ",
    cols, DIM (cols), NULL, 0, args, DIM (args), NULL, NULL
  };
  return &md;
}

static SHOWSTMT_METADATA *
metadata_of_active_log_header (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Volume_id", "int"},
    {"Magic_symbol", "varchar(32)"},
    {"Magic_symbol_location", "int"},
    {"Creation_time", "datetime"},
    {"Release", "varchar(32)"},
    {"Compatibility_disk_version", "varchar(32)"},
    {"Db_page_size", "int"},
    {"Log_page_size", "int"},
    {"Shutdown", "int"},
    {"Next_trans_id", "int"},
    {"Num_avg_trans", "int"},
    {"Num_avg_locks", "int"},
    {"Num_active_log_pages", "int"},
    {"Db_charset", "int"},
    {"First_active_log_page", "bigint"},
    {"Current_append", "varchar(64)"},
    {"Checkpoint", "varchar(64)"},
    {"Next_archive_page_id", "bigint"},
    {"Active_physical_page_id", "int"},
    {"Next_archive_num", "int"},
    {"Last_archive_num_for_syscrashes", "int"},
    {"Last_deleted_archive_num", "int"},
    {"Backup_lsa_level0", "varchar(64)"},
    {"Backup_lsa_level1", "varchar(64)"},
    {"Backup_lsa_level2", "varchar(64)"},
    {"Log_prefix", "varchar(256)"},
    {"Has_logging_been_skipped", "int"},
    {"Perm_status", "varchar(64)"},
    {"Backup_info_level0", "varchar(128)"},
    {"Backup_info_level1", "varchar(128)"},
    {"Backup_info_level2", "varchar(128)"},
    {"Ha_server_state", "varchar(32)"},
    {"Ha_file", "varchar(32)"},
    {"Eof_lsa", "varchar(64)"},
    {"Smallest_lsa_at_last_checkpoint", "varchar(64)"},
    {"Next_mvcc_id", "bigint"},
    {"Mvcc_op_log_lsa", "varchar(32)"},
    {"Last_block_oldest_mvcc_id", "bigint"},
    {"Last_block_newest_mvcc_id", "bigint"}
  };

  static const SHOWSTMT_NAMED_ARG args[] = {
    {NULL, AVT_STRING, ARG_OPTIONAL}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_ACTIVE_LOG_HEADER, "show log header of ",
    cols, DIM (cols), NULL, 0, args, DIM (args), NULL, NULL
  };

  return &md;
}

static SHOWSTMT_METADATA *
metadata_of_archive_log_header (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Volume_id", "int"},
    {"Magic_symbol", "varchar(32)"},
    {"Magic_symbol_location", "int"},
    {"Creation_time", "datetime"},
    {"Next_trans_id", "bigint"},
    {"Num_pages", "int"},
    {"First_page_id", "bigint"},
    {"Archive_num", "int"}
  };

  static const SHOWSTMT_NAMED_ARG args[] = {
    {NULL, AVT_STRING, ARG_OPTIONAL}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_ARCHIVE_LOG_HEADER, "show archive log header of ",
    cols, DIM (cols), NULL, 0, args, DIM (args), NULL, NULL
  };

  return &md;
}

static SHOWSTMT_METADATA *
metadata_of_slotted_page_header (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Volume_id", "int"},
    {"Page_id", "int"},
    {"Num_slots", "int"},
    {"Num_records", "int"},
    {"Anchor_type", "varchar(32)"},
    {"Alignment", "varchar(8)"},
    {"Total_free_area", "int"},
    {"Contiguous_free_area", "int"},
    {"Free_space_offset", "int"},
    {"Need_update_best_hint", "int"},
    {"Is_saving", "int"},
    {"Flags", "int"}
  };

  static const SHOWSTMT_NAMED_ARG args[] = {
    {"volume", AVT_INTEGER, ARG_REQUIRED},
    {"page", AVT_INTEGER, ARG_REQUIRED}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_SLOTTED_PAGE_HEADER, "show slotted page header of ",
    cols, DIM (cols), NULL, 0, args, DIM (args), NULL, NULL
  };

  return &md;
}

static SHOWSTMT_METADATA *
metadata_of_slotted_page_slots (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Volume_id", "int"},
    {"Page_id", "int"},
    {"Slot_id", "int"},
    {"Offset", "int"},
    {"Type", "varchar(32)"},
    {"Length", "int"},
    {"Waste", "int"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {3, ORDER_ASC}
  };

  static const SHOWSTMT_NAMED_ARG args[] = {
    {"volume", AVT_INTEGER, ARG_REQUIRED},
    {"page", AVT_INTEGER, ARG_REQUIRED}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_SLOTTED_PAGE_SLOTS, "show slotted page slots of ",
    cols, DIM (cols), orderby, DIM (orderby), args, DIM (args), NULL, NULL
  };

  return &md;
}

static SHOWSTMT_METADATA *
metadata_of_access_status (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"user_name", "varchar(32)"},
    {"last_access_time", "datetime"},
    {"last_access_host", "varchar(32)"},
    {"program_name", "varchar(32)"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_ACCESS_STATUS, "show access status",
    cols, DIM (cols), orderby, DIM (orderby), NULL, 0,
    pt_check_access_status, NULL
  };

  return &md;
}

static SHOWSTMT_METADATA *
metadata_of_heap_header (SHOW_ONLY_ALL flag)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Table_name", "varchar(256)"},
    {"Class_oid", "varchar(64)"},
    {"Volume_id", "int"},
    {"File_id", "int"},
    {"Header_page_id", "int"},
    {"Overflow_vfid", "varchar(64)"},
    {"Next_vpid", "varchar(64)"},
    {"Unfill_space", "int"},
    {"Estimates_num_pages", "bigint"},
    {"Estimates_num_recs", "bigint"},
    {"Estimates_avg_rec_len", "int"},
    {"Estimates_num_high_best", "int"},
    {"Estimates_num_others_high_best", "int"},
    {"Estimates_head", "int"},
    {"Estimates_best_list", "varchar(512)"},
    {"Estimates_num_second_best", "int"},
    {"Estimates_head_second_best", "int"},
    {"Estimates_tail_second_best", "int"},
    {"Estimates_num_substitutions", "int"},
    {"Estimates_second_best_list", "varchar(256)"},
    {"Estimates_last_vpid", "varchar(64)"},
    {"Estimates_full_search_vpid", "varchar(64)"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC}
  };

  static const SHOWSTMT_NAMED_ARG args[] = {
    {NULL, AVT_IDENTIFIER, ARG_REQUIRED}
  };

  static SHOWSTMT_METADATA md_only = {
    SHOWSTMT_HEAP_HEADER, "show heap header of ",
    cols, DIM (cols), NULL, 0, args, DIM (args),
    pt_check_table_in_show_heap, NULL
  };

  static SHOWSTMT_METADATA md_all = {
    SHOWSTMT_ALL_HEAP_HEADER, "show all heap header of ",
    cols, DIM (cols), orderby, DIM (orderby), args, DIM (args),
    pt_check_table_in_show_heap, NULL
  };

  return (flag == SHOW_ALL) ? &md_all : &md_only;
}

static SHOWSTMT_METADATA *
metadata_of_heap_capacity (SHOW_ONLY_ALL flag)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Table_name", "varchar(256)"},
    {"Class_oid", "varchar(64)"},
    {"Volume_id", "int"},
    {"File_id", "int"},
    {"Header_page_id", "int"},
    {"Num_recs", "bigint"},
    {"Num_relocated_recs", "bigint"},
    {"Num_overflowed_recs", "bigint"},
    {"Num_pages", "bigint"},
    {"Avg_rec_len", "int"},
    {"Avg_free_space_per_page", "int"},
    {"Avg_free_space_per_page_except_last_page", "int"},
    {"Avg_overhead_per_page", "int"},
    {"Repr_id", "int"},
    {"Num_total_attrs", "int"},
    {"Num_fixed_width_attrs", "int"},
    {"Num_variable_width_attrs", "int"},
    {"Num_shared_attrs", "int"},
    {"Num_class_attrs", "int"},
    {"Total_size_fixed_width_attrs", "int"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC}
  };

  static const SHOWSTMT_NAMED_ARG args[] = {
    {NULL, AVT_IDENTIFIER, ARG_REQUIRED}
  };

  static SHOWSTMT_METADATA md_only = {
    SHOWSTMT_HEAP_CAPACITY, "show heap capacity of ",
    cols, DIM (cols), orderby, DIM (orderby), args, DIM (args),
    pt_check_table_in_show_heap, NULL
  };

  static SHOWSTMT_METADATA md_all = {
    SHOWSTMT_ALL_HEAP_CAPACITY, "show all heap capacity of ",
    cols, DIM (cols), orderby, DIM (orderby), args, DIM (args),
    pt_check_table_in_show_heap, NULL
  };

  return (flag == SHOW_ALL) ? &md_all : &md_only;
}

/* for show index header */
static SHOWSTMT_METADATA *
metadata_of_index_header (SHOW_ONLY_ALL flag)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Table_name", "varchar(256)"},
    {"Index_name", "varchar(256)"},
    {"Btid", "varchar(64)"},
    {"Node_level", "int"},
    {"Max_key_len", "int"},
    {"Num_oids", "int"},
    {"Num_nulls", "int"},
    {"Num_keys", "int"},
    {"Topclass_oid", "varchar(64)"},
    {"Unique", "int"},
    {"Overflow_vfid", "varchar(32)"},
    {"Key_type", "varchar(256)"},
    {"Columns", "varchar(256)"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC},
    {2, ORDER_ASC}
  };

  static const SHOWSTMT_NAMED_ARG args1[] = {
    {NULL, AVT_IDENTIFIER, ARG_REQUIRED}
  };
  static const SHOWSTMT_NAMED_ARG args2[] = {
    {NULL, AVT_IDENTIFIER, ARG_REQUIRED},
    {NULL, AVT_IDENTIFIER, ARG_REQUIRED}
  };

  static SHOWSTMT_METADATA md_all = {
    SHOWSTMT_ALL_INDEXES_HEADER, "show all indexes header of ",
    cols, DIM (cols), orderby, DIM (orderby), args1, DIM (args1),
    pt_check_show_index, NULL
  };

  static SHOWSTMT_METADATA md_only = {
    SHOWSTMT_INDEX_HEADER, "show index header of ",
    cols, DIM (cols), NULL, 0, args2, DIM (args2),
    pt_check_show_index, NULL
  };

  return (flag == SHOW_ALL) ? &md_all : &md_only;
}

/* for show index capacity */
static SHOWSTMT_METADATA *
metadata_of_index_capacity (SHOW_ONLY_ALL flag)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Table_name", "varchar(256)"},
    {"Index_name", "varchar(256)"},
    {"Btid", "varchar(64)"},
    {"Num_distinct_key", "int"},
    {"Total_value", "int"},
    {"Avg_num_value_per_key", "int"},
    {"Num_leaf_page", "int"},
    {"Num_non_leaf_page", "int"},
    {"Num_total_page", "int"},
    {"Height", "int"},
    {"Avg_key_len", "int"},
    {"Avg_rec_len", "int"},
    {"Total_space", "varchar(64)"},
    {"Total_used_space", "varchar(64)"},
    {"Total_free_space", "varchar(64)"},
    {"Avg_num_page_key", "int"},
    {"Avg_page_free_space", "varchar(64)"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC},
    {2, ORDER_ASC}
  };

  static const SHOWSTMT_NAMED_ARG args1[] = {
    {NULL, AVT_IDENTIFIER, ARG_REQUIRED}
  };
  static const SHOWSTMT_NAMED_ARG args2[] = {
    {NULL, AVT_IDENTIFIER, ARG_REQUIRED},
    {NULL, AVT_IDENTIFIER, ARG_REQUIRED}
  };

  static SHOWSTMT_METADATA md_all = {
    SHOWSTMT_ALL_INDEXES_CAPACITY, "show all indexes capacity of ",
    cols, DIM (cols), orderby, DIM (orderby), args1, DIM (args1),
    pt_check_show_index, NULL
  };

  static SHOWSTMT_METADATA md_only = {
    SHOWSTMT_INDEX_CAPACITY, "show index capacity of ",
    cols, DIM (cols), NULL, 0, args2, DIM (args2),
    pt_check_show_index, NULL
  };

  return (flag == SHOW_ALL) ? &md_all : &md_only;
}

/* for show critical sections */
static SHOWSTMT_METADATA *
metadata_of_global_critical_sections (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Index", "int"},
    {"Name", "varchar(32)"},
    {"Num_holders", "varchar(16)"},
    {"Num_waiting_readers", "int"},
    {"Num_waiting_writers", "int"},
    {"Owner_thread_index", "int"},
    {"Owner_tran_index", "int"},
    {"Total_enter_count", "bigint"},
    {"Total_waiter_count", "bigint"},
    {"Waiting_promoter_thread_index", "int"},
    {"Max_waiting_msecs", "numeric(10,3)"},
    {"Total_waiting_msecs", "numeric(10,3)"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_GLOBAL_CRITICAL_SECTIONS, "show critical sections",
    cols, DIM (cols), orderby, DIM (orderby), NULL, 0,
    NULL, NULL
  };

  return &md;
}

static SHOWSTMT_METADATA *
metadata_of_job_queues (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Jobq_index", "int"},
    {"Num_total_workers", "int"},
    {"Num_busy_workers", "int"},
    {"Num_connection_workers", "int"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_JOB_QUEUES, "show job queue",
    cols, DIM (cols), orderby, DIM (orderby), NULL, 0, NULL, NULL
  };

  return &md;
}

/* for show timezones */
static SHOWSTMT_METADATA *
metadata_of_timezones (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"timezone_region", "varchar(32)"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_TIMEZONES, "show timezones",
    cols, DIM (cols), orderby, DIM (orderby), NULL, 0,
    NULL, NULL
  };

  return &md;
}

/* for show full timezones */
static SHOWSTMT_METADATA *
metadata_of_full_timezones (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"timezone_region", "varchar(32)"},
    {"region_offset", "varchar(32)"},
    {"dst_offset", "varchar(32)"},
    {"dst_abbreviation", "varchar(32)"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_FULL_TIMEZONES, "show full timezones",
    cols, DIM (cols), orderby, DIM (orderby), NULL, 0,
    NULL, NULL
  };

  return &md;
}

/* for show transaction descriptors */
static SHOWSTMT_METADATA *
metadata_of_tran_tables (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Tran_index", "int"},
    {"Tran_id", "int"},
    {"Is_loose_end", "int"},
    {"State", "varchar(64)"},
    {"Isolation", "varchar(64)"},
    {"Wait_msecs", "int"},
    {"Head_lsa", "varchar(64)"},
    {"Tail_lsa", "varchar(64)"},
    {"Undo_next_lsa", "varchar(64)"},
    {"Postpone_next_lsa", "varchar(64)"},
    {"Savepoint_lsa", "varchar(64)"},
    {"Topop_lsa", "varchar(64)"},
    {"Tail_top_result_lsa", "varchar(64)"},
    {"Client_id", "int"},
    {"Client_type", "varchar(40)"},
    {"Client_info", "varchar(256)"},
    {"Client_db_user", "varchar(40)"},
    {"Client_program", "varchar(256)"},
    {"Client_login_user", "varchar(16)"},
    {"Client_host", "varchar(64)"},
    {"Client_pid", "int"},
    {"Topop_depth", "int"},
    {"Num_unique_btrees", "int"},
    {"Max_unique_btrees", "int"},
    {"Interrupt", "int"},
    {"Num_transient_classnames", "int"},
    {"Repl_max_records", "int"},
    {"Repl_records", "varchar(20)"},
    {"Repl_current_index", "int"},
    {"Repl_append_index", "int"},
    {"Repl_flush_marked_index", "int"},
    {"Repl_insert_lsa", "varchar(64)"},
    {"Repl_update_lsa", "varchar(64)"},
    {"First_save_entry", "varchar(20)"},
    {"Tran_unique_stats", "varchar(20)"},
    {"Modified_class_list", "varchar(20)"},
    {"Num_temp_files", "int"},
    {"Waiting_for_res", "varchar(20)"},
    {"Has_deadlock_priority", "int"},
    {"Suppress_replication", "int"},
    {"Query_timeout", "datetime"},
    {"Query_start_time", "datetime"},
    {"Tran_start_time", "datetime"},
    {"Xasl_id", "varchar(64)"},
    {"Disable_modifications", "int"},
    {"Abort_reason", "varchar(40)"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_TRAN_TABLES, "show transaction tables",
    cols, DIM (cols), orderby, DIM (orderby), NULL, 0, NULL, NULL
  };

  return &md;
}

static SHOWSTMT_METADATA *
metadata_of_threads (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Index", "int"},
    {"Jobq_index", "int"},
    {"Thread_id", "bigint"},
    {"Tran_index", "int"},
    {"Type", "varchar(8)"},
    {"Status", "varchar(8)"},
    {"Resume_status", "varchar(32)"},
    {"Net_request", "varchar(64)"},
    {"Conn_client_id", "int"},
    {"Conn_request_id", "int"},
    {"Conn_index", "int"},
    {"Last_error_code", "int"},
    {"Last_error_msg", "varchar(256)"},
    {"Private_heap_id", "varchar(20)"},
    {"Query_entry", "varchar(20)"},
    {"Interrupted", "int"},
    {"Shutdown", "int"},
    {"Check_interrupt", "int"},
    {"Check_page_validation", "int"},
    {"Wait_for_latch_promote", "int"},
    {"Lockwait_blocked_mode", "varchar(24)"},
    {"Lockwait_start_time", "datetime"},
    {"Lockwait_msecs", "int"},
    {"Lockwait_state", "varchar(24)"},
    {"Next_wait_thread_index", "int"},
    {"Next_tran_wait_thread_index", "int"},
    {"Next_worker_thread_index", "int"}
  };

  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, ORDER_ASC}
  };

  static SHOWSTMT_METADATA md = {
    SHOWSTMT_THREADS, "show threads",
    cols, DIM (cols), orderby, DIM (orderby), NULL, 0, NULL, NULL
  };
  return &md;
}

/*
 * showstmt_get_metadata() -  return show statement column infos
 *   return:-
 *   show_type(in): SHOW statement type
 */
const SHOWSTMT_METADATA *
showstmt_get_metadata (SHOWSTMT_TYPE show_type)
{
  const SHOWSTMT_METADATA *show_meta = NULL;

  assert_release (SHOWSTMT_START < show_type);
  assert_release (show_type < SHOWSTMT_END);

  show_meta = show_Metas[show_type];
  assert_release (show_meta != NULL);
  assert_release (show_meta->show_type == show_type);
  return show_meta;
}

/*
 * showstmt_get_attributes () -  return all DB_ATTRIBUTE
 *   return:-
 *   show_type(in): SHOW statement type
 */
DB_ATTRIBUTE *
showstmt_get_attributes (SHOWSTMT_TYPE show_type)
{
  const SHOWSTMT_METADATA *show_meta = NULL;

  show_meta = showstmt_get_metadata (show_type);

  return show_meta->showstmt_attrs;
}

/*
 * pt_check_show_heap () - check table exists or not 
 *   return: PT_NODE pointer
 *
 *   parser(in):
 *   node(in):
 */
static PT_NODE *
pt_check_table_in_show_heap (PARSER_CONTEXT * parser, PT_NODE * node)
{
  int error = NO_ERROR;
  PT_NODE *show_args_node = NULL, *spec, *derived_table;
  PT_NODE *partition_node = NULL;
  SHOWSTMT_TYPE show_type;
  int partition_type = DB_NOT_PARTITIONED_CLASS;
  const char *table_name = NULL;
  MOP cls;
  SM_CLASS *sm_class = NULL;
  int save;

  if (node->node_type != PT_SELECT)
    {
      return node;
    }

  spec = node->info.query.q.select.from;
  assert (spec != NULL);

  derived_table = spec->info.spec.derived_table;
  assert (derived_table != NULL);

  show_type = derived_table->info.showstmt.show_type;
  assert (show_type == SHOWSTMT_HEAP_HEADER || show_type == SHOWSTMT_ALL_HEAP_HEADER
	  || show_type == SHOWSTMT_HEAP_CAPACITY || show_type == SHOWSTMT_ALL_HEAP_CAPACITY);

  show_args_node = derived_table->info.showstmt.show_args;
  assert (show_args_node != NULL);

  assert (show_args_node->node_type == PT_VALUE);
  assert (show_args_node->type_enum == PT_TYPE_CHAR);

  table_name = (const char *) show_args_node->info.value.data_value.str->bytes;

  cls = sm_find_class (table_name);
  if (cls == NULL)
    {
      PT_ERRORmf (parser, show_args_node, MSGCAT_SET_ERROR, -(ER_LC_UNKNOWN_CLASSNAME), table_name);
      return node;
    }

  AU_DISABLE (save);
  error = au_fetch_class_force (cls, &sm_class, AU_FETCH_READ);
  AU_ENABLE (save);
  if (error == NO_ERROR)
    {
      if (sm_get_class_type (sm_class) != SM_CLASS_CT)
	{
	  PT_ERRORm (parser, show_args_node, MSGCAT_SET_ERROR, -(ER_OBJ_NOT_A_CLASS));
	  return node;
	}
    }

  error = sm_partitioned_class_type (cls, &partition_type, NULL, NULL);
  if (error != NO_ERROR)
    {
      PT_ERRORc (parser, show_args_node, er_msg ());
      return node;
    }

  partition_node = pt_make_integer_value (parser, partition_type);
  if (partition_node == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return node;
    }

  parser_append_node (partition_node, show_args_node);

  return node;
}

/*
 * init_db_attribute_list () : init DB_ATTRIBUTE list for each show statement
 *   return: error code
 *   md(in/out):
 */
static int
init_db_attribute_list (SHOWSTMT_METADATA * md)
{
  int i;
  DB_DOMAIN *domain;
  DB_ATTRIBUTE *attrs = NULL, *att;

  if (md == NULL)
    {
      return NO_ERROR;
    }

  for (i = md->num_cols - 1; i >= 0; i--)
    {
      domain = pt_string_to_db_domain (md->cols[i].type, NULL);
      if (domain == NULL)
	{
	  goto on_error;
	}
      domain = tp_domain_cache (domain);

      att = classobj_make_attribute (md->cols[i].name, domain->type, ID_ATTRIBUTE);
      if (att == NULL)
	{
	  goto on_error;
	}
      att->domain = domain;
      att->auto_increment = NULL;

      if (attrs == NULL)
	{
	  attrs = att;
	}
      else
	{
	  att->order_link = attrs;
	  attrs = att;
	}
    }

  md->showstmt_attrs = attrs;
  return NO_ERROR;

on_error:
  while (attrs != NULL)
    {
      att = attrs;
      attrs = (DB_ATTRIBUTE *) att->order_link;
      att->order_link = NULL;

      classobj_free_attribute (att);
    }

  assert (er_errid () != NO_ERROR);
  return er_errid ();
}

/*
 * free_db_attribute_list () : free DB_ATTRIBUTE list for each show statement
 *   return: 
 *   md(in/out):
 */
static void
free_db_attribute_list (SHOWSTMT_METADATA * md)
{
  DB_ATTRIBUTE *attrs = NULL, *att;

  if (md == NULL)
    {
      return;
    }

  attrs = md->showstmt_attrs;
  md->showstmt_attrs = NULL;
  while (attrs != NULL)
    {
      att = attrs;
      attrs = (DB_ATTRIBUTE *) att->order_link;
      att->order_link = NULL;

      classobj_free_attribute (att);
    }
}

/*
 * showstmt_metadata_init() -- initialize the metadata of show statements
 * return error code>
 */
int
showstmt_metadata_init (void)
{
  int error;
  unsigned int i;

  if (show_Inited)
    {
      return NO_ERROR;
    }

  memset (show_Metas, 0, sizeof (show_Metas));
  show_Metas[SHOWSTMT_VOLUME_HEADER] = metadata_of_volume_header ();
  show_Metas[SHOWSTMT_ACCESS_STATUS] = metadata_of_access_status ();
  show_Metas[SHOWSTMT_ACTIVE_LOG_HEADER] = metadata_of_active_log_header ();
  show_Metas[SHOWSTMT_ARCHIVE_LOG_HEADER] = metadata_of_archive_log_header ();
  show_Metas[SHOWSTMT_SLOTTED_PAGE_HEADER] = metadata_of_slotted_page_header ();
  show_Metas[SHOWSTMT_SLOTTED_PAGE_SLOTS] = metadata_of_slotted_page_slots ();
  show_Metas[SHOWSTMT_HEAP_HEADER] = metadata_of_heap_header (SHOW_ONLY);
  show_Metas[SHOWSTMT_ALL_HEAP_HEADER] = metadata_of_heap_header (SHOW_ALL);
  show_Metas[SHOWSTMT_HEAP_CAPACITY] = metadata_of_heap_capacity (SHOW_ONLY);
  show_Metas[SHOWSTMT_ALL_HEAP_CAPACITY] = metadata_of_heap_capacity (SHOW_ALL);
  show_Metas[SHOWSTMT_INDEX_HEADER] = metadata_of_index_header (SHOW_ONLY);
  show_Metas[SHOWSTMT_INDEX_CAPACITY] = metadata_of_index_capacity (SHOW_ONLY);
  show_Metas[SHOWSTMT_ALL_INDEXES_HEADER] = metadata_of_index_header (SHOW_ALL);
  show_Metas[SHOWSTMT_ALL_INDEXES_CAPACITY] = metadata_of_index_capacity (SHOW_ALL);
  show_Metas[SHOWSTMT_GLOBAL_CRITICAL_SECTIONS] = metadata_of_global_critical_sections ();
  show_Metas[SHOWSTMT_JOB_QUEUES] = metadata_of_job_queues ();
  show_Metas[SHOWSTMT_TIMEZONES] = metadata_of_timezones ();
  show_Metas[SHOWSTMT_FULL_TIMEZONES] = metadata_of_full_timezones ();
  show_Metas[SHOWSTMT_TRAN_TABLES] = metadata_of_tran_tables ();
  show_Metas[SHOWSTMT_THREADS] = metadata_of_threads ();

  for (i = 0; i < DIM (show_Metas); i++)
    {
      error = init_db_attribute_list (show_Metas[i]);
      if (error != NO_ERROR)
	{
	  goto on_error;
	}
    }
  show_Inited = true;
  return NO_ERROR;

on_error:
  for (i = 0; i < DIM (show_Metas); i++)
    {
      free_db_attribute_list (show_Metas[i]);
    }
  return error;
}

/*
 * showstmt_metadata_final() -- free the metadata of show statements
 */
void
showstmt_metadata_final (void)
{
  unsigned int i;

  if (!show_Inited)
    {
      return;
    }

  for (i = 0; i < DIM (show_Metas); i++)
    {
      free_db_attribute_list (show_Metas[i]);
    }
  show_Inited = false;
}

static PT_NODE *
pt_check_access_status (PARSER_CONTEXT * parser, PT_NODE * node)
{
  DB_VALUE oid_val;
  MOP classop;
  PT_NODE *entity = NULL;
  PT_NODE *derived_table = NULL;
  PT_NODE *arg = NULL;

  if (!au_is_dba_group_member (Au_user))
    {
      PT_ERRORmf (parser, NULL, MSGCAT_SET_ERROR, -(ER_AU_DBA_ONLY), "show access status");
      return node;
    }

  entity = node->info.query.q.select.from;
  assert (entity != NULL);

  derived_table = entity->info.spec.derived_table;
  assert (derived_table != NULL);

  classop = sm_find_class ("db_user");
  if (classop == NULL)
    {
      assert (er_errid () != NO_ERROR);
      PT_ERRORc (parser, node, er_msg ());
      return node;
    }

  db_make_oid (&oid_val, &classop->oid_info.oid);
  arg = pt_dbval_to_value (parser, &oid_val);

  derived_table->info.showstmt.show_args = parser_append_node (arg, derived_table->info.showstmt.show_args);

  return node;
}

/*
 * pt_check_show_index () - semantic check for show index.
 *   return:
 *   parser(in):
 *   node(in):
 */
static PT_NODE *
pt_check_show_index (PARSER_CONTEXT * parser, PT_NODE * node)
{
  PT_NODE *show_args_node = NULL;
  MOP cls;
  const char *table_name = NULL;
  const char *index_name = NULL;
  SM_CLASS *sm_class = NULL;
  SM_CLASS_CONSTRAINT *sm_all_constraints = NULL;
  SM_CLASS_CONSTRAINT *sm_constraint = NULL;
  PT_NODE *entity = NULL;
  PT_NODE *derived_table = NULL;
  SHOWSTMT_TYPE show_type;
  int error = NO_ERROR;
  int save;
  int partition_type = DB_NOT_PARTITIONED_CLASS;
  PT_NODE *partition_node = NULL;

  if (node->node_type != PT_SELECT)
    {
      return node;
    }

  entity = node->info.query.q.select.from;
  assert (entity != NULL);

  derived_table = entity->info.spec.derived_table;
  assert (derived_table != NULL);

  show_type = derived_table->info.showstmt.show_type;
  assert (show_type == SHOWSTMT_INDEX_HEADER || show_type == SHOWSTMT_INDEX_CAPACITY
	  || show_type == SHOWSTMT_ALL_INDEXES_HEADER || show_type == SHOWSTMT_ALL_INDEXES_CAPACITY);

  show_args_node = derived_table->info.showstmt.show_args;
  assert (show_args_node != NULL);

  assert (show_args_node->node_type == PT_VALUE);
  assert (show_args_node->type_enum == PT_TYPE_CHAR);
  assert (show_args_node->info.value.data_value.str->length < DB_MAX_IDENTIFIER_LENGTH);

  /* check table name */
  table_name = (const char *) show_args_node->info.value.data_value.str->bytes;
  cls = sm_find_class (table_name);
  if (cls == NULL)
    {
      PT_ERRORmf (parser, show_args_node, MSGCAT_SET_ERROR, -(ER_LC_UNKNOWN_CLASSNAME), table_name);
      return node;
    }

  AU_DISABLE (save);
  error = au_fetch_class_force (cls, &sm_class, AU_FETCH_READ);
  AU_ENABLE (save);
  if (error == NO_ERROR)
    {
      if (sm_get_class_type (sm_class) != SM_CLASS_CT)
	{
	  PT_ERRORm (parser, show_args_node, MSGCAT_SET_ERROR, -(ER_OBJ_NOT_A_CLASS));
	  return node;
	}
    }

  /* check index name */
  if (show_type == SHOWSTMT_INDEX_HEADER || show_type == SHOWSTMT_INDEX_CAPACITY)
    {
      show_args_node = show_args_node->next;
      assert (show_args_node != NULL);
      assert (show_args_node->node_type == PT_VALUE);
      assert (show_args_node->type_enum == PT_TYPE_CHAR);
      assert (show_args_node->info.value.data_value.str->length < DB_MAX_IDENTIFIER_LENGTH);

      index_name = (const char *) show_args_node->info.value.data_value.str->bytes;
      sm_all_constraints = sm_class_constraints (cls);
      sm_constraint = classobj_find_constraint_by_name (sm_all_constraints, index_name);
      if (sm_all_constraints == NULL || sm_constraint == NULL)
	{
	  PT_ERRORmf (parser, show_args_node, MSGCAT_SET_ERROR, -(ER_SM_NO_INDEX), index_name);
	  return node;
	}
    }

  /* get partition type and pass it by args */
  error = sm_partitioned_class_type (cls, &partition_type, NULL, NULL);
  if (error != NO_ERROR)
    {
      PT_ERRORc (parser, show_args_node, er_msg ());
      return node;
    }

  partition_node = pt_make_integer_value (parser, partition_type);
  if (partition_node == NULL)
    {
      PT_INTERNAL_ERROR (parser, "allocate new node");
      return node;
    }
  parser_append_node (partition_node, show_args_node);

  return node;
}
