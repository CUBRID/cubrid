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
 * Query manager (Server Side)
 */

#ifndef _QUERY_MANAGER_H_
#define _QUERY_MANAGER_H_

#ident "$Id$"

#include "storage_common.h"
#include "list_file.h"
#include "dbtype.h"
#include "thread.h"

#define qmgr_free_old_page_and_init(thread_p, page_p, tfile_vfidp) \
  do \
    { \
      qmgr_free_old_page ((thread_p), (page_p), (tfile_vfidp)); \
      (page_p) = NULL; \
    } \
  while (0)

#define NULL_PAGEID_IN_PROGRESS -2

typedef enum
{
  TEMP_FILE_MEMBUF_NONE = -1,
  TEMP_FILE_MEMBUF_NORMAL,
  TEMP_FILE_MEMBUF_KEY_BUFFER,

  TEMP_FILE_MEMBUF_NUM_TYPES
} QMGR_TEMP_FILE_MEMBUF_TYPE;

typedef enum
{
  QMGR_TRAN_NULL,		/* Null transaction: a transaction not issued a query */
  QMGR_TRAN_RUNNING,		/* Running transaction */
  QMGR_TRAN_DELAYED_START,	/* Suspended transaction: waiting for all the waiting transactions to be served */
  QMGR_TRAN_WAITING,		/* Suspended transaction: waiting for a query file page to be freed. */
  QMGR_TRAN_RESUME_TO_DEALLOCATE,	/* Transaction has been resumed to deallocate all query pages. Transaction will 
					 * have to restart the query */
  QMGR_TRAN_RESUME_DUE_DEADLOCK,	/* Transaction has been resumed to deallocate all query pages. The transaction
					 * was involved in a deadlock. Transaction will have to restart the query. Note 
					 * that the transaction is not aborted. */
  QMGR_TRAN_TERMINATED		/* Terminated transaction */
} QMGR_TRAN_STATUS;

typedef struct qmgr_temp_file QMGR_TEMP_FILE;
struct qmgr_temp_file
{
  QMGR_TEMP_FILE *next;
  QMGR_TEMP_FILE *prev;
  FILE_TYPE temp_file_type;
  VFID temp_vfid;
  int membuf_last;
  PAGE_PTR *membuf;
  int membuf_npages;
  QMGR_TEMP_FILE_MEMBUF_TYPE membuf_type;
};

/*
 * Arguments to pass to the routine used to wait for the next available page
 * for streaming queries.
 */
typedef struct qmgr_wait_args QMGR_WAIT_ARGS;
struct qmgr_wait_args
{
  QUERY_ID query_id;
  VPID vpid;
  VPID next_vpid;
  QMGR_TEMP_FILE *tfile_vfidp;
};

typedef enum
{
  OTHERS,
  M_QUERY,
  UNION_QUERY,
  VALUE_QUERY,
  GROUPBY_QUERY,
  ORDERBY_QUERY,
  DISTINCT_QUERY,
  ANALYTIC_QUERY
} QMGR_QUERY_TYPE;

typedef enum
{
  QUERY_IN_PROGRESS,
  QUERY_COMPLETED
} QMGR_QUERY_STATUS;

typedef struct qmgr_query_entry QMGR_QUERY_ENTRY;
struct qmgr_query_entry
{
  QUERY_ID query_id;		/* unique query identifier */
  XASL_ID xasl_id;		/* XASL tree storage identifier */
  XASL_CACHE_ENTRY *xasl_ent;	/* XASL cache entry for this query */
  QFILE_LIST_ID *list_id;	/* result list file identifier */
  QFILE_LIST_CACHE_ENTRY *list_ent;	/* list cache entry for this query */
  QMGR_QUERY_ENTRY *next;
  QMGR_TEMP_FILE *temp_vfid;	/* head of per query temp file VFID */
  int num_tmp;			/* number of tmpfiles allocated */
  int total_count;		/* total number of file pages alloc'd for the entire query */
  char *er_msg;			/* pointer to error message string of last error */
  int errid;			/* errid for last error of query */
  QMGR_QUERY_STATUS query_status;
  QUERY_FLAG query_flag;
  bool is_holdable;		/* true if this query should be available */
};

extern QMGR_QUERY_ENTRY *qmgr_get_query_entry (THREAD_ENTRY * thread_p, QUERY_ID query_id, int trans_ind);
extern int qmgr_allocate_tran_entries (THREAD_ENTRY * thread_p, int trans_cnt);
extern void qmgr_dump (void);
extern int qmgr_initialize (THREAD_ENTRY * thread_p);
extern void qmgr_finalize (THREAD_ENTRY * thread_p);
extern void qmgr_clear_trans_wakeup (THREAD_ENTRY * thread_p, int tran_index, bool tran_died, bool is_abort);
#if defined(ENABLE_UNUSED_FUNCTION)
extern QMGR_TRAN_STATUS qmgr_get_tran_status (THREAD_ENTRY * thread_p, int tran_index);
extern void qmgr_set_tran_status (THREAD_ENTRY * thread_p, int tran_index, QMGR_TRAN_STATUS trans_status);
extern int qmgr_get_query_error_with_entry (QMGR_QUERY_ENTRY * query_entryp);
#endif /* ENABLE_UNUSED_FUNCTION */
extern void qmgr_add_modified_class (THREAD_ENTRY * thread_p, const OID * class_oid);
extern PAGE_PTR qmgr_get_old_page (THREAD_ENTRY * thread_p, VPID * vpidp, QMGR_TEMP_FILE * tfile_vfidp);
extern void qmgr_free_old_page (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr, QMGR_TEMP_FILE * tfile_vfidp);
extern void qmgr_set_dirty_page (THREAD_ENTRY * thread_p, PAGE_PTR page_ptr, int free_page, LOG_DATA_ADDR * addrp,
				 QMGR_TEMP_FILE * tfile_vfidp);
extern PAGE_PTR qmgr_get_new_page (THREAD_ENTRY * thread_p, VPID * vpidp, QMGR_TEMP_FILE * tfile_vfidp);
extern QMGR_TEMP_FILE *qmgr_create_new_temp_file (THREAD_ENTRY * thread_p, QUERY_ID query_id,
						  QMGR_TEMP_FILE_MEMBUF_TYPE membuf_type);
extern QMGR_TEMP_FILE *qmgr_create_result_file (THREAD_ENTRY * thread_p, QUERY_ID query_id);
extern int qmgr_free_list_temp_file (THREAD_ENTRY * thread_p, QUERY_ID query_id, QMGR_TEMP_FILE * tfile_vfidp);
extern int qmgr_free_temp_file_list (THREAD_ENTRY * thread_p, QMGR_TEMP_FILE * tfile_vfidp, QUERY_ID query_id,
				     bool is_error);

#if defined (SERVER_MODE)
extern bool qmgr_is_query_interrupted (THREAD_ENTRY * thread_p, QUERY_ID query_id);
#endif /* SERVER_MODE */

extern void qmgr_set_query_error (THREAD_ENTRY * thread_p, QUERY_ID query_id);
extern void qmgr_setup_empty_list_file (char *page_buf);
extern int qmgr_get_temp_file_membuf_pages (QMGR_TEMP_FILE * temp_file_p);
extern int qmgr_get_sql_id (THREAD_ENTRY * thread_p, char **sql_id_buf, char *query, int sql_len);
extern struct drand48_data *qmgr_get_rand_buf (THREAD_ENTRY * thread_p);
extern QUERY_ID qmgr_get_current_query_id (THREAD_ENTRY * thread_p);
extern char *qmgr_get_query_sql_user_text (THREAD_ENTRY * thread_p, QUERY_ID query_id, int tran_index);

#endif /* _QUERY_MANAGER_H_ */
