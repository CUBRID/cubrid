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
 * query_manager.c - Query manager module
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "storage_common.h"
#include "system_parameter.h"
#include "xserver_interface.h"
#include "error_manager.h"
#include "log_manager.h"
#if defined(SERVER_MODE)
#include "log_impl.h"
#endif /* SERVER_MODE */
#include "critical_section.h"
#include "wait_for_graph.h"
#include "page_buffer.h"
#include "query_manager.h"

#if defined (SERVER_MODE)
#include "connection_defs.h"
#include "job_queue.h"
#include "connection_error.h"
#endif
#include "thread.h"

#ifndef SERVER_MODE

#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;

#define qmgr_initialize_mutex(a)
#define qmgr_destroy_mutex(a)
#define qmgr_lock_mutex(a, b)
#define qmgr_unlock_mutex(a)
#endif

#define QMGR_NUM_ENTRY_PER_ARRAY	300

#define TEMP_FILE_DEFAULT_PAGES         10

#define QMGR_TEMP_FILE_FREE_LIST_SIZE   100

#define QMGR_NUM_TEMP_FILE_LISTS        (TEMP_FILE_MEMBUF_NUM_TYPES)

/* We have two valid types of membuf used by temporary file. */
#define QMGR_IS_VALID_MEMBUF_TYPE(m)    ((m) == TEMP_FILE_MEMBUF_NORMAL \
    || (m) == TEMP_FILE_MEMBUF_KEY_BUFFER)

/* For streaming queries */
typedef struct query_async QMGR_ASYNC_QUERY;
struct query_async
{
#if defined (ENABLE_UNUSED_FUNCTION)
  XASL_CACHE_CLONE *cache_clone_p;	/* cache clone pointer */
#endif				/* ENABLE_UNUSED_FUNCTION */
  XASL_NODE *xasl;
  const DB_VALUE *dbval_p;
  QUERY_ID query_id;
  int dbval_count;
  int tran_index;
  HL_HEAPID pri_heap_id;
};

/*
 *       		     ALLOCATION STRUCTURES
 *
 * A resource mechanism used to effectively handle memory allocation for the
 * query entry structures.
 */

#if defined (SERVER_MODE)
/* This struct is used when implements recursive mutex. */
typedef struct qmgr_mutex QMGR_MUTEX;
struct qmgr_mutex
{
  pthread_t owner;		/* mutex owner */
  unsigned int lock_count;	/* how many times we acquired mutex */
  pthread_mutex_t lock;
  pthread_cond_t not_busy_cond;
  unsigned int nwaits;		/* the number of waiters */
};
#endif

typedef struct qmgr_tran_entry QMGR_TRAN_ENTRY;
struct qmgr_tran_entry
{
#if defined (SERVER_MODE)
  QMGR_MUTEX lock;
#endif
  QMGR_TRAN_STATUS trans_stat;	/* transaction status */
  QMGR_QUERY_ENTRY *query_entry_list_p;	/* linked list of query entries */

  OID_BLOCK_LIST *modified_classes_p;	/* array of class OIDs */

#if defined (SERVER_MODE)
  THREAD_ENTRY *wait_thread_p;
  int active_sync_query_count;
  bool exist_active_query;
#endif
};

typedef struct qmgr_temp_file_list QMGR_TEMP_FILE_LIST;
struct qmgr_temp_file_list
{
  pthread_mutex_t mutex;
  QMGR_TEMP_FILE *list;
  int count;
};
/*
 * Global query table variable used to keep track of query entries and
 * the anchor for the out of space in the temp vol WFG.
 */
typedef struct qmgr_query_table QMGR_QUERY_TABLE;
struct qmgr_query_table
{
  QMGR_TRAN_ENTRY *tran_entries_p;	/* list of transaction entries */
  int num_trans;		/* size of trans_ind[] */

  /* allocation structure resource */
  int free_query_entry_count;	/* number of free query_entry structures     */
  int total_query_entry_count;	/* total query entry number */

  int num_alloced_array;	/* number of allocated query entry array */
  int num_entry_per_array;	/* number of query entry per query entry array */

  QMGR_QUERY_ENTRY *free_query_entry_list_p;	/* free query entry list */
  QMGR_QUERY_ENTRY **query_entry_array_p;	/* query entry resource pool */
  OID_BLOCK_LIST *free_oid_block_list_p;	/* free OID block list */

  /* temp file free list info */
  QMGR_TEMP_FILE_LIST temp_file_list[QMGR_NUM_TEMP_FILE_LISTS];
};

QMGR_QUERY_TABLE qmgr_Query_table = { NULL, 0, 0, 0, 0, 0, NULL, NULL, NULL,
  {{PTHREAD_MUTEX_INITIALIZER, NULL, 0}, {PTHREAD_MUTEX_INITIALIZER, NULL, 0}}
};

static int qmgr_Query_id_count;	/* global query identifier count */

/*
 * 			QM_MUTEX_LOCK/UNLOCK : recursive mutex
 *
 * Solaris 2.7 seems that does not support recursive mutex. There is option
 * which names PTHREAD_MUTEX_RECURSIVE. But it does not work.
 */
#if defined (SERVER_MODE)
static void qmgr_check_mutex_error (int r, const char *file, int line);
static void qmgr_initialize_mutex (QMGR_MUTEX * qm_mutex);
static void qmgr_destroy_mutex (QMGR_MUTEX * qm_mutex);
static void qmgr_lock_mutex (THREAD_ENTRY * thread_p, QMGR_MUTEX * qm_mutex);
static void qmgr_unlock_mutex (QMGR_MUTEX * qm_mutex);
#endif

static bool qmgr_is_page_in_temp_file_buffer (PAGE_PTR page_p,
					      QMGR_TEMP_FILE * temp_file_p);
static bool qmgr_is_not_allowed_result_cache (QUERY_FLAG flag);
static bool qmgr_can_not_get_result_from_cache (QUERY_FLAG flag);
static void qmgr_put_page_header (PAGE_PTR page_p,
				  QFILE_PAGE_HEADER * header_p);

static QMGR_QUERY_ENTRY *qmgr_allocate_query_entry_array (void);
static QMGR_QUERY_ENTRY *qmgr_allocate_query_entry (THREAD_ENTRY * thread_p);
static void qmgr_free_query_entry (THREAD_ENTRY * thread_p,
				   QMGR_QUERY_ENTRY * q_ptr);
static void qmgr_free_query_entry_list (THREAD_ENTRY * thread_p,
					QMGR_QUERY_ENTRY * q_ptr);
static void qmgr_add_query_entry (THREAD_ENTRY * thread_p,
				  QMGR_QUERY_ENTRY * q_ptr, int trans_ind);
static QMGR_QUERY_ENTRY *qmgr_find_query_entry (QMGR_QUERY_ENTRY *
						query_list_p,
						QUERY_ID query_id);
static void qmgr_delete_query_entry (THREAD_ENTRY * thread_p,
				     QUERY_ID query_id, int trans_ind);
static void qmgr_free_tran_entries (THREAD_ENTRY * thread_p);

#if defined (SERVER_MODE)
static void qmgr_check_active_query_and_wait (THREAD_ENTRY * thread_p,
					      QMGR_TRAN_ENTRY * trans_entry,
					      THREAD_ENTRY * curr_thrd,
					      int exec_mode);
static void qmgr_check_waiter_and_wakeup (QMGR_TRAN_ENTRY * trans_entry,
					  THREAD_ENTRY * curr_thrd,
					  int propagate_interrupt);
#endif

static void qmgr_clear_relative_cache_entries (THREAD_ENTRY * thread_p,
					       QMGR_TRAN_ENTRY *
					       tran_entry_p);
static OID_BLOCK_LIST *qmgr_allocate_oid_block (THREAD_ENTRY * thread_p);
static void qmgr_free_oid_block (THREAD_ENTRY * thread_p,
				 OID_BLOCK_LIST * oid_block);
static PAGE_PTR qmgr_get_external_file_page (THREAD_ENTRY * thread_p,
					     VPID * vpid,
					     QMGR_TEMP_FILE * vfid);
static int qmgr_free_query_temp_file_by_query_entry (THREAD_ENTRY * thread_p,
						     QMGR_QUERY_ENTRY * qptr,
						     int tran_idx);
static QMGR_TEMP_FILE *qmgr_allocate_tempfile_with_buffer (int
							   num_buffer_pages);

#if defined (SERVER_MODE)
static void qmgr_execute_async_select (THREAD_ENTRY * thread_p,
				       QMGR_ASYNC_QUERY * async);
static XASL_NODE *qmgr_find_leaf (XASL_NODE * xasl);
static bool qmgr_is_async_executable (XASL_NODE * xasl,
				      QMGR_QUERY_TYPE * query_type);
static bool qmgr_has_unresolved_types (XASL_NODE * xasl_p);
static QFILE_LIST_ID *qmgr_process_async_select (THREAD_ENTRY * thread_p,
						 XASL_CACHE_CLONE * clo,
						 QMGR_QUERY_ENTRY * q_ptr,
						 int dbval_cnt,
						 const DB_VALUE * dbval_ptr);
#endif

static void qmgr_initialize_temp_file_list (QMGR_TEMP_FILE_LIST *
					    temp_file_list_p,
					    QMGR_TEMP_FILE_MEMBUF_TYPE
					    membuf_type);
static void qmgr_finalize_temp_file_list (QMGR_TEMP_FILE_LIST *
					  temp_file_list_p);
static QMGR_TEMP_FILE *qmgr_get_temp_file_from_list (QMGR_TEMP_FILE_LIST *
						     temp_file_list_p);
static void qmgr_put_temp_file_into_list (QMGR_TEMP_FILE * temp_file_p);
static void qmgr_set_query_timeout_to_tdes (int tran_index,
					    int query_timeout);

static bool
qmgr_is_page_in_temp_file_buffer (PAGE_PTR page_p,
				  QMGR_TEMP_FILE * temp_file_p)
{
  return (temp_file_p != NULL
	  && temp_file_p->membuf_last >= 0
	  && temp_file_p->membuf
	  && page_p >= temp_file_p->membuf[0]
	  && page_p <= temp_file_p->membuf[temp_file_p->membuf_last]);
}


static bool
qmgr_is_not_allowed_result_cache (QUERY_FLAG flag)
{
  return (PRM_LIST_QUERY_CACHE_MODE == 0
	  || (PRM_LIST_QUERY_CACHE_MODE == 1
	      && (flag & RESULT_CACHE_INHIBITED))
	  || (PRM_LIST_QUERY_CACHE_MODE == 2
	      && !(flag & RESULT_CACHE_REQUIRED)));
}

static bool
qmgr_can_not_get_result_from_cache (QUERY_FLAG flag)
{
  return (PRM_LIST_QUERY_CACHE_MODE == 0
	  || (PRM_LIST_QUERY_CACHE_MODE > 0
	      && ((flag) & NOT_FROM_RESULT_CACHE)));
}

static void
qmgr_put_page_header (PAGE_PTR page_p, QFILE_PAGE_HEADER * header_p)
{
  OR_PUT_INT ((page_p) + QFILE_TUPLE_COUNT_OFFSET, (header_p)->pg_tplcnt);
  OR_PUT_INT ((page_p) + QFILE_PREV_PAGE_ID_OFFSET, (header_p)->prev_pgid);
  OR_PUT_INT ((page_p) + QFILE_NEXT_PAGE_ID_OFFSET, (header_p)->next_pgid);
  OR_PUT_INT ((page_p) + QFILE_LAST_TUPLE_OFFSET, (header_p)->lasttpl_off);
  OR_PUT_INT ((page_p) + QFILE_OVERFLOW_PAGE_ID_OFFSET,
	      (header_p)->ovfl_pgid);
  OR_PUT_SHORT ((page_p) + QFILE_PREV_VOL_ID_OFFSET, (header_p)->prev_volid);
  OR_PUT_SHORT ((page_p) + QFILE_NEXT_VOL_ID_OFFSET, (header_p)->next_volid);
  OR_PUT_SHORT ((page_p) + QFILE_OVERFLOW_VOL_ID_OFFSET,
		(header_p)->ovfl_volid);
}


#if defined (SERVER_MODE)
static void
qmgr_check_mutex_error (int r, const char *file, int line)
{
  if (r != 0)
    {
      fprintf (stderr, "Error at %s(%d) : (%s)\n **** THREAD EXIT ****\n",
	       file, line, "Mutex operation error");
      pthread_exit (NULL);
    }
}

/*
 * qmgr_initialize_mutex () -
 *   return:
 *   qm_mutex(in)       :
 */
static void
qmgr_initialize_mutex (QMGR_MUTEX * mutex_p)
{
  int r;

  mutex_p->owner = (pthread_t) 0;	/* null thread id */
  mutex_p->lock_count = 0;

  r = pthread_mutex_init (&mutex_p->lock, NULL);
  qmgr_check_mutex_error (r, __FILE__, __LINE__);

  r = pthread_cond_init (&mutex_p->not_busy_cond, NULL);
  qmgr_check_mutex_error (r, __FILE__, __LINE__);
  mutex_p->nwaits = 0;
}

/*
 * qmgr_destroy_mutex () -
 *   return:
 *   qm_mutex(in)       :
 */
static void
qmgr_destroy_mutex (QMGR_MUTEX * mutex_p)
{
  mutex_p->owner = (pthread_t) 0;
  pthread_mutex_destroy (&mutex_p->lock);
  pthread_cond_destroy (&mutex_p->not_busy_cond);
}

/*
 * qmgr_lock_mutex () -
 *   return:
 *   qm_mutex(in)       :
 */
static void
qmgr_lock_mutex (THREAD_ENTRY * thread_p, QMGR_MUTEX * mutex_p)
{
  int r;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

  r = pthread_mutex_lock (&mutex_p->lock);
  qmgr_check_mutex_error (r, __FILE__, __LINE__);

  /* If other thread owns this mutex wait until released */
  if (mutex_p->lock_count > 0 && mutex_p->owner != thread_p->tid)
    {
      do
	{
	  mutex_p->nwaits++;
	  pthread_cond_wait (&mutex_p->not_busy_cond, &mutex_p->lock);
	  mutex_p->nwaits--;
	}
      while (mutex_p->lock_count != 0);
    }

  mutex_p->owner = thread_p->tid;
  mutex_p->lock_count++;

  pthread_mutex_unlock (&mutex_p->lock);
}

/*
 * qmgr_unlock_mutex () -
 *   return:
 *   qm_mutex(in)       :
 */
static void
qmgr_unlock_mutex (QMGR_MUTEX * mutex_p)
{
  int r;

  r = pthread_mutex_lock (&mutex_p->lock);
  qmgr_check_mutex_error (r, __FILE__, __LINE__);

  if (--mutex_p->lock_count == 0)
    {
      mutex_p->owner = (pthread_t) 0;
      if (mutex_p->nwaits > 0)	/* there is an waiter */
	{
	  pthread_cond_signal (&mutex_p->not_busy_cond);
	}
    }

  pthread_mutex_unlock (&mutex_p->lock);
}
#endif

static void
qmgr_mark_query_as_completed (QMGR_QUERY_ENTRY * query_p)
{
#if defined (SERVER_MODE)
  int rv;

  rv = pthread_mutex_lock (&query_p->lock);

  query_p->query_mode = QUERY_COMPLETED;
  query_p->interrupt = false;
  query_p->propagate_interrupt = true;

  if (query_p->nwaits > 0)
    {
      pthread_cond_signal (&query_p->cond);
    }

  pthread_mutex_unlock (&query_p->lock);
#else
  query_p->query_mode = QUERY_COMPLETED;
  query_p->interrupt = false;
  query_p->propagate_interrupt = true;
#endif
}

/*
 * qmgr_allocate_query_entry_array () - Allocate new query entry array which has
 * 'qmgr_Query_table.num_entry_per_array' number of elements. We assume that there is not
 * free query entry when this function is called.
 *   return:
 *
 * Note: This function must be called within the critical section
 *       CSECT_QP_QUERY_TABLE.
 */
static QMGR_QUERY_ENTRY *
qmgr_allocate_query_entry_array (void)
{
  QMGR_QUERY_ENTRY *query_p;
  QMGR_QUERY_TABLE *query_table_p;
  int i;

  /* csect_enter(CSECT_QP_QUERY_TABLE, INF_WAIT); */

  query_table_p = &qmgr_Query_table;

  if (query_table_p->free_query_entry_list_p != NULL ||
      query_table_p->num_alloced_array >= QMGR_QUERY_ENTRY_ARRAY_SIZE ||
      query_table_p->total_query_entry_count !=
      query_table_p->num_alloced_array * query_table_p->num_entry_per_array)
    {
      /* csect_exit(CSECT_QP_QUERY_TABLE); */
      return NULL;
    }

  query_table_p->query_entry_array_p[query_table_p->num_alloced_array] =
    (QMGR_QUERY_ENTRY *) malloc (sizeof (QMGR_QUERY_ENTRY) *
				 query_table_p->num_entry_per_array);
  if (query_table_p->query_entry_array_p[query_table_p->num_alloced_array] ==
      NULL)
    {
      /* csect_exit(CSECT_QP_QUERY_TABLE); */
      return NULL;
    }

  query_p =
    query_table_p->query_entry_array_p[query_table_p->num_alloced_array];
  query_table_p->free_query_entry_list_p = query_p;

  for (i = 1; i < query_table_p->num_entry_per_array; i++)
    {
      query_p->list_id = NULL;
      query_p->next_free =
	&query_table_p->
	query_entry_array_p[query_table_p->num_alloced_array][i];

#if defined (SERVER_MODE)
      pthread_mutex_init (&query_p->lock, NULL);
      pthread_cond_init (&query_p->cond, NULL);
      query_p->nwaits = 0;
#endif

      query_p = query_p->next_free;
    }

  query_p->list_id = NULL;
  query_p->next_free = NULL;

#if defined (SERVER_MODE)
  pthread_mutex_init (&query_p->lock, NULL);
  pthread_cond_init (&query_p->cond, NULL);
  query_p->nwaits = 0;
#endif

  query_table_p->free_query_entry_count = query_table_p->num_entry_per_array;
  query_table_p->total_query_entry_count +=
    query_table_p->free_query_entry_count;

  /* csect_exit(CSECT_QP_QUERY_TABLE); */
  return query_table_p->
    query_entry_array_p[query_table_p->num_alloced_array++];
}

/*
 * qmgr_allocate_query_entry () -
 *   return: QMGR_QUERY_ENTRY * or NULL
 *
 * Note: Allocate a query_entry structure from the free
 * list of query_entry structures if any, or by malloc to allocate a new
 * a structure.
 */
static QMGR_QUERY_ENTRY *
qmgr_allocate_query_entry (THREAD_ENTRY * thread_p)
{
  QMGR_QUERY_ENTRY *query_p;

  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return NULL;
    }

  query_p = qmgr_Query_table.free_query_entry_list_p;

  if (query_p != NULL)
    {
      qmgr_Query_table.free_query_entry_list_p = query_p->next_free;
      qmgr_Query_table.free_query_entry_count--;
    }
  else if (qmgr_Query_table.num_alloced_array >= QMGR_QUERY_ENTRY_ARRAY_SIZE)
    {
      csect_exit (CSECT_QPROC_QUERY_TABLE);
      return NULL;
    }
  else
    {
      if (qmgr_allocate_query_entry_array () == NULL)
	{
	  csect_exit (CSECT_QPROC_QUERY_TABLE);
	  return NULL;
	}

      query_p = qmgr_Query_table.free_query_entry_list_p;
      qmgr_Query_table.free_query_entry_list_p = query_p->next_free;
      qmgr_Query_table.free_query_entry_count--;
    }

  /* assign query id */
  if (qmgr_Query_id_count >= SHRT_MAX - 2)	/* overflow happened */
    {
      qmgr_Query_id_count = 0;
    }
  query_p->query_id = ++qmgr_Query_id_count;

  csect_exit (CSECT_QPROC_QUERY_TABLE);

  /* initialize per query temp file VFID structure */
  query_p->next = NULL;
  query_p->next_free = NULL;
  query_p->temp_vfid = NULL;
  query_p->num_tmp = 0;
  query_p->total_count = 0;
  XASL_ID_SET_NULL (&query_p->xasl_id);
  query_p->xasl_ent = NULL;
  query_p->list_id = NULL;
  query_p->list_ent = NULL;
  query_p->errid = NO_ERROR;
  query_p->er_msg = NULL;
  query_p->interrupt = false;
  query_p->propagate_interrupt = true;
  query_p->query_flag = 0;
  query_p->is_holdable = false;
  VPID_SET_NULL (&query_p->save_vpid);	/* Setup default for save_vpid */

  return query_p;
}

/*
 * qmgr_free_query_entry () -
 *   return:
 *   q_ptr(in)  : Query entry structure to be freed
 *
 * Note: Free the query_entry structure by putting it to the free
 * query_entry structure list if there are not many in the list,
 * or by calling db_free.
 */
static void
qmgr_free_query_entry (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * query_p)
{
#if defined (SERVER_MODE)
  if (query_p->er_msg != NULL)
    {
      free_and_init (query_p->er_msg);
    }
#endif

  if (query_p->list_id != NULL)
    {
      QFILE_FREE_AND_INIT_LIST_ID (query_p->list_id);
    }

  query_p->next = NULL;

  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  query_p->next_free = qmgr_Query_table.free_query_entry_list_p;
  qmgr_Query_table.free_query_entry_list_p = query_p;
  qmgr_Query_table.free_query_entry_count++;

  csect_exit (CSECT_QPROC_QUERY_TABLE);
}

/*
 * qmgr_free_query_entry_list () -
 *   return:
 *   q_ptr(in)  : Query Entry Pointer
 *
 * Note: Free the area allocated for the query entry list
 */
static void
qmgr_free_query_entry_list (THREAD_ENTRY * thread_p,
			    QMGR_QUERY_ENTRY * query_p)
{
  QMGR_QUERY_ENTRY *tmp_query_p;

  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  while (query_p != NULL)
    {
      tmp_query_p = query_p;
      query_p = query_p->next;

#if defined (SERVER_MODE)
      if (tmp_query_p->er_msg != NULL)
	{
	  free_and_init (tmp_query_p->er_msg);
	}
#endif

      if (tmp_query_p->list_id != NULL)
	{
	  QFILE_FREE_AND_INIT_LIST_ID (tmp_query_p->list_id);
	}

      tmp_query_p->next = NULL;
      tmp_query_p->next_free = qmgr_Query_table.free_query_entry_list_p;
      qmgr_Query_table.free_query_entry_list_p = tmp_query_p;
      qmgr_Query_table.free_query_entry_count++;
    }

  csect_exit (CSECT_QPROC_QUERY_TABLE);
}

/*
 * qmgr_add_query_entry () -
 *   return:
 *   q_ptr(in)  : Query Entry Pointer
 *   trans_ind(in)      : this transaction index
 *
 * Note: Add the given query entry to the list of query entries for the
 * current transaction.
 */
static void
qmgr_add_query_entry (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * query_p,
		      int tran_index)
{
  QMGR_TRAN_ENTRY *tran_entry_p;

  if (tran_index == NULL_TRAN_INDEX)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  if (tran_entry_p->trans_stat == QMGR_TRAN_NULL ||
      tran_entry_p->trans_stat == QMGR_TRAN_TERMINATED)
    {
      tran_entry_p->trans_stat = QMGR_TRAN_RUNNING;
      tran_entry_p->query_entry_list_p = query_p;
    }
  else
    {
      query_p->next = tran_entry_p->query_entry_list_p;
      tran_entry_p->query_entry_list_p = query_p;
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);
}

static QMGR_QUERY_ENTRY *
qmgr_find_query_entry (QMGR_QUERY_ENTRY * query_list_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p;

  query_p = query_list_p;
  while (query_p && query_p->query_id != query_id)
    {
      query_p = query_p->next;
    }

  return query_p;
}

/*
 * qmgr_get_query_entry () -
 *   return: QMGR_QUERY_ENTRY *
 *   query_id(in)       : query identifier
 *   trans_ind(in)      : this transaction index(NULL_TRAN_INDEX for unknown)
 *
 * Note: Return the query entry pointer for the given query identifier
 * or NULL if the query entry is not found.
 */
QMGR_QUERY_ENTRY *
qmgr_get_query_entry (THREAD_ENTRY * thread_p, QUERY_ID query_id,
		      int tran_index)
{
  QMGR_QUERY_ENTRY *query_p = NULL;
  QMGR_TRAN_ENTRY *tran_entry_p;

  /*
   * The code for finding the query_entry pointer is in-lined in
   * xqmgr_end_query and qmgr_interrupt_query to avoid calling this function.
   */

  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      return query_p;
    }

  if (tran_index == NULL_TRAN_INDEX)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);
  query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p,
				   query_id);
  qmgr_unlock_mutex (&tran_entry_p->lock);
  if (query_p == NULL)
    {
      /* Maybe it is a holdable result and we'll find it in the session state
       * object. In order to be able to use this result, we need to create
       * a new entry for this query in the transaction query entries and copy
       * result information from the session.
       */
      query_p = qmgr_allocate_query_entry (thread_p);
      if (query_p == NULL)
	{
	  return NULL;
	}

      query_p->query_id = query_id;
      if (xsession_load_query_entry_info (thread_p, query_p) != NO_ERROR)
	{
	  qmgr_free_query_entry (thread_p, query_p);
	  query_p = NULL;
	  return NULL;
	}
#if defined (SERVER_MODE)
      /* mark this query as belonging to this transaction */
      if (thread_p != NULL)
	{
	  query_p->tid = thread_p->tid;
	}
#endif
      /* add it to this transaction also */
      qmgr_add_query_entry (thread_p, query_p, tran_index);
    }
  return query_p;
}

/*
 * qmgr_delete_query_entry () -
 *   return:
 *   query_id(in)       : query identifier
 *   trans_ind(in)      : this transaction index(NULL_TRAN_INDEX for unknown)
 *
 * Note: Delete the query entry for the given query identifier from the
 * query entry list for the current transaction.
 */
static void
qmgr_delete_query_entry (THREAD_ENTRY * thread_p, QUERY_ID query_id,
			 int tran_index)
{
  QMGR_QUERY_ENTRY *query_p = NULL, *prev_query_p = NULL;
  QMGR_TRAN_ENTRY *tran_entry_p;

  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      return;
    }

  if (tran_index == NULL_TRAN_INDEX)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  prev_query_p = NULL;
  query_p = tran_entry_p->query_entry_list_p;

  while (query_p && query_p->query_id != query_id)
    {
      prev_query_p = query_p;
      query_p = query_p->next;
    }

  if (query_p == NULL)
    {
      qmgr_unlock_mutex (&tran_entry_p->lock);
      return;
    }

  if (prev_query_p == NULL)
    {
      /* entry is the first entry */
      tran_entry_p->query_entry_list_p = query_p->next;

      if (tran_entry_p->query_entry_list_p == NULL)
	{
	  tran_entry_p->trans_stat = QMGR_TRAN_TERMINATED;
	}
    }
  else
    {
      prev_query_p->next = query_p->next;
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);
  qmgr_free_query_entry (thread_p, query_p);
}

static void
qmgr_initialize_tran_entry (QMGR_TRAN_ENTRY * tran_entry_p)
{
  tran_entry_p->trans_stat = QMGR_TRAN_NULL;
  tran_entry_p->query_entry_list_p = NULL;

#if defined (SERVER_MODE)
  tran_entry_p->exist_active_query = false;
  tran_entry_p->active_sync_query_count = 0;
  tran_entry_p->wait_thread_p = NULL;
#endif

  tran_entry_p->modified_classes_p = NULL;
  qmgr_initialize_mutex (&tran_entry_p->lock);
}

/*
 * qmgr_allocate_tran_entries () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   trans_cnt(in)      : Number of transactions
 *
 * Note: Allocates(Reallocates) the area pointed by the query manager
 * transaction index pointer
 */
int
qmgr_allocate_tran_entries (THREAD_ENTRY * thread_p, int count)
{
  QMGR_TRAN_ENTRY *tran_entry_p;
  int i;

#if defined (SERVER_MODE)
  if (count <= PRM_CSS_MAX_CLIENTS + 1)
    {
      count = PRM_CSS_MAX_CLIENTS + 1;
    }
#endif

  /* enter critical section, this prevents another to perform malloc/init */
  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      qmgr_Query_table.tran_entries_p =
	(QMGR_TRAN_ENTRY *) malloc (count * sizeof (QMGR_TRAN_ENTRY));

      if (qmgr_Query_table.tran_entries_p == NULL)
	{
	  csect_exit (CSECT_QPROC_QUERY_TABLE);
	  return ER_FAILED;
	}

      qmgr_Query_table.num_trans = count;

      /* initialize newly allocated areas */
      tran_entry_p = qmgr_Query_table.tran_entries_p;
      for (i = 0; i < qmgr_Query_table.num_trans; i++)
	{
	  qmgr_initialize_tran_entry (tran_entry_p);
	  tran_entry_p++;
	}

      csect_exit (CSECT_QPROC_QUERY_TABLE);

      return NO_ERROR;
    }

  if (count <= qmgr_Query_table.num_trans)
    {
      csect_exit (CSECT_QPROC_QUERY_TABLE);
      return NO_ERROR;
    }

  qmgr_Query_table.tran_entries_p =
    (QMGR_TRAN_ENTRY *) realloc (qmgr_Query_table.tran_entries_p,
				 count * sizeof (QMGR_TRAN_ENTRY));
  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      csect_exit (CSECT_QPROC_QUERY_TABLE);
      return ER_FAILED;
    }

  tran_entry_p =
    (QMGR_TRAN_ENTRY *) qmgr_Query_table.tran_entries_p +
    qmgr_Query_table.num_trans;
  for (i = qmgr_Query_table.num_trans; i < count; i++)
    {
      qmgr_initialize_tran_entry (tran_entry_p);
      tran_entry_p++;
    }

  qmgr_Query_table.num_trans = count;

  csect_exit (CSECT_QPROC_QUERY_TABLE);

  return NO_ERROR;
}

/*
 * qmgr_free_tran_entries () -
 *   return:
 *
 * Note: frees the area pointed by the query manager transaction index pointer.
 */
static void
qmgr_free_tran_entries (THREAD_ENTRY * thread_p)
{
  QMGR_TRAN_ENTRY *tran_entry_p;
  int i;

  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      return;
    }

  tran_entry_p = qmgr_Query_table.tran_entries_p;
  for (i = 0; i < qmgr_Query_table.num_trans; i++)
    {
      qmgr_destroy_mutex (&tran_entry_p->lock);
      qmgr_free_query_entry_list (thread_p, tran_entry_p->query_entry_list_p);
      tran_entry_p++;
    }

  free_and_init (qmgr_Query_table.tran_entries_p);
  qmgr_Query_table.num_trans = 0;
}

#if defined (CUBRID_DEBUG)
static const char *
qmgr_get_tran_status_string (QMGR_TRAN_STATUS stat)
{
  switch (stat)
    {
    case QMGR_TRAN_NULL:
      return "QMGR_TRAN_NULL";
    case QMGR_TRAN_RUNNING:
      return "QMGR_TRAN_NULL";
    case QMGR_TRAN_DELAYED_START:
      return "QMGR_TRAN_DELAYED_START";
    case QMGR_TRAN_WAITING:
      return "QMGR_TRAN_WAITING";
    case QMGR_TRAN_RESUME_TO_DEALLOCATE:
      return "QMGR_TRAN_RESUME_TO_DEALLOCATE";
    case QMGR_TRAN_RESUME_DUE_DEADLOCK:
      return "QMGR_TRAN_RESUME_DUE_DEADLOCK";
    case QMGR_TRAN_TERMINATED:
      return "QMGR_TRAN_TERMINATED";
    default:
      return "QMGR_UNKNOWN";
    }
}

static void
qmgr_dump_query_entry (QMGR_QUERY_ENTRY * query_p)
{
  QMGR_TEMP_FILE *temp_vfid_p;
  QFILE_LIST_ID *list_id_p;
  int i;

  fprintf (stdout, "\t\tQuery Entry Structures:\n");
  fprintf (stdout, "\t\tquery_id: %lld\n", (long long) query_p->query_id);
  fprintf (stdout, "\t\txasl_id: {{%d, %d}, {%d, %d}}\n",
	   query_p->xasl_id.first_vpid.pageid,
	   query_p->xasl_id.first_vpid.volid,
	   query_p->xasl_id.temp_vfid.fileid,
	   query_p->xasl_id.temp_vfid.volid);
  fprintf (stdout, "\t\txasl: %p\n", (void *) query_p->xasl);
  fprintf (stdout, "\t\txasl_data: %p\n", query_p->xasl_data);
  fprintf (stdout, "\t\txasl_size: %d\n", query_p->xasl_size);
  fprintf (stdout, "\t\trepeat: %d\n", query_p->repeat);
  fprintf (stdout, "\t\tlist_id: %p\n", (void *) query_p->list_id);

  if (query_p->list_id)
    {
      list_id_p = query_p->list_id;
      fprintf (stdout,
	       "\t\t{type_list: {%d, %p}, tuple_cnt: %d, page_cnt: %d,\n"
	       "\t first_vpid: {%d, %d}, last_vpid: {%d, %d},\n"
	       "\t last_pgptr: %p, last_offset: %d, lasttpl_len: %d}\n",
	       list_id_p->type_list.type_cnt,
	       (void *) list_id_p->type_list.domp,
	       list_id_p->tuple_cnt,
	       list_id_p->page_cnt,
	       list_id_p->first_vpid.pageid,
	       list_id_p->first_vpid.volid,
	       list_id_p->last_vpid.pageid,
	       list_id_p->last_vpid.volid,
	       list_id_p->last_pgptr,
	       list_id_p->last_offset, list_id_p->lasttpl_len);
    }

  if (query_p->temp_vfid != NULL)
    {
      temp_vfid_p = query_p->temp_vfid;

      do
	{
	  fprintf (stdout, "\t\tfile_vfid: %p\n", (void *) &temp_vfid_p);
	  fprintf (stdout, "\t\tvpid_array_index: %d\n",
		   temp_vfid_p->vpid_index);
	  fprintf (stdout, "\t\tvpid_array_count: %d\n",
		   temp_vfid_p->vpid_count);

	  if (temp_vfid_p->vpid_index != -1)
	    {
	      for (i = 1; i < temp_vfid_p->vpid_count; i++)
		{
		  fprintf (stdout, "\t\tvpid_array[%d]:\n", i);
		  fprintf (stdout, "\t\t\tpage_id: %d\n",
			   temp_vfid_p->vpid_array[i].pageid);
		  fprintf (stdout, "\t\t\tvol_id: %d\n",
			   temp_vfid_p->vpid_array[i].volid);
		}
	    }

	  temp_vfid_p = temp_vfid_p->next;
	}
      while (temp_vfid_p != query_p->temp_vfid);
    }

  fprintf (stdout, "\t\tnext: %p\n\n", (void *) query_p->next);
}

/*
 * qmgr_dump () -
 *   return:
 *
 * Note: Dump query manager table for debugging purposes.
 */
void
qmgr_dump (void)
{
  QMGR_TRAN_ENTRY *tran_entry_p;
  QMGR_QUERY_ENTRY *query_p;
  int waiting_count, running_count;
  int i;

  /* Get statistics from query manager table */
  waiting_count = running_count = 0;

  tran_entry_p = qmgr_Query_table.tran_entries_p;
  for (i = 0; i < qmgr_Query_table.num_trans; i++)
    {
      if (tran_entry_p->trans_stat == QMGR_TRAN_WAITING)
	{
	  waiting_count++;
	}
      else if (tran_entry_p->trans_stat == QMGR_TRAN_RUNNING)
	{
	  running_count++;
	}

      tran_entry_p++;
    }

  fprintf (stdout, "\n\tQUERY MANAGER TRANSACTION STRUCTURES: \n");
  fprintf (stdout, "\t===================================== \n");
  fprintf (stdout, "\tTrans_cnt: %d\n", qmgr_Query_table.num_trans);
  fprintf (stdout, "\tWait_trans_cnt: %d\n", waiting_count);
  fprintf (stdout, "\tRun_trans_cnt: %d\n", running_count);
  fprintf (stdout, "\n\tTransaction index array: \n");
  fprintf (stdout, "\t------------------------ \n");

  tran_entry_p = qmgr_Query_table.tran_entries_p;
  for (i = 0; i < qmgr_Query_table.num_trans; i++)
    {
      fprintf (stdout, "\tTrans_ind: %d\n", i);
      fprintf (stdout, "\tTrans_stat: %s\n",
	       qmgr_get_tran_status_string (tran_entry_p->trans_stat));

      fprintf (stdout, "\tTrans_query_entries:\n");

      for (query_p = tran_entry_p->query_entry_list_p; query_p;
	   query_p = query_p->next)
	{
	  qmgr_dump_query_entry (query_p);
	}

      fprintf (stdout, "\t------------------------ \n");
      tran_entry_p++;
    }
}
#endif

/*
 * qmgr_initialize () -
 *   return: int (NO_ERROR or ER_FAILED)
 *
 * Note: Initializes the query manager and the query file manager
 * global variables.
 */
int
qmgr_initialize (THREAD_ENTRY * thread_p)
{
  int total_tran_indices;

  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      total_tran_indices = logtb_get_number_of_total_tran_indices ();
      if (qmgr_allocate_tran_entries (thread_p, total_tran_indices) !=
	  NO_ERROR)
	{
	  csect_exit (CSECT_QPROC_QUERY_TABLE);
	  return ER_FAILED;
	}
    }

  if (qmgr_Query_table.query_entry_array_p == NULL)
    {
      qmgr_Query_table.query_entry_array_p =
	(QMGR_QUERY_ENTRY **) malloc (sizeof (QMGR_QUERY_ENTRY *) *
				      QMGR_QUERY_ENTRY_ARRAY_SIZE);
      if (qmgr_Query_table.query_entry_array_p == NULL)
	{
	  csect_exit (CSECT_QPROC_QUERY_TABLE);
	  return ER_FAILED;
	}

      qmgr_Query_table.num_entry_per_array = QMGR_NUM_ENTRY_PER_ARRAY;

      if (qmgr_allocate_query_entry_array () == NULL)
	{
	  csect_exit (CSECT_QPROC_QUERY_TABLE);
	  return ER_FAILED;
	}
    }

  if (qmgr_Query_table.temp_file_list[TEMP_FILE_MEMBUF_NORMAL].list == NULL)
    {
      qmgr_initialize_temp_file_list (&qmgr_Query_table.temp_file_list
				      [TEMP_FILE_MEMBUF_NORMAL],
				      TEMP_FILE_MEMBUF_NORMAL);
    }
  if (qmgr_Query_table.temp_file_list[TEMP_FILE_MEMBUF_KEY_BUFFER].list ==
      NULL)
    {
      qmgr_initialize_temp_file_list (&qmgr_Query_table.temp_file_list
				      [TEMP_FILE_MEMBUF_KEY_BUFFER],
				      TEMP_FILE_MEMBUF_KEY_BUFFER);
    }

  csect_exit (CSECT_QPROC_QUERY_TABLE);

  qmgr_Query_id_count = 0;
  qfile_initialize ();

#if defined (SERVER_MODE)
  numeric_init_power_value_string ();
#endif

  srand48 ((long) time (NULL));

  scan_initialize ();

  return NO_ERROR;
}

/*
 * qmgr_finalize () -
 *   return:
 *
 * Note: Finalizes the query manager functioning by deallocating the
 * memory area pointed by transaction index list pointer.
 */
void
qmgr_finalize (THREAD_ENTRY * thread_p)
{
  QMGR_QUERY_ENTRY *query_p;
  int i, j;

  scan_finalize ();
  qfile_finalize ();

  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  qmgr_free_tran_entries (thread_p);

  for (i = 0; i < qmgr_Query_table.num_alloced_array; i++)
    {
      for (j = 0; j < qmgr_Query_table.num_entry_per_array; j++)
	{
	  query_p = &qmgr_Query_table.query_entry_array_p[i][j];

	  if (query_p != NULL && query_p->list_id != NULL)
	    {
	      QFILE_FREE_AND_INIT_LIST_ID (query_p->list_id);
	    }

#if defined (SERVER_MODE)
	  pthread_mutex_destroy (&query_p->lock);
	  pthread_cond_destroy (&query_p->cond);
#endif
	}

      free_and_init (qmgr_Query_table.query_entry_array_p[i]);
    }

  qmgr_Query_table.tran_entries_p = NULL;
  qmgr_Query_table.free_query_entry_count = 0;
  qmgr_Query_table.total_query_entry_count = 0;
  qmgr_Query_table.num_alloced_array = 0;
  qmgr_Query_table.free_query_entry_list_p = NULL;
  free_and_init (qmgr_Query_table.query_entry_array_p);

  for (i = 0; i < QMGR_NUM_TEMP_FILE_LISTS; i++)
    {
      qmgr_finalize_temp_file_list (&qmgr_Query_table.temp_file_list[i]);
    }
  csect_exit (CSECT_QPROC_QUERY_TABLE);
}

/*
 * xqmgr_prepare_query () - Prepares a query for later (and repetitive) execution
 *   return:  XASL file id which contains the XASL stream
 *   thrd(in)   :
 *   query_str(in)      : query string; used for hash key of the XASL cache
 *   user_oid(in)       :
 *   xasl_stream(in)    : XASL stream;
 *                        set to NULL if you want to look up the XASL cache
 *   xasl_size(in)      : size of the XASL stream in bytes
 *   xasl_id(in)        :
 *
 * Note: Store the given XASL stream into the XASL file and return its file id.
 * The XASL file is a temporay file, ..
 * If NULL is given as the input argument xasl_stream, this function will look
 * up the XASL cache, and return the cached XASL file id if found. If not found,
 * NULL will be returned.
 */
XASL_ID *
xqmgr_prepare_query (THREAD_ENTRY * thread_p, const char *query_string_p,
		     const OID * user_oid_p, const char *xasl_stream_p,
		     int xasl_size, XASL_ID * xasl_id_p)
{
  XASL_CACHE_ENTRY *cache_entry_p;
  char *p;
  int header_size;
  int i;
  OID creator_oid, *class_oid_list_p = NULL;
  int n_oid_list, *repr_id_list_p = NULL;
  int dbval_cnt;
  XASL_ID temp_xasl_id;

  /* If xasl_stream is NULL, it means that the client requested looking up
     the XASL cache to know there's a reusable execution plan (XASL) for
     this query. The XASL is stored as a file so that the XASL file id
     (XASL_ID) will be returned if found in the cache. */

  if (!xasl_stream_p)
    {
      /* lookup the XASL cache with the query string as the key */
      cache_entry_p =
	qexec_lookup_xasl_cache_ent (thread_p, query_string_p, user_oid_p);
      if (cache_entry_p != NULL)
	{
	  XASL_ID_COPY (xasl_id_p, &(cache_entry_p->xasl_id));
	}
      else
	{
	  XASL_ID_SET_NULL (xasl_id_p);
	}

      return xasl_id_p;
    }

  /* xasl_stream is given. It means that the client generated a XASL for
     this query and requested to store it. As a matter of course, the XASL
     cache will be updated after saving the XASL stream into the file.
     The XASL file id (XASL_ID) will be returned if all right. */

  /* at this time, I'd like to look up once again because it is possible
     that the other competing thread which is running the same query has
     updated the cache before me */
  cache_entry_p =
    qexec_lookup_xasl_cache_ent (thread_p, query_string_p, user_oid_p);
  if (cache_entry_p != NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "xqm_query_prepare: second xs_lookup_xasl_cache_ent query_str %s\n",
		    query_string_p);
      XASL_ID_COPY (xasl_id_p, &(cache_entry_p->xasl_id));
      goto exit_on_end;
    }

  if (qfile_store_xasl (thread_p, xasl_stream_p, xasl_size, xasl_id_p) == 0)
    {
      er_log_debug (ARG_FILE_LINE,
		    "xqm_query_prepare: ls_store_xasl failed\n");
      goto exit_on_error;
    }

  /* save the returned XASL_ID for check later */
  XASL_ID_COPY (&temp_xasl_id, xasl_id_p);

  /* get some information from the XASL stream */
  p = or_unpack_int ((char *) xasl_stream_p, &header_size);
  p = or_unpack_int (p, &dbval_cnt);
  p = or_unpack_oid (p, &creator_oid);
  p = or_unpack_int (p, &n_oid_list);

  if (n_oid_list > 0)
    {
      class_oid_list_p =
	(OID *) db_private_alloc (thread_p, sizeof (OID) * n_oid_list);
      repr_id_list_p =
	(int *) db_private_alloc (thread_p, sizeof (int) * n_oid_list);
      if (class_oid_list_p == NULL || repr_id_list_p == NULL)
	{
	  goto exit_on_error;
	}

      for (i = 0; i < n_oid_list; i++)
	{
	  p = or_unpack_oid (p, &class_oid_list_p[i]);
	}
      for (i = 0; i < n_oid_list; i++)
	{
	  p = or_unpack_int (p, &repr_id_list_p[i]);
	}
    }
  else
    {
      class_oid_list_p = NULL;
      repr_id_list_p = NULL;
    }

  cache_entry_p =
    qexec_update_xasl_cache_ent (thread_p, query_string_p, xasl_id_p,
				 &creator_oid, n_oid_list, class_oid_list_p,
				 repr_id_list_p, dbval_cnt);
  if (cache_entry_p == NULL)
    {
      er_log_debug (ARG_FILE_LINE,
		    "xsm_query_prepare: qexec_update_xasl_cache_ent failed xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
		    xasl_id_p->first_vpid.pageid, xasl_id_p->first_vpid.volid,
		    xasl_id_p->temp_vfid.fileid, xasl_id_p->temp_vfid.volid);
      (void) file_destroy (thread_p, &xasl_id_p->temp_vfid);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      if (class_oid_list_p)
	{
	  db_private_free_and_init (thread_p, class_oid_list_p);
	}
      if (repr_id_list_p)
	{
	  db_private_free_and_init (thread_p, repr_id_list_p);
	}

      return NULL;
    }

  if (class_oid_list_p)
    {
      db_private_free_and_init (thread_p, class_oid_list_p);
    }
  if (repr_id_list_p)
    {
      db_private_free_and_init (thread_p, repr_id_list_p);
    }

  /* check whether qexec_update_xasl_cache_ent() changed the XASL_ID */
  if (!XASL_ID_EQ (&temp_xasl_id, xasl_id_p))
    {
      er_log_debug (ARG_FILE_LINE,
		    "xqm_query_prepare: qexec_update_xasl_cache_ent changed xasl_id { first_vpid { %d %d } temp_vfid { %d %d } } to xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
		    temp_xasl_id.first_vpid.pageid,
		    temp_xasl_id.first_vpid.volid,
		    temp_xasl_id.temp_vfid.fileid,
		    temp_xasl_id.temp_vfid.volid,
		    xasl_id_p->first_vpid.pageid, xasl_id_p->first_vpid.volid,
		    xasl_id_p->temp_vfid.fileid, xasl_id_p->temp_vfid.volid);
      /* the other competing thread which is running the has updated the cache
         very after the moment of the previous check;
         simply abandon my XASL file */
      (void) file_destroy (thread_p, &temp_xasl_id.temp_vfid);
    }

exit_on_end:

  if (class_oid_list_p)
    {
      db_private_free_and_init (thread_p, class_oid_list_p);
    }
  if (repr_id_list_p)
    {
      db_private_free_and_init (thread_p, repr_id_list_p);
    }

  return xasl_id_p;

exit_on_error:

  xasl_id_p = NULL;
  goto exit_on_end;
}

#if defined (SERVER_MODE)
/*
 * qmgr_check_active_query_and_wait () -
 *   return:
 *   trans_entry(in)    :
 *   curr_thrd(in)      :
 *   exec_mode(in)      :
 */
static void
qmgr_check_active_query_and_wait (THREAD_ENTRY * thread_p,
				  QMGR_TRAN_ENTRY * tran_entry_p,
				  THREAD_ENTRY * current_thread_p,
				  int exec_mode)
{
  THREAD_ENTRY *prev_thread_p;

  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  if ((tran_entry_p->exist_active_query
       && tran_entry_p->active_sync_query_count == 0)
      || tran_entry_p->wait_thread_p != NULL)
    {
      current_thread_p->next_wait_thrd = NULL;

      if (tran_entry_p->wait_thread_p == NULL)
	{
	  tran_entry_p->wait_thread_p = current_thread_p;
	}
      else
	{
	  prev_thread_p = tran_entry_p->wait_thread_p;
	  while (prev_thread_p->next_wait_thrd != NULL)
	    {
	      prev_thread_p = prev_thread_p->next_wait_thrd;
	    }
	  prev_thread_p->next_wait_thrd = current_thread_p;
	}

      thread_lock_entry (current_thread_p);
      qmgr_unlock_mutex (&tran_entry_p->lock);

      thread_suspend_wakeup_and_unlock_entry (current_thread_p,
					      THREAD_QMGR_ACTIVE_QRY_SUSPENDED);

      if (current_thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT
	  || (current_thread_p->resume_status !=
	      THREAD_QMGR_ACTIVE_QRY_RESUMED))
	{
	  assert (current_thread_p->resume_status ==
		  THREAD_RESUME_DUE_TO_INTERRUPT);

	  ((QMGR_QUERY_ENTRY *) current_thread_p->query_entry)->interrupt =
	    true;
	  ((QMGR_QUERY_ENTRY *) current_thread_p->
	   query_entry)->propagate_interrupt = true;

	  /* remove current thread_p from the tran wait list, if exist */
	  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

	  if (tran_entry_p->wait_thread_p == current_thread_p)
	    {
	      tran_entry_p->wait_thread_p = current_thread_p->next_wait_thrd;
	      current_thread_p->next_wait_thrd = NULL;
	    }
	  else
	    {
	      prev_thread_p = tran_entry_p->wait_thread_p;

	      while (prev_thread_p)
		{
		  if (prev_thread_p->next_wait_thrd == current_thread_p)
		    {
		      prev_thread_p->next_wait_thrd =
			current_thread_p->next_wait_thrd;
		      current_thread_p->next_wait_thrd = NULL;
		      break;
		    }
		  prev_thread_p = prev_thread_p->next_wait_thrd;
		}
	    }
	  qmgr_unlock_mutex (&tran_entry_p->lock);

	  return;
	}
      else
	{
	  assert (current_thread_p->resume_status ==
		  THREAD_QMGR_ACTIVE_QRY_RESUMED);

	  /* wake up properly */
	}

      qmgr_lock_mutex (thread_p, &tran_entry_p->lock);
    }

  if (IS_SYNC_EXEC_MODE (exec_mode))
    {
      tran_entry_p->active_sync_query_count++;
    }

  tran_entry_p->exist_active_query = true;
  qmgr_unlock_mutex (&tran_entry_p->lock);
}
#endif

/*
 * qmgr_check_waiter_and_wakeup () -
 *   return:
 *   trans_entry(in)    :
 *   curr_thrd(in)      :
 *   propagate_interrupt(in)    :
 */
static void
qmgr_check_waiter_and_wakeup (QMGR_TRAN_ENTRY * tran_entry_p,
			      THREAD_ENTRY * current_thread_p,
			      int propagate_interrupt)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *wait_thread_p;

  qmgr_lock_mutex (current_thread_p, &tran_entry_p->lock);

  if (tran_entry_p->active_sync_query_count > 0)
    {
      tran_entry_p->active_sync_query_count--;
    }

  if (tran_entry_p->active_sync_query_count == 0)
    {
      tran_entry_p->exist_active_query = false;
    }

  wait_thread_p = tran_entry_p->wait_thread_p;

  if (wait_thread_p != NULL)
    {
      tran_entry_p->wait_thread_p = wait_thread_p->next_wait_thrd;
      wait_thread_p->next_wait_thrd = NULL;

      if (er_errid () == ER_INTERRUPTED && propagate_interrupt == true)
	{
	  ((QMGR_QUERY_ENTRY *) wait_thread_p->query_entry)->interrupt = true;
	}
      thread_wakeup (wait_thread_p, THREAD_QMGR_ACTIVE_QRY_RESUMED);
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);
  current_thread_p->query_entry = NULL;
#endif
}

/*
 * xqmgr_execute_query () - Execute a prepared query
 *   return: query result file id
 *   thrd(in)   :
 *   xasl_id(in)        : XASL file id that was a result of query_prepare()
 *   query_idp(out)     : query id to be used for getting results
 *   dbval_count(in)      : number of host variables
 *   dbvals(in) : array of host variables (query input parameters)
 *   flagp(in)  : flag to determine if this is an asynchronous query
 *   clt_cache_time(in) :
 *   srv_cache_time(in) :
 *   query_timeout(in) : query_timeout in millisec.
 *
 * Note1: The query result is returned through a list id (actually the list
 * file). Query id is put for further refernece to this query entry.
 * If there's an error, NULL will be returned.
 *
 * Note2: It is the caller's responsibility to free output QFILE_LIST_ID
 * by calling QFILE_FREE_AND_INIT_LIST_ID().
 */
QFILE_LIST_ID *
xqmgr_execute_query (THREAD_ENTRY * thread_p, const XASL_ID * xasl_id_p,
		     QUERY_ID * query_id_p, int dbval_count,
		     const DB_VALUE * dbvals_p, QUERY_FLAG * flag_p,
		     CACHE_TIME * client_cache_time_p,
		     CACHE_TIME * server_cache_time_p, int query_timeout)
{
  XASL_CACHE_ENTRY *xasl_cache_entry_p;
  QFILE_LIST_CACHE_ENTRY *list_cache_entry_p;
  DB_VALUE_ARRAY params;
  QMGR_QUERY_ENTRY *query_p;
  int tran_index = -1;
  QMGR_TRAN_ENTRY *tran_entry_p;
  QFILE_LIST_ID *list_id_p, *tmp_list_id_p;
  bool is_sync_query, error_flag, cached_result;
#if defined (SERVER_MODE)
  QMGR_QUERY_TYPE dummy;
  THREAD_ENTRY *current_thread_p;
#endif
  XASL_CACHE_CLONE *cache_clone_p;
  bool saved_is_stats_on;

  error_flag = false;
  cached_result = false;
  query_p = NULL;
  *query_id_p = -1;
  list_id_p = NULL;
  xasl_cache_entry_p = NULL;
  list_cache_entry_p = NULL;
  is_sync_query = IS_SYNC_EXEC_MODE (*flag_p);
/*  if (*flag_p & RESULT_HOLDABLE)
    {
      is_sync_query = false;
      *flag_p &= ~ASYNC_EXEC;
    } */
#if defined (SERVER_MODE)
  current_thread_p = NULL;
#endif

  saved_is_stats_on = mnt_server_is_stats_on (thread_p);
  if (DO_NOT_COLLECT_EXEC_STATS (*flag_p) && saved_is_stats_on == true)
    {
      xmnt_server_stop_stats (thread_p);
    }

  /* Check the existance of the given XASL. If someone marked it
     to be deleted, then remove it if possible. */
  cache_clone_p = NULL;		/* mark as pop */
  xasl_cache_entry_p = qexec_check_xasl_cache_ent_by_xasl (thread_p,
							   xasl_id_p,
							   dbval_count,
							   &cache_clone_p);
  if (xasl_cache_entry_p == NULL)
    {
      if (DO_NOT_COLLECT_EXEC_STATS (*flag_p) && saved_is_stats_on == true)
	{
	  xmnt_server_start_stats (thread_p, false);
	}

      /* It doesn't be there or was marked to be deleted. */
      er_log_debug (ARG_FILE_LINE,
		    "xqm_query_execute: xs_check_xasl_cache_ent_by_xasl failed"
		    " xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
		    xasl_id_p->first_vpid.pageid, xasl_id_p->first_vpid.volid,
		    xasl_id_p->temp_vfid.fileid, xasl_id_p->temp_vfid.volid);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      return NULL;
    }

  /* If it is not inhibited from getting the cached result, inspect the list
     cache (query result cache) and get the list file id(QFILE_LIST_ID) to be
     returned to the client if it is in there. The list cache will be
     searched with the XASL cache entry of the target query that is obtained
     from the XASL_ID, because all results of the query with different
     parameters (host variables - DB_VALUES) are linked at
     the XASL cache entry. */
  params.size = dbval_count;
  params.vals = (DB_VALUE *) dbvals_p;
  if (!qmgr_can_not_get_result_from_cache (*flag_p))
    {
      /* lookup the list cache with the parameter values (DB_VALUE array) */
      list_cache_entry_p =
	qfile_lookup_list_cache_entry (thread_p,
				       xasl_cache_entry_p->list_ht_no,
				       &params);
      if (list_cache_entry_p != NULL)
	{
	  /* found the cached result */
	  cached_result = true;
	  /* treat as sync query */
	  is_sync_query = true;
	  *flag_p &= ~ASYNC_EXEC;

	  CACHE_TIME_MAKE (server_cache_time_p,
			   &list_cache_entry_p->time_created);
	  if (CACHE_TIME_EQ (client_cache_time_p, server_cache_time_p))
	    {
	      goto end;
	    }
	}
    }

  if (client_cache_time_p)
    {
      CACHE_TIME_RESET (client_cache_time_p);
    }

  /* Make an query entry */
  /* mark that this transaction is running a query */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

#if defined (SERVER_MODE)
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);
  tran_entry_p->trans_stat = QMGR_TRAN_RUNNING;
  if (tran_entry_p->active_sync_query_count > 0)
    {
      /* treat as sync query */
      is_sync_query = true;
      *flag_p &= ~ASYNC_EXEC;
    }
  qmgr_unlock_mutex (&tran_entry_p->lock);

  /* set a timeout if necessary */
  qmgr_set_query_timeout_to_tdes (tran_index, query_timeout);
#endif

  /* allocate a new query entry */
  query_p = qmgr_allocate_query_entry (thread_p);
  if (query_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QM_QENTRY_RUNOUT, 1,
	      qmgr_Query_table.total_query_entry_count);
      goto error;
    }
  if (*flag_p & RESULT_HOLDABLE)
    {
      query_p->is_holdable = true;
    }
  else
    {
      query_p->is_holdable = false;
    }

  /* initialize query entry */
  XASL_ID_COPY (&query_p->xasl_id, xasl_id_p);
  query_p->xasl_ent = xasl_cache_entry_p;
  query_p->list_ent = list_cache_entry_p;	/* for qfile_end_use_of_list_cache_entry() */
  /*XASL_ID_SET_NULL(&q_ptr->xasl_id); */
  query_p->xasl = NULL;
  query_p->xasl_data = NULL;
  query_p->xasl_size = 0;
  query_p->xasl_buf_info = NULL;
  query_p->repeat = false;
  query_p->query_mode = is_sync_query ? SYNC_MODE : ASYNC_MODE;
  query_p->query_flag = *flag_p;
#if defined (SERVER_MODE)
  if (thread_p != NULL)
    {
      query_p->tid = thread_p->tid;
    }
#endif

  /* to return query id */
  *query_id_p = query_p->query_id;
  /* add the entry to the query table */
  qmgr_add_query_entry (thread_p, query_p, tran_index);

  /* If the result didn't come from the cache, build the execution plan
     (XASL tree) from the cached(stored) XASL stream. */
  if (!cached_result)
    {
      /* load the XASL stream from the file of xasl_id */
      if (cache_clone_p == NULL || cache_clone_p->xasl == NULL)
	{			/* not found clone */
	  /* load the XASL stream from the file of xasl_id */
	  if (qfile_load_xasl (thread_p, xasl_id_p, &query_p->xasl_data,
			       &query_p->xasl_size) == 0)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "xqm_query_execute: ls_load_xasl failed"
			    " xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
			    xasl_id_p->first_vpid.pageid,
			    xasl_id_p->first_vpid.volid,
			    xasl_id_p->temp_vfid.fileid,
			    xasl_id_p->temp_vfid.volid);
	      /* error in loading XASL file */
	      /* end the use of the XASL when an error occurred */
	      (void) qexec_end_use_of_xasl_cache_ent (thread_p, xasl_id_p,
						      false);
	      goto error;
	    }
	}
      /* If the XASL cache entry is required to be kept after a query
         execution, do not end use of it. This means that the statement was
         prepared once and will be executed several times repeatedly.
         So, the XASL file and its xasl_id should not be freed and deleted by
         another transaction, e.g. being cache replacement victim. */
      if (DO_NOT_KEEP_PLAN_CACHE (*flag_p))
	{
	  /* end use of XASL cache entry */
	  (void) qexec_end_use_of_xasl_cache_ent (thread_p, xasl_id_p, false);
	}

      if (cache_clone_p == NULL || cache_clone_p->xasl == NULL)
	{			/* not found clone */
	  /* unpack the XASL stream to the XASL tree for execution */
	  if (stx_map_stream_to_xasl (thread_p, &query_p->xasl,
				      query_p->xasl_data, query_p->xasl_size,
				      &query_p->xasl_buf_info))
	    {
	      /* error occurred during unpacking */
	      goto error;
	    }
	  /* free xasl_stream allocated in the qfile_load_xasl() */
	  db_private_free_and_init (thread_p, query_p->xasl_data);

	  if (cache_clone_p)
	    {
	      /* save unpacked XASL tree info */
	      cache_clone_p->xasl = query_p->xasl;
	      cache_clone_p->xasl_buf_info = query_p->xasl_buf_info;
	      query_p->xasl_buf_info = NULL;	/* clear */
	    }
	}
      else
	{
	  /* get previously unpacked XASL tree info */
	  query_p->xasl = cache_clone_p->xasl;
	}

      /* check the number of the host variables for this XASL */
      if (query_p->xasl->dbval_cnt > dbval_count)
	{
	  er_log_debug (ARG_FILE_LINE,
			"xqm_query_execute: dbval_cnt mismatch %d vs %d\n",
			query_p->xasl->dbval_cnt, dbval_count);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE,
		  0);
	  goto error;
	}

#if defined (SERVER_MODE)
      if (!qmgr_is_async_executable (query_p->xasl, &dummy))
	{
	  /* treate as sync query */
	  is_sync_query = true;
	  *flag_p &= ~ASYNC_EXEC;
	}
      else if (qmgr_has_unresolved_types (query_p->xasl))
	{
	  /* if this query has unresolved types and we want to delay sending
	   * the results to the client until those types have been resolved
	   */
	  is_sync_query = true;
	  *flag_p &= ~ASYNC_EXEC;
	}
#endif
    }

  /* If we've got the cached result, return it. Else, process the query */
  if (cached_result)
    {
      /* allocate new QFILE_LIST_ID to be stored in the query entry
         cloning from the QFILE_LIST_ID of the found list cache entry */
      query_p->list_id = qfile_clone_list_id (&list_cache_entry_p->list_id,
					      false);
      if (query_p->list_id == NULL)
	{
	  goto error;
	}
      query_p->list_id->query_id = query_p->query_id;

      /* allocate new QFILE_LIST_ID to be returned as the result and copy from
         the query result; the caller is responsible to free this */
      list_id_p = qfile_clone_list_id (query_p->list_id, false);
      if (list_id_p == NULL)
	{
	  goto error;		/* maybe, memory allocation error */
	}
      list_id_p->last_pgptr = NULL;

      /* mark that the query is completed */
      qmgr_mark_query_as_completed (query_p);

    }
  else
    {
      /* Adjust XASL flag for query result cache.
         For the last list file(QFILE_LIST_ID) as the query result,
         the permanent query result file(FILE_QUERY_AREA) rather than
         temporary file(FILE_EITHER_TMP) will be created
         if and only if XASL_TO_BE_CACHED flag is set. */
      if (!qmgr_is_not_allowed_result_cache (*flag_p))
	{
	  assert (query_p->xasl != NULL);
	  XASL_SET_FLAG (query_p->xasl, XASL_TO_BE_CACHED);
	}

      /* synchronous or asynchronous execution mode? */
      if (is_sync_query)
	{
	  /* regardless of the given query flag (SYNC_MODE or ASYNC_MODE)
	     reset it to sync mode (SYNC_EXEC) */
	  *flag_p &= ~ASYNC_EXEC;

#if defined (SERVER_MODE)
	  /* if a query is executing,
	     wait until exist_active_query becomes false */
	  current_thread_p =
	    (thread_p) ? thread_p : thread_get_thread_entry_info ();
	  current_thread_p->query_entry = query_p;

	  qmgr_check_active_query_and_wait (thread_p, tran_entry_p,
					    current_thread_p, SYNC_EXEC);

	  if (query_p->interrupt)
	    {
	      /* this was async query and interrupted by ???;
	         treat as an error */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      qmgr_set_query_error (thread_p, query_p->query_id);
	      goto error;
	    }
#endif
	  /* execute the query with the value list, if any */
	  query_p->list_id = qexec_execute_query (thread_p, query_p->xasl,
						  dbval_count, dbvals_p,
						  query_p->query_id);
	  /* Note: qexec_execute_query() returns listid (NOT NULL)
	     even if an error was occurred.
	     We should check the error condition and free listid. */
	  if (query_p->errid < 0)
	    {
	      if (query_p->list_id)
		{
		  QFILE_FREE_AND_INIT_LIST_ID (query_p->list_id);
		}
	      /* error occurred during executing the query */
	      goto error;
	    }

	  /* allocate new QFILE_LIST_ID to be returned as the result and copy from
	     the query result; the caller is responsible to free this */
	  list_id_p = qfile_clone_list_id (query_p->list_id, false);
	  if (list_id_p == NULL)
	    {
	      goto error;
	    }
	  list_id_p->last_pgptr = NULL;

#if defined (SERVER_MODE)
	  /* wake up waiters if any */
	  qmgr_check_waiter_and_wakeup (tran_entry_p, current_thread_p,
					query_p->propagate_interrupt);
#endif
	  /* everything is ok, mark that the query is completed */
	  qmgr_mark_query_as_completed (query_p);
	}
      else
	{
#if defined (SERVER_MODE)
	  /* start the query in asynchronous mode and get temporary QFILE_LIST_ID */
	  list_id_p = qmgr_process_async_select (thread_p, cache_clone_p,
						 query_p, dbval_count,
						 dbvals_p);
	  if (list_id_p == NULL)
	    {
	      /* error while starting async query */
	      goto error;
	    }
#else
	  /* error - cannot run a asynchronous query in stand-alone mode */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_NOT_IN_STANDALONE, 1, "asynchronous query");
#endif
	}

      /* If it is allowed to cache the query result or if it is required
         to cache, put the list file id(QFILE_LIST_ID) into the list cache.
         Provided are the corresponding XASL cache entry to be linked,
         and the parameters (host variables - DB_VALUES). */
      if (!qmgr_is_not_allowed_result_cache (*flag_p))
	{
	  /* check once more to ensure that the related XASL entry exists */
	  xasl_cache_entry_p = qexec_check_xasl_cache_ent_by_xasl (thread_p,
								   xasl_id_p,
								   -1, NULL);
	  if (xasl_cache_entry_p == NULL)
	    {
	      /* it could be happen if the XASL entry was (to be) deleted */
	      er_log_debug (ARG_FILE_LINE,
			    "xqm_query_execute: xs_check_xasl_cache_ent_by_xasl failed"
			    " xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
			    xasl_id_p->first_vpid.pageid,
			    xasl_id_p->first_vpid.volid,
			    xasl_id_p->temp_vfid.fileid,
			    xasl_id_p->temp_vfid.volid);
	    }
	  else
	    {
	      if (list_id_p == NULL)
		{
		  goto error;
		}

	      /* the type of the result file should be FILE_QUERY_AREA
	         in order not to deleted at the time of query_end */
	      if (file_get_type (thread_p, &list_id_p->temp_vfid) !=
		  FILE_QUERY_AREA)
		{
		  /* duplicate the list file */
		  tmp_list_id_p = qfile_duplicate_list (thread_p, list_id_p,
							QFILE_FLAG_RESULT_FILE);
		  if (tmp_list_id_p)
		    {
		      qfile_destroy_list (thread_p, list_id_p);
		      QFILE_FREE_AND_INIT_LIST_ID (list_id_p);
		      list_id_p = tmp_list_id_p;
		    }
		}
	      else
		{
		  tmp_list_id_p = list_id_p;	/* just for next if condition */
		}

	      if (tmp_list_id_p == NULL)
		{
		  goto end;	/* return without inserting into the cache */
		}

	      /* update the cache entry for the result associated with
	         the used parameter values (DB_VALUE array) if there is,
	         or make new one */
	      list_cache_entry_p =
		qfile_update_list_cache_entry (thread_p,
					       &xasl_cache_entry_p->
					       list_ht_no, &params, list_id_p,
					       xasl_cache_entry_p->
					       query_string);
	      if (list_cache_entry_p == NULL)
		{
		  char *s;
		  s = (params.size > 0) ?
		    pr_valstring (&params.vals[0]) : NULL;
		  er_log_debug (ARG_FILE_LINE,
				"xqm_query_execute: ls_update_xasl failed"
				" xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }"
				" params { %d %s ... }\n",
				xasl_id_p->first_vpid.pageid,
				xasl_id_p->first_vpid.volid,
				xasl_id_p->temp_vfid.fileid,
				xasl_id_p->temp_vfid.volid, params.size,
				s ? s : "(null)");
		  if (s)
		    {
		      free_and_init (s);
		    }
		}
	      else
		{
		  /* record list cache entry into the query entry
		     for qfile_end_use_of_list_cache_entry() */
		  query_p->list_ent = list_cache_entry_p;

		  CACHE_TIME_MAKE (server_cache_time_p,
				   &list_cache_entry_p->time_created);
		}
	    }
	}
    }

  goto end;			/* skip error handling codelet */

error:
  error_flag = true;
  /* wake up waiters if any */
#if defined (SERVER_MODE)
  if (current_thread_p)
    {
      qmgr_check_waiter_and_wakeup (tran_entry_p, current_thread_p,
				    query_p->propagate_interrupt);
    }
#endif

  /* free QFILE_LIST_ID */
  if (list_id_p)
    {
      QFILE_FREE_AND_INIT_LIST_ID (list_id_p);
    }

  /* end the use of the cached result if any when an error occurred */
  if (cached_result)
    {
      (void) qfile_end_use_of_list_cache_entry (thread_p, list_cache_entry_p,
						false);
    }

end:
  /* free the XASL tree */
  if (is_sync_query && query_p && query_p->xasl_buf_info)
    {
      stx_free_xasl_unpack_info (query_p->xasl_buf_info);
      query_p->xasl_buf_info = NULL;
      query_p->xasl = NULL;
    }

  /* save XASL tree */
  if (is_sync_query && cache_clone_p)
    {
      (void) qexec_check_xasl_cache_ent_by_xasl (thread_p, xasl_id_p, -1,
						 &cache_clone_p);
    }

  if (error_flag && query_p)
    {
      /* mark that the query is completed and then delete this query entry */
      qmgr_mark_query_as_completed (query_p);

      if (qmgr_free_query_temp_file_by_query_entry
	  (thread_p, query_p, tran_index) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"xqm_query_execute: qm_free_query_temp_file_by_qentry");
	}

      qmgr_delete_query_entry (thread_p, query_p->query_id, tran_index);
      *query_id_p = 0;
    }

  if (DO_NOT_COLLECT_EXEC_STATS (*flag_p) && saved_is_stats_on == true)
    {
      xmnt_server_start_stats (thread_p, false);
    }

  return list_id_p;
}

/*
 * xqmgr_prepare_and_execute_query () -
 *   return: Query result file identifier, or NULL
 *   thrd(in)   :
 *   xasl(in)       : XASL tree pointer in unprepared form
 *   xasl_size(in)      : memory area size pointed by the xasl
 *   query_id(in)       :
 *   dbval_count(in)      : Number of positional values
 *   dbval_p(in)      : List of positional values
 *   flag(in)   :
 *   query_timeout(in): set a timeout only if it is positive
 *
 * Note: The specified query is executed and the query result structure
 * which will be basically used for client side cursor operations
 * is returned. If val_cnt > 0, the list of given positional
 * values are used during query execution.
 * The query plan is dropped after the execution.
 * If there is an error, NULL is returned.
 */
QFILE_LIST_ID *
xqmgr_prepare_and_execute_query (THREAD_ENTRY * thread_p, char *xasl_p,
				 int xasl_size, QUERY_ID * query_id_p,
				 int dbval_count, DB_VALUE * dbval_p,
				 QUERY_FLAG * flag_p, int query_timeout)
{
  QMGR_QUERY_ENTRY *query_p;
  QFILE_LIST_ID *list_id_p;
  int tran_index;
  QMGR_TRAN_ENTRY *tran_entry_p;
  bool saved_is_stats_on;

  list_id_p = NULL;
  query_p = NULL;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

#if defined (SERVER_MODE)
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);
  tran_entry_p->trans_stat = QMGR_TRAN_RUNNING;
  qmgr_unlock_mutex (&tran_entry_p->lock);

  /* set a timeout if necessary */
  qmgr_set_query_timeout_to_tdes (tran_index, query_timeout);
#endif

  saved_is_stats_on = mnt_server_is_stats_on (thread_p);
  if (DO_NOT_COLLECT_EXEC_STATS (*flag_p) && saved_is_stats_on == true)
    {
      xmnt_server_stop_stats (thread_p);
    }

  query_p = qmgr_allocate_query_entry (thread_p);
  if (query_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QM_QENTRY_RUNOUT, 1,
	      qmgr_Query_table.total_query_entry_count);
      goto error;
    }

  /* initialize query entry */
  XASL_ID_SET_NULL (&query_p->xasl_id);
  query_p->xasl = NULL;
  query_p->xasl_data = xasl_p;
  query_p->xasl_size = xasl_size;
  query_p->repeat = false;
  query_p->query_mode = SYNC_MODE;
  query_p->query_flag = *flag_p;
#if defined (SERVER_MODE)
  query_p->tid = thread_p->tid;
#endif

  /* add query entry to the query table */
  qmgr_add_query_entry (thread_p, query_p, tran_index);

  /* unpack the XASL stream to the XASL tree for execution */
  if (stx_map_stream_to_xasl (thread_p, &query_p->xasl,
			      query_p->xasl_data, query_p->xasl_size,
			      &query_p->xasl_buf_info))
    {
      /* error occurred during unpacking */
      goto error;
    }
  /* xasl will be freed at sqmgr_prepare_and_execute_query() */
  query_p->xasl_data = xasl_p = NULL;


#if defined (SERVER_MODE)
  if (IS_SYNC_EXEC_MODE (*flag_p)
      || tran_entry_p->active_sync_query_count > 0)
#else
  if (IS_SYNC_EXEC_MODE (*flag_p))
#endif
    {
      *flag_p &= ~ASYNC_EXEC;	/* reset to SYNC_EXEC */

#if defined (SERVER_MODE)
      /* if a query is executing, wait until exist_active_query becomes false */
      thread_p->query_entry = query_p;

      qmgr_check_active_query_and_wait (thread_p, tran_entry_p, thread_p,
					SYNC_EXEC);

      if (query_p->interrupt)
	{
	  /* free the XASL tree */
	  stx_free_xasl_unpack_info (query_p->xasl_buf_info);
	  query_p->xasl_buf_info = NULL;

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	  qmgr_set_query_error (thread_p, query_p->query_id);

	  qmgr_check_waiter_and_wakeup (tran_entry_p, thread_p,
					query_p->propagate_interrupt);
	  goto error;
	}
#endif

      /* execute the query using the value list, if any */
      query_p->list_id =
	qexec_execute_query (thread_p, query_p->xasl, dbval_count, dbval_p,
			     query_p->query_id);

      /* Note: qexec_execute_query() returns listid (NOT NULL) even if an error
       *       was occurred. We should check the error condition and free listid.
       */
      if (query_p->errid < 0)
	{			/* error has occurred */
	  if (query_p->list_id)
	    {
	      QFILE_FREE_AND_INIT_LIST_ID (query_p->list_id);
	    }
	}

      /* free the XASL tree */
      stx_free_xasl_unpack_info (query_p->xasl_buf_info);
      query_p->xasl_buf_info = NULL;


      /* set main memory XASL pointer to NULL */
      query_p->xasl = (XASL_NODE *) NULL;

      *query_id_p = query_p->query_id;

      if (query_p->list_id == NULL)
	{			/* query execution failed */
	  qmgr_check_waiter_and_wakeup (tran_entry_p, thread_p,
					query_p->propagate_interrupt);
	  goto error;
	}

      /* copy and return query result file identifier */
      list_id_p = (QFILE_LIST_ID *) malloc (sizeof (QFILE_LIST_ID));
      if (list_id_p == NULL)
	{
	  qmgr_check_waiter_and_wakeup (tran_entry_p, thread_p,
					query_p->propagate_interrupt);
	  goto error;
	}

      QFILE_CLEAR_LIST_ID (list_id_p);

      if (qfile_copy_list_id (list_id_p, query_p->list_id, true) != NO_ERROR)
	{
	  qmgr_check_waiter_and_wakeup (tran_entry_p, thread_p,
					query_p->propagate_interrupt);
	  goto error;
	}
      list_id_p->last_pgptr = NULL;

      if (list_id_p->sort_list)
	{
	  qfile_free_sort_list (list_id_p->sort_list);
	  list_id_p->sort_list = NULL;
	}

      qmgr_check_waiter_and_wakeup (tran_entry_p, thread_p,
				    query_p->propagate_interrupt);

      qmgr_mark_query_as_completed (query_p);
    }
  else
    {
#if defined (SERVER_MODE)
      list_id_p =
	qmgr_process_async_select (thread_p, NULL, query_p, dbval_count,
				   dbval_p);
      if (list_id_p == NULL)
	{
	  goto async_error;
	}
      else
	{
	  *query_id_p = query_p->query_id;
	}
#else
      /* this is an error - cannot run a streaming query in stand-alone mode */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_NOT_IN_STANDALONE, 1, "asynchronous query");
      list_id_p = NULL;
#endif
    }

  if (DO_NOT_COLLECT_EXEC_STATS (*flag_p) && saved_is_stats_on == true)
    {
      xmnt_server_start_stats (thread_p, false);
    }

  return list_id_p;

error:
  /*
   * free the query entry when error occurrs. note that the query_id should be
   * set to 0 so as to upper levels can detect the error.
   */
  if (query_p)
    {
      qmgr_mark_query_as_completed (query_p);
      if (qmgr_free_query_temp_file_by_query_entry
	  (thread_p, query_p, tran_index) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE,
			"xqm_query_prepare_and_execute: qm_free_query_temp_file_by_qentry");
	}
      qmgr_delete_query_entry (thread_p, query_p->query_id, tran_index);
    }

#if defined (SERVER_MODE)
async_error:
#endif /* SERVER_MODE */
  *query_id_p = 0;
  if (list_id_p)
    {
      free_and_init (list_id_p);
    }

  if (DO_NOT_COLLECT_EXEC_STATS (*flag_p) && saved_is_stats_on == true)
    {
      xmnt_server_start_stats (thread_p, false);
    }

  return NULL;
}

/*
 * xqmgr_end_query () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   thrd(in)   : this thread handle
 *   query_id(in)       : Query Identifier
 *
 * Note: The query result file is destroyed for the specified query.
 * If the query is not repetitive, this calls also removes the
 * query entry from the server query table and invalidates the
 * query identifier. If the query result file destruction fails,
 * ER_FAILED code is returned, but still query entry is removed
 * query identifier is invalidated for unrepetitive queries.
 */
int
xqmgr_end_query (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p = NULL;
  QMGR_TRAN_ENTRY *tran_entry_p = NULL;
  int tran_index, rc = NO_ERROR;
#if defined (SERVER_MODE)
  int rv;
#endif

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  /* get query entry */
  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID, 1,
	      query_id);
      return ER_FAILED;
    }

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p,
				   query_id);
  if (query_p == NULL)
    {
      /* maybe this is a holdable result and we'll find it in the
         session state object */
      xsession_remove_query_entry_info (thread_p, query_id);
      qmgr_unlock_mutex (&tran_entry_p->lock);
      return NO_ERROR;
    }

  if (query_p->is_holdable)
    {
      /* We also need to remove the associated query from the session.
         The call below will not destroy the associated list files */
      xsession_clear_query_entry_info (thread_p, query_id);
    }

#if defined (SERVER_MODE)
  rv = pthread_mutex_lock (&query_p->lock);
  if (query_p->query_mode != QUERY_COMPLETED)
    {
      logtb_set_tran_index_interrupt (thread_p, tran_index, true);
      query_p->interrupt = true;
      query_p->propagate_interrupt = false;
      qmgr_unlock_mutex (&tran_entry_p->lock);
      query_p->nwaits++;

      rv = pthread_cond_wait (&query_p->cond, &query_p->lock);
      query_p->nwaits--;

      query_p->interrupt = false;
      logtb_set_tran_index_interrupt (thread_p, tran_index, false);

      pthread_mutex_unlock (&query_p->lock);
    }
  else
    {
      pthread_mutex_unlock (&query_p->lock);
      qmgr_unlock_mutex (&tran_entry_p->lock);
    }
#endif

  /* end use of the list file of the cahed result */
  if (query_p->xasl_ent && query_p->list_ent)
    {
      (void) qfile_end_use_of_list_cache_entry (thread_p, query_p->list_ent,
						false);
    }

  /* destroy query result list file */
  if (query_p->list_id != NULL)
    {
      QFILE_FREE_AND_INIT_LIST_ID (query_p->list_id);

      /* free external volumes, if any */
      rc = qmgr_free_query_temp_file_by_query_entry (thread_p, query_p,
						     tran_index);
    }

  /* free XASL tree if it hasn't been freed yet */
  if (query_p->xasl_buf_info != NULL)
    {
      stx_free_xasl_unpack_info (query_p->xasl_buf_info);
      query_p->xasl = NULL;
      query_p->xasl_buf_info = NULL;
    }

  /* remove the query entry if not repetitive query */
  if (query_p->repeat == false)
    {
      XASL_ID_SET_NULL (&query_p->xasl_id);
      qmgr_delete_query_entry (thread_p, query_p->query_id, tran_index);
    }

  return rc;
}

/*
 * xqmgr_drop_query_plan () - Drop the stored query plan
 *   return: NO_ERROR or ER_FAILED
 *   query_str(in)      : query string; used for hash key of the XASL cache
 *   user_oid(in)       :
 *   xasl_id(in)        : XASL file id which contains the XASL stream
 *   delete(in) :
 *
 * Note: Delete the XASL cache specified by either the query string or the XASL
 * file id upon request of the client.
 */
int
xqmgr_drop_query_plan (THREAD_ENTRY * thread_p, const char *query_string_p,
		       const OID * user_oid_p, const XASL_ID * xasl_id_p,
		       bool is_drop)
{
  if (query_string_p && user_oid_p)
    {
      if (is_drop)
	{
	  /* delete the XASL cache entry */
	  if (qexec_remove_xasl_cache_ent_by_qstr
	      (thread_p, query_string_p, user_oid_p) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "xqm_query_drop_plan: xs_remove_xasl_cache_ent_by_qstr failed for query_str %s\n",
			    query_string_p);
	      return ER_FAILED;
	    }
	}
      else
	{
	  return ER_FAILED;
	}
    }

  if (xasl_id_p)
    {
      if (is_drop)
	{
	  /* delete the XASL cache entry */
	  if (qexec_remove_xasl_cache_ent_by_xasl (thread_p, xasl_id_p) !=
	      NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "xqm_query_drop_plan: xs_remove_xasl_cache_ent_by_xasl failed for xasl_id { first_vpid { %d %d } temp_vfid { %d %d } }\n",
			    xasl_id_p->first_vpid.pageid,
			    xasl_id_p->first_vpid.volid,
			    xasl_id_p->temp_vfid.fileid,
			    xasl_id_p->temp_vfid.volid);
	      return ER_FAILED;
	    }
	}
      else
	{
	  /* end use of the XASL cache entry */
	  (void) qexec_end_use_of_xasl_cache_ent (thread_p, xasl_id_p, false);
	}
    }

  return NO_ERROR;
}

/*
 * xqmgr_drop_all_query_plans () - Drop all the stored query plans
 *   return: NO_ERROR or ER_FAILED
 *
 * Note: Clear all XASL cache entires out upon request of the client.
 */
int
xqmgr_drop_all_query_plans (THREAD_ENTRY * thread_p)
{
  return qexec_remove_all_xasl_cache_ent_by_xasl (thread_p);
}

/*
 * xqmgr_dump_query_plans () - Dump the content of the XASL cache
 *   return:
 *   outfp(in)  :
 */
void
xqmgr_dump_query_plans (THREAD_ENTRY * thread_p, FILE * out_fp)
{
  (void) qexec_dump_xasl_cache_internal (thread_p, out_fp, 7);
  (void) qexec_dump_filter_pred_cache_internal (thread_p, out_fp, 7);
}

/*
 * xqmgr_dump_query_cache () -
 *   return:
 *   outfp(in)  :
 */
void
xqmgr_dump_query_cache (THREAD_ENTRY * thread_p, FILE * out_fp)
{
  (void) qfile_dump_list_cache_internal (thread_p, out_fp);
}

/*
 *       	       TRANSACTION COORDINATION ROUTINES
 */

static void
qmgr_clear_relative_cache_entries (THREAD_ENTRY * thread_p,
				   QMGR_TRAN_ENTRY * tran_entry_p)
{
  OID_BLOCK_LIST *oid_block_p;
  OID *class_oid_p;
  int i;

  for (oid_block_p = tran_entry_p->modified_classes_p;
       oid_block_p; oid_block_p = oid_block_p->next)
    {
      for (i = 0, class_oid_p = oid_block_p->oid_array;
	   i < oid_block_p->last_oid_idx; i++, class_oid_p++)
	{
	  if (qexec_clear_list_cache_by_class (thread_p, class_oid_p) !=
	      NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "qm_clear_trans_wakeup: qexec_clear_list_cache_by_class failed for"
			    " class { %d %d %d }\n",
			    class_oid_p->pageid, class_oid_p->slotid,
			    class_oid_p->volid);
	    }
	}
    }
}

/*
 * qmgr_clear_trans_wakeup () -
 *   return:
 *   tran_index(in)     : Log Transaction index
 *   tran_died(in)      : Flag to indicate if the transaction has died
 *   is_abort(in)       :
 *
 * Note: This routine is called by the transaction manager and perfoms
 * a clean_up processing for the given transaction index. For
 * each non-repetitive query (that is not currently executing)
 * issued by the transaction, it
 * destroys the query result file, the XASL tree plan and
 * invalidates the query entry(identifier). For each repetitive
 * query issued by the transaction, it destroys the query result
 * file, however it destroys the XASL tree plan and the query
 * entry(identifier), only if the transaction has died. The XASL
 * tree plan for repetitive queries is kept for aborted
 * transactions because it can still be used by the transaction
 * to execute queries.
 */
void
qmgr_clear_trans_wakeup (THREAD_ENTRY * thread_p, int tran_index,
			 bool is_tran_died, bool is_abort)
{
  QMGR_QUERY_ENTRY *q, *p, *t;
  QMGR_TRAN_ENTRY *tran_entry_p;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /* for bulletproofing check if tran_index is a valid index,
   * note that normally this should never happen...
   */
  if (tran_index >= qmgr_Query_table.num_trans
#if defined (SERVER_MODE)
      || tran_index == LOG_SYSTEM_TRAN_INDEX
#endif
    )
    {
#ifdef QP_DEBUG
      er_log_debug (ARG_FILE_LINE, "qm_clear_trans_wakeup:"
		    "Invalid transaction index %d called...\n", tran_index);
#endif
      return;
    }

  p = NULL;

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  if (!QFILE_IS_LIST_CACHE_DISABLED)
    {
      qfile_clear_uncommited_list_cache_entry (tran_index);
    }

  /* if the transaction is aborting, clear relative cache entries */
  if (tran_entry_p->modified_classes_p)
    {
      if (is_abort)
	{
	  qmgr_clear_relative_cache_entries (thread_p, tran_entry_p);
	}

      qmgr_free_oid_block (thread_p, tran_entry_p->modified_classes_p);
      tran_entry_p->modified_classes_p = NULL;
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);

  if (tran_entry_p->query_entry_list_p == NULL)
    {
      return;
    }

  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);
  q = tran_entry_p->query_entry_list_p;

#if defined (SERVER_MODE)
  /* interrupt all active queries */
  while (q != NULL)
    {
      rv = pthread_mutex_lock (&q->lock);

      if (q->query_mode != QUERY_COMPLETED)
	{
	  logtb_set_tran_index_interrupt (thread_p, tran_index, true);
	  q->interrupt = true;
	}

      pthread_mutex_unlock (&q->lock);
      q = q->next;
    }
#endif

#if defined (SERVER_MODE)
/* check if all active queries are finished */
again:
  q = tran_entry_p->query_entry_list_p;
  if (q == NULL)
    {
      qmgr_unlock_mutex (&tran_entry_p->lock);
      return;
    }

  while (q != NULL)
    {
      rv = pthread_mutex_lock (&q->lock);
      /* If I'm the async_query executor, "q_ptr->query_mode" will be set to
       * QUERY_COMPLETED by me. Do you feel uneasy about skipping my query ?
       * Q : Why don't you use "thread_get_thread_entry_info()->tid" instead of
       * "THREAD_ID()" ? */
      if (q->query_mode != QUERY_COMPLETED && q->tid != pthread_self ())
	{
	  qmgr_unlock_mutex (&tran_entry_p->lock);

	  q->nwaits++;
	  pthread_cond_wait (&q->cond, &q->lock);
	  q->nwaits--;
	  pthread_mutex_unlock (&q->lock);

	  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);
	  goto again;
	}
      pthread_mutex_unlock (&q->lock);
      q = q->next;
    }
#endif

  q = tran_entry_p->query_entry_list_p;
  if (q == NULL)
    {
      qmgr_unlock_mutex (&tran_entry_p->lock);
      return;
    }

  while (q != NULL)
    {
      if (q->is_holdable && !is_abort && !is_tran_died)
	{
	  /* this is a commit and we have to add the result to the holdable
	     queries list */
	  if (q->query_mode != QUERY_COMPLETED)
	    {
	      er_log_debug (ARG_FILE_LINE, "query %d not completed !\n",
			    q->query_id);
	    }
	  else
	    {
	      er_log_debug (ARG_FILE_LINE, "query %d is completed!\n",
			    q->query_id);
	    }
	  xsession_store_query_entry_info (thread_p, q);
	  /* reset result info */
	  q->list_id = NULL;
	  q->temp_vfid = NULL;
	}
      /* destroy the query result if not destroyed yet */
      if (q->list_id != NULL)
	{
	  qfile_close_list (thread_p, q->list_id);
	  QFILE_FREE_AND_INIT_LIST_ID (q->list_id);
	}

      /* Note: In cases of abort, the qm must delete its own
       * tempfiles otherwise the file manager will delete them out from
       * under us, leaving us with dangling list_id's and such.  See the
       * functions file_new_destroy_all_tmp and log_abort_local.
       */
      if (q->temp_vfid != NULL)
	{
	  qmgr_free_query_temp_file_by_query_entry (thread_p, q, tran_index);
	}

      /* Clear and free the XASL tree if not yet done */
      /* Do not clear the XASL tree if it is currently being used to execute
       * a query.  It will be cleaned up during normal query failure (The
       * query must have failed due to transaction abortion for us to get
       * here).
       */
      if (q->xasl && q->xasl->query_in_progress == false)
	{
	  if (q->xasl_buf_info != NULL)
	    {
	      (void) qexec_clear_xasl (thread_p, q->xasl, true);
	      stx_free_xasl_unpack_info (q->xasl_buf_info);
	    }
	  /* must nullify qptr->xasl_buf_info here. otherwise,
	   * qmgr_prepare_and_execute_query can seg fault in
	   * its own call to stx_free_xasl_unpack_info.
	   */
	  q->xasl_buf_info = NULL;
	  q->xasl = (XASL_NODE *) NULL;
	}

      /* drop the XASL plan and invalidate query only if the query is
       * non-repetitive and not currently executing or the transaction has died
       */
      if ((!q->xasl || (q->xasl && q->xasl->query_in_progress == false))
	  && (q->repeat == false || is_tran_died))
	{
	  XASL_ID_SET_NULL (&q->xasl_id);
	  /* if there were external volumes created for the transaction,
	   * free them so that they can be used by coming transactions.
	   */
	  if (qmgr_free_query_temp_file_by_query_entry (thread_p, q,
							tran_index) !=
	      NO_ERROR)
	    {
#ifdef QP_DEBUG
	      er_log_debug (ARG_FILE_LINE, "qm_clear_trans_wakeup: "
			    "External volume deletion failed.\n");
#endif
	    }
	  /* remove query entry */
	  t = q;
	  if (p == NULL)
	    {
	      tran_entry_p->query_entry_list_p = q->next;
	      q = tran_entry_p->query_entry_list_p;
	    }
	  else
	    {
	      p->next = q->next;
	      q = p->next;
	    }
	  qmgr_free_query_entry (thread_p, t);

	}
      else
	{
	  p = q;
	  q = q->next;
	}
    }

  if (tran_entry_p->query_entry_list_p == NULL)
    {
      tran_entry_p->trans_stat = QMGR_TRAN_TERMINATED;
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * qmgr_get_tran_status () -
 *   return:
 *   tran_index(in)     :
 */
QMGR_TRAN_STATUS
qmgr_get_tran_status (THREAD_ENTRY * thread_p, int tran_index)
{
  if (tran_index >= 0)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }

  return qmgr_Query_table.tran_entries_p[tran_index].trans_stat;
}

/*
 * qmgr_set_tran_status () -
 *   return:
 *   tran_index(in)     :
 *   trans_status(in)   :
 */
void
qmgr_set_tran_status (THREAD_ENTRY * thread_p, int tran_index,
		      QMGR_TRAN_STATUS trans_status)
{
  if (tran_index >= 0)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }

  qmgr_Query_table.tran_entries_p[tran_index].trans_stat = trans_status;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * qmgr_allocate_oid_block () -
 *   return:
 */
static OID_BLOCK_LIST *
qmgr_allocate_oid_block (THREAD_ENTRY * thread_p)
{
  OID_BLOCK_LIST *oid_block_p;

  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return NULL;
    }

  oid_block_p = qmgr_Query_table.free_oid_block_list_p;

  if (oid_block_p)
    {
      qmgr_Query_table.free_oid_block_list_p = oid_block_p->next;
    }
  else
    {
      oid_block_p = (OID_BLOCK_LIST *) malloc (sizeof (OID_BLOCK_LIST));
      if (oid_block_p == NULL)
	{
	  csect_exit (CSECT_QPROC_QUERY_TABLE);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (OID_BLOCK_LIST));
	  return NULL;
	}
    }


  oid_block_p->next = NULL;
  oid_block_p->last_oid_idx = 0;
  csect_exit (CSECT_QPROC_QUERY_TABLE);

  return oid_block_p;
}

/*
 * qmgr_free_oid_block () -
 *   return:
 *   oid_block(in)      :
 */
static void
qmgr_free_oid_block (THREAD_ENTRY * thread_p, OID_BLOCK_LIST * oid_block_p)
{
  OID_BLOCK_LIST *p;

  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  for (p = oid_block_p; p->next; p = p->next)
    {
      p->last_oid_idx = 0;
    }

  p->last_oid_idx = 0;
  p->next = qmgr_Query_table.free_oid_block_list_p;
  qmgr_Query_table.free_oid_block_list_p = oid_block_p;

  csect_exit (CSECT_QPROC_QUERY_TABLE);
}

/*
 * qmgr_add_modified_class () -
 *   return:
 *   class_oid(in)      :
 */
void
qmgr_add_modified_class (THREAD_ENTRY * thread_p, const OID * class_oid_p)
{
  int tran_index;
  QMGR_TRAN_ENTRY *tran_entry_p;
  OID_BLOCK_LIST *oid_block_p, *tmp_oid_block_p;
  OID *tmp_oid_p;
  int i;
  bool found;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  if (tran_entry_p->modified_classes_p == NULL
      && (tran_entry_p->modified_classes_p =
	  qmgr_allocate_oid_block (thread_p)) == NULL)
    {
      qmgr_unlock_mutex (&tran_entry_p->lock);
      return;
    }

  found = false;
  tmp_oid_block_p = tran_entry_p->modified_classes_p;
  do
    {
      oid_block_p = tmp_oid_block_p;
      for (i = 0, tmp_oid_p = oid_block_p->oid_array;
	   i < oid_block_p->last_oid_idx; i++, tmp_oid_p++)
	{
	  if (oid_compare (class_oid_p, tmp_oid_p) == 0)
	    {
	      found = true;
	      break;
	    }
	}
      tmp_oid_block_p = oid_block_p->next;
    }
  while (tmp_oid_block_p);

  if (!found)
    {
      if (oid_block_p->last_oid_idx < OID_BLOCK_ARRAY_SIZE)
	{
	  oid_block_p->oid_array[oid_block_p->last_oid_idx++] = *class_oid_p;
	}
      else if ((oid_block_p->next = qmgr_allocate_oid_block (thread_p)) !=
	       NULL)
	{
	  oid_block_p = oid_block_p->next;
	  oid_block_p->oid_array[oid_block_p->last_oid_idx++] = *class_oid_p;
	}
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);
}

/*
 *       	     PAGE ALLOCATION/DEALLOCATION ROUTINES
 */

/*
 * qmgr_get_old_page () -
 *   return:
 *   vpidp(in)  :
 *   tfile_vfidp(in)    :
 */
PAGE_PTR
qmgr_get_old_page (THREAD_ENTRY * thread_p, VPID * vpid_p,
		   QMGR_TEMP_FILE * tfile_vfid_p)
{
  int tran_index;
  PAGE_PTR page_p;
#if defined(SERVER_MODE)
  bool dummy;
#endif /* SERVER_MODE */

  if (vpid_p->volid == NULL_VOLID && tfile_vfid_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_TEMP_FILE, 1,
	      LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      return NULL;
    }

  if (vpid_p->volid == NULL_VOLID)
    {
      /* return memory buffer */
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

      if (vpid_p->pageid >= 0 && vpid_p->pageid <= tfile_vfid_p->membuf_last)
	{
	  /* interrupt check */
#if defined (SERVER_MODE)
	  if (thread_get_check_interrupt (thread_p) == true
	      && logtb_is_interrupted_tran (thread_p, true, &dummy,
					    tran_index) == true)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      page_p = NULL;
	    }
	  else
	    {
	      page_p = tfile_vfid_p->membuf[vpid_p->pageid];
	    }
#else
	  page_p = tfile_vfid_p->membuf[vpid_p->pageid];
#endif
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_INVALID_TEMP_FILE, 1, tran_index);
	  page_p = NULL;
	}
    }
  else
    {
      /* return temp file page */
      page_p = pgbuf_fix (thread_p, vpid_p, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
    }

  return page_p;
}

/*
 * qmgr_free_old_page () -
 *   return:
 *   page_ptr(in)       :
 *   tfile_vfidp(in)    :
 */
void
qmgr_free_old_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p,
		    QMGR_TEMP_FILE * tfile_vfid_p)
{
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (tfile_vfid_p == NULL)	/* already closed */
    {
      return;
    }

  if (!qmgr_is_page_in_temp_file_buffer (page_p, tfile_vfid_p))
    {
      pgbuf_unfix (thread_p, page_p);
    }
#if defined (SERVER_MODE)
  else
    {
      rv = pthread_mutex_lock (&tfile_vfid_p->membuf_mutex);
      if (tfile_vfid_p->membuf_thread_p)
	{
#if 0				/* wakeup */
	  if (tfile_vfid_p->wait_page_ptr == page_p)
	    {
	      thread_wakeup (tfile_vfid_p->membuf_thread_p,
			     THREAD_QMGR_MEMBUF_PAGE_RESUMED);
	      tfile_vfid_p->membuf_thread_p = NULL;
	      tfile_vfid_p->wait_page_ptr = NULL;
	    }
#else
	  /* xqfile_get_list_file_page() is doing conditional wait */
	  thread_wakeup (tfile_vfid_p->membuf_thread_p,
			 THREAD_QMGR_MEMBUF_PAGE_RESUMED);
	  tfile_vfid_p->membuf_thread_p = NULL;
#endif
	}

      pthread_mutex_unlock (&tfile_vfid_p->membuf_mutex);
    }
#endif
}

/*
 * qmgr_set_dirty_page () -
 *   return:
 *   page_ptr(in)       :
 *   free_page(in)      :
 *   addrp(in)  :
 *   tfile_vfidp(in)    :
 */
void
qmgr_set_dirty_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, int free_page,
		     LOG_DATA_ADDR * addr_p, QMGR_TEMP_FILE * tfile_vfid_p)
{
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (!qmgr_is_page_in_temp_file_buffer (page_p, tfile_vfid_p))
    {
      log_skip_logging (thread_p, addr_p);
      pgbuf_set_dirty (thread_p, page_p, free_page);
    }
#if defined (SERVER_MODE)
  else if (free_page == FREE)
    {
      rv = pthread_mutex_lock (&tfile_vfid_p->membuf_mutex);
      if (tfile_vfid_p->membuf_thread_p)
	{
	  /* xqfile_get_list_file_page() is doing conditional wait */
#if 0				/* wakeup */
	  if (tfile_vfid_p->wait_page_ptr == page_p)
	    {
	      thread_wakeup (tfile_vfid_p->membuf_thread_p,
			     THREAD_QMGR_MEMBUF_PAGE_RESUMED);
	      tfile_vfid_p->membuf_thread_p = NULL;
	      tfile_vfid_p->wait_page_ptr = NULL;
	    }
#else
	  thread_wakeup (tfile_vfid_p->membuf_thread_p,
			 THREAD_QMGR_MEMBUF_PAGE_RESUMED);
	  tfile_vfid_p->membuf_thread_p = NULL;
#endif
	}
      pthread_mutex_unlock (&tfile_vfid_p->membuf_mutex);
    }
#endif
}

/*
 * qmgr_get_new_page () -
 *   return: PAGE_PTR
 *   vpidp(in)  : Set to the allocated real page identifier
 *   tfile_vfidp(in)    : Query Associated with the XASL tree
 *
 * Note: A new query file page is allocated and returned. The page
 * fetched and returned, is not locked. This routine is called
 * succesively to allocate pages for the query result files (list
 * files) or XASL tree files.
 * If an error occurs, NULL pointer is returned.
 */
PAGE_PTR
qmgr_get_new_page (THREAD_ENTRY * thread_p, VPID * vpid_p,
		   QMGR_TEMP_FILE * tfile_vfid_p)
{
  PAGE_PTR page_p;

  if (tfile_vfid_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_TEMP_FILE, 1,
	      LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      return NULL;
    }

  /* first page, return memory buffer instead real temp file page */
  if (tfile_vfid_p->membuf != NULL
      && tfile_vfid_p->membuf_last < tfile_vfid_p->membuf_npages - 1)
    {
      vpid_p->volid = NULL_VOLID;
      vpid_p->pageid = ++(tfile_vfid_p->membuf_last);
      return tfile_vfid_p->membuf[tfile_vfid_p->membuf_last];
    }

  /* memory buffer is exhausted; create temp file */
  if (VFID_ISNULL (&tfile_vfid_p->temp_vfid))
    {
      if (file_create_tmp (thread_p, &tfile_vfid_p->temp_vfid,
			   TEMP_FILE_DEFAULT_PAGES, NULL) == NULL)
	{
	  vpid_p->pageid = NULL_PAGEID;
	  return NULL;
	}
      tfile_vfid_p->temp_file_type = FILE_EITHER_TMP;
      tfile_vfid_p->last_free_page_index =
	file_get_numpages (thread_p, &tfile_vfid_p->temp_vfid) - 1;
    }

  /* try to get pages from an external temp file */
  page_p = qmgr_get_external_file_page (thread_p, vpid_p, tfile_vfid_p);
  if (page_p == NULL)
    {
      /* more temp file page is unavailable; cause error to stop the query */
      vpid_p->pageid = NULL_PAGEID;
      if (er_errid () == ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_QPROC_OUT_OF_TEMP_SPACE, 0);
	}
    }

  return page_p;
}

/*
 * qmgr_get_external_file_page () -
 *   return: PAGE_PTR
 *   vpid(in)   : Set to the allocated virtual page identifier
 *   tmp_vfid(in)       : tempfile_vfid struct pointer
 *
 * Note: This function tries to allocate a new page from an external
 * query file, fetchs and returns the page pointer. Since,
 * pages are not shared by different transactions, it does not
 * lock the page on fetching. If it can not allocate a new page,
 * necessary error code is set and NULL pointer is returned.
 */
static PAGE_PTR
qmgr_get_external_file_page (THREAD_ENTRY * thread_p, VPID * vpid_p,
			     QMGR_TEMP_FILE * tmp_vfid_p)
{
  PAGE_PTR page_p;
  int nthpg;
  LOG_DATA_ADDR addr;
  DKNPAGES num_pages = TEMP_FILE_DEFAULT_PAGES;
  QFILE_PAGE_HEADER page_header =
    { 0, NULL_PAGEID, NULL_PAGEID, 0, NULL_PAGEID,
    NULL_VOLID, NULL_VOLID, NULL_VOLID
  };

  /*
   * If there are existing pages allocated in the vpid_array[], use them
   * Currently we use file_find_nthpages to allocate xx vpids which are then stored in
   * the query_entry structure. vpid_count is the actual # of pages returned
   * by file_find_nthpages and used to compare against the vpid_index to avoid handing
   * out garbage.
   */

  if (tmp_vfid_p->vpid_index != -1)
    {
      page_p = pgbuf_fix (thread_p,
			  &(tmp_vfid_p->vpid_array[tmp_vfid_p->vpid_index]),
			  NEW_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
      if (page_p == NULL)
	{
	  VPID_SET_NULL (vpid_p);
	  return NULL;
	}

      tmp_vfid_p->curr_free_page_index++;
      *vpid_p = tmp_vfid_p->vpid_array[tmp_vfid_p->vpid_index++];
      qmgr_put_page_header (page_p, &page_header);

      addr.vfid = &tmp_vfid_p->temp_vfid;
      addr.pgptr = page_p;
      addr.offset = -1;		/* irrelevant */
      log_skip_logging (thread_p, &addr);	/* ignore logging */

      pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

      if (tmp_vfid_p->vpid_index >= tmp_vfid_p->vpid_count)
	{
	  tmp_vfid_p->vpid_index = -1;
	}

      return page_p;
    }

  if (tmp_vfid_p->curr_free_page_index > tmp_vfid_p->last_free_page_index)
    {
      /* existing temporary file needs to be expanded */

      /*
       * allocate next extent of pages for the file
       * Don't care about initializing the pages
       */
      if (file_alloc_pages_as_noncontiguous (thread_p, &tmp_vfid_p->temp_vfid,
					     vpid_p, &nthpg, num_pages, NULL,
					     NULL, NULL, NULL) == NULL)
	{
	  /* if error was no more pages, ignore this expected error code */
	  if (er_errid () != ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME)
	    {
	      return NULL;
	    }

	  /* Find out how many pages are available and ask for that many pages.
	   * Since we are in a multi-user environment, we have no guarantee that
	   * we'll be able to get the number of pages maxpgs_could_allocate()
	   * returns.  Also right before we call maxpgs, another user may free
	   * up a bunch of pages, so only ask for the min of num_pages and
	   * our current request.
	   */
	  num_pages =
	    file_find_maxpages_allocable (thread_p, &tmp_vfid_p->temp_vfid);
	  if (num_pages <= 0)
	    {
	      return NULL;
	    }

	  /*
	   * allocate next extent of pages for the file
	   * Don't care about initializing the pages
	   */
	  if (file_alloc_pages_as_noncontiguous (thread_p,
						 &tmp_vfid_p->temp_vfid,
						 vpid_p, &nthpg, num_pages,
						 NULL, NULL, NULL,
						 NULL) == NULL)
	    {
	      return NULL;
	    }
	}

      /* reset file page indices information */
      tmp_vfid_p->last_free_page_index += num_pages;
    }

  /* fetch and return the external volume page */
  tmp_vfid_p->vpid_count =
    file_find_nthpages (thread_p, &tmp_vfid_p->temp_vfid,
			tmp_vfid_p->vpid_array,
			tmp_vfid_p->curr_free_page_index,
			QMGR_VPID_ARRAY_SIZE);
  if (tmp_vfid_p->vpid_count == -1)
    {
      tmp_vfid_p->vpid_index = -1;
      return NULL;
    }

  tmp_vfid_p->curr_free_page_index++;
  tmp_vfid_p->vpid_index = 0;
  tmp_vfid_p->total_count += num_pages;

  page_p = pgbuf_fix (thread_p,
		      &(tmp_vfid_p->vpid_array[tmp_vfid_p->vpid_index]),
		      NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page_p == NULL)
    {
      VPID_SET_NULL (vpid_p);
      return NULL;
    }

  *vpid_p = tmp_vfid_p->vpid_array[tmp_vfid_p->vpid_index++];
  qmgr_put_page_header (page_p, &page_header);

  if (tmp_vfid_p->vpid_index >= tmp_vfid_p->vpid_count)
    {
      tmp_vfid_p->vpid_index = -1;
    }

  addr.vfid = &tmp_vfid_p->temp_vfid;
  addr.pgptr = page_p;
  addr.offset = -1;		/* irrelevant */
  log_skip_logging (thread_p, &addr);	/* ignore logging */

  pgbuf_set_dirty (thread_p, page_p, DONT_FREE);

  return page_p;
}

static QMGR_TEMP_FILE *
qmgr_allocate_tempfile_with_buffer (int num_buffer_pages)
{
  size_t size;
  QMGR_TEMP_FILE *tempfile_p;

  size = DB_ALIGN (sizeof (QMGR_TEMP_FILE), MAX_ALIGNMENT);
  size += DB_ALIGN (sizeof (PAGE_PTR) * num_buffer_pages, MAX_ALIGNMENT);
  size += DB_PAGESIZE * num_buffer_pages;

  tempfile_p = (QMGR_TEMP_FILE *) malloc (size);
  if (tempfile_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      size);
    }
  memset (tempfile_p, 0x00, size);

  return tempfile_p;
}

/*
 * qmgr_create_new_temp_file () -
 *   return:
 *   query_id(in)       :
 */
QMGR_TEMP_FILE *
qmgr_create_new_temp_file (THREAD_ENTRY * thread_p, QUERY_ID query_id,
			   QMGR_TEMP_FILE_MEMBUF_TYPE membuf_type)
{
  QMGR_QUERY_ENTRY *query_p;
  QMGR_TRAN_ENTRY *tran_entry_p;
  int tran_index, i, num_buffer_pages;
  QMGR_TEMP_FILE *tfile_vfid_p, *temp;
  PAGE_PTR page_p;
  QFILE_PAGE_HEADER pgheader = { 0, NULL_PAGEID, NULL_PAGEID, 0, NULL_PAGEID,
    NULL_VOLID, NULL_VOLID, NULL_VOLID
  };

  assert (QMGR_IS_VALID_MEMBUF_TYPE (membuf_type));
  if (!QMGR_IS_VALID_MEMBUF_TYPE (membuf_type))
    {
      return NULL;
    }

  num_buffer_pages = (membuf_type == TEMP_FILE_MEMBUF_NORMAL) ?
    PRM_TEMP_MEM_BUFFER_PAGES : PRM_INDEX_SCAN_KEY_BUFFER_PAGES;

  tfile_vfid_p =
    qmgr_get_temp_file_from_list (&qmgr_Query_table.temp_file_list
				  [membuf_type]);
  if (tfile_vfid_p == NULL)
    {
      tfile_vfid_p = qmgr_allocate_tempfile_with_buffer (num_buffer_pages);
    }

  if (tfile_vfid_p == NULL)
    {
      return NULL;
    }

  tfile_vfid_p->membuf = (PAGE_PTR *) ((PAGE_PTR) tfile_vfid_p +
				       DB_ALIGN (sizeof (QMGR_TEMP_FILE),
						 MAX_ALIGNMENT));

  /* initialize tfile_vfid */
  VFID_SET_NULL (&tfile_vfid_p->temp_vfid);
  tfile_vfid_p->temp_file_type = FILE_EITHER_TMP;
  tfile_vfid_p->curr_free_page_index = 0;
  tfile_vfid_p->last_free_page_index = -1;
  tfile_vfid_p->vpid_index = -1;
  tfile_vfid_p->vpid_count = 0;
  tfile_vfid_p->membuf_npages = num_buffer_pages;
  tfile_vfid_p->membuf_type = membuf_type;

  for (i = 0; i < QMGR_VPID_ARRAY_SIZE; i++)
    {
      VPID_SET_NULL (&tfile_vfid_p->vpid_array[i]);
    }

  tfile_vfid_p->total_count = 0;
  tfile_vfid_p->membuf_last = -1;
  page_p = (PAGE_PTR) ((PAGE_PTR) tfile_vfid_p->membuf +
		       DB_ALIGN (sizeof (PAGE_PTR) *
				 tfile_vfid_p->membuf_npages, MAX_ALIGNMENT));

  for (i = 0; i < tfile_vfid_p->membuf_npages; i++)
    {
      tfile_vfid_p->membuf[i] = page_p;
      qmgr_put_page_header (page_p, &pgheader);
      page_p += DB_PAGESIZE;
    }

#if defined (SERVER_MODE)
  pthread_mutex_init (&tfile_vfid_p->membuf_mutex, NULL);
  tfile_vfid_p->membuf_thread_p = NULL;
#if 0				/* async wakeup */
  tfile_vfid_p->wait_page_ptr = NULL;
#endif
#endif

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);
  /* find query entry */
  if (qmgr_Query_table.tran_entries_p != NULL)
    {
      query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p,
				       query_id);
    }
  else
    {
      query_p = NULL;
    }

  if (query_p == NULL)
    {
      free_and_init (tfile_vfid_p);
      qmgr_unlock_mutex (&tran_entry_p->lock);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID, 1,
	      query_id);
      return NULL;
    }

  /* chain allocated tfile_vfid to the query_entry */
  temp = query_p->temp_vfid;
  query_p->temp_vfid = tfile_vfid_p;
  if (temp != NULL)
    {
      /* link to the list */
      tfile_vfid_p->next = temp;
      tfile_vfid_p->prev = temp->prev;
      tfile_vfid_p->prev->next = tfile_vfid_p;
      temp->prev = tfile_vfid_p;
    }
  else
    {
      /* Add transaction to wfg as a holder of temporary file space, but
         only do so for the first temp file that we create.  From the wfg's
         point of view, there's no difference between holding one file or
         holding one hundred. */
      tfile_vfid_p->next = tfile_vfid_p;
      tfile_vfid_p->prev = tfile_vfid_p;
    }
  /* increase the counter of query entry */
  query_p->num_tmp++;

  qmgr_unlock_mutex (&tran_entry_p->lock);

  return tfile_vfid_p;
}

/*
 * qmgr_create_result_file () - create a temporary file for query result
 *   return:
 *   query_id(in)       :
 */
QMGR_TEMP_FILE *
qmgr_create_result_file (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p;
  int tran_index, i;
  QMGR_TEMP_FILE *tfile_vfid_p, *temp;
  QMGR_TRAN_ENTRY *tran_entry_p;

  /* Allocate a tfile_vfid and create a temporary file for query result */

  tfile_vfid_p = (QMGR_TEMP_FILE *) malloc (sizeof (QMGR_TEMP_FILE));
  if (tfile_vfid_p == NULL)
    {
      return NULL;
    }

  VFID_SET_NULL (&tfile_vfid_p->temp_vfid);

  if (file_create_queryarea (thread_p, &tfile_vfid_p->temp_vfid,
			     TEMP_FILE_DEFAULT_PAGES,
			     "Query result file") == NULL)
    {
      free_and_init (tfile_vfid_p);
      return NULL;
    }

  tfile_vfid_p->temp_file_type = FILE_QUERY_AREA;
  /* initialize the allocated tfile_vfid */
  tfile_vfid_p->curr_free_page_index = 0;
  tfile_vfid_p->last_free_page_index =
    file_get_numpages (thread_p, &tfile_vfid_p->temp_vfid) - 1;
  tfile_vfid_p->vpid_index = -1;
  tfile_vfid_p->vpid_count = 0;

  for (i = 0; i < QMGR_VPID_ARRAY_SIZE; i++)
    {
      VPID_SET_NULL (&tfile_vfid_p->vpid_array[i]);
    }

  tfile_vfid_p->total_count = 0;
  tfile_vfid_p->membuf_last = PRM_TEMP_MEM_BUFFER_PAGES - 1;
  tfile_vfid_p->membuf = NULL;
  tfile_vfid_p->membuf_npages = 0;
  tfile_vfid_p->membuf_type = TEMP_FILE_MEMBUF_NONE;

#if defined (SERVER_MODE)
  pthread_mutex_init (&tfile_vfid_p->membuf_mutex, NULL);
  tfile_vfid_p->membuf_thread_p = NULL;
#endif

#if 0				/* async wakeup */
  tfile_vfid_p->wait_page_ptr = NULL;
#endif

  /* Find the query entry and chain the created temp file to the entry */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &(qmgr_Query_table.tran_entries_p[tran_index]);
  /* lock the query entry table until the end of this function */
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);
  /* find the query entry */
  if (qmgr_Query_table.tran_entries_p != NULL)
    {
      query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p,
				       query_id);
    }
  else
    {
      query_p = NULL;
    }

  if (query_p == NULL)
    {
      /* query entry is not found */
      qmgr_unlock_mutex (&tran_entry_p->lock);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID,
	      1, query_id);
      free_and_init (tfile_vfid_p);
      return NULL;
    }

  /* chain the tfile_vfid to the query_entry->temp_vfid */
  temp = query_p->temp_vfid;
  query_p->temp_vfid = tfile_vfid_p;
  if (temp != NULL)
    {
      /* insert into the head of the double linked list */
      tfile_vfid_p->next = temp;
      tfile_vfid_p->prev = temp->prev;
      tfile_vfid_p->prev->next = tfile_vfid_p;
      temp->prev = tfile_vfid_p;
    }
  else
    {
      /* first one */
      tfile_vfid_p->next = tfile_vfid_p;
      tfile_vfid_p->prev = tfile_vfid_p;
    }
  /* increase the counter of query entry */
  query_p->num_tmp++;

  qmgr_unlock_mutex (&tran_entry_p->lock);

  return tfile_vfid_p;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * qmgr_free_query_temp_file () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   query_id(in) : Query ID to determine what temp file (if any) to destroy
 *
 * Note: Destroy the external temporary file used, if any.
 */
int
qmgr_free_query_temp_file (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p;
  QMGR_TRAN_ENTRY *tran_entry_p;
  int tran_index, rc;
  QMGR_TEMP_FILE *tfile_vfid_p, *temp;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  if (qmgr_Query_table.tran_entries_p != NULL)
    {
      query_p =
	qmgr_find_query_entry (tran_entry_p->query_entry_list_p, query_id);
    }
  else
    {
      query_p = NULL;
    }

  if (query_p == NULL)
    {
      /* if this is a streaming query and it is terminating */
      qmgr_unlock_mutex (&tran_entry_p->lock);
      return NO_ERROR;
    }

  rc = NO_ERROR;
  if (query_p->temp_vfid)
    {
      tfile_vfid_p = query_p->temp_vfid;
      tfile_vfid_p->prev->next = NULL;

      while (tfile_vfid_p)
	{
	  if (!(tfile_vfid_p->temp_file_type == FILE_QUERY_AREA
		&& query_p->errid >= 0)
	      && !VFID_ISNULL (&tfile_vfid_p->temp_vfid))
	    {
	      if (file_destroy (thread_p, &tfile_vfid_p->temp_vfid) !=
		  NO_ERROR)
		{
		  /* stop; return error */
		  rc = ER_FAILED;
		}
	    }
	  temp = tfile_vfid_p;
	  tfile_vfid_p = tfile_vfid_p->next;

#if defined (SERVER_MODE)
	  rv = pthread_mutex_lock (&temp->membuf_mutex);

	  if (temp->membuf_thread_p)
	    {
	      if (er_errid () != NO_ERROR)
		{
		  qmgr_set_query_error (thread_p, query_id);
		}
	      thread_wakeup (temp->membuf_thread_p,
			     THREAD_QMGR_MEMBUF_PAGE_RESUMED);
	      temp->membuf_thread_p = NULL;
	    }
	  pthread_mutex_unlock (&temp->membuf_mutex);
	  pthread_mutex_destroy (&temp->membuf_mutex);
#endif

	  if (temp->temp_file_type != FILE_QUERY_AREA)
	    {
	      rv =
		pthread_mutex_lock (&qmgr_Query_table.temp_file_free_mutex);

	      /* add to the free list */
	      if (qmgr_Query_table.temp_file_free_count <
		  QMGR_TEMP_FILE_FREE_LIST_SIZE)
		{
		  temp->prev = NULL;
		  temp->next = qmgr_Query_table.temp_file_free_list;
		  qmgr_Query_table.temp_file_free_list = temp;
		  qmgr_Query_table.temp_file_free_count++;
		  temp = NULL;
		}

	      pthread_mutex_unlock (&qmgr_Query_table.temp_file_free_mutex);
	    }

	  /* free too many temp_file */
	  if (temp)
	    {
	      free_and_init (temp);
	    }
	}
      query_p->temp_vfid = NULL;
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);
  return rc;
}
#endif

/*
 * qmgr_free_temp_file_list () - free temporary files in tfile_vfid_p
 * return : error code or NO_ERROR
 * thread_p (in) :
 * tfile_vfid_p (in)  : temporary files list
 * query_id (in)      : query id
 * is_error (in)      : true if query was unsuccessful
 */
int
qmgr_free_temp_file_list (THREAD_ENTRY * thread_p,
			  QMGR_TEMP_FILE * tfile_vfid_p, QUERY_ID query_id,
			  bool is_error)
{
  QMGR_TEMP_FILE *temp = NULL;
  int rc = NO_ERROR, fd_ret = NO_ERROR;

  /* make sure temp file list is not cyclic */
  assert (tfile_vfid_p->prev == NULL || tfile_vfid_p->prev->next == NULL);

  while (tfile_vfid_p)
    {
      fd_ret = NO_ERROR;
      if ((tfile_vfid_p->temp_file_type != FILE_QUERY_AREA || is_error)
	  && !VFID_ISNULL (&tfile_vfid_p->temp_vfid))
	{
	  fd_ret = file_destroy (thread_p, &tfile_vfid_p->temp_vfid);
	  if (fd_ret != NO_ERROR)
	    {
	      /* set error but continue with the destroy process */
	      rc = ER_FAILED;
	    }
	}

      temp = tfile_vfid_p;
      tfile_vfid_p = tfile_vfid_p->next;

#if defined (SERVER_MODE)
      pthread_mutex_lock (&temp->membuf_mutex);
      if (temp->membuf_thread_p)
	{
	  if (fd_ret != NO_ERROR)
	    {
	      qmgr_set_query_error (thread_p, query_id);
	    }
	  thread_wakeup (temp->membuf_thread_p,
			 THREAD_QMGR_MEMBUF_PAGE_RESUMED);
	  temp->membuf_thread_p = NULL;
	}

      pthread_mutex_unlock (&temp->membuf_mutex);
      pthread_mutex_destroy (&temp->membuf_mutex);
#endif

      if (temp->temp_file_type != FILE_QUERY_AREA)
	{
	  qmgr_put_temp_file_into_list (temp);
	}
      else
	{
	  free_and_init (temp);
	}
    }

  return rc;
}

/*
 * qmgr_free_query_temp_file_by_query_entry () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   query_entryp(in)   : Query entry ptr to determine what temp file (if any)
 *                        to destroy
 *   tran_idx(in)       :
 *
 * Note: Destroy the external temporary file used, if any.
 */
static int
qmgr_free_query_temp_file_by_query_entry (THREAD_ENTRY * thread_p,
					  QMGR_QUERY_ENTRY * query_p,
					  int tran_index)
{
  int rc;
  QMGR_TEMP_FILE *tfile_vfid_p;
  QMGR_TRAN_ENTRY *tran_entry_p;

  if (query_p == NULL || qmgr_Query_table.tran_entries_p == NULL)
    {
      return NO_ERROR;
    }

  if (tran_index == NULL_TRAN_INDEX)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  rc = NO_ERROR;
  if (query_p->temp_vfid)
    {
      bool is_error = (query_p->errid < 0);
      tfile_vfid_p = query_p->temp_vfid;
      tfile_vfid_p->prev->next = NULL;

      rc = qmgr_free_temp_file_list (thread_p, tfile_vfid_p,
				     query_p->query_id, is_error);

      query_p->temp_vfid = NULL;
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);
  return rc;
}

/*
 * qmgr_free_list_temp_file () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   query_id(in) : Query ID to determine what temp file (if any) to destroy
 *   tfile_vfidp(in): Address of QMGR_TEMP_FILE
 *
 * Note: Destroy the external temporary file used, if any.  The caller
 * is responsible for setting pointers to this tmp_vfid to NULL afterwards.
 */
int
qmgr_free_list_temp_file (THREAD_ENTRY * thread_p, QUERY_ID query_id,
			  QMGR_TEMP_FILE * tfile_vfid_p)
{
  QMGR_QUERY_ENTRY *query_p;
  QMGR_TRAN_ENTRY *tran_entry_p;
  int tran_index, rc;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  if (qmgr_Query_table.tran_entries_p != NULL)
    {
      query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p,
				       query_id);
    }
  else
    {
      query_p = NULL;
    }

  if (query_p == NULL)
    {
      /* if this is a streaming query and it is terminating */
      qmgr_unlock_mutex (&tran_entry_p->lock);
      return NO_ERROR;
    }

  rc = NO_ERROR;
  if (query_p->temp_vfid)
    {
      if (!VFID_ISNULL (&tfile_vfid_p->temp_vfid))
	{
	  if (file_destroy (thread_p, &tfile_vfid_p->temp_vfid) != NO_ERROR)
	    {
	      /* stop; return error */
	      rc = ER_FAILED;
	    }
	}

      if (query_p->temp_vfid->next == query_p->temp_vfid)
	{
	  query_p->temp_vfid = NULL;
	}
      else
	{
	  tfile_vfid_p->next->prev = tfile_vfid_p->prev;
	  tfile_vfid_p->prev->next = tfile_vfid_p->next;
	  if (query_p->temp_vfid == tfile_vfid_p)
	    {
	      query_p->temp_vfid = tfile_vfid_p->next;
	    }
	}

#if defined (SERVER_MODE)
      rv = pthread_mutex_lock (&tfile_vfid_p->membuf_mutex);

      if (tfile_vfid_p->membuf_thread_p)
	{
	  if (er_errid () != NO_ERROR)
	    {
	      qmgr_set_query_error (thread_p, query_id);
	    }
	  thread_wakeup (tfile_vfid_p->membuf_thread_p,
			 THREAD_QMGR_MEMBUF_PAGE_RESUMED);
	  tfile_vfid_p->membuf_thread_p = NULL;
	}

      pthread_mutex_unlock (&tfile_vfid_p->membuf_mutex);
      pthread_mutex_destroy (&tfile_vfid_p->membuf_mutex);
#endif

      if (tfile_vfid_p->temp_file_type != FILE_QUERY_AREA)
	{
	  qmgr_put_temp_file_into_list (tfile_vfid_p);
	}
      else if (tfile_vfid_p)
	{
	  /* free too many temp_file */
	  free_and_init (tfile_vfid_p);
	}
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);
  return NO_ERROR;
}

#if defined (SERVER_MODE)
/*
 * qmgr_is_thread_executing_async_query () -
 *   return:
 *   thrd_entry(in)     :
 */
bool
qmgr_is_thread_executing_async_query (THREAD_ENTRY * thread_p)
{
  QMGR_QUERY_ENTRY *query_p = (QMGR_QUERY_ENTRY *) thread_p->query_entry;

  return (query_p != NULL) && (query_p->query_mode == ASYNC_MODE);
}

/*
 * qmgr_execute_async_select () -
 *   return:
 *   async(in)  :
 */
static void
qmgr_execute_async_select (THREAD_ENTRY * thread_p,
			   QMGR_ASYNC_QUERY * async_query_p)
{
  XASL_CACHE_CLONE *cache_clone_p;
  XASL_NODE *xasl_p;
  int dbval_count;
  DB_VALUE *dbval_p, *tmp_dbval_p;
  int tran_index, i;
  QUERY_ID query_id;
  QMGR_QUERY_ENTRY *query_p = NULL;
  QMGR_TRAN_ENTRY *tran_entry_p;
  HL_HEAPID pri_heap_id;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }

#if defined (ENABLE_UNUSED_FUNCTION)
  cache_clone_p = async_query_p->cache_clone_p;
#else /* ENABLE_UNUSED_FUNCTION */
  cache_clone_p = NULL;
#endif /* !ENABLE_UNUSED_FUNCTION */
  xasl_p = async_query_p->xasl;
  dbval_count = async_query_p->dbval_count;
  dbval_p = (DB_VALUE *) async_query_p->dbval_p;
  query_id = async_query_p->query_id;
  tran_index = async_query_p->tran_index;
  pri_heap_id = async_query_p->pri_heap_id;

  thread_p->tran_index = tran_index;
  pthread_mutex_unlock (&thread_p->tran_index_lock);

  free_and_init (async_query_p);

  XASL_SET_FLAG (xasl_p, XASL_QEXEC_MODE_ASYNC);

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p,
				   query_id);

  qmgr_unlock_mutex (&tran_entry_p->lock);

  if (query_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID,
	      1, query_id);

      if (dbval_p != NULL)
	{
	  HL_HEAPID curr_heap_id;

	  curr_heap_id = db_change_private_heap (NULL, pri_heap_id);

	  for (i = 0, tmp_dbval_p = dbval_p; i < dbval_count;
	       i++, tmp_dbval_p++)
	    {
	      db_value_clear (tmp_dbval_p);
	    }
	  db_private_free (NULL, dbval_p);

	  db_change_private_heap (NULL, curr_heap_id);
	}

      if (cache_clone_p)
	{
#if defined (ENABLE_UNUSED_FUNCTION)
	  /* plan_cache=on; called from xqmgr_execute_query() */
	  /* push clone back to free_list - give up; add to free_list */
	  (void) qexec_free_xasl_cache_clo (cache_clone_p);
#endif /* ENABLE_UNUSED_FUNCTION */
	}
      else
	{
	  /* later, free XASL tree at xqmgr_end_query() */
	}

      return;
    }

  query_p->tid = thread_p->tid;
  /* save query entry pointer */
  thread_p->query_entry = query_p;

  qmgr_check_active_query_and_wait (thread_p, tran_entry_p, thread_p,
				    ASYNC_EXEC);
  if (query_p->interrupt)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      qmgr_set_query_error (thread_p, query_p->query_id);
      query_p->list_id = qexec_get_xasl_list_id (xasl_p);
    }
  else
    {
      query_p->list_id = qexec_execute_query (thread_p, xasl_p, dbval_count,
					      dbval_p, query_id);
    }

  if (dbval_p != NULL)
    {
      HL_HEAPID curr_heap_id;

      curr_heap_id = db_change_private_heap (NULL, pri_heap_id);

      for (i = 0, tmp_dbval_p = dbval_p; i < dbval_count; i++, tmp_dbval_p++)
	{
	  db_value_clear (tmp_dbval_p);
	}
      db_private_free (NULL, dbval_p);

      db_change_private_heap (NULL, curr_heap_id);
    }

  if (cache_clone_p)
    {
#if defined (ENABLE_UNUSED_FUNCTION)
      /* plan_cache=on; called from xqmgr_execute_query() */
      /* save XASL tree */
      (void) qexec_check_xasl_cache_ent_by_xasl (thread_p, &query_p->xasl_id,
						 -1, &cache_clone_p);
#endif /* ENABLE_UNUSED_FUNCTION */
    }
  else
    {
      /* plan_cache=off; called from xqmgr_prepare_and_execute_query() */
      /* free XASL tree */
      stx_free_xasl_unpack_info (query_p->xasl_buf_info);
    }
  query_p->xasl = (XASL_NODE *) NULL;
  query_p->xasl_buf_info = NULL;

  qmgr_check_waiter_and_wakeup (tran_entry_p, thread_p,
				query_p->propagate_interrupt);

  /* Mark the Async Query as completed */
  qmgr_mark_query_as_completed (query_p);

  db_destroy_private_heap (thread_p, pri_heap_id);

  /* clear memory to be used at async worker thread */
  db_clear_private_heap (thread_p, 0);

  return;
}
#endif

/*
 * xqmgr_get_query_info () -
 *   return:
 *   query_id(in)       :
 */
int
xqmgr_get_query_info (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p = NULL;

#if defined (SERVER_MODE)
  /* if query execution has error. */
  if (qmgr_get_query_error_with_id (thread_p, query_id) < 0)
    {
      qmgr_set_query_error (thread_p, query_id);
      return -1;
    }
#endif

  query_p = qmgr_get_query_entry (thread_p, query_id, NULL_TRAN_INDEX);

  if (query_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID,
	      1, query_id);
      return -1;
    }

  /* Errors are handled by sqmgr_get_query_info */
  if (query_p->list_id)
    {
      return (query_p->list_id->tuple_cnt);
    }
  else
    {
      return -1;
    }
}

/*
 * qmgr_get_area_error_async () -
 *   return:
 *   length(in) :
 *   count(in)  :
 *   query_id(in)       :
 */
void *
qmgr_get_area_error_async (THREAD_ENTRY * thread_p, int *length_p, int count,
			   QUERY_ID query_id)
{
  int len;
  char *area, *ptr;
  QMGR_QUERY_ENTRY *query_p;
#if 0
  VPID vpid;
#endif
  int done = true;
  int s_len, strlen;
  int errid;
  char *er_msg;
#if 0
  PAGE_PTR last_pgptr = NULL;
#endif
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  query_p = qmgr_get_query_entry (thread_p, query_id, NULL_TRAN_INDEX);
  if (query_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID,
	      1, query_id);
      return NULL;
    }


  rv = pthread_mutex_lock (&query_p->lock);
  errid = query_p->errid;
  er_msg = query_p->er_msg;
  done = (query_p->query_mode == ASYNC_MODE) ? false : true;
  pthread_mutex_unlock (&query_p->lock);

  if (er_msg == NULL || errid == NO_ERROR)
    {
      *length_p = len = (OR_INT_SIZE * 5);
      strlen = 0;
    }
  else
    {
      s_len = or_packed_string_length (er_msg, &strlen);
      *length_p = len = (OR_INT_SIZE * 4) + s_len;
    }

  area = (char *) malloc (len);
  if (area == NULL)
    {
      *length_p = 0;
      return NULL;
    }

  ptr = area;

#if 0
  if (query_p->list_id != NULL)
    {
      last_pgptr = query_p->list_id->last_pgptr;
      if (last_pgptr)
	{
	  QFILE_GET_NEXT_VPID (&vpid,
			       last_pgptr) if (vpid.pageid != NULL_PAGEID)
	    {
	      done = false;
	      /*
	       * If the query is not completed, return ONLY the number of
	       * tuples that can be retrieved without blocking the client app.
	       * That is, do not count the tuples on the uncompleted last page.
	       */
	      count -= QFILE_GET_TUPLE_COUNT (last_pgptr);
	    }
	}
    }
#endif

  OR_PUT_INT (ptr, len);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, done);
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, count);
  ptr += OR_INT_SIZE;
  /*
   * Now copy the information
   */
  OR_PUT_INT (ptr, (int) (errid));
  ptr += OR_INT_SIZE;
  or_pack_string_with_length (ptr, er_msg, strlen);

  return area;
}

/*
 * qmgr_interrupt_query () -
 *   return:
 *   query_id(in)       :
 */
bool
qmgr_interrupt_query (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p = NULL;
  QMGR_TRAN_ENTRY *tran_entry_p;
  int tran_index;
  int is_interrupted;

  /*
   * get query entry - This is done in-line to avoid qmgr_get_query_entry
   * from returning NULL when the query is being interrupted
   */
  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID,
	      1, query_id);
      return true;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p,
				   query_id);

  if (query_p == NULL)
    {
      qmgr_unlock_mutex (&tran_entry_p->lock);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID,
	      1, query_id);
      return true;
    }
  else
    {
      is_interrupted = query_p->interrupt;
      qmgr_unlock_mutex (&tran_entry_p->lock);
    }

  return (is_interrupted);
}

#if defined (SERVER_MODE)

/*
 * xqmgr_sync_query () -
 *   return:
 *   query_id(in)       :
 *   wait(in)   :
 *   new_list_id(in)    :
 *   call_from_server(in)       :
 *
 * Note: The parameter(QFILE_LIST_ID *new_list_id) is added to update the list_id
 */
int
xqmgr_sync_query (THREAD_ENTRY * thread_p, QUERY_ID query_id, int wait,
		  QFILE_LIST_ID * new_list_id_p, int call_from_server)
{
  QMGR_QUERY_ENTRY *query_p = NULL;
  QMGR_TRAN_ENTRY *tran_entry_p;
  int trans_ind, rv, prev_error;

  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      return ER_FAILED;
    }

  trans_ind = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[trans_ind];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p,
				   query_id);

  if (query_p == NULL)
    {
      qmgr_unlock_mutex (&tran_entry_p->lock);
      return ER_FAILED;
    }

  if (IS_SYNC_EXEC_MODE (query_p->query_flag))
    {
      /* Query is either not a streaming query or is completed or has error */
      qmgr_unlock_mutex (&tran_entry_p->lock);
      goto end;
    }

  rv = pthread_mutex_lock (&query_p->lock);
  qmgr_unlock_mutex (&tran_entry_p->lock);
  /*
   * If the query is not completed and "wait" is set, then wait else
   * terminate the query and then wait.
   */
  if (wait != true && query_p->query_mode != QUERY_COMPLETED)
    {
      logtb_set_tran_index_interrupt (thread_p, trans_ind, true);
      query_p->interrupt = true;
      if (call_from_server)
	{
	  query_p->propagate_interrupt = false;
	}
    }

  /* Now wait for the query to actually end */
  if (query_p)
    {
      if (query_p->query_mode != QUERY_COMPLETED)
	{
	  query_p->nwaits++;
	  rv = pthread_cond_wait (&query_p->cond, &query_p->lock);
	  query_p->nwaits--;
	  pthread_mutex_unlock (&query_p->lock);
	}
      else
	{
	  pthread_mutex_unlock (&query_p->lock);
	}
    }

  prev_error = er_errid ();

  /* check if query has error.
   * A new error in the query_entry will be set */
  if (qmgr_get_query_error_with_id (thread_p, query_id) < 0)
    {
      if (wait != true && er_errid () == ER_INTERRUPTED)
	{
	  /* If there was a previous error, do not call er_clear. */
	  if (call_from_server && prev_error == NO_ERROR)
	    {
	      er_clear ();
	    }
	  goto end;
	}

      return ER_FAILED;
    }

end:
  if (new_list_id_p && query_p->list_id)
    {
      /* copy the current list id */
      if (qfile_copy_list_id (new_list_id_p, query_p->list_id, true) !=
	  NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * qmgr_is_async_query_interrupted () -
 *   return:
 *   query_id(in)       :
 */
bool
qmgr_is_async_query_interrupted (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p = NULL;
  QMGR_TRAN_ENTRY *tran_entry_p;
  int tran_index;
  bool r = false;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  qmgr_lock_mutex (thread_p, &tran_entry_p->lock);

  query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p,
				   query_id);

  if (query_p != NULL)
    {
      r = (query_p->query_mode == ASYNC_MODE && query_p->interrupt == true);
    }

  qmgr_unlock_mutex (&tran_entry_p->lock);

  return r;
}

/*
 * qmgr_get_query_error_with_id () -
 *   return:
 *   query_id(in)       :
 */
int
qmgr_get_query_error_with_id (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p = NULL;

  query_p = qmgr_get_query_entry (thread_p, query_id, NULL_TRAN_INDEX);

  if (query_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID,
	      (int) query_id);
      return ER_QPROC_UNKNOWN_QUERYID;
    }

  return qmgr_get_query_error_with_entry (query_p);
}
#endif

/*
 * qmgr_get_query_error_with_entry () -
 *   return:
 *   query_entryp(in)   :
 */
int
qmgr_get_query_error_with_entry (QMGR_QUERY_ENTRY * query_p)
{
  int errid;
  char *er_msg;
  char *error_area, *p;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  rv = pthread_mutex_lock (&query_p->lock);
  errid = query_p->errid;
  er_msg = query_p->er_msg;
  pthread_mutex_unlock (&query_p->lock);

  if (errid < 0)
    {
      p = error_area =
	(char *) malloc (3 * OR_INT_SIZE + strlen (er_msg) + 1);

      if (error_area)
	{
	  p = or_pack_int (p, errid);
	  p = or_pack_int (p, ER_ERROR_SEVERITY);
	  p = or_pack_int (p, strlen (er_msg) + 1);
	  strcpy (p, er_msg);

	  er_set_area_error (error_area);
	  free_and_init (error_area);
	}
    }

  return errid;
}

/*
 * qmgr_set_query_error () - set current thread's error to query entry
 *   return:
 *   query_id(in)       :
 */
void
qmgr_set_query_error (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  query_p = qmgr_get_query_entry (thread_p, query_id, NULL_TRAN_INDEX);
  if (query_p != NULL)
    {
      rv = pthread_mutex_lock (&query_p->lock);
      if (query_p->errid != NO_ERROR)
	{
	  /* if an error was already set, don't overwrite it */
	  pthread_mutex_unlock (&query_p->lock);
	  return;
	}

      query_p->errid = er_errid ();
      if (query_p->errid != NO_ERROR)
	{
#if defined (SERVER_MODE)
	  char *ptr = (char *) er_msg ();

	  if (ptr != NULL)
	    {
	      query_p->er_msg = strdup (ptr);
	    }
	  else
	    {
	      query_p->er_msg = NULL;
	    }
#else
	  query_p->er_msg = (char *) er_msg ();
#endif
	}
      else
	{
	  query_p->er_msg = NULL;
	}

      pthread_mutex_unlock (&query_p->lock);
    }
}

#if defined (SERVER_MODE)
/*
 * qmgr_find_leaf () -
 *   return:
 *   xasl(in)   :
 */
static XASL_NODE *
qmgr_find_leaf (XASL_NODE * xasl_p)
{
  /* Search down the left side until a BUILDLIST_PROC node is found */
  for (xasl_p = xasl_p->proc.union_.left; xasl_p;
       xasl_p = xasl_p->proc.union_.left)
    {
      if (xasl_p->type == BUILDLIST_PROC)
	{
	  break;
	}
    }

  return xasl_p;
}

/*
 * qmgr_has_unresolved_types () - check if the outptr list has unresolved
 *				  types
 * return : boolean
 * xasl_p (in) : XASL tree
 */
static bool
qmgr_has_unresolved_types (XASL_NODE * xasl_p)
{
  PROC_TYPE xasl_type = xasl_p->type;
  REGU_VARIABLE_LIST list = NULL;

  if (xasl_p->outptr_list == NULL)
    {
      return false;
    }

  switch (xasl_type)
    {
    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
    case BUILDLIST_PROC:
    case BUILDVALUE_PROC:
    case SCAN_PROC:
    case MERGELIST_PROC:
    case CONNECTBY_PROC:
      break;
    default:
      return false;
      break;
    }
  list = xasl_p->outptr_list->valptrp;
  while (list != NULL)
    {
      if (list->value.domain == NULL)
	{
	  continue;
	}
      if (TP_DOMAIN_TYPE (list->value.domain) == DB_TYPE_VARIABLE)
	{
	  return true;
	}
      list = list->next;
    }
  return false;
}

/*
 * qmgr_is_async_executable () -
 *   return:
 *   xasl(in)       :
 *   query_type(in)     :
 */
static bool
qmgr_is_async_executable (XASL_NODE * xasl_p, QMGR_QUERY_TYPE * query_type_p)
{
  PROC_TYPE xasl_type = xasl_p->type;

  if (xasl_type == UNION_PROC
      || xasl_type == DIFFERENCE_PROC || xasl_type == INTERSECTION_PROC)
    {
      *query_type_p = UNION_QUERY;
      return false;
    }

  if (xasl_type == BUILDVALUE_PROC)
    {
      *query_type_p = VALUE_QUERY;
      return false;
    }

  if (xasl_type == BUILDLIST_PROC
      && xasl_p->proc.buildlist.groupby_list != NULL)
    {
      *query_type_p = GROUPBY_QUERY;
      return false;
    }

  if (xasl_type == BUILDLIST_PROC
      && xasl_p->proc.buildlist.a_func_list != NULL)
    {
      *query_type_p = ANALYTIC_QUERY;
      return false;
    }

  if (xasl_p->option == Q_DISTINCT)
    {
      *query_type_p = DISTINCT_QUERY;
      return false;
    }

  /* ORDER BY */
  if (xasl_type == BUILDLIST_PROC && xasl_p->orderby_list)
    {
      if (xasl_p->ordbynum_val != NULL
	  || !qfile_is_sort_list_covered (xasl_p->after_iscan_list,
					  xasl_p->orderby_list))
	{
	  /* cannot ignore sorting */
	  *query_type_p = ORDERBY_QUERY;
	  return false;
	}
    }

  *query_type_p = OTHERS;
  return true;
}

/*
 * qmgr_process_async_select () -
 *   return:
 *   cache_clone_p(in)    :
 *   q_ptr(in)  :
 *   dbval_count(in)      :
 *   dbval_p(in)      :
 */
static QFILE_LIST_ID *
qmgr_process_async_select (THREAD_ENTRY * thread_p,
			   XASL_CACHE_CLONE * cache_clone_p,
			   QMGR_QUERY_ENTRY * query_p, int dbval_count,
			   const DB_VALUE * dbval_p)
{
  QFILE_LIST_ID *tmp_list_p;
  XASL_NODE *xasl_p, *tmp_xasl_p;
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;
  OUTPTR_LIST *outptr_list_p;
  XASL_STATE xasl_state;
  QMGR_ASYNC_QUERY *async_query_p;
  CSS_JOB_ENTRY *job_p;
  QMGR_QUERY_TYPE query_type;

  xasl_state.vd.dbval_cnt = dbval_count;
  xasl_state.vd.dbval_ptr = (DB_VALUE *) dbval_p;
  xasl_state.vd.xasl_state = &xasl_state;

  /* save the query_id into the XASL state struct */
  xasl_state.query_id = query_p->query_id;
  /* initialize error line */
  xasl_state.qp_xasl_line = 0;

  /*
   * If this streaming query will require post-processing such as
   * orderby or groupby, then we cannot return a valid VPID to the user
   * since the list_file will be changed during the sorting. So, we
   * save the current VPID in the query result structure and return the
   * negative of the query_id as an indicator that this query required
   * post-processing causing xqfile_get_list_file_page() to sync up the
   * query before returning.
   */
  /* Above comment is not true. Somedays circumstance was changed by someone.
   * Currently, if the query is a type of requiring post-processing such as
   * UNION, DIFFERENCE, INTERSECTION, GROUP BY, ORDER BY, or DISTINCT then
   * 'list_id' of the 'query_entry' remains not set and 'first_vpid' of the
   * 'list_id' returned to the client is set to NULL_PAGEID_ASYNC.
   * 'xqfile_get_list_file_page()' will wait util it is set to sync up the
   * query if 'query_entry->list_id == NULL || pageid == NULL_PAGEID_ASYNC'.
   */

  /* 'list_id' to be returned */
  tmp_list_p = (QFILE_LIST_ID *) malloc (sizeof (QFILE_LIST_ID));
  if (tmp_list_p == NULL)
    {
      return (QFILE_LIST_ID *) NULL;
    }

  QFILE_CLEAR_LIST_ID (tmp_list_p);

  xasl_p = query_p->xasl;
  if (!qmgr_is_async_executable (xasl_p, &query_type))
    {
      if (query_type == UNION_QUERY)
	{
	  /* if this query is a union, difference or intersection, then the
	   * 'list_id' to copy from will be one of the leaf nodes of the tree.
	   * Must search the tree of xasl_nodes until we find a BUILDLIST_PROC
	   * node which will be the leaf. Use the 'list_id' from this
	   * 'xasl_node'.
	   */
	  tmp_xasl_p = qmgr_find_leaf (xasl_p);
	  if (tmp_xasl_p == NULL)
	    {
	      free_and_init (tmp_list_p);
	      return (QFILE_LIST_ID *) NULL;
	    }
	  outptr_list_p = tmp_xasl_p->outptr_list;
#if 0
	  /* copy 'list_id' from ... */
	  if (qfile_copy_list_id (tmp_list_p, tmp_xasl_p->list_id, true) !=
	      NO_ERROR)
	    {
	      free_and_init (tmp_list_p);
	      return (QFILE_LIST_ID *) NULL;
	    }
#endif
	  /* now set up the 'type_list' in the new list file */
	  if (qdata_get_valptr_type_list (thread_p, outptr_list_p, &type_list)
	      != NO_ERROR)
	    {
	      free_and_init (tmp_list_p);
	      return (QFILE_LIST_ID *) NULL;
	    }

	  if (qfile_modify_type_list (&type_list, tmp_list_p) != NO_ERROR)
	    {
	      if (type_list.domp)
		{
		  db_private_free_and_init (thread_p, type_list.domp);
		}
	      free_and_init (tmp_list_p);
	      return (QFILE_LIST_ID *) NULL;
	    }

	  if (type_list.domp)
	    {
	      db_private_free_and_init (thread_p, type_list.domp);
	    }
	}
      else if (query_type == VALUE_QUERY || query_type == GROUPBY_QUERY
	       || query_type == ORDERBY_QUERY || query_type == DISTINCT_QUERY
	       || query_type == ANALYTIC_QUERY)
	{
	  if (xasl_p->type == BUILDLIST_PROC
	      && xasl_p->proc.buildlist.groupby_list != NULL)
	    {
	      outptr_list_p = xasl_p->proc.buildlist.g_outptr_list;
	    }
	  else if (xasl_p->type == BUILDLIST_PROC
		   && xasl_p->proc.buildlist.a_func_list != NULL)
	    {
	      outptr_list_p = xasl_p->proc.buildlist.a_outptr_list;
	    }
	  else
	    {
	      outptr_list_p = xasl_p->outptr_list;
	    }

	  /* now set up the 'type_list' in the new list file */
	  if (qdata_get_valptr_type_list (thread_p, outptr_list_p, &type_list)
	      != NO_ERROR)
	    {
	      free_and_init (tmp_list_p);
	      return (QFILE_LIST_ID *) NULL;
	    }

	  if (qfile_modify_type_list (&type_list, tmp_list_p) != NO_ERROR)
	    {
	      if (type_list.domp)
		{
		  db_private_free_and_init (thread_p, type_list.domp);
		}
	      free_and_init (tmp_list_p);
	      return (QFILE_LIST_ID *) NULL;
	    }

	  if (type_list.domp)
	    {
	      db_private_free_and_init (thread_p, type_list.domp);
	    }
	}
      else
	{			/* error case - (query_type == OTHERS) */
	  free_and_init (tmp_list_p);
	  return (QFILE_LIST_ID *) NULL;
	}

      VPID_SET (&tmp_list_p->first_vpid, NULL_VOLID, NULL_PAGEID_ASYNC);

    }
  else
    {
      /* xasl->type == BUILDLIST_PROC */

      (void) qexec_start_mainblock_iterations (thread_p, xasl_p, &xasl_state);

      /* set 'list_id' of 'query_entry' to the 'list_id' of 'xasl_node' */
      query_p->list_id = xasl_p->list_id;
      if (query_p->list_id == NULL)
	{
	  free_and_init (tmp_list_p);
	  return (QFILE_LIST_ID *) NULL;
	}
      query_p->list_id->query_id = query_p->query_id;

      /*
       * This is still a streaming query, but we need a real first page.
       * qfile_get_first_page() can return ER_FAILED, so should check it here.
       */
      if (qfile_get_first_page (thread_p, query_p->list_id) != NO_ERROR)
	{
	  free_and_init (tmp_list_p);
	  return (QFILE_LIST_ID *) NULL;
	}

      /* copy and return query result file identifier */
      if (qfile_copy_list_id (tmp_list_p, query_p->list_id, true) != NO_ERROR)
	{
	  free_and_init (tmp_list_p);
	  return (QFILE_LIST_ID *) NULL;
	}
    }

  /* packing up the arguments to 'qexec_execute_query()' */
  async_query_p = (QMGR_ASYNC_QUERY *) malloc (sizeof (QMGR_ASYNC_QUERY));
  if (async_query_p != NULL)
    {
#if defined (ENABLE_UNUSED_FUNCTION)
      async_query_p->cache_clone_p = cache_clone_p;
#endif /* ENABLE_UNUSED_FUNCTION */
      async_query_p->xasl = xasl_p;
      async_query_p->dbval_count = dbval_count;
      async_query_p->dbval_p = dbval_p;
      async_query_p->query_id = query_p->query_id;
      async_query_p->tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      async_query_p->pri_heap_id = db_replace_private_heap (thread_p);

      /*
       * setting query mode flag to ASYNC_MODE here to prevent a async
       * query which has returned above due to error can not be roll backed.
       */
      query_p->query_mode = ASYNC_MODE;

      job_p = css_make_job_entry (thread_get_current_conn_entry (),
				  (CSS_THREAD_FN) qmgr_execute_async_select,
				  (CSS_THREAD_ARG) async_query_p,
				  -1 /* implicit: DEFAULT */ );

      if (job_p == NULL)
	{
	  free_and_init (async_query_p);
	  free_and_init (tmp_list_p);
	  return (QFILE_LIST_ID *) NULL;
	}

      css_add_to_job_queue (job_p);

      return tmp_list_p;
    }
  else
    {
      free_and_init (tmp_list_p);
      return (QFILE_LIST_ID *) NULL;
    }
}
#endif

void
qmgr_setup_empty_list_file (char *page_p)
{
  QFILE_PAGE_HEADER header;

  header.pg_tplcnt = 0;
  header.lasttpl_off = QFILE_PAGE_HEADER_SIZE;
  header.prev_pgid = header.next_pgid = header.ovfl_pgid = NULL_PAGEID;
  header.prev_volid = header.next_volid = header.ovfl_volid = NULL_VOLID;

  qmgr_put_page_header (page_p, &header);
}

/*
 * qmgr_initialize_temp_file_list () -
 *   return: none
 *   temp_file_list_p(in): temporary file list to be initialized
 *   membuf_type(in):
 */
void
qmgr_initialize_temp_file_list (QMGR_TEMP_FILE_LIST * temp_file_list_p,
				QMGR_TEMP_FILE_MEMBUF_TYPE membuf_type)
{
  int i, num_buffer_pages;
  QMGR_TEMP_FILE *temp_file_p;
#if defined(SERVER_MODE)
  int rv;
#endif

  assert (temp_file_list_p != NULL
	  && QMGR_IS_VALID_MEMBUF_TYPE (membuf_type));
  if (temp_file_list_p == NULL || !QMGR_IS_VALID_MEMBUF_TYPE (membuf_type))
    {
      return;
    }

  num_buffer_pages = (membuf_type == TEMP_FILE_MEMBUF_NORMAL) ?
    PRM_TEMP_MEM_BUFFER_PAGES : PRM_INDEX_SCAN_KEY_BUFFER_PAGES;

  pthread_mutex_init (&temp_file_list_p->mutex, NULL);
  rv = pthread_mutex_lock (&temp_file_list_p->mutex);
  temp_file_list_p->list = NULL;

  for (i = 0; i < QMGR_TEMP_FILE_FREE_LIST_SIZE; i++)
    {
      temp_file_p = qmgr_allocate_tempfile_with_buffer (num_buffer_pages);
      if (temp_file_p == NULL)
	{
	  break;
	}
      /* add to the free list */
      temp_file_p->prev = NULL;
      temp_file_p->next = temp_file_list_p->list;
      temp_file_p->membuf_npages = num_buffer_pages;
      temp_file_p->membuf_type = membuf_type;
      temp_file_list_p->list = temp_file_p;
    }

  temp_file_list_p->count = i;

  pthread_mutex_unlock (&temp_file_list_p->mutex);
}

/*
 * qmgr_finalize_temp_file_list () -
 *   return: none
 *   temp_file_list_p(in): temporary file list to be finalized
 */
void
qmgr_finalize_temp_file_list (QMGR_TEMP_FILE_LIST * temp_file_list_p)
{
  QMGR_TEMP_FILE *temp_file_p;

  assert (temp_file_list_p != NULL);
  if (temp_file_list_p == NULL)
    {
      return;
    }

  while (temp_file_list_p->list)
    {
      temp_file_p = temp_file_list_p->list;
      temp_file_list_p->list = temp_file_p->next;
      free_and_init (temp_file_p);
    }
  temp_file_list_p->count = 0;
  pthread_mutex_destroy (&temp_file_list_p->mutex);
}

/*
 * qmgr_get_temp_file_from_list () -
 *   return: temporary file
 *   temp_file_list_p(in): temporary file list
 */
QMGR_TEMP_FILE *
qmgr_get_temp_file_from_list (QMGR_TEMP_FILE_LIST * temp_file_list_p)
{
  QMGR_TEMP_FILE *temp_file_p = NULL;
#if defined(SERVER_MODE)
  int rv;
#endif
  assert (temp_file_list_p != NULL);
  if (temp_file_list_p == NULL)
    {
      return NULL;
    }

  rv = pthread_mutex_lock (&temp_file_list_p->mutex);

  /* delete from the free list */
  if (temp_file_list_p->list)
    {
      temp_file_p = temp_file_list_p->list;
      temp_file_list_p->list = temp_file_p->next;
      temp_file_p->prev = temp_file_p->next = NULL;
      temp_file_list_p->count--;
    }

  pthread_mutex_unlock (&temp_file_list_p->mutex);

  return temp_file_p;
}

/*
 * qmgr_put_temp_file_into_list () -
 *   return: none
 *   temp_file_list_p(in): temporary file list
 */
void
qmgr_put_temp_file_into_list (QMGR_TEMP_FILE * temp_file_p)
{
  QMGR_TEMP_FILE_LIST *temp_file_list_p;
#if defined(SERVER_MODE)
  int rv;
#endif
  assert (temp_file_p != NULL);
  if (temp_file_p == NULL)
    {
      return;
    }

  if (QMGR_IS_VALID_MEMBUF_TYPE (temp_file_p->membuf_type))
    {
      temp_file_list_p =
	&qmgr_Query_table.temp_file_list[temp_file_p->membuf_type];

      rv = pthread_mutex_lock (&temp_file_list_p->mutex);

      /* add to the free list */
      if (temp_file_list_p->count < QMGR_TEMP_FILE_FREE_LIST_SIZE)
	{
	  temp_file_p->prev = NULL;
	  temp_file_p->next = temp_file_list_p->list;
	  temp_file_list_p->list = temp_file_p;
	  temp_file_list_p->count++;
	  temp_file_p = NULL;
	}

      pthread_mutex_unlock (&temp_file_list_p->mutex);
    }
  if (temp_file_p)
    {
      free_and_init (temp_file_p);
    }
}

/*
 * qmgr_get_temp_file_membuf_pages () -
 *   return: number of membuf pages belonging to the temporary file
 *   temp_file_list_p(in): temporary file
 */
int
qmgr_get_temp_file_membuf_pages (QMGR_TEMP_FILE * temp_file_p)
{
  assert (temp_file_p != NULL);
  if (temp_file_p == NULL)
    {
      return -1;
    }
  return temp_file_p->membuf_npages;
}

/*
 * qmgr_set_query_timeout_to_tdes () - calculate timeout and set to transaction
 *                                     descriptor
 *   return: void
 *   tran_index(in):
 *   query_timeout(in): milli seconds
 */
void
qmgr_set_query_timeout_to_tdes (int tran_index, int query_timeout)
{
  LOG_TDES *tdes_p;
#if !defined(HAVE_ATOMIC_BUILTINS)
  struct timeval tv;
#endif /* !HAVE_ATOMIC_BUILTINS */

  tdes_p = LOG_FIND_TDES (tran_index);
  assert (tdes_p != NULL);
  if (tdes_p != NULL)
    {
      if (query_timeout > 0)
	{
	  /* We use log_Clock_msec instead of calling gettimeofday
	   * if the system supports atomic built-ins.
	   */
#if defined(HAVE_ATOMIC_BUILTINS)
	  tdes_p->query_timeout = log_Clock_msec + query_timeout;
#else /* HAVE_ATOMIC_BUILTINS */
	  gettimeofday (&tv, NULL);
	  tdes_p->query_timeout =
	    (tv.tv_sec * 1000) + (tv.tv_usec / 1000) + query_timeout;
#endif /* HAVE_ATOMIC_BUILTINS */
	}
      else if (query_timeout == 0)
	{
	  tdes_p->query_timeout = 0;
	}
      else
	{
	  /*
	   * This means that the query is not the first of a bundle of queries.
	   * We will apply a timeout to the bundle, not each query.
	   * Actually CAS always sends -1 in this case.
	   */
	  assert (query_timeout == -1);
	}
    }
}
