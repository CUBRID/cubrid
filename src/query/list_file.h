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
 * List files (Server Side)
 */

#ifndef _LIST_FILE_H_
#define _LIST_FILE_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "external_sort.h"
#if defined (SERVER_MODE)
#include "log_comm.h"		// for TRAN_ISOLATION; todo - remove it.
#endif // SERVER_MODE
#include "query_list.h"
#include "storage_common.h"
#include "thread_compat.hpp"

#include "xasl_cache.h"

#include <stdio.h>

// forward definitions
struct or_buf;
typedef struct or_buf OR_BUF;
struct valptr_list_node;
struct xasl_node_header;

extern int qfile_Is_list_cache_disabled;

#define QFILE_IS_LIST_CACHE_DISABLED (qfile_Is_list_cache_disabled)

#define QFILE_FREE_AND_INIT_LIST_ID(list_id) \
  do {                                       \
    if (list_id != NULL)                     \
    {                                        \
      qfile_free_list_id (list_id);          \
      list_id = NULL;                        \
    }                                        \
  } while (0)

typedef struct qfile_page_header QFILE_PAGE_HEADER;
struct qfile_page_header
{
  int pg_tplcnt;		/* tuple count for the page */
  PAGEID prev_pgid;		/* previous page identifier */
  PAGEID next_pgid;		/* next page identifier */
  int lasttpl_off;		/* offset value of the last tuple */
  PAGEID ovfl_pgid;		/* overflow page identifier */
  VOLID prev_volid;		/* previous page volume identifier */
  VOLID next_volid;		/* next page volume identifier */
  VOLID ovfl_volid;		/* overflow page volume identifier */
};
#define QFILE_PAGE_HEADER_INITIALIZER \
  { 0, NULL_PAGEID, NULL_PAGEID, 0, NULL_PAGEID, NULL_VOLID, NULL_VOLID, NULL_VOLID }

/* query result(list file) cache entry type definition */
typedef struct qfile_list_cache_entry QFILE_LIST_CACHE_ENTRY;
struct qfile_list_cache_entry
{
  int list_ht_no;		/* list_ht no to which this entry belongs */
  DB_VALUE_ARRAY param_values;	/* parameter values bound to this result */
  QFILE_LIST_ID list_id;	/* list file(query result) identifier */
#if defined(SERVER_MODE)
  QFILE_LIST_CACHE_ENTRY *tran_next;	/* next entry in the transaction list */
  TRAN_ISOLATION tran_isolation;	/* isolation level of the transaction which made this result */
  bool uncommitted_marker;	/* the transaction that made this entry is not committed yet */
  int *tran_index_array;	/* array of TID(tran index)s that are currently using this list file; size is
				 * MAX_NTRANS */
  size_t last_ta_idx;		/* index of the last element in TIDs array */
#endif				/* SERVER_MODE */
  XASL_CACHE_ENTRY *xcache_entry;	/* xasl_cache entry */
  const char *query_string;	/* query string; information purpose only */
  struct timeval time_created;	/* when this entry created */
  struct timeval time_last_used;	/* when this entry used lastly */
  int ref_count;		/* how many times this query used */
  bool deletion_marker;		/* this entry will be deleted if marker set */
  bool invalidate;		/* related xcache entry is erased */
};

enum
{
  QFILE_LIST_QUERY_CACHE_MODE_OFF = 0,
  QFILE_LIST_QUERY_CACHE_MODE_SELECTIVELY_OFF = 1,
  QFILE_LIST_QUERY_CACHE_MODE_SELECTIVELY_ON = 2
};

/* List manipulation routines */
extern int qfile_initialize (void);
extern void qfile_finalize (void);
extern void qfile_destroy_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id);
extern void qfile_close_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id);
extern int qfile_add_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, QFILE_TUPLE tpl);
extern int qfile_add_tuple_get_pos_in_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, QFILE_TUPLE tpl,
					    QFILE_TUPLE_POSITION * tuple_pos);
extern int qfile_add_overflow_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, PAGE_PTR ovfl_tpl_pg,
					     QFILE_LIST_ID * input_list_id);
extern int qfile_get_first_page (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id);

/* Copy routines */
extern int qfile_copy_list_id (QFILE_LIST_ID * dest_list_id, const QFILE_LIST_ID * src_list_id, bool include_sort_list);
extern QFILE_LIST_ID *qfile_clone_list_id (const QFILE_LIST_ID * list_id, bool include_sort_list);

/* Free routines */
extern void qfile_free_list_id (QFILE_LIST_ID * list_id);
extern void qfile_free_sort_list (THREAD_ENTRY * thread_p, SORT_LIST * sort_list);

/* Alloc routines */
extern SORT_LIST *qfile_allocate_sort_list (THREAD_ENTRY * thread_p, int cnt);

/* sort_list related routines */
extern bool qfile_is_sort_list_covered (SORT_LIST * covering_list, SORT_LIST * covered_list);

/* Sorting related routines */
extern SORT_STATUS qfile_make_sort_key (THREAD_ENTRY * thread_p, SORTKEY_INFO * info, RECDES * key,
					QFILE_LIST_SCAN_ID * input_scan, QFILE_TUPLE_RECORD * tplrec);
extern QFILE_TUPLE qfile_generate_sort_tuple (SORTKEY_INFO * info, SORT_REC * sort_rec, RECDES * output_recdes);
extern int qfile_compare_partial_sort_record (const void *pk0, const void *pk1, void *arg);
extern int qfile_compare_all_sort_record (const void *pk0, const void *pk1, void *arg);
extern int qfile_get_estimated_pages_for_sorting (QFILE_LIST_ID * listid, SORTKEY_INFO * info);
extern SORTKEY_INFO *qfile_initialize_sort_key_info (SORTKEY_INFO * info, SORT_LIST * list,
						     QFILE_TUPLE_VALUE_TYPE_LIST * types);
extern void qfile_clear_sort_key_info (SORTKEY_INFO * info);
extern QFILE_LIST_ID *qfile_sort_list_with_func (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id,
						 SORT_LIST * sort_list, QUERY_OPTIONS option, int ls_flag,
						 SORT_GET_FUNC * get_fn, SORT_PUT_FUNC * put_fn, SORT_CMP_FUNC * cmp_fn,
						 void *extra_arg, int limit, bool do_close);
extern QFILE_LIST_ID *qfile_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, SORT_LIST * sort_list,
				       QUERY_OPTIONS option, bool do_close);

/* Query result(list file) cache routines */
extern int qfile_initialize_list_cache (THREAD_ENTRY * thread_p);
extern int qfile_finalize_list_cache (THREAD_ENTRY * thread_p);
extern int qfile_clear_list_cache (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xcache_entry, bool invalidate);
extern int qfile_dump_list_cache_internal (THREAD_ENTRY * thread_p, FILE * fp);
#if defined (CUBRID_DEBUG)
extern int qfile_dump_list_cache (THREAD_ENTRY * thread_p, const char *fname);
#endif
/* query result(list file) cache entry manipulation functions */
void qfile_clear_uncommited_list_cache_entry (THREAD_ENTRY * thread_p, int tran_index);
QFILE_LIST_CACHE_ENTRY *qfile_lookup_list_cache_entry (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xasl,
						       const DB_VALUE_ARRAY * params, bool * result_cached);
QFILE_LIST_CACHE_ENTRY *qfile_update_list_cache_entry (THREAD_ENTRY * thread_p, int list_ht_no,
						       const DB_VALUE_ARRAY * params, const QFILE_LIST_ID * list_id,
						       XASL_CACHE_ENTRY * xasl);
int qcache_get_new_ht_no (THREAD_ENTRY * thread_p);
void qcache_free_ht_no (THREAD_ENTRY * thread_p, int ht_no);

int qfile_end_use_of_list_cache_entry (THREAD_ENTRY * thread_p, QFILE_LIST_CACHE_ENTRY * lent, bool marker);

/* Scan related routines */
extern int qfile_modify_type_list (QFILE_TUPLE_VALUE_TYPE_LIST * type_list, QFILE_LIST_ID * list_id);
extern void qfile_clear_list_id (QFILE_LIST_ID * list_id);

extern void qfile_load_xasl_node_header (THREAD_ENTRY * thread_p, char *xasl_stream, xasl_node_header * xasl_header_p);
extern QFILE_LIST_ID *qfile_open_list (THREAD_ENTRY * thread_p, QFILE_TUPLE_VALUE_TYPE_LIST * type_list,
				       SORT_LIST * sort_list, QUERY_ID query_id, int flag);
extern int qfile_reopen_list_as_append_mode (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p);
extern int qfile_save_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, QFILE_TUPLE_TYPE tuple_type, char *page_p,
			     int *tuple_length_p);
extern int qfile_generate_tuple_into_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, QFILE_TUPLE_TYPE tpl_type);
extern int qfile_fast_intint_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, int v1, int v2);
extern int qfile_fast_intval_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, int v1, DB_VALUE * v2);
extern int qfile_fast_val_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, DB_VALUE * val);
extern int qfile_add_item_to_list (THREAD_ENTRY * thread_p, char *item, int item_size, QFILE_LIST_ID * list_id);
extern QFILE_LIST_ID *qfile_combine_two_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * lhs_file,
					      QFILE_LIST_ID * rhs_file, int flag);
extern int qfile_copy_tuple_descr_to_tuple (THREAD_ENTRY * thread_p, QFILE_TUPLE_DESCRIPTOR * tpl_descr,
					    QFILE_TUPLE_RECORD * tplrec);
extern int qfile_reallocate_tuple (QFILE_TUPLE_RECORD * tplrec, int tpl_size);
extern int qfile_unify_types (QFILE_LIST_ID * list_id1, const QFILE_LIST_ID * list_id2);
#if defined (CUBRID_DEBUG)
extern void qfile_print_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id);
#endif
extern QFILE_LIST_ID *qfile_duplicate_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, int flag);
extern int qfile_get_tuple (THREAD_ENTRY * thread_p, PAGE_PTR first_page, QFILE_TUPLE tuplep,
			    QFILE_TUPLE_RECORD * tplrec, QFILE_LIST_ID * list_idp);
extern void qfile_save_current_scan_tuple_position (QFILE_LIST_SCAN_ID * s_id, QFILE_TUPLE_POSITION * ls_tplpos);
extern SCAN_CODE qfile_jump_scan_tuple_position (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * s_id,
						 QFILE_TUPLE_POSITION * ls_tplpos, QFILE_TUPLE_RECORD * tplrec,
						 int peek);
extern int qfile_start_scan_fix (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * s_id);
extern int qfile_open_list_scan (QFILE_LIST_ID * list_id, QFILE_LIST_SCAN_ID * s_id);
extern SCAN_CODE qfile_scan_list_next (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * s_id, QFILE_TUPLE_RECORD * tplrec,
				       int peek);
extern SCAN_CODE qfile_scan_list_prev (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * s_id, QFILE_TUPLE_RECORD * tplrec,
				       int peek);
extern void qfile_end_scan_fix (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * s_id);
extern void qfile_close_scan (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * s_id);

/* Miscellaneous */
extern QFILE_TUPLE_VALUE_FLAG qfile_locate_tuple_value (QFILE_TUPLE tpl, int index, char **tpl_val, int *val_size);
extern QFILE_TUPLE_VALUE_FLAG qfile_locate_tuple_value_r (QFILE_TUPLE tpl, int index, char **tpl_val, int *val_size);
extern int qfile_locate_tuple_next_value (OR_BUF * iterator, OR_BUF * buf, QFILE_TUPLE_VALUE_FLAG * flag);
extern bool qfile_has_next_page (PAGE_PTR page_p);
extern int qfile_update_domains_on_type_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p,
					      valptr_list_node * valptr_list_p);
extern int qfile_set_tuple_column_value (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, PAGE_PTR curr_page_p,
					 VPID * vpid_p, QFILE_TUPLE tuple_p, int col_num, DB_VALUE * value_p,
					 TP_DOMAIN * domain);
extern int qfile_overwrite_tuple (THREAD_ENTRY * thread_p, PAGE_PTR first_page, QFILE_TUPLE tuplep,
				  QFILE_TUPLE_RECORD * tplrec, QFILE_LIST_ID * list_idp);
extern void qfile_update_qlist_count (THREAD_ENTRY * thread_p, const QFILE_LIST_ID * list_p, int inc);
extern int qfile_get_list_cache_number_of_entries (int ht_no);
extern bool qfile_has_no_cache_entries ();


#endif /* _LIST_FILE_H_ */
