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
 * query_manager.c - Query manager module
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "query_manager.h"

#include "file_manager.h"
#include "compile_context.h"
#include "log_append.hpp"
#include "object_primitive.h"
#include "object_representation.h"
#include "xserver_interface.h"
#include "query_executor.h"
#include "stream_to_xasl.h"
#include "session.h"
#include "filter_pred_cache.h"
#include "crypt_opfunc.h"
#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */
#include "thread_entry.hpp"
#include "xasl_cache.h"
#include "xasl_unpack_info.hpp"

#if !defined (SERVER_MODE)

#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
extern int method_Num_method_jsp_calls;
#define IS_IN_METHOD_OR_JSP_CALL() (method_Num_method_jsp_calls > 0)

#endif

#define QMGR_MAX_QUERY_ENTRY_PER_TRAN   100

#define QMGR_TEMP_FILE_FREE_LIST_SIZE   100

#define QMGR_NUM_TEMP_FILE_LISTS        (TEMP_FILE_MEMBUF_NUM_TYPES)

#define QMGR_SQL_ID_LENGTH      13

/* We have two valid types of membuf used by temporary file. */
#define QMGR_IS_VALID_MEMBUF_TYPE(m)    ((m) == TEMP_FILE_MEMBUF_NORMAL || (m) == TEMP_FILE_MEMBUF_KEY_BUFFER)

enum qmgr_page_type
{
  QMGR_UNKNOWN_PAGE,
  QMGR_MEMBUF_PAGE,
  QMGR_TEMP_FILE_PAGE
};
typedef enum qmgr_page_type QMGR_PAGE_TYPE;

/*
 *       		     ALLOCATION STRUCTURES
 *
 * A resource mechanism used to effectively handle memory allocation for the
 * query entry structures.
 */

#define OID_BLOCK_ARRAY_SIZE    10
typedef struct oid_block_list
{
  struct oid_block_list *next;
  int last_oid_idx;
  OID oid_array[OID_BLOCK_ARRAY_SIZE];
} OID_BLOCK_LIST;

typedef struct qmgr_tran_entry QMGR_TRAN_ENTRY;
struct qmgr_tran_entry
{
  QMGR_TRAN_STATUS trans_stat;	/* transaction status */
  int query_id_generator;	/* global query identifier count */

  int num_query_entries;	/* number of allocated query entries */

  QMGR_QUERY_ENTRY *query_entry_list_p;	/* linked list of query entries */
  QMGR_QUERY_ENTRY *free_query_entry_list_p;	/* free query entry list */

  OID_BLOCK_LIST *modified_classes_p;	/* array of class OIDs */
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
  OID_BLOCK_LIST *free_oid_block_list_p;	/* free OID block list */

  /* temp file free list info */
  QMGR_TEMP_FILE_LIST temp_file_list[QMGR_NUM_TEMP_FILE_LISTS];
};

QMGR_QUERY_TABLE qmgr_Query_table = { NULL, 0, NULL,
  {{PTHREAD_MUTEX_INITIALIZER, NULL, 0}, {PTHREAD_MUTEX_INITIALIZER, NULL, 0}}
};

#if !defined(SERVER_MODE)
static struct drand48_data qmgr_rand_buf;
#endif

static QMGR_PAGE_TYPE qmgr_get_page_type (PAGE_PTR page_p, QMGR_TEMP_FILE * temp_file_p);
static bool qmgr_is_allowed_result_cache (QUERY_FLAG flag);
static bool qmgr_can_get_from_cache (QUERY_FLAG flag);
static bool qmgr_can_get_result_from_cache (QUERY_FLAG flag);
static void qmgr_put_page_header (PAGE_PTR page_p, QFILE_PAGE_HEADER * header_p);

static QMGR_QUERY_ENTRY *qmgr_allocate_query_entry (THREAD_ENTRY * thread_p, QMGR_TRAN_ENTRY * tran_entry_p);
static void qmgr_free_query_entry (THREAD_ENTRY * thread_p, QMGR_TRAN_ENTRY * tran_entry_p, QMGR_QUERY_ENTRY * q_ptr);
static void qmgr_deallocate_query_entries (QMGR_QUERY_ENTRY * q_ptr);
static void qmgr_deallocate_oid_blocks (OID_BLOCK_LIST * oid_block);
static void qmgr_add_query_entry (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * q_ptr, int trans_ind);
static QMGR_QUERY_ENTRY *qmgr_find_query_entry (QMGR_QUERY_ENTRY * query_list_p, QUERY_ID query_id);
static void qmgr_delete_query_entry (THREAD_ENTRY * thread_p, QUERY_ID query_id, int trans_ind);
static void qmgr_free_tran_entries (void);

static void qmgr_clear_relative_cache_entries (THREAD_ENTRY * thread_p, QMGR_TRAN_ENTRY * tran_entry_p);
static bool qmgr_is_related_class_modified (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xasl_cache, int tran_index);
static OID_BLOCK_LIST *qmgr_allocate_oid_block (THREAD_ENTRY * thread_p);
static void qmgr_free_oid_block (THREAD_ENTRY * thread_p, OID_BLOCK_LIST * oid_block);
static int qmgr_init_external_file_page (THREAD_ENTRY * thread_p, PAGE_PTR page, void *args);
static PAGE_PTR qmgr_get_external_file_page (THREAD_ENTRY * thread_p, VPID * vpid, QMGR_TEMP_FILE * vfid);
static int qmgr_free_query_temp_file_helper (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * query_p);
static int qmgr_free_query_temp_file (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * qptr, int tran_idx);
static QMGR_TEMP_FILE *qmgr_allocate_tempfile_with_buffer (int num_buffer_pages);

#if defined (SERVER_MODE)
static XASL_NODE *qmgr_find_leaf (XASL_NODE * xasl);
static QFILE_LIST_ID *qmgr_process_query (THREAD_ENTRY * thread_p, XASL_NODE * xasl_tree, char *xasl_stream,
					  int xasl_stream_size, int dbval_count, const DB_VALUE * dbvals_p,
					  QUERY_FLAG flag, QMGR_QUERY_ENTRY * query_p, QMGR_TRAN_ENTRY * tran_entry_p);
static void qmgr_reset_query_exec_info (int tran_index);
static void qmgr_set_query_exec_info_to_tdes (int tran_index, int query_timeout, const XASL_ID * xasl_id);
#endif

static void qmgr_initialize_temp_file_list (QMGR_TEMP_FILE_LIST * temp_file_list_p,
					    QMGR_TEMP_FILE_MEMBUF_TYPE membuf_type);
static void qmgr_finalize_temp_file_list (QMGR_TEMP_FILE_LIST * temp_file_list_p);
static QMGR_TEMP_FILE *qmgr_get_temp_file_from_list (QMGR_TEMP_FILE_LIST * temp_file_list_p);
static void qmgr_put_temp_file_into_list (QMGR_TEMP_FILE * temp_file_p);

static int copy_bind_value_to_tdes (THREAD_ENTRY * thread_p, int num_bind_vals, DB_VALUE * bind_vals);

/*
 * qmgr_get_page_type () -
 *
 *   return: QMGR_PAGE_TYPE
 *
 *   page_p(in):
 *   temp_file_p(in):
 */
static QMGR_PAGE_TYPE
qmgr_get_page_type (PAGE_PTR page_p, QMGR_TEMP_FILE * temp_file_p)
{
  PAGE_PTR begin_page = NULL, end_page = NULL;

  if (temp_file_p != NULL && temp_file_p->membuf_last >= 0 && temp_file_p->membuf && page_p >= temp_file_p->membuf[0]
      && page_p <= temp_file_p->membuf[temp_file_p->membuf_last])
    {
      return QMGR_MEMBUF_PAGE;
    }

  begin_page = (PAGE_PTR) ((PAGE_PTR) temp_file_p->membuf
			   + DB_ALIGN (sizeof (PAGE_PTR) * temp_file_p->membuf_npages, MAX_ALIGNMENT));
  end_page = begin_page + temp_file_p->membuf_npages * DB_PAGESIZE;
  if (begin_page <= page_p && page_p <= end_page)
    {
      /* defense code */
      assert (false);
      return QMGR_UNKNOWN_PAGE;
    }

  return QMGR_TEMP_FILE_PAGE;
}

static bool
qmgr_is_allowed_result_cache (QUERY_FLAG flag)
{
  static int query_cache_mode = prm_get_integer_value (PRM_ID_LIST_QUERY_CACHE_MODE);

  if (QFILE_IS_LIST_CACHE_DISABLED)
    {
      return false;
    }

  if (query_cache_mode == QFILE_LIST_QUERY_CACHE_MODE_OFF
      || query_cache_mode == QFILE_LIST_QUERY_CACHE_MODE_SELECTIVELY_OFF
      || (query_cache_mode == QFILE_LIST_QUERY_CACHE_MODE_SELECTIVELY_ON && !(flag & RESULT_CACHE_REQUIRED)))
    {
      return false;
    }

  return true;
}

static void
qmgr_put_page_header (PAGE_PTR page_p, QFILE_PAGE_HEADER * header_p)
{
  OR_PUT_INT ((page_p) + QFILE_TUPLE_COUNT_OFFSET, (header_p)->pg_tplcnt);
  OR_PUT_INT ((page_p) + QFILE_PREV_PAGE_ID_OFFSET, (header_p)->prev_pgid);
  OR_PUT_INT ((page_p) + QFILE_NEXT_PAGE_ID_OFFSET, (header_p)->next_pgid);
  OR_PUT_INT ((page_p) + QFILE_LAST_TUPLE_OFFSET, (header_p)->lasttpl_off);
  OR_PUT_INT ((page_p) + QFILE_OVERFLOW_PAGE_ID_OFFSET, (header_p)->ovfl_pgid);
  OR_PUT_SHORT ((page_p) + QFILE_PREV_VOL_ID_OFFSET, (header_p)->prev_volid);
  OR_PUT_SHORT ((page_p) + QFILE_NEXT_VOL_ID_OFFSET, (header_p)->next_volid);
  OR_PUT_SHORT ((page_p) + QFILE_OVERFLOW_VOL_ID_OFFSET, (header_p)->ovfl_volid);
#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  memset (page_p + QFILE_RESERVED_OFFSET, 0, QFILE_PAGE_HEADER_SIZE - QFILE_RESERVED_OFFSET);
#endif
}

static void
qmgr_mark_query_as_completed (QMGR_QUERY_ENTRY * query_p)
{
  query_p->query_status = QUERY_COMPLETED;
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
qmgr_allocate_query_entry (THREAD_ENTRY * thread_p, QMGR_TRAN_ENTRY * tran_entry_p)
{
  QMGR_QUERY_ENTRY *query_p;
  QUERY_ID hint_query_id;
  int i;
  bool usable = false;

  static_assert (QMGR_MAX_QUERY_ENTRY_PER_TRAN < SHRT_MAX, "Bad query entry count");

  query_p = tran_entry_p->free_query_entry_list_p;

  if (query_p)
    {
      tran_entry_p->free_query_entry_list_p = query_p->next;
    }
  else if (QMGR_MAX_QUERY_ENTRY_PER_TRAN < tran_entry_p->num_query_entries)
    {
      assert (QMGR_MAX_QUERY_ENTRY_PER_TRAN >= tran_entry_p->num_query_entries);
      return NULL;
    }
  else
    {
      query_p = (QMGR_QUERY_ENTRY *) malloc (sizeof (QMGR_QUERY_ENTRY));
      if (query_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QMGR_QUERY_ENTRY));
	  return NULL;
	}

      query_p->list_id = NULL;

      tran_entry_p->num_query_entries++;
    }

  /* assign query id */
  hint_query_id = 0;
  for (i = 0; i < QMGR_MAX_QUERY_ENTRY_PER_TRAN; i++)
    {
      if (tran_entry_p->query_id_generator >= SHRT_MAX - 2)	/* overflow happened */
	{
	  tran_entry_p->query_id_generator = 0;
	}
      query_p->query_id = ++tran_entry_p->query_id_generator;

      usable = session_is_queryid_idle (thread_p, query_p->query_id, &hint_query_id);
      if (usable == true)
	{
	  /* it is usable */
	  break;
	}

      if (i == 0)
	{
	  /* optimization: The second try uses the current max query_id as hint.
	   * This may help us to quickly locate an available id.
	   */
	  assert (hint_query_id != 0);
	  tran_entry_p->query_id_generator = (int) hint_query_id;
	}
    }
  assert (usable == true);

  /* initialize per query temp file VFID structure */
  query_p->next = NULL;
  query_p->temp_vfid = NULL;
  query_p->num_tmp = 0;
  query_p->total_count = 0;
  XASL_ID_SET_NULL (&query_p->xasl_id);
  query_p->xasl_ent = NULL;
  query_p->list_id = NULL;
  query_p->list_ent = NULL;
  query_p->errid = NO_ERROR;
  query_p->er_msg = NULL;
  query_p->query_flag = 0;
  query_p->is_holdable = false;
  query_p->includes_tde_class = false;

#if defined (NDEBUG)
  /* just a safe guard for a release build. I don't expect it will be hit. */
  if (usable == false)
    {
      qmgr_free_query_entry (thread_p, tran_entry_p, query_p);
      return NULL;
    }
#endif /* NDEBUG */

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
qmgr_free_query_entry (THREAD_ENTRY * thread_p, QMGR_TRAN_ENTRY * tran_entry_p, QMGR_QUERY_ENTRY * query_p)
{
#if defined (SERVER_MODE)
  if (query_p->er_msg)
    {
      free_and_init (query_p->er_msg);
    }
#endif

  if (query_p->list_id)
    {
      QFILE_FREE_AND_INIT_LIST_ID (query_p->list_id);
    }

  query_p->next = NULL;

  query_p->next = tran_entry_p->free_query_entry_list_p;
  tran_entry_p->free_query_entry_list_p = query_p;
}

/*
 * qmgr_deallocate_oid_blocks () -
 *   return:
 *   oid_blocks(in)  : oid_block pointer
 *
 * Note: Free the area allocated for the oid_blocks
 */
static void
qmgr_deallocate_oid_blocks (OID_BLOCK_LIST * oid_block)
{
  OID_BLOCK_LIST *oid;

  while (oid_block)
    {
      oid = oid_block;
      oid_block = oid_block->next;

      free (oid);
    }
}

/*
 * qmgr_deallocate_query_entries () -
 *   return:
 *   q_ptr(in)  : Query Entry Pointer
 *
 * Note: Free the area allocated for the query entry list
 */
static void
qmgr_deallocate_query_entries (QMGR_QUERY_ENTRY * query_p)
{
  QMGR_QUERY_ENTRY *p;

  while (query_p)
    {
      p = query_p;
      query_p = query_p->next;

#if defined (SERVER_MODE)
      if (p->er_msg)
	{
	  free_and_init (p->er_msg);
	}
#endif

      if (p->list_id)
	{
	  QFILE_FREE_AND_INIT_LIST_ID (p->list_id);
	}

      free_and_init (p);
    }
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
qmgr_add_query_entry (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * query_p, int tran_index)
{
  QMGR_TRAN_ENTRY *tran_entry_p;

  if (tran_index == NULL_TRAN_INDEX)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  if (tran_entry_p->trans_stat == QMGR_TRAN_NULL || tran_entry_p->trans_stat == QMGR_TRAN_TERMINATED)
    {
      tran_entry_p->trans_stat = QMGR_TRAN_RUNNING;
      tran_entry_p->query_entry_list_p = query_p;
    }
  else
    {
      query_p->next = tran_entry_p->query_entry_list_p;
      tran_entry_p->query_entry_list_p = query_p;
    }
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
qmgr_get_query_entry (THREAD_ENTRY * thread_p, QUERY_ID query_id, int tran_index)
{
  QMGR_QUERY_ENTRY *query_p = NULL;
  QMGR_TRAN_ENTRY *tran_entry_p;

  /*
   * The code for finding the query_entry pointer is in-lined in
   * xqmgr_end_query and qmgr_is_query_interrupted to avoid calling this function.
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

  query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p, query_id);
  if (query_p != NULL)
    {
      return query_p;
    }

  /* Maybe it is a holdable result and we'll find it in the session state object. In order to be able to use this
   * result, we need to create a new entry for this query in the transaction query entries and copy result information
   * from the session. */
  query_p = qmgr_allocate_query_entry (thread_p, tran_entry_p);
  if (query_p == NULL)
    {
      return NULL;
    }

  query_p->query_id = query_id;
  if (xsession_load_query_entry_info (thread_p, query_p) != NO_ERROR)
    {
      qmgr_free_query_entry (thread_p, tran_entry_p, query_p);
      query_p = NULL;

      return NULL;
    }

  /* add it to this transaction also */
  qmgr_add_query_entry (thread_p, query_p, tran_index);

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
qmgr_delete_query_entry (THREAD_ENTRY * thread_p, QUERY_ID query_id, int tran_index)
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

  prev_query_p = NULL;
  query_p = tran_entry_p->query_entry_list_p;

  while (query_p && query_p->query_id != query_id)
    {
      prev_query_p = query_p;
      query_p = query_p->next;
    }

  if (query_p == NULL)
    {
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

  qmgr_free_query_entry (thread_p, tran_entry_p, query_p);
}

static void
qmgr_initialize_tran_entry (QMGR_TRAN_ENTRY * tran_entry_p)
{
  tran_entry_p->trans_stat = QMGR_TRAN_NULL;
  tran_entry_p->query_id_generator = 0;
  tran_entry_p->num_query_entries = 0;
  tran_entry_p->query_entry_list_p = NULL;
  tran_entry_p->free_query_entry_list_p = NULL;
  tran_entry_p->modified_classes_p = NULL;
}

/*
 * qmgr_allocate_tran_entries () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   num_new_entries(in)      : Number of transactions
 *
 * Note: Allocates(Reallocates) the area pointed by the query manager
 * transaction index pointer
 */
int
qmgr_allocate_tran_entries (THREAD_ENTRY * thread_p, int num_new_entries)
{
  QMGR_TRAN_ENTRY *tran_entry_p;
  int i, num_current_entries;
  size_t tran_entry_size;

#if defined (SERVER_MODE)
  num_new_entries = MAX (num_new_entries, MAX_NTRANS);
#endif

  /* enter critical section, this prevents another to perform malloc/init */
  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  num_current_entries = qmgr_Query_table.num_trans;

  if (num_new_entries <= num_current_entries)
    {
      /* enough */
      csect_exit (thread_p, CSECT_QPROC_QUERY_TABLE);
      return NO_ERROR;
    }

  tran_entry_size = num_new_entries * sizeof (QMGR_TRAN_ENTRY);
  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      qmgr_Query_table.tran_entries_p = (QMGR_TRAN_ENTRY *) malloc (tran_entry_size);
    }
  else
    {
      qmgr_Query_table.tran_entries_p = (QMGR_TRAN_ENTRY *) realloc (qmgr_Query_table.tran_entries_p, tran_entry_size);
    }

  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      csect_exit (thread_p, CSECT_QPROC_QUERY_TABLE);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, tran_entry_size);
      return ER_FAILED;
    }

  /* initialize newly allocated areas */
  tran_entry_p = (QMGR_TRAN_ENTRY *) qmgr_Query_table.tran_entries_p + num_current_entries;
  for (i = qmgr_Query_table.num_trans; i < num_new_entries; i++, tran_entry_p++)
    {
      qmgr_initialize_tran_entry (tran_entry_p);
    }

  qmgr_Query_table.num_trans = num_new_entries;

  csect_exit (thread_p, CSECT_QPROC_QUERY_TABLE);

  return NO_ERROR;
}

/*
 * qmgr_free_tran_entries () -
 *   return:
 *
 * Note: frees the area pointed by the query manager transaction index pointer.
 */
static void
qmgr_free_tran_entries (void)
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
      qmgr_deallocate_query_entries (tran_entry_p->query_entry_list_p);
      qmgr_deallocate_query_entries (tran_entry_p->free_query_entry_list_p);
      qmgr_deallocate_oid_blocks (tran_entry_p->modified_classes_p);

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
  fprintf (stdout, "\t\txasl_id: {{%08x | %08x | %08x | %08x | %08x}, {%d sec %d usec}}\n",
	   SHA1_AS_ARGS (&query_p->xasl_id.sha1), CACHE_TIME_AS_ARGS (&query_p->xasl_id.time_stored));
  fprintf (stdout, "\t\tlist_id: %p\n", (void *) query_p->list_id);

  if (query_p->list_id)
    {
      list_id_p = query_p->list_id;
      fprintf (stdout,
	       "\t\t{type_list: {%d, %p}, tuple_cnt: %d, page_cnt: %d,\n"
	       "\t first_vpid: {%d, %d}, last_vpid: {%d, %d},\n"
	       "\t last_pgptr: %p, last_offset: %d, lasttpl_len: %d}\n", list_id_p->type_list.type_cnt,
	       (void *) list_id_p->type_list.domp, list_id_p->tuple_cnt, list_id_p->page_cnt,
	       list_id_p->first_vpid.pageid, list_id_p->first_vpid.volid, list_id_p->last_vpid.pageid,
	       list_id_p->last_vpid.volid, list_id_p->last_pgptr, list_id_p->last_offset, list_id_p->lasttpl_len);
    }

  if (query_p->temp_vfid)
    {
      temp_vfid_p = query_p->temp_vfid;

      do
	{
	  fprintf (stdout, "\t\tfile_vfid: %p\n", (void *) &temp_vfid_p);

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
      fprintf (stdout, "\tTrans_stat: %s\n", qmgr_get_tran_status_string (tran_entry_p->trans_stat));

      fprintf (stdout, "\tTrans_query_entries:\n");

      for (query_p = tran_entry_p->query_entry_list_p; query_p; query_p = query_p->next)
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
#if !defined(SERVER_MODE)
  struct timeval t;
#endif

  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      total_tran_indices = logtb_get_number_of_total_tran_indices ();
      if (qmgr_allocate_tran_entries (thread_p, total_tran_indices) != NO_ERROR)
	{
	  csect_exit (thread_p, CSECT_QPROC_QUERY_TABLE);
	  return ER_FAILED;
	}
    }

  if (qmgr_Query_table.temp_file_list[TEMP_FILE_MEMBUF_NORMAL].list != NULL)
    {
      qmgr_finalize_temp_file_list (&qmgr_Query_table.temp_file_list[TEMP_FILE_MEMBUF_NORMAL]);
    }
  qmgr_initialize_temp_file_list (&qmgr_Query_table.temp_file_list[TEMP_FILE_MEMBUF_NORMAL], TEMP_FILE_MEMBUF_NORMAL);

  if (qmgr_Query_table.temp_file_list[TEMP_FILE_MEMBUF_KEY_BUFFER].list != NULL)
    {
      qmgr_finalize_temp_file_list (&qmgr_Query_table.temp_file_list[TEMP_FILE_MEMBUF_KEY_BUFFER]);
    }
  qmgr_initialize_temp_file_list (&qmgr_Query_table.temp_file_list[TEMP_FILE_MEMBUF_KEY_BUFFER],
				  TEMP_FILE_MEMBUF_KEY_BUFFER);

  csect_exit (thread_p, CSECT_QPROC_QUERY_TABLE);

  qfile_initialize ();

#if defined (SERVER_MODE)
  numeric_init_power_value_string ();
#endif

  srand48 ((long) time (NULL));

#if !defined(SERVER_MODE)
  gettimeofday (&t, NULL);
  srand48_r ((long) t.tv_usec, &qmgr_rand_buf);
#endif

  return scan_initialize ();
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
  int i;

  scan_finalize ();
  qfile_finalize ();

  if (csect_enter (thread_p, CSECT_QPROC_QUERY_TABLE, INF_WAIT) != NO_ERROR)
    {
      return;
    }

  qmgr_free_tran_entries ();

  assert (qmgr_Query_table.tran_entries_p == NULL && qmgr_Query_table.num_trans == 0);

  for (i = 0; i < QMGR_NUM_TEMP_FILE_LISTS; i++)
    {
      qmgr_finalize_temp_file_list (&qmgr_Query_table.temp_file_list[i]);
    }

  csect_exit (thread_p, CSECT_QPROC_QUERY_TABLE);
}

/*
 * xqmgr_prepare_query () - Prepares a query for later (and repetitive) execution
 *   return	     : Error code.
 *   thread_p (in)   : thread entry.
 *   context (in)    : query string; used for hash key of the XASL cache
 *   stream (in/out) : XASL stream, size, xasl_id & xasl_header info; set to NULL if you want to look up the XASL cache
 *
 * Note: Store the given XASL stream into the XASL file and return its file id.
 * The XASL file is a temporay file, ..
 * If NULL is given as the input argument xasl_stream, this function will look up the XASL cache,
 * and return the cached XASL file id if found. If not found, NULL will be returned.
 */
int
xqmgr_prepare_query (THREAD_ENTRY * thread_p, COMPILE_CONTEXT * context, xasl_stream * stream)
{
  XASL_CACHE_ENTRY *cache_entry_p = NULL;
  char *p;
  int header_size;
  int i;
  OID creator_oid, *class_oid_list_p = NULL;
  int n_oid_list, *tcard_list_p = NULL;
  int *class_locks = NULL;
  int dbval_cnt;
  int error_code = NO_ERROR;
  xasl_cache_rt_check_result recompile_due_to_threshold = XASL_CACHE_RECOMPILE_NOT_NEEDED;

  /* If xasl_stream is NULL, it means that the client requested looking up the XASL cache to know there's a reusable
   * execution plan (XASL) for this query. The XASL is stored as a file so that the XASL file id (XASL_ID) will be
   * returned if found in the cache.
   */

  if (stream->buffer == NULL && context->recompile_xasl)
    {
      /* Recompile requested by no xasl_stream is provided. */
      assert_release (false);
      return ER_FAILED;
    }

  XASL_ID_SET_NULL (stream->xasl_id);
  if (!context->recompile_xasl)
    {
      error_code =
	xcache_find_sha1 (thread_p, &context->sha1, XASL_CACHE_SEARCH_FOR_PREPARE, &cache_entry_p,
			  &recompile_due_to_threshold);
      if (error_code != NO_ERROR)
	{
	  assert (false);
	  return error_code;
	}
      if (cache_entry_p != NULL)
	{
	  if (recompile_due_to_threshold != XASL_CACHE_RECOMPILE_NOT_NEEDED)
	    {
	      assert (recompile_due_to_threshold == XASL_CACHE_RECOMPILE_PREPARE);
	      XASL_ID_COPY (stream->xasl_id, &cache_entry_p->xasl_id);
	      xcache_unfix (thread_p, cache_entry_p);
	      context->recompile_xasl = true;
	      return NO_ERROR;
	    }
	  else
	    {
	      /* Found entry. */
	      XASL_ID_COPY (stream->xasl_id, &cache_entry_p->xasl_id);
	      if (stream->buffer == NULL && stream->xasl_header != NULL)
		{
		  /* also header was requested. */
		  qfile_load_xasl_node_header (thread_p, stream->buffer, stream->xasl_header);
		}
	      xcache_unfix (thread_p, cache_entry_p);
	      goto exit_on_end;
	    }
	}
      if (stream->buffer == NULL)
	{
	  /* No entry found. */
	  if (recompile_due_to_threshold != XASL_CACHE_RECOMPILE_NOT_NEEDED)
	    {
	      /* We need to force recompile. */
	      assert (recompile_due_to_threshold == XASL_CACHE_RECOMPILE_PREPARE);
	      context->recompile_xasl = true;
	    }
	  return NO_ERROR;
	}
    }
  /* Add new entry to xasl cache. */
  assert (cache_entry_p == NULL);
  assert (stream->buffer != NULL);

  /* get some information from the XASL stream */
  p = or_unpack_int ((char *) stream->buffer, &header_size);
  p = or_unpack_int (p, &dbval_cnt);
  p = or_unpack_oid (p, &creator_oid);
  p = or_unpack_int (p, &n_oid_list);

  if (n_oid_list > 0)
    {
      class_oid_list_p = (OID *) db_private_alloc (thread_p, sizeof (OID) * n_oid_list);
      class_locks = (int *) db_private_alloc (thread_p, sizeof (LOCK) * n_oid_list);
      tcard_list_p = (int *) db_private_alloc (thread_p, sizeof (int) * n_oid_list);
      if (class_oid_list_p == NULL || class_locks == NULL || tcard_list_p == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit_on_error;
	}

      for (i = 0; i < n_oid_list; i++)
	{
	  p = or_unpack_oid (p, &class_oid_list_p[i]);
	}
      for (i = 0; i < n_oid_list; i++)
	{
	  p = or_unpack_int (p, &class_locks[i]);
	}
      for (i = 0; i < n_oid_list; i++)
	{
	  p = or_unpack_int (p, &tcard_list_p[i]);
	}
    }
  else
    {
      class_oid_list_p = NULL;
      class_locks = NULL;
      tcard_list_p = NULL;
    }

  error_code =
    xcache_insert (thread_p, context, stream, n_oid_list, class_oid_list_p, class_locks, tcard_list_p, &cache_entry_p);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit_on_error;
    }
  if (cache_entry_p == NULL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error_code = ER_FAILED;
      goto exit_on_error;
    }
  XASL_ID_COPY (stream->xasl_id, &cache_entry_p->xasl_id);
  xcache_unfix (thread_p, cache_entry_p);

exit_on_end:

  if (class_oid_list_p)
    {
      db_private_free_and_init (thread_p, class_oid_list_p);
    }
  if (class_locks)
    {
      db_private_free_and_init (thread_p, class_locks);
    }
  if (tcard_list_p)
    {
      db_private_free_and_init (thread_p, tcard_list_p);
    }

  return error_code;

exit_on_error:

  assert (error_code != NO_ERROR);
  ASSERT_ERROR ();
  goto exit_on_end;
}

/*
 * qmgr_process_query () - Execute a prepared query as sync mode
 *   return		   : query result file id
 *   thread_p (in)         : Thread entry.
 *   xasl_tree (in)        : XASL tree already unpacked or NULL.
 *   xasl_stream (in)      : XASL stream.
 *   xasl_stream_size (in) : XASL stream size.
 *   dbval_count (in)	   : number of host variables
 *   dbvals_p (in)	   : array of host variables (query input parameters)
 *   flag (in)		   : flag
 *   query_p (in)	   : QMGR_QUERY_ENTRY *
 *   tran_entry_p (in)	   : QMGR_TRAN_ENTRY *
 *
 * Note1: The query result is returned through a list id (actually the list
 * file). Query id is put for further reference to this query entry.
 * If there's an error, NULL will be returned.
 *
 * Note2: It is the caller's responsibility to free output QFILE_LIST_ID
 * by calling QFILE_FREE_AND_INIT_LIST_ID().
 */
static QFILE_LIST_ID *
qmgr_process_query (THREAD_ENTRY * thread_p, XASL_NODE * xasl_tree, char *xasl_stream, int xasl_stream_size,
		    int dbval_count, const DB_VALUE * dbvals_p, QUERY_FLAG flag, QMGR_QUERY_ENTRY * query_p,
		    QMGR_TRAN_ENTRY * tran_entry_p)
{
  XASL_NODE *xasl_p;
  XASL_UNPACK_INFO *xasl_buf_info;
  QFILE_LIST_ID *list_id;

  assert (query_p != NULL);
  assert (tran_entry_p != NULL);

  xasl_p = NULL;
  xasl_buf_info = NULL;
  list_id = NULL;

  if (xasl_tree != NULL)
    {
      /* check the number of the host variables for this XASL */
      if (xasl_tree->dbval_cnt > dbval_count)
	{
	  er_log_debug (ARG_FILE_LINE, "qmgr_process_query: dbval_cnt mismatch %d vs %d\n", xasl_tree->dbval_cnt,
			dbval_count);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
	  goto exit_on_error;
	}

      /* Adjust XASL flag for query result cache. For the last list file(QFILE_LIST_ID) as the query result, the
       * permanent query result file(FILE_QUERY_AREA) rather than temporary file(FILE_TEMP) will be created if and only
       * if XASL_TO_BE_CACHED flag is set. */
      if (qmgr_is_allowed_result_cache (flag))
	{
	  XASL_SET_FLAG (xasl_tree, XASL_TO_BE_CACHED);
	}
      xasl_p = xasl_tree;
    }
  else
    {
      if (stx_map_stream_to_xasl (thread_p, &xasl_p, false, xasl_stream, xasl_stream_size, &xasl_buf_info) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  query_p->includes_tde_class = XASL_IS_FLAGED (xasl_p, XASL_INCLUDES_TDE_CLASS);
#if !defined(NDEBUG)
  er_log_debug (ARG_FILE_LINE, "TDE: qmgr_process_query(): includes_tde_class = %d\n", query_p->includes_tde_class);
#endif /* !NDEBUG */


  if (flag & RETURN_GENERATED_KEYS)
    {
      XASL_SET_FLAG (xasl_p, XASL_RETURN_GENERATED_KEYS);
    }

  /* execute the query with the value list, if any */
  query_p->list_id = qexec_execute_query (thread_p, xasl_p, dbval_count, dbvals_p, query_p->query_id);
  thread_p->no_logging = false;

  /* Note: qexec_execute_query() returns listid (NOT NULL) even if an error was occurred. We should check the error
   * condition and free listid. */
  if (query_p->errid < 0)
    {				/* error has occurred */
      if (query_p->list_id)
	{
	  QFILE_FREE_AND_INIT_LIST_ID (query_p->list_id);
	}
      /* error occurred during executing the query */
      goto exit_on_error;
    }

  assert (query_p->list_id != NULL);

  /* allocate new QFILE_LIST_ID to be returned as the result and copy from the query result; the caller is responsible
   * to free this */
  list_id = qfile_clone_list_id (query_p->list_id, false);
  if (list_id == NULL)
    {
      goto exit_on_error;
    }
  assert (list_id->sort_list == NULL);

  list_id->last_pgptr = NULL;

end:

  if (xasl_buf_info)
    {
      /* free the XASL tree */
      free_xasl_unpack_info (thread_p, xasl_buf_info);
    }

  return list_id;

exit_on_error:

  assert (list_id == NULL);
  goto end;
}

/*
 * xqmgr_execute_query () - Execute a prepared query
 *   return: query result file id
 *   thrd(in)   :
 *   xasl_id(in)        : XASL file id that was a result of prepare_query()
 *   query_idp(out)     : query id to be used for getting results
 *   dbval_count(in)      : number of host variables
 *   dbval_p(in) : array of host variables (query input parameters)
 *   flagp(in)  : flag
 *   clt_cache_time(in) :
 *   srv_cache_time(in) :
 *   query_timeout(in) : query_timeout in millisec.
 *   info(out) : execution info from server
 *
 * Note1: The query result is returned through a list id (actually the list
 * file). Query id is put for further refernece to this query entry.
 * If there's an error, NULL will be returned.
 *
 * Note2: It is the caller's responsibility to free output QFILE_LIST_ID
 * by calling QFILE_FREE_AND_INIT_LIST_ID().
 */
QFILE_LIST_ID *
xqmgr_execute_query (THREAD_ENTRY * thread_p, const XASL_ID * xasl_id_p, QUERY_ID * query_id_p, int dbval_count,
		     void *dbval_p, QUERY_FLAG * flag_p, CACHE_TIME * client_cache_time_p,
		     CACHE_TIME * server_cache_time_p, int query_timeout, xasl_cache_ent ** ret_cache_entry_p)
{
  XASL_CACHE_ENTRY *xasl_cache_entry_p = NULL;
  XASL_CLONE xclone = XASL_CLONE_INITIALIZER;
  QFILE_LIST_CACHE_ENTRY *list_cache_entry_p;
  DB_VALUE *dbvals_p;
#if defined (SERVER_MODE)
  DB_VALUE *dbval;
  HL_HEAPID old_pri_heap_id;
  char *data;
  int i;
#endif
  DB_VALUE_ARRAY params;
  QMGR_QUERY_ENTRY *query_p;
  int tran_index = -1;
  QMGR_TRAN_ENTRY *tran_entry_p;
  QFILE_LIST_ID *list_id_p, *tmp_list_id_p;
  bool cached_result;
  bool saved_is_stats_on;
  bool xasl_trace;
  bool is_xasl_pinned_reference;
  bool do_not_cache = false;

  cached_result = false;
  query_p = NULL;
  *query_id_p = -1;
  list_id_p = NULL;
  xasl_cache_entry_p = NULL;
  list_cache_entry_p = NULL;

  dbvals_p = NULL;
#if defined (SERVER_MODE)
  data = (char *) dbval_p;
  old_pri_heap_id = 0;
#endif

#if defined (SERVER_MODE)
  assert (thread_get_recursion_depth (thread_p) == 0);
#elif defined (SA_MODE)
  assert (thread_get_recursion_depth (thread_p) == 0 || IS_IN_METHOD_OR_JSP_CALL ());
#endif

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  saved_is_stats_on = perfmon_server_is_stats_on (thread_p);

  xasl_trace = IS_XASL_TRACE_TEXT (*flag_p) || IS_XASL_TRACE_JSON (*flag_p);

  is_xasl_pinned_reference = IS_XASL_CACHE_PINNED_REFERENCE (*flag_p);

  if (DO_NOT_COLLECT_EXEC_STATS (*flag_p))
    {
      if (saved_is_stats_on == true)
	{
	  perfmon_stop_watch (thread_p);
	}
    }
  else if (xasl_trace == true)
    {
      thread_trace_on (thread_p);
      perfmon_start_watch (thread_p);

      if (IS_XASL_TRACE_TEXT (*flag_p))
	{
	  thread_set_trace_format (thread_p, QUERY_TRACE_TEXT);
	}
      else if (IS_XASL_TRACE_JSON (*flag_p))
	{
	  thread_set_trace_format (thread_p, QUERY_TRACE_JSON);
	}
    }

  xasl_cache_entry_p = NULL;
  if (xcache_find_xasl_id_for_execute (thread_p, xasl_id_p, &xasl_cache_entry_p, &xclone) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }
  if (xasl_cache_entry_p == NULL)
    {
      /* XASL cache entry not found. */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      perfmon_inc_stat (thread_p, PSTAT_PC_NUM_INVALID_XASL_ID);
      return NULL;
    }
  if (xclone.xasl == NULL || xclone.xasl_buf == NULL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_XASLNODE, 0);
      perfmon_inc_stat (thread_p, PSTAT_PC_NUM_INVALID_XASL_ID);
      return NULL;
    }

  if (ret_cache_entry_p)
    {
      *ret_cache_entry_p = xasl_cache_entry_p;
    }

  if (IS_TRIGGER_INVOLVED (*flag_p))
    {
      session_set_trigger_state (thread_p, true);
    }

#if defined (SERVER_MODE)
  if (dbval_count)
    {
      char *ptr;

      assert (data != NULL);

      dbvals_p = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE) * dbval_count);
      if (dbvals_p == NULL)
	{
	  goto exit_on_error;
	}

      /* unpack DB_VALUEs from the received data */
      ptr = data;
      for (i = 0, dbval = dbvals_p; i < dbval_count; i++, dbval++)
	{
	  ptr = or_unpack_db_value (ptr, dbval);
	}
    }
#else
  dbvals_p = (DB_VALUE *) dbval_p;
#endif

  /* If it is not inhibited from getting the cached result, inspect the list cache (query result cache) and get the
   * list file id(QFILE_LIST_ID) to be returned to the client if it is in there. The list cache will be searched with
   * the XASL cache entry of the target query that is obtained from the XASL_ID, because all results of the query with
   * different parameters (host variables - DB_VALUES) are linked at the XASL cache entry.
   */
  params.size = dbval_count;
  params.vals = dbvals_p;

  if (copy_bind_value_to_tdes (thread_p, dbval_count, dbvals_p) != NO_ERROR)
    {
      goto exit_on_error;
    }

  if (qmgr_is_allowed_result_cache (*flag_p))
    {
      if (qmgr_is_related_class_modified (thread_p, xasl_cache_entry_p, tran_index))
	{
	  do_not_cache = true;
	}

      if (do_not_cache == false)
	{
	  /* lookup the list cache with the parameter values (DB_VALUE array) */
	  list_cache_entry_p = qfile_lookup_list_cache_entry (thread_p, xasl_cache_entry_p, &params, &cached_result);

	  /* If we've got the cached result, return it. */
	  if (cached_result)
	    {
	      /* found the cached result */
	      CACHE_TIME_MAKE (server_cache_time_p, &list_cache_entry_p->time_created);
	    }
	}
    }

  if (client_cache_time_p)
    {
      CACHE_TIME_RESET (client_cache_time_p);
    }

  /* Make an query entry */
  /* mark that this transaction is running a query */
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

#if defined(ENABLE_SYSTEMTAP)
  if (tran_entry_p->trans_stat == QMGR_TRAN_NULL || tran_entry_p->trans_stat == QMGR_TRAN_TERMINATED)
    {
      CUBRID_TRAN_START (tran_index);
    }
#endif /* ENABLE_SYSTEMTAP */

#if defined (SERVER_MODE)
  tran_entry_p->trans_stat = QMGR_TRAN_RUNNING;

  /* allocate a new query entry */
  query_p = qmgr_allocate_query_entry (thread_p, tran_entry_p);

  /* set a timeout if necessary */
  qmgr_set_query_exec_info_to_tdes (tran_index, query_timeout, xasl_id_p);
#else
  /* allocate a new query entry */
  query_p = qmgr_allocate_query_entry (thread_p, tran_entry_p);
#endif

  if (query_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QM_QENTRY_RUNOUT, 1, QMGR_MAX_QUERY_ENTRY_PER_TRAN);
      goto exit_on_error;
    }

  /* initialize query entry */
  XASL_ID_COPY (&query_p->xasl_id, xasl_id_p);
  query_p->xasl_ent = xasl_cache_entry_p;
  if (cached_result)
    {
      query_p->list_ent = list_cache_entry_p;	/* for qfile_end_use_of_list_cache_entry() */
    }
  query_p->query_status = QUERY_IN_PROGRESS;
  query_p->query_flag = *flag_p;
  if (*flag_p & RESULT_HOLDABLE)
    {
      query_p->is_holdable = true;
    }
  else
    {
      query_p->is_holdable = false;
    }

  /* add the entry to the query table */
  qmgr_add_query_entry (thread_p, query_p, tran_index);

  /* to return query id */
  *query_id_p = query_p->query_id;

  /* If we've got the cached result, return it. Else, process the query */
  if (cached_result)
    {
      /* allocate new QFILE_LIST_ID to be stored in the query entry cloning from the QFILE_LIST_ID of the found list
       * cache entry */
      query_p->list_id = qfile_clone_list_id (&list_cache_entry_p->list_id, false);
      if (query_p->list_id == NULL)
	{
	  goto exit_on_error;
	}
      query_p->list_id->query_id = query_p->query_id;

      /* allocate new QFILE_LIST_ID to be returned as the result and copy from the query result; the caller is
       * responsible to free this */
      list_id_p = qfile_clone_list_id (query_p->list_id, false);
      if (list_id_p == NULL)
	{
	  goto exit_on_error;	/* maybe, memory allocation error */
	}
      list_id_p->last_pgptr = NULL;

      /* mark that the query is completed */
      qmgr_mark_query_as_completed (query_p);

      goto end;			/* OK */
    }

  /* If the result didn't come from the cache, build the execution plan (XASL tree) from the cached(stored) XASL
   * stream. */

  assert (cached_result == false);

  list_id_p =
    qmgr_process_query (thread_p, xclone.xasl, NULL, 0, dbval_count, dbvals_p, *flag_p, query_p, tran_entry_p);
  if (list_id_p == NULL)
    {
      goto exit_on_error;
    }

  /* everything is ok, mark that the query is completed */
  qmgr_mark_query_as_completed (query_p);

  /* If it is allowed to cache the query result or if it is required to cache, put the list file id(QFILE_LIST_ID) into
   * the list cache. Provided are the corresponding XASL cache entry to be linked, and the parameters (host variables -
   * DB_VALUES). */
  if (qmgr_is_allowed_result_cache (*flag_p) && do_not_cache == false && xasl_cache_entry_p->list_ht_no >= 0)
    {
      /* check once more to ensure that the related XASL entry is still valid */
      if (xcache_can_entry_cache_list (xasl_cache_entry_p))
	{
	  if (list_id_p == NULL)
	    {
	      goto end;
	    }

	  if (list_cache_entry_p && !cached_result)
	    {
	      goto end;
	    }

	  /* the type of the result file should be FILE_QUERY_AREA in order not to deleted at the time of query_end */
	  if (list_id_p->tfile_vfid != NULL && list_id_p->tfile_vfid->temp_file_type != FILE_QUERY_AREA)
	    {
	      /* duplicate the list file */
	      tmp_list_id_p = qfile_duplicate_list (thread_p, list_id_p, QFILE_FLAG_RESULT_FILE);
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
	      goto end;		/* return without inserting into the cache */
	    }

	  /* update the cache entry for the result associated with the used parameter values (DB_VALUE array) if there
	   * is, or make new one
	   * in case list_ht_no is less than 0,
	   *     the cache is not found and should be newly added (surely list_cache_entry_p is null)
	   * in case list_ht_no is not less than 0 and list_cache_entry_p is null
	   *     the cache entry is found but the entry is used by other transaction
	   */
	  if (list_cache_entry_p && xasl_cache_entry_p->list_ht_no < 0)
	    {
	      assert (false);
	    }

	  list_cache_entry_p =
	    qfile_update_list_cache_entry (thread_p, xasl_cache_entry_p->list_ht_no, &params, list_id_p,
					   xasl_cache_entry_p);

	  if (list_cache_entry_p == NULL)
	    {
	      char *s;

	      s = (params.size > 0) ? pr_valstring (&params.vals[0]) : NULL;
	      er_log_debug (ARG_FILE_LINE,
			    "xqmgr_execute_query: ls_update_xasl failed "
			    "xasl_id { sha1 { %08x | %08x | %08x | %08x | %08x } time_stored { %d sec %d usec } } "
			    "params { %d %s ... }\n",
			    SHA1_AS_ARGS (&xasl_id_p->sha1), CACHE_TIME_AS_ARGS (&xasl_id_p->time_stored),
			    params.size, s ? s : "(null)");
	      if (s)
		{
		  db_private_free (thread_p, s);
		}

	      goto end;
	    }

	  /* record list cache entry into the query entry for qfile_end_use_of_list_cache_entry() */
	  query_p->list_ent = list_cache_entry_p;

	  CACHE_TIME_MAKE (server_cache_time_p, &list_cache_entry_p->time_created);
	}
    }

end:

  xcache_retire_clone (thread_p, xasl_cache_entry_p, &xclone);
  if (ret_cache_entry_p != NULL && *ret_cache_entry_p != NULL)
    {
      /* The XASL cache entry is output. */
      assert (*ret_cache_entry_p == xasl_cache_entry_p);
    }
  else if (xasl_cache_entry_p != NULL)
    {
      xcache_unfix (thread_p, xasl_cache_entry_p);
    }

  if (IS_TRIGGER_INVOLVED (*flag_p))
    {
      session_set_trigger_state (thread_p, false);
    }

#if defined (SERVER_MODE)
  if (dbvals_p)
    {
      for (i = 0, dbval = dbvals_p; i < dbval_count; i++, dbval++)
	{
	  pr_clear_value (dbval);
	}
      db_private_free_and_init (thread_p, dbvals_p);
    }
#endif

  if (DO_NOT_COLLECT_EXEC_STATS (*flag_p) && saved_is_stats_on == true)
    {
      perfmon_start_watch (thread_p);
    }

#if defined (SERVER_MODE)
  qmgr_reset_query_exec_info (tran_index);
#endif

  return list_id_p;

exit_on_error:

  /* end the use of the cached result if any when an error occurred */
  if (cached_result)
    {
      (void) qfile_end_use_of_list_cache_entry (thread_p, list_cache_entry_p, false);
    }

  if (query_p)
    {
      /* mark that the query is completed and then delete this query entry */
      qmgr_mark_query_as_completed (query_p);

      if (qmgr_free_query_temp_file (thread_p, query_p, tran_index) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE, "xqmgr_execute_query: qmgr_free_query_temp_file");
	}

      qmgr_delete_query_entry (thread_p, query_p->query_id, tran_index);
    }

  *query_id_p = 0;

  /* free QFILE_LIST_ID */
  if (list_id_p)
    {
      QFILE_FREE_AND_INIT_LIST_ID (list_id_p);
    }

  if (xasl_trace == true && saved_is_stats_on == false)
    {
      perfmon_stop_watch (thread_p);
    }

  goto end;
}

/*
 * copy_bind_value_to_tdes - copy bind values to transaction descriptor
 * return:
 *   thread_p(in):
 *   num_bind_vals(in):
 *   bind_vals(in):
 */
static int
copy_bind_value_to_tdes (THREAD_ENTRY * thread_p, int num_bind_vals, DB_VALUE * bind_vals)
{
  LOG_TDES *tdes;
  DB_VALUE *vals;
  int i;
  HL_HEAPID save_heap_id;

  tdes = LOG_FIND_CURRENT_TDES (thread_p);
  if (tdes == NULL)
    {
      return ER_FAILED;
    }

  if (tdes != NULL && tdes->num_exec_queries < MAX_NUM_EXEC_QUERY_HISTORY)
    {
      tdes->bind_history[tdes->num_exec_queries].vals = NULL;
      tdes->bind_history[tdes->num_exec_queries].size = num_bind_vals;

      if (num_bind_vals > 0)
	{
	  save_heap_id = db_change_private_heap (thread_p, 0);

	  vals = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE) * num_bind_vals);
	  if (vals == NULL)
	    {
	      (void) db_change_private_heap (thread_p, save_heap_id);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_VALUE) * num_bind_vals);

	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

	  for (i = 0; i < num_bind_vals; i++)
	    {
	      pr_clone_value (&bind_vals[i], &vals[i]);
	    }

	  tdes->bind_history[tdes->num_exec_queries].vals = vals;
	  (void) db_change_private_heap (thread_p, save_heap_id);
	}
    }

  tdes->num_exec_queries++;
  return NO_ERROR;
}

/*
 * xqmgr_prepare_and_execute_query () -
 *   return: Query result file identifier, or NULL
 *   thrd(in)   :
 *   xasl_stream(in)       : XASL tree pointer in unprepared form
 *   xasl_stream_size(in)      : memory area size pointed by the xasl_stream
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
xqmgr_prepare_and_execute_query (THREAD_ENTRY * thread_p, char *xasl_stream, int xasl_stream_size,
				 QUERY_ID * query_id_p, int dbval_count, void *dbval_p, QUERY_FLAG * flag_p,
				 int query_timeout)
{
#if defined (SERVER_MODE)
  DB_VALUE *dbval;
  HL_HEAPID old_pri_heap_id;
  char *data;
  int i;
#endif
  DB_VALUE *dbvals_p;
  QMGR_QUERY_ENTRY *query_p;
  QFILE_LIST_ID *list_id_p;
  int tran_index;
  QMGR_TRAN_ENTRY *tran_entry_p;
  bool saved_is_stats_on;
  bool xasl_trace;

  query_p = NULL;
  *query_id_p = -1;
  list_id_p = NULL;

  dbvals_p = NULL;
  assert (thread_get_recursion_depth (thread_p) == 0);

#if defined (SERVER_MODE)
  data = (char *) dbval_p;
  old_pri_heap_id = 0;
#endif

  saved_is_stats_on = perfmon_server_is_stats_on (thread_p);
  xasl_trace = IS_XASL_TRACE_TEXT (*flag_p) || IS_XASL_TRACE_JSON (*flag_p);

  if (DO_NOT_COLLECT_EXEC_STATS (*flag_p))
    {
      if (saved_is_stats_on == true)
	{
	  perfmon_stop_watch (thread_p);
	}
    }
  else if (xasl_trace == true)
    {
      thread_trace_on (thread_p);
      perfmon_start_watch (thread_p);

      if (IS_XASL_TRACE_TEXT (*flag_p))
	{
	  thread_set_trace_format (thread_p, QUERY_TRACE_TEXT);
	}
      else if (IS_XASL_TRACE_JSON (*flag_p))
	{
	  thread_set_trace_format (thread_p, QUERY_TRACE_JSON);
	}
    }

  if (IS_TRIGGER_INVOLVED (*flag_p))
    {
      session_set_trigger_state (thread_p, true);
    }

  /* Make an query entry */
  /* mark that this transaction is running a query */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

#if defined(ENABLE_SYSTEMTAP)
  if (tran_entry_p->trans_stat == QMGR_TRAN_NULL || tran_entry_p->trans_stat == QMGR_TRAN_TERMINATED)
    {
      CUBRID_TRAN_START (tran_index);
    }
#endif /* ENABLE_SYSTEMTAP */

#if defined (SERVER_MODE)
  tran_entry_p->trans_stat = QMGR_TRAN_RUNNING;

  /* allocate a new query entry */
  query_p = qmgr_allocate_query_entry (thread_p, tran_entry_p);

  /* set a timeout if necessary */
  qmgr_set_query_exec_info_to_tdes (tran_index, query_timeout, NULL);

  if (dbval_count)
    {
      char *ptr;

      assert (data != NULL);

      dbvals_p = (DB_VALUE *) db_private_alloc (thread_p, sizeof (DB_VALUE) * dbval_count);
      if (dbvals_p == NULL)
	{
	  goto exit_on_error;
	}

      /* unpack DB_VALUEs from the received data */
      ptr = data;
      for (i = 0, dbval = dbvals_p; i < dbval_count; i++, dbval++)
	{
	  ptr = or_unpack_db_value (ptr, dbval);
	}
    }
#else
  dbvals_p = (DB_VALUE *) dbval_p;

  /* allocate a new query entry */
  query_p = qmgr_allocate_query_entry (thread_p, tran_entry_p);
#endif

  if (query_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QM_QENTRY_RUNOUT, 1, QMGR_MAX_QUERY_ENTRY_PER_TRAN);
      goto exit_on_error;
    }

  /* initialize query entry */
  XASL_ID_SET_NULL (&query_p->xasl_id);
  query_p->xasl_ent = NULL;
  query_p->list_ent = NULL;
  query_p->query_status = QUERY_IN_PROGRESS;
  query_p->query_flag = *flag_p;

  assert (!(*flag_p & RESULT_HOLDABLE));
  query_p->is_holdable = false;

  /* add query entry to the query table */
  qmgr_add_query_entry (thread_p, query_p, tran_index);

  /* to return query id */
  *query_id_p = query_p->query_id;

  list_id_p = qmgr_process_query (thread_p, NULL, xasl_stream, xasl_stream_size, dbval_count, dbvals_p, *flag_p,
				  query_p, tran_entry_p);
  if (list_id_p == NULL)
    {
      goto exit_on_error;
    }

  /* everything is ok, mark that the query is completed */
  qmgr_mark_query_as_completed (query_p);

end:
  if (IS_TRIGGER_INVOLVED (*flag_p))
    {
      session_set_trigger_state (thread_p, false);
    }

#if defined (SERVER_MODE)
  if (dbvals_p)
    {
      for (i = 0, dbval = dbvals_p; i < dbval_count; i++, dbval++)
	{
	  pr_clear_value (dbval);
	}
      db_private_free_and_init (thread_p, dbvals_p);
    }
#endif

  if (DO_NOT_COLLECT_EXEC_STATS (*flag_p) && saved_is_stats_on == true)
    {
      perfmon_start_watch (thread_p);
    }

#if defined (SERVER_MODE)
  qmgr_reset_query_exec_info (tran_index);
#endif

  return list_id_p;

exit_on_error:

  /*
   * free the query entry when error occurs. note that the query_id should be
   * set to 0 so as to upper levels can detect the error.
   */
  if (query_p)
    {
      /* mark that the query is completed and then delete this query entry */
      qmgr_mark_query_as_completed (query_p);

      if (qmgr_free_query_temp_file (thread_p, query_p, tran_index) != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE, "xqmgr_prepare_and_execute_query: qmgr_free_query_temp_file");
	}
      qmgr_delete_query_entry (thread_p, query_p->query_id, tran_index);
    }

  *query_id_p = 0;

  if (list_id_p)
    {
      QFILE_FREE_AND_INIT_LIST_ID (list_id_p);
    }

  if (xasl_trace == true && saved_is_stats_on == false)
    {
      perfmon_stop_watch (thread_p);
    }

  goto end;
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

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  /* get query entry */
  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID, 1, query_id);
      return ER_FAILED;
    }

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p, query_id);
  if (query_p == NULL)
    {
      /* maybe this is a holdable result and we'll find it in the session state object */
      xsession_remove_query_entry_info (thread_p, query_id);
      return NO_ERROR;
    }

  if (query_p->is_holdable)
    {
      /* We also need to remove the associated query from the session. The call below will not destroy the associated
       * list files */
      xsession_clear_query_entry_info (thread_p, query_id);
    }

  assert (query_p->query_status == QUERY_COMPLETED);

  /* query is closed */
  if (query_p->xasl_ent && query_p->list_ent)
    {
      (void) qfile_end_use_of_list_cache_entry (thread_p, query_p->list_ent, false);
    }

  /* destroy the temp file from list id */
  if (query_p->list_id)
    {
      QFILE_FREE_AND_INIT_LIST_ID (query_p->list_id);

      /* free external volumes, if any */
      rc = qmgr_free_query_temp_file (thread_p, query_p, tran_index);
    }

  XASL_ID_SET_NULL (&query_p->xasl_id);
  qmgr_delete_query_entry (thread_p, query_p->query_id, tran_index);

  return rc;
}

/*
 * xqmgr_drop_all_query_plans () - Drop all the stored query plans
 *   return: NO_ERROR or ER_FAILED
 *
 * Note: Clear all XASL/filter predicate cache entries out upon request of the client.
 */
int
xqmgr_drop_all_query_plans (THREAD_ENTRY * thread_p)
{
  xcache_drop_all (thread_p);
  fpcache_drop_all (thread_p);
  return NO_ERROR;
}

/*
 * xqmgr_dump_query_plans () - Dump the content of the XASL cache
 *   return:
 *   outfp(in)  :
 */
void
xqmgr_dump_query_plans (THREAD_ENTRY * thread_p, FILE * out_fp)
{
  xcache_dump (thread_p, out_fp);
  fpcache_dump (thread_p, out_fp);
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
qmgr_clear_relative_cache_entries (THREAD_ENTRY * thread_p, QMGR_TRAN_ENTRY * tran_entry_p)
{
  OID_BLOCK_LIST *oid_block_p;
  OID *class_oid_p;
  int i;

  for (oid_block_p = tran_entry_p->modified_classes_p; oid_block_p; oid_block_p = oid_block_p->next)
    {
      for (i = 0, class_oid_p = oid_block_p->oid_array; i < oid_block_p->last_oid_idx; i++, class_oid_p++)
	{
	  if (xcache_invalidate_qcaches (thread_p, class_oid_p) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "qm_clear_trans_wakeup: qexec_clear_list_cache_by_class failed for class { %d %d %d }\n",
			    class_oid_p->pageid, class_oid_p->slotid, class_oid_p->volid);
	    }
	}
    }
}

static bool
qmgr_is_related_class_modified (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xasl_cache, int tran_index)
{
  QMGR_TRAN_ENTRY *tran_entry_p;
  OID_BLOCK_LIST *oid_block_p;

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  for (oid_block_p = tran_entry_p->modified_classes_p; oid_block_p; oid_block_p = oid_block_p->next)
    {
      QMGR_QUERY_ENTRY *query_p;
      OID *class_oid_p;
      int oid_idx, i;

      for (i = 0; i < oid_block_p->last_oid_idx; i++)
	{
	  class_oid_p = &oid_block_p->oid_array[i];
	  for (oid_idx = 0; oid_idx < xasl_cache->n_related_objects; oid_idx++)
	    {
	      if (OID_EQ (&xasl_cache->related_objects[oid_idx].oid, class_oid_p))
		{
		  /* Found relation. */
		  return true;
		}
	    }
	}
    }

  return false;
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
qmgr_clear_trans_wakeup (THREAD_ENTRY * thread_p, int tran_index, bool is_tran_died, bool is_abort)
{
  QMGR_QUERY_ENTRY *query_p, *q;
  QMGR_TRAN_ENTRY *tran_entry_p;

  /* for bulletproofing check if tran_index is a valid index, note that normally this should never happen... */
  if (tran_index >= qmgr_Query_table.num_trans
#if defined (SERVER_MODE)
      || tran_index == LOG_SYSTEM_TRAN_INDEX
#endif
    )
    {
#ifdef QP_DEBUG
      er_log_debug (ARG_FILE_LINE, "qm_clear_trans_wakeup:Invalid transaction index %d called...\n", tran_index);
#endif
      return;
    }

  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];
  /* if the transaction is aborting, clear relative cache entries */
  if (tran_entry_p->modified_classes_p)
    {
      if (!QFILE_IS_LIST_CACHE_DISABLED && !qfile_has_no_cache_entries ())
	{
	  qmgr_clear_relative_cache_entries (thread_p, tran_entry_p);
	}
      qmgr_free_oid_block (thread_p, tran_entry_p->modified_classes_p);
      tran_entry_p->modified_classes_p = NULL;
    }
  if (tran_entry_p->query_entry_list_p == NULL)
    {
      return;
    }

#if defined (SERVER_MODE) && !defined (NDEBUG)
  /* there should be no active query */
  for (query_p = tran_entry_p->query_entry_list_p; query_p != NULL; query_p = query_p->next)
    {
      assert (query_p->query_status == QUERY_COMPLETED);
    }
#endif

  query_p = tran_entry_p->query_entry_list_p;
  while (query_p)
    {
      if (query_p->is_holdable)
	{
	  if (is_abort || is_tran_died)
	    {
	      /* Make sure query entry info is not leaked in session. */
	      xsession_clear_query_entry_info (thread_p, query_p->query_id);
	    }
	  else
	    {
	      /* this is a commit and we have to add the result to the holdable queries list. */
	      if (query_p->query_status != QUERY_COMPLETED)
		{
		  er_log_debug (ARG_FILE_LINE, "query %d not completed !\n", query_p->query_id);
		}
	      else
		{
		  er_log_debug (ARG_FILE_LINE, "query %d is completed!\n", query_p->query_id);
		}
	      xsession_store_query_entry_info (thread_p, query_p);
	      /* reset result info */
	      query_p->list_id = NULL;
	      query_p->temp_vfid = NULL;
	    }
	}

      /* destroy the query result if not destroyed yet */
      if (query_p->list_id)
	{
	  qfile_close_list (thread_p, query_p->list_id);
	  QFILE_FREE_AND_INIT_LIST_ID (query_p->list_id);
	}

      if (query_p->temp_vfid != NULL)
	{
	  (void) qmgr_free_query_temp_file_helper (thread_p, query_p);
	}

      XASL_ID_SET_NULL (&query_p->xasl_id);

      /* end use of the list file of the cached result */
      if (query_p->xasl_ent != NULL && query_p->list_ent != NULL)
	{
	  (void) qfile_end_use_of_list_cache_entry (thread_p, query_p->list_ent, false);
	}

      /* remove query entry */
      tran_entry_p->query_entry_list_p = query_p->next;
      qmgr_free_query_entry (thread_p, tran_entry_p, query_p);

      query_p = tran_entry_p->query_entry_list_p;
    }

  assert (tran_entry_p->query_entry_list_p == NULL);
  tran_entry_p->trans_stat = QMGR_TRAN_TERMINATED;
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
qmgr_set_tran_status (THREAD_ENTRY * thread_p, int tran_index, QMGR_TRAN_STATUS trans_status)
{
  if (tran_index >= 0)
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }

  qmgr_Query_table.tran_entries_p[tran_index].trans_stat = trans_status;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * qmgr_free_oid_block () -
 *   return:
 *   oid_block(in)      :
 */
static void
qmgr_free_oid_block (THREAD_ENTRY * thread_p, OID_BLOCK_LIST * oid_block_p)
{
  OID_BLOCK_LIST *p;

  for (p = oid_block_p; p; p = p->next)
    {
      p->last_oid_idx = 0;
    }
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
  if (tran_entry_p->modified_classes_p == NULL)
    {
      tran_entry_p->modified_classes_p = (OID_BLOCK_LIST *) malloc (sizeof (OID_BLOCK_LIST));
      if (tran_entry_p->modified_classes_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (OID_BLOCK_LIST));
	  return;
	}
      tran_entry_p->modified_classes_p->last_oid_idx = 0;
      tran_entry_p->modified_classes_p->next = NULL;
    }

  found = false;
  tmp_oid_block_p = tran_entry_p->modified_classes_p;
  do
    {
      oid_block_p = tmp_oid_block_p;
      for (i = 0, tmp_oid_p = oid_block_p->oid_array; i < oid_block_p->last_oid_idx; i++, tmp_oid_p++)
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
      else if ((oid_block_p->next = (OID_BLOCK_LIST *) malloc (sizeof (OID_BLOCK_LIST))))
	{
	  oid_block_p = oid_block_p->next;
	  oid_block_p->last_oid_idx = 0;
	  oid_block_p->next = NULL;
	  oid_block_p->oid_array[oid_block_p->last_oid_idx++] = *class_oid_p;
	}
      else
	{
	  assert (false);
	}
    }
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
qmgr_get_old_page (THREAD_ENTRY * thread_p, VPID * vpid_p, QMGR_TEMP_FILE * tfile_vfid_p)
{
  int tran_index;
  PAGE_PTR page_p;
#if defined(SERVER_MODE)
  bool dummy;
#endif /* SERVER_MODE */

  if (vpid_p->volid == NULL_VOLID && tfile_vfid_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_TEMP_FILE, 1, LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      return NULL;
    }

  if (vpid_p->volid == NULL_VOLID)
    {
      /* return memory buffer */
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

      if (vpid_p->pageid >= 0 && vpid_p->pageid <= tfile_vfid_p->membuf_last)
	{
	  page_p = tfile_vfid_p->membuf[vpid_p->pageid];

	  /* interrupt check */
#if defined (SERVER_MODE)
	  if (logtb_get_check_interrupt (thread_p) == true
	      && logtb_is_interrupted_tran (thread_p, true, &dummy, tran_index) == true)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	      page_p = NULL;
	    }
#endif
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_TEMP_FILE, 1, tran_index);
	  page_p = NULL;
	}
    }
  else
    {
      /* return temp file page */
      page_p = pgbuf_fix (thread_p, vpid_p, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);

      if (page_p != NULL)
	{
	  (void) pgbuf_check_page_ptype (thread_p, page_p, PAGE_QRESULT);
	}
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
qmgr_free_old_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, QMGR_TEMP_FILE * tfile_vfid_p)
{
  QMGR_PAGE_TYPE page_type;

  if (page_p == NULL)
    {
      assert (0);
      return;
    }
  if (tfile_vfid_p == NULL)
    {
      pgbuf_unfix (thread_p, page_p);
      return;
    }

  page_type = qmgr_get_page_type (page_p, tfile_vfid_p);
  if (page_type == QMGR_UNKNOWN_PAGE)
    {
      assert (false);
      return;
    }

  if (page_type == QMGR_TEMP_FILE_PAGE)
    {
      /* The list files came from list file cache have no tfile_vfid_p. */
      pgbuf_unfix (thread_p, page_p);
    }
#if defined (SERVER_MODE)
  else
    {
      assert (page_type == QMGR_MEMBUF_PAGE);
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
qmgr_set_dirty_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, int free_page, LOG_DATA_ADDR * addr_p,
		     QMGR_TEMP_FILE * tfile_vfid_p)
{
  QMGR_PAGE_TYPE page_type;

  page_type = qmgr_get_page_type (page_p, tfile_vfid_p);
  if (page_type == QMGR_UNKNOWN_PAGE)
    {
      assert (false);
      return;
    }

  if (page_type == QMGR_TEMP_FILE_PAGE)
    {
      log_skip_logging (thread_p, addr_p);
      pgbuf_set_dirty (thread_p, page_p, free_page);
    }
#if defined (SERVER_MODE)
  else if (free_page == (int) FREE)
    {
      assert (page_type == QMGR_MEMBUF_PAGE);
    }
#endif
}

/*
 * qmgr_get_new_page () -
 *   return: PAGE_PTR
 *   vpidp(in)  : Set to the allocated real page identifier
 *   tfile_vfidp(in)    : Query Associated with the XASL tree
 *
 * Note: A new query file page is allocated and returned. The page fetched and returned, is not locked.
 * This routine is called succesively to allocate pages for the query result files (list files) or XASL tree files.
 * If an error occurs, NULL pointer is returned.
 */
PAGE_PTR
qmgr_get_new_page (THREAD_ENTRY * thread_p, VPID * vpid_p, QMGR_TEMP_FILE * tfile_vfid_p)
{
  PAGE_PTR page_p;
  QMGR_QUERY_ENTRY *query_p = NULL;

  if (tfile_vfid_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_TEMP_FILE, 1, LOG_FIND_THREAD_TRAN_INDEX (thread_p));
      return NULL;
    }

  /* first page, return memory buffer instead real temp file page */
  if (tfile_vfid_p->membuf != NULL && tfile_vfid_p->membuf_last < tfile_vfid_p->membuf_npages - 1)
    {
      vpid_p->volid = NULL_VOLID;
      vpid_p->pageid = ++(tfile_vfid_p->membuf_last);
      return tfile_vfid_p->membuf[tfile_vfid_p->membuf_last];
    }

  /* memory buffer is exhausted; create temp file */
  if (VFID_ISNULL (&tfile_vfid_p->temp_vfid))
    {
      TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;
      if (file_create_temp (thread_p, 1, &tfile_vfid_p->temp_vfid) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return NULL;
	}
      tfile_vfid_p->temp_file_type = FILE_TEMP;

      if (tfile_vfid_p->tde_encrypted)
	{
	  tde_algo = (TDE_ALGORITHM) prm_get_integer_value (PRM_ID_TDE_DEFAULT_ALGORITHM);
	}

      if (file_apply_tde_algorithm (thread_p, &tfile_vfid_p->temp_vfid, tde_algo) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  file_temp_retire (thread_p, &tfile_vfid_p->temp_vfid);
	  VFID_SET_NULL (&tfile_vfid_p->temp_vfid);
	  return NULL;
	}
    }

  /* try to get pages from an external temp file */
  page_p = qmgr_get_external_file_page (thread_p, vpid_p, tfile_vfid_p);
  if (page_p == NULL)
    {
      /* more temp file page is unavailable; cause error to stop the query */
      vpid_p->pageid = NULL_PAGEID;
      if (er_errid () == ER_FILE_NOT_ENOUGH_PAGES_IN_VOLUME)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_OUT_OF_TEMP_SPACE, 0);
	}
    }

  return page_p;
}

/*
 * qmgr_init_external_file_page () - initialize new query result page
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * page (in)     : new page
 * args (in)     : not used
 */
static int
qmgr_init_external_file_page (THREAD_ENTRY * thread_p, PAGE_PTR page, void *args)
{
  QFILE_PAGE_HEADER page_header = QFILE_PAGE_HEADER_INITIALIZER;

  pgbuf_set_page_ptype (thread_p, page, PAGE_QRESULT);
  qmgr_put_page_header (page, &page_header);
  pgbuf_set_dirty (thread_p, page, DONT_FREE);

  return NO_ERROR;
}

/*
 * qmgr_get_external_file_page () -
 *   return: PAGE_PTR
 *   vpid(in)   : Set to the allocated virtual page identifier
 *   tmp_vfid(in)       : tempfile_vfid struct pointer
 *
 * Note: This function tries to allocate a new page from an external query file, fetchs and returns the page pointer.
 * Since pages are not shared by different transactions, it does not lock the page on fetching.
 * If it can not allocate a new page, necessary error code is set and NULL pointer is returned.
 */
static PAGE_PTR
qmgr_get_external_file_page (THREAD_ENTRY * thread_p, VPID * vpid_p, QMGR_TEMP_FILE * tmp_vfid_p)
{
  PAGE_PTR page_p = NULL;

  VPID_SET_NULL (vpid_p);
  if (file_alloc (thread_p, &tmp_vfid_p->temp_vfid, qmgr_init_external_file_page, NULL, vpid_p, &page_p) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }
  assert (page_p != NULL);
  assert (pgbuf_get_page_ptype (thread_p, page_p) == PAGE_QRESULT);
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, size);
      return NULL;
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
qmgr_create_new_temp_file (THREAD_ENTRY * thread_p, QUERY_ID query_id, QMGR_TEMP_FILE_MEMBUF_TYPE membuf_type)
{
  QMGR_QUERY_ENTRY *query_p;
  QMGR_TRAN_ENTRY *tran_entry_p;
  int tran_index, i, num_buffer_pages;
  QMGR_TEMP_FILE *tfile_vfid_p, *temp;
  PAGE_PTR page_p;
  QFILE_PAGE_HEADER pgheader = { 0, NULL_PAGEID, NULL_PAGEID, 0, NULL_PAGEID, NULL_VOLID, NULL_VOLID, NULL_VOLID };
  static int temp_mem_buffer_pages = prm_get_integer_value (PRM_ID_TEMP_MEM_BUFFER_PAGES);
  static int index_scan_key_buffer_pages = prm_get_integer_value (PRM_ID_INDEX_SCAN_KEY_BUFFER_PAGES);

  assert (QMGR_IS_VALID_MEMBUF_TYPE (membuf_type));

  if (!QMGR_IS_VALID_MEMBUF_TYPE (membuf_type))
    {
      return NULL;
    }

  num_buffer_pages = ((membuf_type == TEMP_FILE_MEMBUF_NORMAL) ? temp_mem_buffer_pages : index_scan_key_buffer_pages);

  tfile_vfid_p = qmgr_get_temp_file_from_list (&qmgr_Query_table.temp_file_list[membuf_type]);
  if (tfile_vfid_p == NULL)
    {
      tfile_vfid_p = qmgr_allocate_tempfile_with_buffer (num_buffer_pages);
    }

  if (tfile_vfid_p == NULL)
    {
      return NULL;
    }

  tfile_vfid_p->membuf = (PAGE_PTR *) ((PAGE_PTR) tfile_vfid_p + DB_ALIGN (sizeof (QMGR_TEMP_FILE), MAX_ALIGNMENT));

  /* initialize tfile_vfid */
  VFID_SET_NULL (&tfile_vfid_p->temp_vfid);
  tfile_vfid_p->temp_file_type = FILE_TEMP;
  tfile_vfid_p->membuf_npages = num_buffer_pages;
  tfile_vfid_p->membuf_type = membuf_type;
  tfile_vfid_p->preserved = false;
  tfile_vfid_p->tde_encrypted = false;
  tfile_vfid_p->membuf_last = -1;

  page_p = (PAGE_PTR) ((PAGE_PTR) tfile_vfid_p->membuf
		       + DB_ALIGN (sizeof (PAGE_PTR) * tfile_vfid_p->membuf_npages, MAX_ALIGNMENT));

  for (i = 0; i < tfile_vfid_p->membuf_npages; i++)
    {
      tfile_vfid_p->membuf[i] = page_p;
      qmgr_put_page_header (page_p, &pgheader);
      page_p += DB_PAGESIZE;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  /* find query entry */
  if (qmgr_Query_table.tran_entries_p != NULL)
    {
      query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p, query_id);
    }
  else
    {
      query_p = NULL;
    }

  if (query_p == NULL)
    {
      free_and_init (tfile_vfid_p);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID, 1, query_id);
      return NULL;
    }

  if (query_p->includes_tde_class)
    {
      tfile_vfid_p->tde_encrypted = true;
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
      /* Add transaction to wfg as a holder of temporary file space, but only do so for the first temp file that we
       * create.  From the wfg's point of view, there's no difference between holding one file or holding one hundred. */
      tfile_vfid_p->next = tfile_vfid_p;
      tfile_vfid_p->prev = tfile_vfid_p;
    }

  /* increment the counter of query entry */
  query_p->num_tmp++;

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
  int tran_index;
  QMGR_TEMP_FILE *tfile_vfid_p, *temp;
  QMGR_TRAN_ENTRY *tran_entry_p;
  TDE_ALGORITHM tde_algo = TDE_ALGORITHM_NONE;

  /* Allocate a tfile_vfid and create a temporary file for query result */

  tfile_vfid_p = (QMGR_TEMP_FILE *) malloc (sizeof (QMGR_TEMP_FILE));
  if (tfile_vfid_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QMGR_TEMP_FILE));
      return NULL;
    }

  VFID_SET_NULL (&tfile_vfid_p->temp_vfid);

  if (file_create_query_area (thread_p, &tfile_vfid_p->temp_vfid) != NO_ERROR)
    {
      free_and_init (tfile_vfid_p);
      return NULL;
    }


  tfile_vfid_p->temp_file_type = FILE_QUERY_AREA;

  tfile_vfid_p->membuf_last = prm_get_integer_value (PRM_ID_TEMP_MEM_BUFFER_PAGES) - 1;
  tfile_vfid_p->membuf = NULL;
  tfile_vfid_p->membuf_npages = 0;
  tfile_vfid_p->membuf_type = TEMP_FILE_MEMBUF_NONE;
  tfile_vfid_p->preserved = false;
  tfile_vfid_p->tde_encrypted = false;

  /* Find the query entry and chain the created temp file to the entry */

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &(qmgr_Query_table.tran_entries_p[tran_index]);

  /* find the query entry */
  if (qmgr_Query_table.tran_entries_p != NULL)
    {
      query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p, query_id);
    }
  else
    {
      query_p = NULL;
    }

  if (query_p == NULL)
    {
      /* query entry is not found */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID, 1, query_id);
      file_temp_retire (thread_p, &tfile_vfid_p->temp_vfid);
      free_and_init (tfile_vfid_p);
      return NULL;
    }

  if (query_p->includes_tde_class)
    {
      tfile_vfid_p->tde_encrypted = true;
      tde_algo = (TDE_ALGORITHM) prm_get_integer_value (PRM_ID_TDE_DEFAULT_ALGORITHM);
    }

  if (file_apply_tde_algorithm (thread_p, &tfile_vfid_p->temp_vfid, tde_algo) != NO_ERROR)
    {
      file_temp_retire (thread_p, &tfile_vfid_p->temp_vfid);
      free_and_init (tfile_vfid_p);
      return NULL;
    }

  if (qmgr_is_allowed_result_cache (query_p->query_flag))
    {
      file_temp_preserve (thread_p, &tfile_vfid_p->temp_vfid);
      tfile_vfid_p->preserved = true;
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

  /* increment the counter of query entry */
  query_p->num_tmp++;

  return tfile_vfid_p;
}

/*
 * qmgr_free_temp_file_list () - free temporary files in tfile_vfid_p
 * return : error code or NO_ERROR
 * thread_p (in) :
 * tfile_vfid_p (in)  : temporary files list
 * query_id (in)      : query id
 * is_error (in)      : true if query was unsuccessful
 * was_preserved (in) : true if query was preserved
 */
int
qmgr_free_temp_file_list (THREAD_ENTRY * thread_p, QMGR_TEMP_FILE * tfile_vfid_p, QUERY_ID query_id, bool is_error)
{
  QMGR_TEMP_FILE *temp = NULL;
  int rc = NO_ERROR, fd_ret = NO_ERROR;

  /* make sure temp file list is not cyclic */
  assert (tfile_vfid_p->prev == NULL || tfile_vfid_p->prev->next == NULL);

  while (tfile_vfid_p)
    {
      fd_ret = NO_ERROR;
      if ((tfile_vfid_p->temp_file_type != FILE_QUERY_AREA || is_error) && !VFID_ISNULL (&tfile_vfid_p->temp_vfid))
	{
	  if (tfile_vfid_p->preserved)
	    {
	      fd_ret = file_temp_retire_preserved (thread_p, &tfile_vfid_p->temp_vfid);
	      if (fd_ret != NO_ERROR)
		{
		  /* set error but continue with the destroy process */
		  ASSERT_ERROR ();
		  rc = ER_FAILED;
		}
	    }
	  else
	    {
	      fd_ret = file_temp_retire (thread_p, &tfile_vfid_p->temp_vfid);
	      if (fd_ret != NO_ERROR)
		{
		  /* set error but continue with the destroy process */
		  ASSERT_ERROR ();
		  rc = ER_FAILED;
		}
	    }
	}

      temp = tfile_vfid_p;
      tfile_vfid_p = tfile_vfid_p->next;

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
 * qmgr_free_query_temp_file_helper () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   query_entryp(in/out)   : Query entry ptr to determine what temp file (if any) to destroy
 *
 * Note: Destroy the external temporary file used, if any.
 */
static int
qmgr_free_query_temp_file_helper (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * query_p)
{
  QMGR_TEMP_FILE *tfile_vfid_p;
  int rc = NO_ERROR;

  assert (query_p != NULL);

  if (query_p->temp_vfid != NULL)
    {
      bool is_error = (query_p->errid < 0);

      tfile_vfid_p = query_p->temp_vfid;
      tfile_vfid_p->prev->next = NULL;

      rc = qmgr_free_temp_file_list (thread_p, tfile_vfid_p, query_p->query_id, is_error);

      query_p->temp_vfid = NULL;
    }

  return rc;
}

/*
 * qmgr_free_query_temp_file () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   query_entryp(in/out)   : Query entry ptr to determine what temp file (if any) to destroy
 *   tran_idx(in)       :
 *
 * Note: Destroy the external temporary file used, if any.
 */
static int
qmgr_free_query_temp_file (THREAD_ENTRY * thread_p, QMGR_QUERY_ENTRY * query_p, int tran_index)
{
  int rc;
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

  rc = NO_ERROR;
  if (query_p->temp_vfid != NULL)
    {
      rc = qmgr_free_query_temp_file_helper (thread_p, query_p);
    }

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
qmgr_free_list_temp_file (THREAD_ENTRY * thread_p, QUERY_ID query_id, QMGR_TEMP_FILE * tfile_vfid_p)
{
  QMGR_QUERY_ENTRY *query_p;
  QMGR_TRAN_ENTRY *tran_entry_p;
  int tran_index, rc;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  if (qmgr_Query_table.tran_entries_p != NULL)
    {
      query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p, query_id);
    }
  else
    {
      query_p = NULL;
    }

  if (query_p == NULL)
    {
      return NO_ERROR;
    }

  rc = NO_ERROR;
  if (query_p->temp_vfid)
    {
      if (!VFID_ISNULL (&tfile_vfid_p->temp_vfid))
	{
	  if (tfile_vfid_p->preserved)
	    {
	      if (file_temp_retire_preserved (thread_p, &tfile_vfid_p->temp_vfid) != NO_ERROR)
		{
		  /* stop; return error */
		  rc = ER_FAILED;
		}
	    }
	  else if (file_temp_retire (thread_p, &tfile_vfid_p->temp_vfid) != NO_ERROR)
	    {
	      /* stop; return error */
	      rc = ER_FAILED;
	    }
	  VFID_SET_NULL (&tfile_vfid_p->temp_vfid);
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

  return NO_ERROR;
}

#if defined (SERVER_MODE)
/*
 * qmgr_is_query_interrupted () -
 *   return:
 *   query_id(in)       :
 */
bool
qmgr_is_query_interrupted (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p = NULL;
  QMGR_TRAN_ENTRY *tran_entry_p;
  int tran_index;
  bool dummy;

  /*
   * get query entry - This is done in-line to avoid qmgr_get_query_entry
   * from returning NULL when the query is being interrupted
   */
  if (qmgr_Query_table.tran_entries_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID, 1, query_id);
      return true;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  query_p = qmgr_find_query_entry (tran_entry_p->query_entry_list_p, query_id);
  if (query_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_QUERYID, 1, query_id);
      return true;
    }

  return (logtb_get_check_interrupt (thread_p) && logtb_is_interrupted_tran (thread_p, true, &dummy, tran_index));
}
#endif /* SERVER_MODE */

#if defined (ENABLE_UNUSED_FUNCTION)
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
  int rv;

  errid = query_p->errid;
  er_msg = query_p->er_msg;

  if (errid < 0)
    {
      p = error_area = (char *) malloc (3 * OR_INT_SIZE + strlen (er_msg) + 1);

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
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * qmgr_set_query_error () - set current thread's error to query entry
 *   return:
 *   query_id(in)       :
 */
void
qmgr_set_query_error (THREAD_ENTRY * thread_p, QUERY_ID query_id)
{
  QMGR_QUERY_ENTRY *query_p;

  query_p = qmgr_get_query_entry (thread_p, query_id, NULL_TRAN_INDEX);
  if (query_p != NULL)
    {
      if (query_p->errid != NO_ERROR)
	{
	  /* if an error was already set, don't overwrite it */
	  return;
	}

      assert (er_errid () != NO_ERROR);
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
  for (xasl_p = xasl_p->proc.union_.left; xasl_p; xasl_p = xasl_p->proc.union_.left)
    {
      if (xasl_p->type == BUILDLIST_PROC)
	{
	  break;
	}
    }

  return xasl_p;
}
#endif /* (SERVER_MODE) */

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
qmgr_initialize_temp_file_list (QMGR_TEMP_FILE_LIST * temp_file_list_p, QMGR_TEMP_FILE_MEMBUF_TYPE membuf_type)
{
  int i, num_buffer_pages;
  QMGR_TEMP_FILE *temp_file_p;
  int rv;

  assert (temp_file_list_p != NULL && QMGR_IS_VALID_MEMBUF_TYPE (membuf_type));
  if (temp_file_list_p == NULL || !QMGR_IS_VALID_MEMBUF_TYPE (membuf_type))
    {
      return;
    }

  num_buffer_pages = ((membuf_type == TEMP_FILE_MEMBUF_NORMAL) ? prm_get_integer_value (PRM_ID_TEMP_MEM_BUFFER_PAGES)
		      : prm_get_integer_value (PRM_ID_INDEX_SCAN_KEY_BUFFER_PAGES));

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
  int rv;

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
  int rv;

  assert (temp_file_p != NULL);
  if (temp_file_p == NULL)
    {
      return;
    }

  temp_file_p->membuf_last = -1;

  if (QMGR_IS_VALID_MEMBUF_TYPE (temp_file_p->membuf_type))
    {
      temp_file_list_p = &qmgr_Query_table.temp_file_list[temp_file_p->membuf_type];

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

#if defined (SERVER_MODE)
/*
 * qmgr_set_query_exec_info_to_tdes () - calculate timeout and set to transaction
 *                                     descriptor
 *   return: void
 *   tran_index(in):
 *   query_timeout(in): milli seconds
 */
static void
qmgr_set_query_exec_info_to_tdes (int tran_index, int query_timeout, const XASL_ID * xasl_id)
{
  LOG_TDES *tdes_p;

  tdes_p = LOG_FIND_TDES (tran_index);
  assert (tdes_p != NULL);
  if (tdes_p != NULL)
    {
      /* We use log_Clock_msec instead of calling gettimeofday if the system supports atomic built-ins. */
      tdes_p->query_start_time = log_get_clock_msec ();

      if (query_timeout > 0)
	{
	  tdes_p->query_timeout = tdes_p->query_start_time + query_timeout;
	}
      else if (query_timeout == 0)
	{
	  tdes_p->query_timeout = 0;
	}
      else if (query_timeout != -1)
	{
	  /* already expired */
	  tdes_p->query_timeout = tdes_p->query_start_time;
	}
      else
	{
	  /*
	   * query_timeout == -1
	   * This means that the query is not the first of a bundle of queries.
	   * We will apply a timeout to the bundle, not each query.
	   * Actually CAS always sends -1 in this case.
	   */
	}
      if (tdes_p->tran_start_time == 0)
	{
	  /* set transaction start time, if this is the first query */
	  tdes_p->tran_start_time = tdes_p->query_start_time;
	}
      if (xasl_id != NULL)
	{
	  XASL_ID_COPY (&tdes_p->xasl_id, xasl_id);
	}
    }
}

/*
 * qmgr_reset_query_exec_info () - reset query_start_time and xasl_id of tdes
 *   return: void
 *   tran_index(in):
 */
static void
qmgr_reset_query_exec_info (int tran_index)
{
  LOG_TDES *tdes_p;

  tdes_p = LOG_FIND_TDES (tran_index);
  assert (tdes_p != NULL);
  if (tdes_p != NULL)
    {
      tdes_p->query_start_time = 0;
      XASL_ID_SET_NULL (&tdes_p->xasl_id);
      tdes_p->query_timeout = 0;
    }
}
#endif

/*
 * qmgr_get_sql_id ()
 *   return: error_code
 *   sql_id_buf(out):
 *   buf_size(in):
 *   query(in):
 *   sql_len(in):
 *
 *   note : caller must free sql_id_buf
 *
 *   CUBRID SQL_ID is generated from md5 hash_value. The last 13 hexa-digit string of md5-hash(32 hexa-digit) string.
 *   Oracle's SQL_ID is also generated from md5 hash-value. But it uses the last 8 hexa-digit to generate 13-digit string.
 *   So the SQL_ID of a query is different in CUBRID and oracle, even though the length is same.
 */
int
qmgr_get_sql_id (THREAD_ENTRY * thread_p, char **sql_id_buf, char *query, size_t sql_len)
{
  char hashstring[32 + 1] = { '\0' };
  char *ret_buf;

  if (sql_id_buf == NULL)
    {
      assert_release (0);
      return ER_FAILED;
    }

  ret_buf = (char *) malloc (sizeof (char) * (QMGR_SQL_ID_LENGTH + 1));
  if (ret_buf == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (QMGR_SQL_ID_LENGTH + 1));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  int ec = crypt_md5_buffer_hex (query, sql_len, hashstring);
  if (ec != NO_ERROR)
    {
      free (ret_buf);
      return ec;
    }

  /* copy last 13 hexa-digit to ret_buf */
  strncpy (ret_buf, hashstring + 19, QMGR_SQL_ID_LENGTH);
  ret_buf[QMGR_SQL_ID_LENGTH] = '\0';

  *sql_id_buf = ret_buf;

  return NO_ERROR;
}

/* qmgr_get_rand_buf() : return the drand48_data reference
 * thread_p(in):
 */
struct drand48_data *
qmgr_get_rand_buf (THREAD_ENTRY * thread_p)
{
#if defined(SERVER_MODE)
  return &thread_p->rand_buf;
#else
  return &qmgr_rand_buf;
#endif
}

/*
 * qmgr_get_current_query_id () - return the current query id
 *   return: QUERY_ID
 *   thread_p(in):
 */
QUERY_ID
qmgr_get_current_query_id (THREAD_ENTRY * thread_p)
{
  QMGR_TRAN_ENTRY *tran_entry_p = NULL;
  QUERY_ID query_id = NULL_QUERY_ID;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tran_entry_p = &qmgr_Query_table.tran_entries_p[tran_index];

  if (tran_entry_p->query_entry_list_p != NULL)
    {
      query_id = tran_entry_p->query_entry_list_p->query_id;
    }

  return query_id;
}

/*
 * qmgr_get_query_sql_user_text () - return sql_user_text of the given query_id
 *   return: query string
 *   thread_p(in):
 *   query_id(in):
 *   tran_index(in):
 */
char *
qmgr_get_query_sql_user_text (THREAD_ENTRY * thread_p, QUERY_ID query_id, int tran_index)
{
  QMGR_QUERY_ENTRY *query_ent_p = NULL;
  XASL_CACHE_ENTRY *xasl_ent_p = NULL;
  char *query_str = NULL;

  query_ent_p = qmgr_get_query_entry (thread_p, query_id, tran_index);
  if (query_ent_p != NULL)
    {
      xasl_ent_p = query_ent_p->xasl_ent;
      if (xasl_ent_p != NULL)
	{
	  query_str = xasl_ent_p->sql_info.sql_user_text;
	}
    }

  return query_str;
}
