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
 * list_file.c - Query List File Manager
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <search.h>
#include <stddef.h>
#include <assert.h>

#include "list_file.h"

#include "binaryheap.h"
#include "db_value_printer.hpp"
#include "dbtype.h"
#include "error_manager.h"
#include "log_append.hpp"
#include "object_primitive.h"
#include "object_representation.h"
#include "query_manager.h"
#include "query_opfunc.h"
#include "stream_to_xasl.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"	// for thread_sleep
#include "xasl.h"
#include "xasl_cache.h"

/* TODO */
#if !defined (SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;

#define thread_sleep(a)
#endif /* not SERVER_MODE */

#define QFILE_CHECK_LIST_FILE_IS_CLOSED(list_id)

#define QFILE_DEFAULT_PAGES 4

#if defined (SERVER_MODE)
#define LS_PUT_NEXT_VPID(ptr) \
  do \
    { \
      OR_PUT_INT ((ptr) + QFILE_NEXT_PAGE_ID_OFFSET, NULL_PAGEID_IN_PROGRESS); \
      OR_PUT_SHORT ((ptr) + QFILE_NEXT_VOL_ID_OFFSET, NULL_VOLID); \
    } \
  while (0)
#else
#define LS_PUT_NEXT_VPID(ptr) \
  do \
    { \
       OR_PUT_INT ((ptr) + QFILE_NEXT_PAGE_ID_OFFSET, NULL_PAGEID); \
       OR_PUT_SHORT ((ptr) + QFILE_NEXT_VOL_ID_OFFSET, NULL_VOLID); \
    } \
  while (0)
#endif

typedef struct qfile_cleanup_candidate QFILE_CACHE_CLEANUP_CANDIDATE;
struct qfile_cleanup_candidate
{
  QFILE_LIST_CACHE_ENTRY *qcache;
  double weight;
};

typedef SCAN_CODE (*ADVANCE_FUCTION) (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID *, QFILE_TUPLE_RECORD *,
				      QFILE_LIST_SCAN_ID *, QFILE_TUPLE_RECORD *, QFILE_TUPLE_VALUE_TYPE_LIST *);

/* query result(list file) cache related things */
typedef struct qfile_list_cache QFILE_LIST_CACHE;
struct qfile_list_cache
{
  MHT_TABLE **list_hts;		/* array of memory hash tables for list cache; pool for list_ht of XASL_CACHE_ENTRY */
  int *free_ht_list;		/* array of freed hash tables */
  int next_ht_no;		/* the next freed hash table number */
  unsigned int n_hts;		/* number of elements of list_hts */
  int n_entries;		/* total number of cache entries */
  int n_pages;			/* total number of pages used by the cache */
  unsigned int lookup_counter;	/* counter of cache lookup */
  unsigned int hit_counter;	/* counter of cache hit */
  unsigned int miss_counter;	/* counter of cache miss */
  unsigned int full_counter;	/* counter of cache full & replacement */
};

typedef struct qfile_list_cache_candidate QFILE_LIST_CACHE_CANDIDATE;
struct qfile_list_cache_candidate
{
  int total_num;		/* total number of cache entries */
  int num_candidates;		/* number of candidates */
  int num_victims;		/* number of victims */
  int selcnt;
  QFILE_LIST_CACHE_ENTRY **time_candidates;	/* candidates who are old aged */
  QFILE_LIST_CACHE_ENTRY **ref_candidates;	/* candidates who are recently used */
  QFILE_LIST_CACHE_ENTRY **victims;	/* victims; cache entries to be deleted */
  int c_idx;
  int v_idx;
  bool include_in_use;
};

/* list cache entry pooling */
#define FIXED_SIZE_OF_POOLED_LIST_CACHE_ENTRY   4096
#define ADDITION_FOR_POOLED_LIST_CACHE_ENTRY    offsetof(QFILE_POOLED_LIST_CACHE_ENTRY, s.entry)

#define POOLED_LIST_CACHE_ENTRY_FROM_LIST_CACHE_ENTRY(p) \
  ((QFILE_POOLED_LIST_CACHE_ENTRY *) ((char*) p - ADDITION_FOR_POOLED_LIST_CACHE_ENTRY))

typedef union qfile_pooled_list_cache_entry QFILE_POOLED_LIST_CACHE_ENTRY;
union qfile_pooled_list_cache_entry
{
  struct
  {
    int next;			/* next entry in the free list */
    QFILE_LIST_CACHE_ENTRY entry;	/* list cache entry data */
  } s;
  char dummy[FIXED_SIZE_OF_POOLED_LIST_CACHE_ENTRY];
  /*
   * 4K size including list cache entry itself
   * and reserved spaces for list_cache_ent.param_values
   */
};

static const int RESERVED_SIZE_FOR_LIST_CACHE_ENTRY =
  (FIXED_SIZE_OF_POOLED_LIST_CACHE_ENTRY - ADDITION_FOR_POOLED_LIST_CACHE_ENTRY);

typedef struct qfile_list_cache_entry_pool QFILE_LIST_CACHE_ENTRY_POOL;
struct qfile_list_cache_entry_pool
{
  QFILE_POOLED_LIST_CACHE_ENTRY *pool;	/* pre-allocated array */
  int n_entries;		/* number of entries in the pool */
  int free_list;		/* the head(first entry) of the free list */
};

/*
 * query result(list file) cache related things
 */

/* list cache and related information */
static QFILE_LIST_CACHE qfile_List_cache = { NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0 };

/* information of candidates to be removed from XASL cache */
static QFILE_LIST_CACHE_CANDIDATE qfile_List_cache_candidate = { 0, 0, 0, 0, NULL, NULL, NULL, 0, 0, false };

/* list cache entry pool */
static QFILE_LIST_CACHE_ENTRY_POOL qfile_List_cache_entry_pool = { NULL, 0, 0 };

/* sort list freelist */
static LF_FREELIST qfile_sort_list_Freelist;

static void *qfile_alloc_sort_list (void);
static int qfile_dealloc_sort_list (void *sort_list);

static LF_ENTRY_DESCRIPTOR qfile_sort_list_entry_desc = {
  offsetof (SORT_LIST, local_next),
  offsetof (SORT_LIST, next),
  offsetof (SORT_LIST, del_id),
  0,				/* does not have a key, not used in a hash table */
  0,				/* does not have a mutex */
  LF_EM_NOT_USING_MUTEX,
  qfile_alloc_sort_list,
  qfile_dealloc_sort_list,
  NULL,
  NULL,
  NULL, NULL, NULL,		/* no key */
  NULL				/* no inserts */
};

/*
 * Query File Manager Constants/Global Variables
 */
int qfile_Is_list_cache_disabled;

static int qfile_Max_tuple_page_size;

static int qfile_get_sort_list_size (SORT_LIST * sort_list);
static int qfile_compare_tuple_values (QFILE_TUPLE tplp1, QFILE_TUPLE tplp2, TP_DOMAIN * domain, int *cmp);
#if defined (CUBRID_DEBUG)
static void qfile_print_tuple (QFILE_TUPLE_VALUE_TYPE_LIST * type_list, QFILE_TUPLE tpl);
#endif
static void qfile_initialize_page_header (PAGE_PTR page_p);
static void qfile_set_dirty_page_and_skip_logging (THREAD_ENTRY * thread_p, PAGE_PTR page_p, VFID * vfid_p,
						   int free_page);
static bool qfile_is_first_tuple (QFILE_LIST_ID * list_id_p);
static bool qfile_is_last_page_full (QFILE_LIST_ID * list_id_p, int tuple_length, const bool is_ovf_page);
static void qfile_set_dirty_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, int free_page, QMGR_TEMP_FILE * vfid_p);
static PAGE_PTR qfile_allocate_new_page (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, PAGE_PTR page_p,
					 bool is_ovf_page);
static PAGE_PTR qfile_allocate_new_ovf_page (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, PAGE_PTR page_p,
					     PAGE_PTR prev_page_p, int tuple_length, int offset,
					     int *tuple_page_size_p);
static int qfile_allocate_new_page_if_need (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, PAGE_PTR * page_p,
					    int tuple_length, bool is_ovf_page);
static void qfile_add_tuple_to_list_id (QFILE_LIST_ID * list_id_p, PAGE_PTR page_p, int tuple_length,
					int written_tuple_length);
static int qfile_save_single_bound_item_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, char *tuple_p, char *page_p,
					       int tuple_length);
static int qfile_save_normal_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, char *tuple_p, char *page_p,
				    int tuple_length);
static int qfile_save_sort_key_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, char *tuple_p, char *page_p,
				      int tuple_length);
static int qfile_save_merge_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, char *tuple_p, char *page_p,
				   int *tuple_length_p);

static int qfile_compare_tuple_helper (QFILE_TUPLE lhs, QFILE_TUPLE rhs, QFILE_TUPLE_VALUE_TYPE_LIST * types, int *cmp);
static SCAN_CODE qfile_advance_single (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * next_scan,
				       QFILE_TUPLE_RECORD * next_tpl, QFILE_LIST_SCAN_ID * last_scan,
				       QFILE_TUPLE_RECORD * last_tpl, QFILE_TUPLE_VALUE_TYPE_LIST * types);
static SCAN_CODE qfile_advance_group (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * next_scan,
				      QFILE_TUPLE_RECORD * next_tpl, QFILE_LIST_SCAN_ID * last_scan,
				      QFILE_TUPLE_RECORD * last_tpl, QFILE_TUPLE_VALUE_TYPE_LIST * types);
static int qfile_add_one_tuple (THREAD_ENTRY * thread_p, QFILE_LIST_ID * dst, QFILE_TUPLE lhs, QFILE_TUPLE rhs);
static int qfile_add_two_tuple (THREAD_ENTRY * thread_p, QFILE_LIST_ID * dst, QFILE_TUPLE lhs, QFILE_TUPLE rhs);
static int qfile_advance (THREAD_ENTRY * thread_p, ADVANCE_FUCTION advance_func, QFILE_TUPLE_RECORD * side_p,
			  QFILE_TUPLE_RECORD * last_side_p, QFILE_LIST_SCAN_ID * scan_p,
			  QFILE_LIST_SCAN_ID * last_scan_p, QFILE_LIST_ID * side_file_p, int *have_side_p);
static int qfile_copy_tuple (THREAD_ENTRY * thread_p, QFILE_LIST_ID * to_list_id_p, QFILE_LIST_ID * from_list_id_p);
static void qfile_close_and_free_list_file (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id);

static QFILE_LIST_ID *qfile_union_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1, QFILE_LIST_ID * list_id2,
					int flag);

static SORT_STATUS qfile_get_next_sort_item (THREAD_ENTRY * thread_p, RECDES * recdes, void *arg);
static int qfile_put_next_sort_item (THREAD_ENTRY * thread_p, const RECDES * recdes, void *arg);
static SORT_INFO *qfile_initialize_sort_info (SORT_INFO * info, QFILE_LIST_ID * listid, SORT_LIST * sort_list);
static void qfile_clear_sort_info (SORT_INFO * info);
static int qfile_copy_list_pages (THREAD_ENTRY * thread_p, VPID * old_first_vpidp, QMGR_TEMP_FILE * old_tfile_vfidp,
				  VPID * new_first_vpidp, VPID * new_last_vpidp, QMGR_TEMP_FILE * new_tfile_vfidp);
static int qfile_get_tuple_from_current_list (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p,
					      QFILE_TUPLE_RECORD * tuple_record_p);

static SCAN_CODE qfile_scan_next (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * s_id);
static SCAN_CODE qfile_scan_prev (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * s_id);
static SCAN_CODE qfile_retrieve_tuple (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p,
				       QFILE_TUPLE_RECORD * tuple_record_p, int peek);
static SCAN_CODE qfile_scan_list (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p,
				  SCAN_CODE (*scan_func) (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID *),
				  QFILE_TUPLE_RECORD * tuple_record_p, int peek);

#if defined(SERVER_MODE)
static int qfile_compare_tran_id (const void *t1, const void *t2);
#endif /* SERVER_MODE */
static unsigned int qfile_hash_db_value_array (const void *key, unsigned int htsize);
static int qfile_compare_equal_db_value_array (const void *key1, const void *key2);

/* for list cache */
static int qfile_assign_list_cache (void);
static QFILE_LIST_CACHE_ENTRY *qfile_allocate_list_cache_entry (int req_size);
static int qfile_free_list_cache_entry (THREAD_ENTRY * thread_p, void *data, void *args);
static int qfile_print_list_cache_entry (THREAD_ENTRY * thread_p, FILE * fp, const void *key, void *data, void *args);
static void qfile_add_uncommitted_list_cache_entry (int tran_index, QFILE_LIST_CACHE_ENTRY * lent);
static void qfile_delete_uncommitted_list_cache_entry (int tran_index, QFILE_LIST_CACHE_ENTRY * lent);
static int qfile_delete_list_cache_entry (THREAD_ENTRY * thread_p, void *data);
static int qfile_end_use_of_list_cache_entry_local (THREAD_ENTRY * thread_p, void *data, void *args);
static bool qfile_is_early_time (struct timeval *a, struct timeval *b);

static int qfile_get_list_cache_entry_size_for_allocate (int nparam);
#if defined(SERVER_MODE)
static int *qfile_get_list_cache_entry_tran_index_array (QFILE_LIST_CACHE_ENTRY * ent);
#endif /* SERVER_MODE */
static DB_VALUE *qfile_get_list_cache_entry_param_values (QFILE_LIST_CACHE_ENTRY * ent);
static int qfile_compare_with_null_value (int o0, int o1, SUBKEY_INFO key_info);
static int qfile_compare_with_interpolation_domain (char *fp0, char *fp1, SUBKEY_INFO * subkey,
						    SORTKEY_INFO * key_info);

#if defined(SERVER_MODE)
static BH_CMP_RESULT
qfile_compare_cleanup_candidates (const void *left, const void *right, BH_CMP_ARG ignore_arg)
{
  double left_weight = ((QFILE_CACHE_CLEANUP_CANDIDATE *) left)->weight;
  double right_weight = ((QFILE_CACHE_CLEANUP_CANDIDATE *) right)->weight;

  if (left_weight < right_weight)
    {
      return BH_LT;
    }
  else if (left_weight == right_weight)
    {
      return BH_EQ;
    }
  else
    {
      return BH_GT;
    }
}

static int
qfile_list_cache_cleanup (THREAD_ENTRY * thread_p)
{
  BINARY_HEAP *bh = NULL;
  QFILE_CACHE_CLEANUP_CANDIDATE candidate;
  int candidate_index;
  unsigned int i, n;

  struct timeval current_time;
  int cleanup_count = prm_get_integer_value (PRM_ID_LIST_MAX_QUERY_CACHE_ENTRIES) * 8 / 10;
  int cleanup_pages = prm_get_integer_value (PRM_ID_LIST_MAX_QUERY_CACHE_PAGES) * 8 / 10;

  if (cleanup_count < 1)
    {
      cleanup_count = 1;
    }

  if (cleanup_pages < 1)
    {
      cleanup_pages = 1;
    }

  bh =
    bh_create (thread_p, cleanup_count, sizeof (QFILE_CACHE_CLEANUP_CANDIDATE), qfile_compare_cleanup_candidates, NULL);

  if (bh == NULL)
    {
      return ER_FAILED;
    }

  gettimeofday (&current_time, NULL);

  /* Collect candidates for cleanup. */
  for (n = 0; n < qfile_List_cache.n_hts; n++)
    {
      MHT_TABLE *ht;
      HENTRY_PTR *hvector;
      HENTRY_PTR hentry;
      INT64 page_ref;
      INT64 lru_sec;

      ht = qfile_List_cache.list_hts[n];
      for (hvector = ht->table, i = 0; i < ht->size; hvector++, i++)
	{
	  if (*hvector != NULL)
	    {
	      /* Go over the linked list */
	      for (hentry = *hvector; hentry != NULL; hentry = hentry->next)
		{
		  candidate.qcache = (QFILE_LIST_CACHE_ENTRY *) hentry->data;
		  assert (candidate.qcache);
		  assert (candidate.qcache->xcache_entry);
		  if (candidate.qcache->last_ta_idx > 0)
		    {
		      // exclude in-transaction
		      continue;
		    }
		  page_ref = candidate.qcache->list_id.page_cnt + 1;
		  lru_sec = current_time.tv_sec - candidate.qcache->time_last_used.tv_sec + 1;
		  candidate.weight = (double) (candidate.qcache->ref_count + 1) / (double) (page_ref * lru_sec);
		  (void) bh_try_insert (bh, &candidate, NULL);
		}
	    }
	}
    }

  /* traverse in reverse for weight ordering, from light weight to heavy weight */
  for (candidate_index = bh->element_count - 1; candidate_index >= 0; candidate_index--)
    {
      bh_element_at (bh, candidate_index, &candidate);
      qfile_delete_list_cache_entry (thread_p, candidate.qcache);
      if (qfile_List_cache.n_entries <= cleanup_count)
	{
	  if (qfile_List_cache.n_pages <= cleanup_pages)
	    {
	      break;
	    }
	}
    }

  bh_destroy (thread_p, bh);

  return NO_ERROR;
}
#endif

int
qcache_get_new_ht_no (THREAD_ENTRY * thread_p)
{
  int ht_no = -1;

  if (qfile_List_cache.next_ht_no >= 0)
    {
      ht_no = qfile_List_cache.next_ht_no;
      qfile_List_cache.next_ht_no = qfile_List_cache.free_ht_list[qfile_List_cache.next_ht_no];
    }

  return ht_no;
}

void
qcache_free_ht_no (THREAD_ENTRY * thread_p, int ht_no)
{
  (void) mht_clear (qfile_List_cache.list_hts[ht_no], NULL, NULL);
  qfile_List_cache.free_ht_list[ht_no] = qfile_List_cache.next_ht_no;
  qfile_List_cache.next_ht_no = ht_no;
}

/* qfile_modify_type_list () -
 *   return:
 *   type_list(in):
 *   list_id(out):
 */
int
qfile_modify_type_list (QFILE_TUPLE_VALUE_TYPE_LIST * type_list_p, QFILE_LIST_ID * list_id_p)
{
  size_t type_list_size;

  list_id_p->type_list.type_cnt = type_list_p->type_cnt;

  list_id_p->type_list.domp = NULL;
  if (list_id_p->type_list.type_cnt != 0)
    {
      type_list_size = list_id_p->type_list.type_cnt * DB_SIZEOF (TP_DOMAIN *);
      list_id_p->type_list.domp = (TP_DOMAIN **) malloc (type_list_size);
      if (list_id_p->type_list.domp == NULL)
	{
	  return ER_FAILED;
	}

      memcpy (list_id_p->type_list.domp, type_list_p->domp, type_list_size);
    }

  list_id_p->tpl_descr.f_valp = NULL;
  list_id_p->tpl_descr.clear_f_val_at_clone_decache = NULL;
  return NO_ERROR;
}

/*
 * qfile_copy_list_id - Copy contents of source list_id into destination list_id
 *  return: NO_ERROR or ER_FAILED
 *  dest_list_id(out): destination list_id
 *  src_list_id(in): source list_id
 *  include_sort_list(in):
 */
int
qfile_copy_list_id (QFILE_LIST_ID * dest_list_id_p, const QFILE_LIST_ID * src_list_id_p, bool is_include_sort_list)
{
  size_t type_list_size;

  memcpy (dest_list_id_p, src_list_id_p, DB_SIZEOF (QFILE_LIST_ID));

  /* copy domain info of type list */
  dest_list_id_p->type_list.domp = NULL;
  if (dest_list_id_p->type_list.type_cnt > 0)
    {
      type_list_size = dest_list_id_p->type_list.type_cnt * sizeof (TP_DOMAIN *);
      dest_list_id_p->type_list.domp = (TP_DOMAIN **) malloc (type_list_size);

      if (dest_list_id_p->type_list.domp == NULL)
	{
	  return ER_FAILED;
	}

      memcpy (dest_list_id_p->type_list.domp, src_list_id_p->type_list.domp, type_list_size);
    }

  /* copy sort list */
  dest_list_id_p->sort_list = NULL;
  if (is_include_sort_list && src_list_id_p->sort_list)
    {
      SORT_LIST *src, *dest = NULL;
      int len;

      len = qfile_get_sort_list_size (src_list_id_p->sort_list);
      if (len > 0)
	{
	  dest = qfile_allocate_sort_list (NULL, len);
	  if (dest == NULL)
	    {
	      free_and_init (dest_list_id_p->type_list.domp);
	      return ER_FAILED;
	    }
	}

      /* copy sort list item */
      for (src = src_list_id_p->sort_list, dest_list_id_p->sort_list = dest; src != NULL && dest != NULL;
	   src = src->next, dest = dest->next)
	{
	  dest->s_order = src->s_order;
	  dest->s_nulls = src->s_nulls;
	  dest->pos_descr.dom = src->pos_descr.dom;
	  dest->pos_descr.pos_no = src->pos_descr.pos_no;
	}
    }
  else
    {
      dest_list_id_p->sort_list = NULL;
    }

  memset (&dest_list_id_p->tpl_descr, 0, sizeof (QFILE_TUPLE_DESCRIPTOR));

  qfile_update_qlist_count (thread_get_thread_entry_info (), dest_list_id_p, 1);

  return NO_ERROR;
}

/*
 * qfile_clone_list_id () - Clone (allocate and copy) the list_id
 *   return: cloned list id
 *   list_id(in): source list id
 *   incluse_sort_list(in):
 */
QFILE_LIST_ID *
qfile_clone_list_id (const QFILE_LIST_ID * list_id_p, bool is_include_sort_list)
{
  QFILE_LIST_ID *cloned_id_p;

  /* allocate new LIST_ID to be returned */
  cloned_id_p = (QFILE_LIST_ID *) malloc (DB_SIZEOF (QFILE_LIST_ID));
  if (cloned_id_p)
    {
      if (qfile_copy_list_id (cloned_id_p, list_id_p, is_include_sort_list) != NO_ERROR)
	{
	  free_and_init (cloned_id_p);
	}
    }

  return cloned_id_p;
}

/*
 * qfile_clear_list_id () -
 *   list_id(in/out): List identifier
 *
 * Note: The allocated areas inside the area pointed by the list_id is
 *       freed and the area pointed by list_id is set to null values.
 */
void
qfile_clear_list_id (QFILE_LIST_ID * list_id_p)
{
  qfile_update_qlist_count (thread_get_thread_entry_info (), list_id_p, -1);

  if (list_id_p->tpl_descr.f_valp)
    {
      free_and_init (list_id_p->tpl_descr.f_valp);
    }

  if (list_id_p->tpl_descr.clear_f_val_at_clone_decache)
    {
      free_and_init (list_id_p->tpl_descr.clear_f_val_at_clone_decache);
    }

  if (list_id_p->sort_list)
    {
      qfile_free_sort_list (NULL, list_id_p->sort_list);
      list_id_p->sort_list = NULL;
    }

  if (list_id_p->type_list.domp != NULL)
    {
      free_and_init (list_id_p->type_list.domp);
    }

  QFILE_CLEAR_LIST_ID (list_id_p);
}

/*
 * qfile_free_list_id () -
 *   return:
 *   list_id(in/out): List identifier
 *
 * Note: The allocated areas inside the area pointed by the list_id and
 *       the area itself pointed by the list_id are freed.
 */
void
qfile_free_list_id (QFILE_LIST_ID * list_id_p)
{
  /* This function is remained for debugging purpose. Do not call this function directly. Use
   * QFILE_FREE_AND_INIT_LIST_ID macro. */
  qfile_clear_list_id (list_id_p);
  free (list_id_p);
}


/*
 * qfile_free_sort_list () -
 *   return:
 *   sort_list(in): Sort item list pointer
 *
 * Note: The area allocated for sort_list is freed.
 */
void
qfile_free_sort_list (THREAD_ENTRY * thread_p, SORT_LIST * sort_list_p)
{
  LF_TRAN_ENTRY *t_entry;
  SORT_LIST *tmp;

  /* get tran entry */
  t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FREE_SORT_LIST);

  while (sort_list_p != NULL)
    {
      tmp = sort_list_p;
      sort_list_p = sort_list_p->next;
      tmp->next = NULL;

      if (lf_freelist_retire (t_entry, &qfile_sort_list_Freelist, tmp) != NO_ERROR)
	{
	  assert (false);
	  return;
	}
    }
}

/*
 * qfile_allocate_sort_list () -
 *   return: sort item list, or NULL
 *   cnt(in): Number of nodes in the list
 *
 * Note: A linked list of cnt sort structure nodes is allocated and returned.
 *
 * Note: Only qfile_free_sort_list function should be used to free the area
 *       since the linked list is allocated in a contigous region.
 */
SORT_LIST *
qfile_allocate_sort_list (THREAD_ENTRY * thread_p, int count)
{
  LF_TRAN_ENTRY *t_entry;
  SORT_LIST *head = NULL, *tail = NULL, *tmp;

  if (count <= 0)
    {
      return NULL;
    }

  /* fetch tran entry */
  t_entry = thread_get_tran_entry (thread_p, THREAD_TS_FREE_SORT_LIST);

  /* allocate complete list */
  while (count > 0)
    {
      tmp = (SORT_LIST *) lf_freelist_claim (t_entry, &qfile_sort_list_Freelist);
      if (tmp == NULL)
	{
	  assert (false);
	  return NULL;
	}

      if (head == NULL)
	{
	  head = tmp;
	  tail = tmp;
	}
      else
	{
	  tail->next = tmp;
	  tail = tmp;
	}

      count--;
    }

  return head;
}

/*
 * qfile_alloc_sort_list () - allocate a sort list
 *   returns: new sort list
 */
static void *
qfile_alloc_sort_list (void)
{
  return malloc (sizeof (SORT_LIST));
}

/*
 * qfile_dealloc_sort_list () - deallocate a sort list
 *   returns: error code or NO_ERROR
 *   sort_list(in): sort list to free
 */
static int
qfile_dealloc_sort_list (void *sort_list)
{
  free (sort_list);
  return NO_ERROR;
}

/*
 * qfile_get_sort_list_size () -
 *   return: the number of sort_list item
 *   sort_list(in): sort item list pointer
 *
 */
static int
qfile_get_sort_list_size (SORT_LIST * sort_list_p)
{
  SORT_LIST *s;
  int len = 0;

  for (s = sort_list_p; s; s = s->next)
    {
      ++len;
    }

  return len;
}

/*
 * qfile_is_sort_list_covered () -
 *   return: true or false
 *   covering_list(in): covering sort item list pointer
 *   covered_list(in): covered sort item list pointer
 *
 * Note: if covering_list covers covered_list returns true.
 *       otherwise, returns false.
 */
bool
qfile_is_sort_list_covered (SORT_LIST * covering_list_p, SORT_LIST * covered_list_p)
{
  SORT_LIST *s1, *s2;

  if (covered_list_p == NULL)
    {
      return false;
    }

  for (s1 = covering_list_p, s2 = covered_list_p; s1 && s2; s1 = s1->next, s2 = s2->next)
    {
      if (s1->s_order != s2->s_order || s1->s_nulls != s2->s_nulls || s1->pos_descr.pos_no != s2->pos_descr.pos_no)
	{
	  return false;
	}
    }

  if (s1 == NULL && s2)
    {
      return false;
    }
  else
    {
      return true;
    }
}

/*
 * qfile_compare_tuple_values () -
 *   return: NO_ERROR or error code
 *   tpl1(in): First tuple value
 *   tpl2(in): Second tuple value
 *   domain(in): both tuple values must be of the same domain
 *
 * Note: This routine checks if two tuple values are equal.
 *       Coercion is not done.
 *       If both values are UNBOUND, the values are treated as equal.
 */
static int
qfile_compare_tuple_values (QFILE_TUPLE tuple1, QFILE_TUPLE tuple2, TP_DOMAIN * domain_p, int *compare_result)
{
  OR_BUF buf;
  DB_VALUE dbval1, dbval2;
  int length1, length2;
  PR_TYPE *pr_type_p;
  bool is_copy;
  DB_TYPE type = TP_DOMAIN_TYPE (domain_p);
  int rc;

  pr_type_p = domain_p->type;
  is_copy = false;
  is_copy = pr_is_set_type (type) ? true : false;
  length1 = QFILE_GET_TUPLE_VALUE_LENGTH (tuple1);

  /* zero length means NULL */
  if (length1 == 0)
    {
      db_make_null (&dbval1);
    }
  else
    {
      or_init (&buf, (char *) tuple1 + QFILE_TUPLE_VALUE_HEADER_SIZE, length1);
      rc = pr_type_p->data_readval (&buf, &dbval1, domain_p, -1, is_copy, NULL, 0);
      if (rc != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  length2 = QFILE_GET_TUPLE_VALUE_LENGTH (tuple2);

  /* zero length means NULL */
  if (length2 == 0)
    {
      db_make_null (&dbval2);
    }
  else
    {
      or_init (&buf, (char *) tuple2 + QFILE_TUPLE_VALUE_HEADER_SIZE, length2);
      rc = pr_type_p->data_readval (&buf, &dbval2, domain_p, -1, is_copy, NULL, 0);
      if (rc != NO_ERROR)
	{
	  pr_clear_value (&dbval1);
	  return ER_FAILED;
	}
    }

  if (length1 == 0 && length2 == 0)
    {
      *compare_result = DB_EQ;	/* NULL values compare equal in this routine */
    }
  else if (length1 == 0)
    {
      *compare_result = DB_LT;
    }
  else if (length2 == 0)
    {
      *compare_result = DB_GT;
    }
  else
    {
      *compare_result = pr_type_p->cmpval (&dbval1, &dbval2, 0, 1, NULL, domain_p->collation_id);
    }

  pr_clear_value (&dbval1);
  pr_clear_value (&dbval2);

  return NO_ERROR;
}

/*
 * qfile_unify_types () -
 *   return:
 *   list_id1(in/out): Destination list identifier
 *   list_id2(in): Source list identifier
 *
 * Note: For every destination type which is DB_TYPE_NULL,
 *       set it to the source type.
 *       This should probably set an error for non-null mismatches.
 */
int
qfile_unify_types (QFILE_LIST_ID * list_id1_p, const QFILE_LIST_ID * list_id2_p)
{
  int i;
  int max_count = list_id1_p->type_list.type_cnt;
  DB_TYPE type1, type2;

  if (max_count != list_id2_p->type_list.type_cnt)
    {
      /* error, but is ignored for now. */
      if (max_count > list_id2_p->type_list.type_cnt)
	{
	  max_count = list_id2_p->type_list.type_cnt;
	}
    }

  for (i = 0; i < max_count; i++)
    {
      type1 = TP_DOMAIN_TYPE (list_id1_p->type_list.domp[i]);
      type2 = TP_DOMAIN_TYPE (list_id2_p->type_list.domp[i]);

      if (type1 == DB_TYPE_VARIABLE)
	{
	  /* The domain of list1 is not resolved, because there is no tuple. */
	  assert_release (list_id1_p->tuple_cnt == 0);
	  list_id1_p->type_list.domp[i] = list_id2_p->type_list.domp[i];
	  continue;
	}
      else if (type2 == DB_TYPE_VARIABLE)
	{
	  /* The domain of list2 is not resolved, because there is no tuple. */
	  assert_release (list_id2_p->tuple_cnt == 0);
	  continue;
	}

      if (type1 == DB_TYPE_NULL)
	{
	  list_id1_p->type_list.domp[i] = list_id2_p->type_list.domp[i];
	}
      else
	{
	  if (type2 != DB_TYPE_NULL && (list_id1_p->type_list.domp[i] != list_id2_p->type_list.domp[i]))
	    {
	      if (type1 == type2
		  && ((pr_is_string_type (type1) && pr_is_variable_type (type1)) || (type1 == DB_TYPE_JSON)))
		{
		  /* OK for variable string types with different precision or json types */
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INCOMPATIBLE_TYPES, 0);
		  return ER_QPROC_INCOMPATIBLE_TYPES;
		}
	    }
	}

      if (TP_DOMAIN_COLLATION_FLAG (list_id1_p->type_list.domp[i]) != TP_DOMAIN_COLL_NORMAL
	  || TP_DOMAIN_COLLATION_FLAG (list_id2_p->type_list.domp[i]) != TP_DOMAIN_COLL_NORMAL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QSTR_INCOMPATIBLE_COLLATIONS, 0);
	  return ER_QSTR_INCOMPATIBLE_COLLATIONS;
	}
    }

  return NO_ERROR;
}

/*
 * qfile_locate_tuple_value () -
 *   return: V_BOUND/V_UNBOUND
 *   tpl(in): tuple
 *   index(in): value position number
 *   tpl_val(out): set to point to the specified value
 *   val_size(out): set to the value size
 *
 * Note: Sets the tpl_val pointer to the specified value.
 *
 * Note: The index validity check must be done by the caller.
 */
QFILE_TUPLE_VALUE_FLAG
qfile_locate_tuple_value (QFILE_TUPLE tuple, int index, char **tuple_value_p, int *value_size_p)
{
  tuple += QFILE_TUPLE_LENGTH_SIZE;
  return qfile_locate_tuple_value_r (tuple, index, tuple_value_p, value_size_p);
}

/*
 * qfile_locate_tuple_value_r () -
 *   return: V_BOUND/V_UNBOUND
 *   tpl(in): tuple
 *   index(in): value position number
 *   tpl_val(out): set to point to the specified value
 *   val_size(out): set to the value size
 *
 * Note: The index validity check must be done by the caller.
 */
QFILE_TUPLE_VALUE_FLAG
qfile_locate_tuple_value_r (QFILE_TUPLE tuple, int index, char **tuple_value_p, int *value_size_p)
{
  int i;

  for (i = 0; i < index; i++)
    {
      tuple += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (tuple);
    }

  *tuple_value_p = tuple + QFILE_TUPLE_VALUE_HEADER_SIZE;
  *value_size_p = QFILE_GET_TUPLE_VALUE_LENGTH (tuple);

  if (QFILE_GET_TUPLE_VALUE_FLAG (tuple) == V_UNBOUND)
    {
      return V_UNBOUND;
    }
  else
    {
      return V_BOUND;
    }
}

/*
 * qfile_locate_tuple_next_value () -
 *   return: error code or no error
 *   iterator(in): OR_BUF for iterating tuple values
 *   buf(out): output OR_BUF to be positioned on next tuple value
 *   flag(out): V_BOUND/V_UNBOUND
 */
int
qfile_locate_tuple_next_value (OR_BUF * iterator, OR_BUF * buf, QFILE_TUPLE_VALUE_FLAG * flag)
{
  int value_size = QFILE_GET_TUPLE_VALUE_LENGTH (iterator->ptr);
  *flag = QFILE_GET_TUPLE_VALUE_FLAG (iterator->ptr);

  /* initialize output buffer */
  OR_BUF_INIT ((*buf), iterator->ptr + QFILE_TUPLE_VALUE_HEADER_SIZE, value_size);

  /* advance iterator */
  return or_advance (iterator, QFILE_TUPLE_VALUE_HEADER_SIZE + value_size);
}

#if defined (CUBRID_DEBUG)
/*
 * qfile_print_tuple () - Prints the tuple content associated with the type list
 *   return: none
 *   type_list(in): type list
 *   tpl(in): tuple
 * Note: Each tuple start is aligned with MAX_ALIGNMENT
 *       Each tuple value header is aligned with MAX_ALIGNMENT,
 *       Each tuple value is aligned with MAX_ALIGNMENT
 */
static void
qfile_print_tuple (QFILE_TUPLE_VALUE_TYPE_LIST * type_list_p, QFILE_TUPLE tuple)
{
  DB_VALUE dbval;
  PR_TYPE *pr_type_p;
  int i;
  char *tuple_p;
  OR_BUF buf;

  db_make_null (&dbval);

  if (type_list_p == NULL || type_list_p->type_cnt <= 0)
    {
      return;
    }

  fprintf (stdout, "\n{ ");
  tuple_p = (char *) tuple + QFILE_TUPLE_LENGTH_SIZE;

  for (i = 0; i < type_list_p->type_cnt; i++)
    {
      if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_p) == V_BOUND)
	{
	  pr_type_p = type_list_p->domp[i]->type;
	  or_init (&buf, tuple_p + QFILE_TUPLE_VALUE_HEADER_SIZE, QFILE_GET_TUPLE_VALUE_LENGTH (tuple_p));
	  (*(pr_type_p->readval)) (&buf, &dbval, type_list_p->domp[i], -1, true, NULL, 0);

	  db_fprint_value (stdout, &dbval);
	  if (pr_is_set_type (pr_type_p->id))
	    {
	      pr_clear_value (&dbval);
	    }
	}
      else
	{
	  fprintf (stdout, "VALUE_UNBOUND");
	}

      if (i != type_list_p->type_cnt - 1)
	{
	  fprintf (stdout, " , ");
	}

      tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (tuple_p);
    }

  fprintf (stdout, " }\n");
}
#endif

static void
qfile_initialize_page_header (PAGE_PTR page_p)
{
  OR_PUT_INT (page_p + QFILE_TUPLE_COUNT_OFFSET, 0);
  OR_PUT_INT (page_p + QFILE_PREV_PAGE_ID_OFFSET, NULL_PAGEID);
  OR_PUT_INT (page_p + QFILE_NEXT_PAGE_ID_OFFSET, NULL_PAGEID);
  OR_PUT_INT (page_p + QFILE_LAST_TUPLE_OFFSET, 0);
  OR_PUT_INT (page_p + QFILE_OVERFLOW_PAGE_ID_OFFSET, NULL_PAGEID);
  OR_PUT_SHORT (page_p + QFILE_PREV_VOL_ID_OFFSET, NULL_VOLID);
  OR_PUT_SHORT (page_p + QFILE_NEXT_VOL_ID_OFFSET, NULL_VOLID);
  OR_PUT_SHORT (page_p + QFILE_OVERFLOW_VOL_ID_OFFSET, NULL_VOLID);
#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  memset (page_p + QFILE_RESERVED_OFFSET, 0, QFILE_PAGE_HEADER_SIZE - QFILE_RESERVED_OFFSET);
#endif
}

static void
qfile_set_dirty_page_and_skip_logging (THREAD_ENTRY * thread_p, PAGE_PTR page_p, VFID * vfid_p, int free_page)
{
  LOG_DATA_ADDR addr;

  addr.vfid = vfid_p;
  addr.pgptr = page_p;
  addr.offset = -1;
  log_skip_logging (thread_p, &addr);

  pgbuf_set_dirty (thread_p, page_p, free_page);
}

/*
 * qfile_load_xasl_node_header () - Load XASL node header from xasl stream
 *
 * return	       : void
 * thread_p (in)       : thread entry
 * xasl_stream (in)    : XASL stream
 * xasl_header_p (out) : pointer to XASL node header
 */
void
qfile_load_xasl_node_header (THREAD_ENTRY * thread_p, char *xasl_stream, xasl_node_header * xasl_header_p)
{
  if (xasl_header_p == NULL)
    {
      /* cannot save XASL node header */
      return;
    }
  /* initialize XASL node header */
  INIT_XASL_NODE_HEADER (xasl_header_p);

  if (xasl_stream == NULL)
    {
      /* cannot obtain XASL stream */
      return;
    }

  /* get XASL node header from stream */
  if (stx_map_stream_to_xasl_node_header (thread_p, xasl_header_p, xasl_stream) != NO_ERROR)
    {
      assert (false);
    }
}

/*
 * qfile_initialize () -
 *   return: int (true : successful initialization,
 *                false: unsuccessful initialization)
 *
 * Note: This routine initializes the query file manager structures
 * and global variables.
 */
int
qfile_initialize (void)
{
  qfile_Is_list_cache_disabled =
    ((prm_get_integer_value (PRM_ID_LIST_QUERY_CACHE_MODE) == QFILE_LIST_QUERY_CACHE_MODE_OFF)
     || (prm_get_integer_value (PRM_ID_LIST_MAX_QUERY_CACHE_ENTRIES) <= 0)
     || (prm_get_integer_value (PRM_ID_LIST_MAX_QUERY_CACHE_PAGES) <= 0));

  qfile_Max_tuple_page_size = QFILE_MAX_TUPLE_SIZE_IN_PAGE;

  if (lf_freelist_init (&qfile_sort_list_Freelist, 10, 100, &qfile_sort_list_entry_desc, &free_sort_list_Ts) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  return true;
}

/* qfile_finalize () -
 *   return:
 */
void
qfile_finalize (void)
{
  lf_freelist_destroy (&qfile_sort_list_Freelist);
}

/*
 * qfile_open_list () -
 *   return: QFILE_LIST_ID *, or NULL
 *   type_list(in/out): type list for the list file to be created
 *   sort_list(in): sort info for the list file to be created
 *   query_id(in): query id associated with this list file
 *   flag(in): {QFILE_FLAG_RESULT_FILE, QFILE_FLAG_DISTINCT, QFILE_FLAG_ALL}
 *             whether to do 'all' or 'distinct' operation
 *
 * Note: A list file is created by using the specified type list and
 *       the list file identifier is set. The first page of the list
 *       file is allocated only when the first tuple is inserted to
 *       list file, if any.
 *	 A 'SORT_LIST' is associated to the output list file according to
 *	 'sort_list_p' input argument (if not null), or created if the
 *	 QFILE_FLAG_DISTINCT flag is specified; if neither QFILE_FLAG_DISTINCT
 *	 or 'sort_list_p' are supplied, no SORT_LIST is associated.
 *
 */
QFILE_LIST_ID *
qfile_open_list (THREAD_ENTRY * thread_p, QFILE_TUPLE_VALUE_TYPE_LIST * type_list_p, SORT_LIST * sort_list_p,
		 QUERY_ID query_id, int flag)
{
  QFILE_LIST_ID *list_id_p;
  int len, i;
  SORT_LIST *src_sort_list_p, *dest_sort_list_p;
  size_t type_list_size;

  list_id_p = (QFILE_LIST_ID *) malloc (sizeof (QFILE_LIST_ID));
  if (list_id_p == NULL)
    {
      return NULL;
    }

  QFILE_CLEAR_LIST_ID (list_id_p);
  list_id_p->tuple_cnt = 0;
  list_id_p->page_cnt = 0;
  list_id_p->type_list.type_cnt = type_list_p->type_cnt;
  list_id_p->query_id = query_id;

  if (QFILE_IS_FLAG_SET (flag, QFILE_FLAG_RESULT_FILE) && !QFILE_IS_LIST_CACHE_DISABLED)
    {
      list_id_p->tfile_vfid = qmgr_create_result_file (thread_p, query_id);
    }
  else if (QFILE_IS_FLAG_SET (flag, QFILE_FLAG_USE_KEY_BUFFER))
    {
      list_id_p->tfile_vfid = qmgr_create_new_temp_file (thread_p, query_id, TEMP_FILE_MEMBUF_KEY_BUFFER);
    }
  else
    {
      list_id_p->tfile_vfid = qmgr_create_new_temp_file (thread_p, query_id, TEMP_FILE_MEMBUF_NORMAL);
    }

  if (list_id_p->tfile_vfid == NULL)
    {
      free_and_init (list_id_p);
      return NULL;
    }

  VFID_COPY (&(list_id_p->temp_vfid), &(list_id_p->tfile_vfid->temp_vfid));
  list_id_p->type_list.domp = NULL;

  if (list_id_p->type_list.type_cnt != 0)
    {
      type_list_size = list_id_p->type_list.type_cnt * DB_SIZEOF (TP_DOMAIN *);
      list_id_p->type_list.domp = (TP_DOMAIN **) malloc (type_list_size);
      if (list_id_p->type_list.domp == NULL)
	{
	  free_and_init (list_id_p);
	  return NULL;
	}

      memcpy (list_id_p->type_list.domp, type_list_p->domp, type_list_size);
    }

  /* build sort_list */
  if (QFILE_IS_FLAG_SET (flag, QFILE_FLAG_DISTINCT))
    {
      len = list_id_p->type_list.type_cnt;
      if (len > 0)
	{
	  dest_sort_list_p = qfile_allocate_sort_list (thread_p, len);
	  if (dest_sort_list_p == NULL)
	    {
	      free_and_init (list_id_p->type_list.domp);
	      free_and_init (list_id_p);
	      return NULL;
	    }

	  for (i = 0, list_id_p->sort_list = dest_sort_list_p; i < len; i++, dest_sort_list_p = dest_sort_list_p->next)
	    {
	      dest_sort_list_p->s_order = S_ASC;
	      dest_sort_list_p->s_nulls = S_NULLS_FIRST;
	      dest_sort_list_p->pos_descr.dom = list_id_p->type_list.domp[i];
	      dest_sort_list_p->pos_descr.pos_no = i;
	    }
	}
    }
  else if (sort_list_p != NULL)
    {
      len = qfile_get_sort_list_size (sort_list_p);
      if (len > 0)
	{
	  dest_sort_list_p = qfile_allocate_sort_list (thread_p, len);
	  if (dest_sort_list_p == NULL)
	    {
	      free_and_init (list_id_p->type_list.domp);
	      free_and_init (list_id_p);
	      return NULL;
	    }

	  for (src_sort_list_p = sort_list_p, list_id_p->sort_list = dest_sort_list_p; src_sort_list_p;
	       src_sort_list_p = src_sort_list_p->next, dest_sort_list_p = dest_sort_list_p->next)
	    {
	      dest_sort_list_p->s_order = src_sort_list_p->s_order;
	      dest_sort_list_p->s_nulls = src_sort_list_p->s_nulls;
	      dest_sort_list_p->pos_descr.dom = src_sort_list_p->pos_descr.dom;
	      dest_sort_list_p->pos_descr.pos_no = src_sort_list_p->pos_descr.pos_no;
	    }
	}
    }
  else
    {
      /* no DISTINCT and no source SORT_LIST supplied */
      list_id_p->sort_list = NULL;
    }

  qfile_update_qlist_count (thread_p, list_id_p, 1);

  return list_id_p;
}

/*
 * qfile_close_list () -
 *   return: none
 *   list_id(in/out): List file identifier
 *
 * Note: The specified list file is closed and memory buffer for the
 *       list file is freed.
 */
void
qfile_close_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p)
{
  if (list_id_p)
    {
      if (list_id_p->last_pgptr != NULL)
	{
	  QFILE_PUT_NEXT_VPID_NULL (list_id_p->last_pgptr);

	  qmgr_free_old_page_and_init (thread_p, list_id_p->last_pgptr, list_id_p->tfile_vfid);
	}
    }
}

/*
 * qfile_reopen_list_as_append_mode () -
 *   thread_p(in) :
 *   list_id_p(in):
 *
 * Note:
 */
int
qfile_reopen_list_as_append_mode (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p)
{
  PAGE_PTR last_page_ptr;
  QMGR_TEMP_FILE *temp_file_p;

  if (list_id_p->tfile_vfid == NULL)
    {
      /* Invalid list_id_p. list_id_p might be cleared or not be opened. list_id_p must have valid QMGR_TEMP_FILE to
       * reopen. */
      assert_release (0);
      return ER_FAILED;
    }

  if (VPID_ISNULL (&list_id_p->first_vpid))
    {
      assert_release (VPID_ISNULL (&list_id_p->last_vpid));
      assert_release (list_id_p->last_pgptr == NULL);

      return NO_ERROR;
    }

  if (list_id_p->last_pgptr != NULL)
    {
      return NO_ERROR;
    }

  temp_file_p = list_id_p->tfile_vfid;

  if (temp_file_p->membuf && list_id_p->last_vpid.volid == NULL_VOLID)
    {
      /* The last page is in the membuf */
      assert_release (temp_file_p->membuf_last >= list_id_p->last_vpid.pageid);
      /* The page of last record in the membuf */
      last_page_ptr = temp_file_p->membuf[list_id_p->last_vpid.pageid];
    }
  else
    {
      assert_release (!VPID_ISNULL (&list_id_p->last_vpid));
      last_page_ptr =
	pgbuf_fix (thread_p, &list_id_p->last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (last_page_ptr == NULL)
	{
	  return ER_FAILED;
	}

      (void) pgbuf_check_page_ptype (thread_p, last_page_ptr, PAGE_QRESULT);

    }

  list_id_p->last_pgptr = last_page_ptr;

  return NO_ERROR;
}

static bool
qfile_is_first_tuple (QFILE_LIST_ID * list_id_p)
{
  return VPID_ISNULL (&list_id_p->first_vpid);
}

static bool
qfile_is_last_page_full (QFILE_LIST_ID * list_id_p, int tuple_length, const bool is_ovf_page)
{
  assert (tuple_length >= 0 && list_id_p->last_offset >= 0);
  assert ((tuple_length + list_id_p->last_offset) >= 0);
  assert (list_id_p->last_offset <= DB_PAGESIZE);

  if (!is_ovf_page && list_id_p->last_offset <= QFILE_PAGE_HEADER_SIZE)
    {
      /* empty page - it must have at least one tuple record. */
      assert (list_id_p->last_offset == QFILE_PAGE_HEADER_SIZE);
      return false;
    }

  return (tuple_length + list_id_p->last_offset) > DB_PAGESIZE;
}

static void
qfile_set_dirty_page (THREAD_ENTRY * thread_p, PAGE_PTR page_p, int free_page, QMGR_TEMP_FILE * vfid_p)
{
  LOG_DATA_ADDR addr;

  addr.vfid = NULL;
  addr.pgptr = page_p;
  addr.offset = -1;

  qmgr_set_dirty_page (thread_p, page_p, free_page, &addr, vfid_p);
}

static PAGE_PTR
qfile_allocate_new_page (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, PAGE_PTR page_p, bool is_ovf_page)
{
  PAGE_PTR new_page_p;
  VPID new_vpid;

#if defined (SERVER_MODE)
  if (qmgr_is_query_interrupted (thread_p, list_id_p->query_id) == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
      return NULL;
    }
#endif /* SERVER_MODE */

  new_page_p = qmgr_get_new_page (thread_p, &new_vpid, list_id_p->tfile_vfid);
  if (new_page_p == NULL)
    {
      return NULL;
    }

  QFILE_PUT_TUPLE_COUNT (new_page_p, 0);
  QFILE_PUT_PREV_VPID (new_page_p, &list_id_p->last_vpid);

  /*
   * For streaming query support, set next_vpid differently
   */
  if (is_ovf_page)
    {
      QFILE_PUT_NEXT_VPID_NULL (new_page_p);
    }
  else
    {
      LS_PUT_NEXT_VPID (new_page_p);
    }

  QFILE_PUT_LAST_TUPLE_OFFSET (new_page_p, QFILE_PAGE_HEADER_SIZE);
  QFILE_PUT_OVERFLOW_VPID_NULL (new_page_p);

  if (page_p)
    {
      QFILE_PUT_NEXT_VPID (page_p, &new_vpid);
    }

  list_id_p->page_cnt++;

  if (page_p)
    {
      qfile_set_dirty_page (thread_p, page_p, FREE, list_id_p->tfile_vfid);
    }
  else
    {
      /* first list file tuple */
      QFILE_COPY_VPID (&list_id_p->first_vpid, &new_vpid);
    }
  QFILE_COPY_VPID (&list_id_p->last_vpid, &new_vpid);
  list_id_p->last_pgptr = new_page_p;
  list_id_p->last_offset = QFILE_PAGE_HEADER_SIZE;

  return new_page_p;
}

static PAGE_PTR
qfile_allocate_new_ovf_page (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, PAGE_PTR page_p, PAGE_PTR prev_page_p,
			     int tuple_length, int offset, int *tuple_page_size_p)
{
  PAGE_PTR new_page_p;
  VPID new_vpid;

  *tuple_page_size_p = MIN (tuple_length - offset, qfile_Max_tuple_page_size);

  new_page_p = qmgr_get_new_page (thread_p, &new_vpid, list_id_p->tfile_vfid);
  if (new_page_p == NULL)
    {
      return NULL;
    }

  list_id_p->page_cnt++;

  QFILE_PUT_NEXT_VPID_NULL (new_page_p);
  QFILE_PUT_TUPLE_COUNT (new_page_p, QFILE_OVERFLOW_TUPLE_COUNT_FLAG);
  QFILE_PUT_OVERFLOW_TUPLE_PAGE_SIZE (new_page_p, *tuple_page_size_p);
  QFILE_PUT_OVERFLOW_VPID_NULL (new_page_p);

  /*
   * connect the previous page to this page and free,
   * if it is not the first page
   */
  QFILE_PUT_OVERFLOW_VPID (prev_page_p, &new_vpid);
  if (prev_page_p != page_p)
    {
      qfile_set_dirty_page (thread_p, prev_page_p, FREE, list_id_p->tfile_vfid);
    }

  return new_page_p;
}

static int
qfile_allocate_new_page_if_need (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, PAGE_PTR * page_p,
				 int tuple_length, bool is_ovf_page)
{
  PAGE_PTR new_page_p;

  if (qfile_is_first_tuple (list_id_p) || qfile_is_last_page_full (list_id_p, tuple_length, is_ovf_page))
    {
      new_page_p = qfile_allocate_new_page (thread_p, list_id_p, *page_p, is_ovf_page);
      if (new_page_p == NULL)
	{
	  return ER_FAILED;
	}

      *page_p = new_page_p;
    }

  QFILE_PUT_TUPLE_COUNT (*page_p, QFILE_GET_TUPLE_COUNT (*page_p) + 1);
  QFILE_PUT_LAST_TUPLE_OFFSET (*page_p, list_id_p->last_offset);

  return NO_ERROR;
}

static void
qfile_add_tuple_to_list_id (QFILE_LIST_ID * list_id_p, PAGE_PTR page_p, int tuple_length, int written_tuple_length)
{
  QFILE_PUT_PREV_TUPLE_LENGTH (page_p, list_id_p->lasttpl_len);

  list_id_p->tuple_cnt++;
  list_id_p->lasttpl_len = tuple_length;
  list_id_p->last_offset += written_tuple_length;
  assert (list_id_p->last_offset <= DB_PAGESIZE);
}

/*
 * qfile_add_tuple_to_list () - The given tuple is added to the end of the list file
 *   return: int (NO_ERROR or ER_FAILED)
 *   list_id(in): List File Identifier
 * 	 tpl(in): Tuple to be added
 *
 */
int
qfile_add_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, QFILE_TUPLE tuple)
{
  PAGE_PTR cur_page_p, new_page_p, prev_page_p;
  int tuple_length;
  char *page_p, *tuple_p;
  int offset, tuple_page_size;

  if (list_id_p == NULL)
    {
      return ER_FAILED;
    }

  QFILE_CHECK_LIST_FILE_IS_CLOSED (list_id_p);

  cur_page_p = list_id_p->last_pgptr;
  tuple_length = QFILE_GET_TUPLE_LENGTH (tuple);

  if (qfile_allocate_new_page_if_need (thread_p, list_id_p, &cur_page_p, tuple_length, false) != NO_ERROR)
    {
      return ER_FAILED;
    }

  page_p = (char *) cur_page_p + list_id_p->last_offset;
  tuple_page_size = MIN (tuple_length, qfile_Max_tuple_page_size);
  assert ((list_id_p->last_offset + tuple_page_size) <= DB_PAGESIZE);
  memcpy (page_p, tuple, tuple_page_size);

  qfile_add_tuple_to_list_id (list_id_p, page_p, tuple_length, tuple_page_size);

  prev_page_p = cur_page_p;
  for (offset = tuple_page_size, tuple_p = (char *) tuple + offset; offset < tuple_length;
       offset += tuple_page_size, tuple_p += tuple_page_size)
    {
      new_page_p =
	qfile_allocate_new_ovf_page (thread_p, list_id_p, cur_page_p, prev_page_p, tuple_length, offset,
				     &tuple_page_size);
      if (new_page_p == NULL)
	{
	  if (prev_page_p != cur_page_p)
	    {
	      qfile_set_dirty_page (thread_p, prev_page_p, FREE, list_id_p->tfile_vfid);
	    }
	  return ER_FAILED;
	}

      memcpy ((char *) new_page_p + QFILE_PAGE_HEADER_SIZE, tuple_p, tuple_page_size);

      prev_page_p = new_page_p;
    }

  if (prev_page_p != cur_page_p)
    {
      QFILE_PUT_OVERFLOW_VPID_NULL (prev_page_p);
      qfile_set_dirty_page (thread_p, prev_page_p, FREE, list_id_p->tfile_vfid);
    }

  qfile_set_dirty_page (thread_p, cur_page_p, DONT_FREE, list_id_p->tfile_vfid);

  return NO_ERROR;
}

static int
qfile_save_single_bound_item_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, char *tuple_p, char *page_p,
				    int tuple_length)
{
  int align;

  align = DB_ALIGN (tuple_descr_p->item_size, MAX_ALIGNMENT);

  QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_BOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, align);

  tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
  memcpy (tuple_p, tuple_descr_p->item, tuple_descr_p->item_size);
#if !defined(NDEBUG)
  /* suppress valgrind UMW error */
  memset (tuple_p + tuple_descr_p->item_size, 0, align - tuple_descr_p->item_size);
#endif

  QFILE_PUT_TUPLE_LENGTH (page_p, tuple_length);

  return NO_ERROR;
}

static int
qfile_save_normal_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, char *tuple_p, char *page_p, int tuple_length)
{
  int i, tuple_value_size;

  for (i = 0; i < tuple_descr_p->f_cnt; i++)
    {
      if (qdata_copy_db_value_to_tuple_value (tuple_descr_p->f_valp[i],
					      !(tuple_descr_p->clear_f_val_at_clone_decache[i]),
					      tuple_p, &tuple_value_size) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      tuple_p += tuple_value_size;
    }

  QFILE_PUT_TUPLE_LENGTH (page_p, tuple_length);
  return NO_ERROR;
}

static int
qfile_save_sort_key_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, char *tuple_p, char *page_p, int tuple_length)
{
  SORTKEY_INFO *key_info_p;
  SORT_REC *sort_rec_p;
  int i, c, nkeys, len;
  char *src_p;

  key_info_p = (SORTKEY_INFO *) (tuple_descr_p->sortkey_info);
  nkeys = key_info_p->nkeys;
  sort_rec_p = (SORT_REC *) (tuple_descr_p->sort_rec);

  for (i = 0; i < nkeys; i++)
    {
      c = key_info_p->key[i].permuted_col;

      if (sort_rec_p->s.offset[c] == 0)
	{
	  QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_UNBOUND);
	  QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, 0);
	}
      else
	{
	  src_p = (char *) sort_rec_p + sort_rec_p->s.offset[c] - QFILE_TUPLE_VALUE_HEADER_SIZE;
	  len = QFILE_GET_TUPLE_VALUE_LENGTH (src_p);
	  memcpy (tuple_p, src_p, len + QFILE_TUPLE_VALUE_HEADER_SIZE);
	  tuple_p += len;
	}

      tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
    }

  QFILE_PUT_TUPLE_LENGTH (page_p, tuple_length);
  return NO_ERROR;
}

static int
qfile_save_merge_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, char *tuple_p, char *page_p, int *tuple_length_p)
{
  QFILE_TUPLE_RECORD *tuple_rec1_p, *tuple_rec2_p;
  QFILE_LIST_MERGE_INFO *merge_info_p;
  char *src_p;
  int i, tuple_value_size;
  INT32 ls_unbound[2] = { 0, 0 };

  QFILE_PUT_TUPLE_VALUE_FLAG ((char *) ls_unbound, V_UNBOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH ((char *) ls_unbound, 0);

  tuple_rec1_p = tuple_descr_p->tplrec1;
  tuple_rec2_p = tuple_descr_p->tplrec2;
  merge_info_p = tuple_descr_p->merge_info;

  *tuple_length_p = QFILE_TUPLE_LENGTH_SIZE;

  for (i = 0; i < merge_info_p->ls_pos_cnt; i++)
    {
      if (merge_info_p->ls_outer_inner_list[i] == QFILE_OUTER_LIST)
	{
	  if (tuple_rec1_p)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tuple_rec1_p->tpl, merge_info_p->ls_pos_list[i], src_p);
	    }
	  else
	    {
	      src_p = (char *) ls_unbound;
	    }
	}
      else
	{
	  if (tuple_rec2_p)
	    {
	      QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tuple_rec2_p->tpl, merge_info_p->ls_pos_list[i], src_p);
	    }
	  else
	    {
	      src_p = (char *) ls_unbound;
	    }
	}

      tuple_value_size = QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (src_p);
      memcpy (tuple_p, src_p, tuple_value_size);
      tuple_p += tuple_value_size;
      *tuple_length_p += tuple_value_size;
    }

  QFILE_PUT_TUPLE_LENGTH (page_p, *tuple_length_p);
  return NO_ERROR;
}

int
qfile_save_tuple (QFILE_TUPLE_DESCRIPTOR * tuple_descr_p, QFILE_TUPLE_TYPE tuple_type, char *page_p,
		  int *tuple_length_p)
{
  char *tuple_p;

  tuple_p = (char *) page_p + QFILE_TUPLE_LENGTH_SIZE;

  switch (tuple_type)
    {
    case T_SINGLE_BOUND_ITEM:
      (void) qfile_save_single_bound_item_tuple (tuple_descr_p, tuple_p, page_p, *tuple_length_p);
      break;

    case T_NORMAL:
      if (qfile_save_normal_tuple (tuple_descr_p, tuple_p, page_p, *tuple_length_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      break;

    case T_SORTKEY:
      (void) qfile_save_sort_key_tuple (tuple_descr_p, tuple_p, page_p, *tuple_length_p);
      break;

    case T_MERGE:
      (void) qfile_save_merge_tuple (tuple_descr_p, tuple_p, page_p, tuple_length_p);
      break;

    default:
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * qfile_generate_tuple_into_list () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   list_id(in/out): List File Identifier
 *   tpl_type(in): tuple descriptor type
 * 		   - single bound field tuple or multi field tuple
 *
 */
int
qfile_generate_tuple_into_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, QFILE_TUPLE_TYPE tuple_type)
{
  QFILE_TUPLE_DESCRIPTOR *tuple_descr_p;
  PAGE_PTR cur_page_p;
  int tuple_length;
  char *page_p;

  if (list_id_p == NULL)
    {
      return ER_FAILED;
    }

  QFILE_CHECK_LIST_FILE_IS_CLOSED (list_id_p);

  cur_page_p = list_id_p->last_pgptr;
  tuple_descr_p = &(list_id_p->tpl_descr);
  tuple_length = tuple_descr_p->tpl_size;

  assert (tuple_length <= qfile_Max_tuple_page_size);

  if (qfile_allocate_new_page_if_need (thread_p, list_id_p, &cur_page_p, tuple_length, false) != NO_ERROR)
    {
      return ER_FAILED;
    }

  page_p = (char *) cur_page_p + list_id_p->last_offset;
  if (qfile_save_tuple (tuple_descr_p, tuple_type, page_p, &tuple_length) != NO_ERROR)
    {
      return ER_FAILED;
    }

  assert ((page_p + tuple_length - cur_page_p) <= DB_PAGESIZE);

  qfile_add_tuple_to_list_id (list_id_p, page_p, tuple_length, tuple_length);

  qfile_set_dirty_page (thread_p, cur_page_p, DONT_FREE, list_id_p->tfile_vfid);
  return NO_ERROR;
}

/*
 * qfile_fast_intint_tuple_to_list () - generate a two integer value tuple into a listfile
 *   return: int (NO_ERROR or ER_FAILED)
 *   list_id(in/out): List File Identifier
 *   v1(in): first int value
 *   v2(in): second int value
 *
 * NOTE: This function is meant to skip usual validation od DB_VALUES and
 * disk size computation in order to generate the tuple as fast as possible.
 * Also, it must write tuples identical to tuples generated by
 * qfile_generate_tuple_into_list via the built tuple descriptor. Generated
 * tuples must be readable and scanable via usual qfile routines.
 */
int
qfile_fast_intint_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, int v1, int v2)
{
  PAGE_PTR cur_page_p;
  int tuple_length, tuple_value_length, tuple_value_size;
  char *page_p, *tuple_p;

  if (list_id_p == NULL)
    {
      return ER_FAILED;
    }

  QFILE_CHECK_LIST_FILE_IS_CLOSED (list_id_p);

  /* compute sizes */
  tuple_value_size = DB_ALIGN (tp_Integer.disksize, MAX_ALIGNMENT);
  tuple_value_length = QFILE_TUPLE_VALUE_HEADER_SIZE + tuple_value_size;
  tuple_length = QFILE_TUPLE_LENGTH_SIZE + tuple_value_length * 2;

  /* fetch page or alloc if necessary */
  cur_page_p = list_id_p->last_pgptr;
  if (qfile_allocate_new_page_if_need (thread_p, list_id_p, &cur_page_p, tuple_length, false) != NO_ERROR)
    {
      return ER_FAILED;
    }
  page_p = (char *) cur_page_p + list_id_p->last_offset;
  tuple_p = page_p + QFILE_TUPLE_LENGTH_SIZE;

  /* write the two not-null integers */
  QFILE_PUT_TUPLE_LENGTH (page_p, tuple_length);

  QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_BOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, tuple_value_size);
  OR_PUT_INT (tuple_p + QFILE_TUPLE_VALUE_HEADER_SIZE, v1);
  tuple_p += tuple_value_length;

  QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_BOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, tuple_value_size);
  OR_PUT_INT (tuple_p + QFILE_TUPLE_VALUE_HEADER_SIZE, v2);

  /* list_id maintainance stuff */
  qfile_add_tuple_to_list_id (list_id_p, page_p, tuple_length, tuple_length);

  qfile_set_dirty_page (thread_p, cur_page_p, DONT_FREE, list_id_p->tfile_vfid);
  return NO_ERROR;
}

/*
 * qfile_fast_intval_tuple_to_list () - generate a two value tuple into a file
 *   return: int (NO_ERROR, error code or positive overflow tuple size)
 *   list_id(in/out): List File Identifier
 *   v1(in): integer value
 *   v2(in): generic value
 *
 * NOTE: This function is meant to partially skip usual validation of DB_VALUES
 * and disk size computation in order to generate the tuple as fast as
 * possible. Also, it must write tuples identical to tuples generated by
 * qfile_generate_tuple_into_list via the built tuple descriptor. Generated
 * tuples must be readable and scanable via usual qfile routines.
 */
int
qfile_fast_intval_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, int v1, DB_VALUE * v2)
{
  PAGE_PTR cur_page_p;
  int tuple_length, tuple_int_value_size, tuple_int_value_length;
  int tuple_value_size, tuple_value_length;
  char *page_p, *tuple_p;

  if (list_id_p == NULL)
    {
      return ER_FAILED;
    }

  QFILE_CHECK_LIST_FILE_IS_CLOSED (list_id_p);

  /* compute sizes */
  tuple_int_value_size = DB_ALIGN (tp_Integer.disksize, MAX_ALIGNMENT);
  tuple_int_value_length = QFILE_TUPLE_VALUE_HEADER_SIZE + tuple_int_value_size;
  tuple_value_size = DB_ALIGN (pr_data_writeval_disk_size (v2), MAX_ALIGNMENT);
  tuple_value_length = QFILE_TUPLE_VALUE_HEADER_SIZE + tuple_value_size;
  tuple_length = QFILE_TUPLE_LENGTH_SIZE + tuple_int_value_length + tuple_value_length;

  /* register tuple size and see if we can write it or not */
  list_id_p->tpl_descr.tpl_size = tuple_length;
  if (tuple_length > QFILE_MAX_TUPLE_SIZE_IN_PAGE)
    {
      /* can't write it here */
      return tuple_length;
    }

  /* fetch page or alloc if necessary */
  cur_page_p = list_id_p->last_pgptr;
  if (qfile_allocate_new_page_if_need (thread_p, list_id_p, &cur_page_p, tuple_length, false) != NO_ERROR)
    {
      return ER_FAILED;
    }
  page_p = (char *) cur_page_p + list_id_p->last_offset;
  tuple_p = page_p + QFILE_TUPLE_LENGTH_SIZE;

  /* write the two not-null integers */
  QFILE_PUT_TUPLE_LENGTH (page_p, tuple_length);

  QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_BOUND);
  QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, tuple_int_value_size);
  OR_PUT_INT (tuple_p + QFILE_TUPLE_VALUE_HEADER_SIZE, v1);
  tuple_p += tuple_int_value_length;

  if (DB_IS_NULL (v2))
    {
      QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_UNBOUND);
      QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, 0);
    }
  else
    {
      DB_TYPE dbval_type = DB_VALUE_DOMAIN_TYPE (v2);
      PR_TYPE *pr_type = pr_type_from_id (dbval_type);
      OR_BUF buf;

      QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_BOUND);
      QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, tuple_value_size);

      OR_BUF_INIT (buf, tuple_p + QFILE_TUPLE_VALUE_HEADER_SIZE, tuple_value_size);
      if (pr_type == NULL || pr_type->data_writeval (&buf, v2) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  /* list_id maintainance stuff */
  qfile_add_tuple_to_list_id (list_id_p, page_p, tuple_length, tuple_length);

  qfile_set_dirty_page (thread_p, cur_page_p, DONT_FREE, list_id_p->tfile_vfid);
  return NO_ERROR;
}

/*
 * qfile_fast_val_tuple_to_list () - generate a one value tuple into a file
 *   return: int (NO_ERROR, error code or positive overflow tuple size)
 *   list_id(in/out): List File Identifier
 *   val(in): integer value
 *
 * NOTE: This function is meant to partially skip usual validation of DB_VALUES
 * and disk size computation in order to generate the tuple as fast as
 * possible. Also, it must write tuples identical to tuples generated by
 * qfile_generate_tuple_into_list via the built tuple descriptor. Generated
 * tuples must be readable and scanable via usual qfile routines.
 */
int
qfile_fast_val_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, DB_VALUE * val)
{
  PAGE_PTR cur_page_p;
  int tuple_length;
  int tuple_value_size, tuple_value_length;
  char *page_p, *tuple_p;

  if (list_id_p == NULL)
    {
      return ER_FAILED;
    }

  QFILE_CHECK_LIST_FILE_IS_CLOSED (list_id_p);

  tuple_value_size = DB_ALIGN (pr_data_writeval_disk_size (val), MAX_ALIGNMENT);
  tuple_value_length = QFILE_TUPLE_VALUE_HEADER_SIZE + tuple_value_size;
  tuple_length = QFILE_TUPLE_LENGTH_SIZE + tuple_value_length;

  /* register tuple size and see if we can write it or not */
  list_id_p->tpl_descr.tpl_size = tuple_length;
  if (tuple_length > QFILE_MAX_TUPLE_SIZE_IN_PAGE)
    {
      /* can't write it here */
      return tuple_length;
    }

  /* fetch page or alloc if necessary */
  cur_page_p = list_id_p->last_pgptr;
  if (qfile_allocate_new_page_if_need (thread_p, list_id_p, &cur_page_p, tuple_length, false) != NO_ERROR)
    {
      return ER_FAILED;
    }
  page_p = (char *) cur_page_p + list_id_p->last_offset;
  tuple_p = page_p + QFILE_TUPLE_LENGTH_SIZE;

  /* write the two not-null integers */
  QFILE_PUT_TUPLE_LENGTH (page_p, tuple_length);

  if (DB_IS_NULL (val))
    {
      QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_UNBOUND);
      QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, 0);
    }
  else
    {
      DB_TYPE dbval_type = DB_VALUE_DOMAIN_TYPE (val);
      PR_TYPE *pr_type = pr_type_from_id (dbval_type);
      OR_BUF buf;

      QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_BOUND);
      QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, tuple_value_size);

      OR_BUF_INIT (buf, tuple_p + QFILE_TUPLE_VALUE_HEADER_SIZE, tuple_value_size);
      if (pr_type == NULL || pr_type->data_writeval (&buf, val) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  /* list_id maintainance stuff */
  qfile_add_tuple_to_list_id (list_id_p, page_p, tuple_length, tuple_length);

  qfile_set_dirty_page (thread_p, cur_page_p, DONT_FREE, list_id_p->tfile_vfid);
  return NO_ERROR;
}


/*
 * qfile_add_overflow_tuple_to_list () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   list_id(in/out): List File Identifier
 *   ovfl_tpl_pg(in): First page of the overflow tuple to be added
 *   input_list_id(in):
 *
 * Note: The indicated overflow tuple is added to the end of the list
 *              file. The given page contains the initial portion of the
 *              tuple and the rest of the tuple is formed from following
 *              overflow pages.
 *
 * Note: This routine is a specific routine of qfile_add_tuple_to_list used by list file
 *       sorting mechanism.
 */
int
qfile_add_overflow_tuple_to_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, PAGE_PTR ovf_tuple_page_p,
				  QFILE_LIST_ID * input_list_id_p)
{
  PAGE_PTR cur_page_p, new_page_p = NULL, prev_page_p, ovf_page_p;
  int tuple_length;
  char *page_p;
  int offset, tuple_page_size;
  QFILE_TUPLE tuple;
  VPID ovf_vpid;

  QFILE_CHECK_LIST_FILE_IS_CLOSED (list_id_p);

  tuple = (char *) ovf_tuple_page_p + QFILE_PAGE_HEADER_SIZE;
  cur_page_p = list_id_p->last_pgptr;
  tuple_length = QFILE_GET_TUPLE_LENGTH (tuple);

  if (qfile_allocate_new_page_if_need (thread_p, list_id_p, &cur_page_p, tuple_length, true) != NO_ERROR)
    {
      return ER_FAILED;
    }

  page_p = (char *) cur_page_p + list_id_p->last_offset;
  tuple_page_size = MIN (tuple_length, qfile_Max_tuple_page_size);
  memcpy (page_p, tuple, tuple_page_size);

  qfile_add_tuple_to_list_id (list_id_p, page_p, tuple_length, tuple_page_size);

  prev_page_p = cur_page_p;
  QFILE_GET_OVERFLOW_VPID (&ovf_vpid, ovf_tuple_page_p);

  for (offset = tuple_page_size; offset < tuple_length; offset += tuple_page_size)
    {
      ovf_page_p = qmgr_get_old_page (thread_p, &ovf_vpid, input_list_id_p->tfile_vfid);
      if (ovf_page_p == NULL)
	{
	  if (new_page_p)
	    {
	      qfile_set_dirty_page (thread_p, new_page_p, FREE, list_id_p->tfile_vfid);
	    }
	  return ER_FAILED;
	}

      QFILE_GET_OVERFLOW_VPID (&ovf_vpid, ovf_page_p);

      new_page_p =
	qfile_allocate_new_ovf_page (thread_p, list_id_p, cur_page_p, prev_page_p, tuple_length, offset,
				     &tuple_page_size);
      if (new_page_p == NULL)
	{
	  if (prev_page_p != cur_page_p)
	    {
	      qfile_set_dirty_page (thread_p, prev_page_p, FREE, list_id_p->tfile_vfid);
	    }
	  qmgr_free_old_page_and_init (thread_p, ovf_page_p, input_list_id_p->tfile_vfid);
	  return ER_FAILED;
	}

      memcpy ((char *) new_page_p + QFILE_PAGE_HEADER_SIZE, (char *) ovf_page_p + QFILE_PAGE_HEADER_SIZE,
	      tuple_page_size);

      qmgr_free_old_page_and_init (thread_p, ovf_page_p, input_list_id_p->tfile_vfid);
      prev_page_p = new_page_p;
    }

  if (prev_page_p != cur_page_p)
    {
      QFILE_PUT_OVERFLOW_VPID_NULL (prev_page_p);
      qfile_set_dirty_page (thread_p, prev_page_p, FREE, list_id_p->tfile_vfid);
    }

  qfile_set_dirty_page (thread_p, cur_page_p, DONT_FREE, list_id_p->tfile_vfid);
  return NO_ERROR;
}

/*
 * qfile_get_first_page () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   list_id(in): List File Identifier
 */
int
qfile_get_first_page (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p)
{
  PAGE_PTR new_page_p;
  VPID new_vpid;

  if (list_id_p->tfile_vfid == NULL)
    {
      list_id_p->tfile_vfid = qmgr_create_new_temp_file (thread_p, list_id_p->query_id, TEMP_FILE_MEMBUF_NORMAL);
      if (list_id_p->tfile_vfid == NULL)
	{
	  return ER_FAILED;
	}
    }

  new_page_p = qmgr_get_new_page (thread_p, &new_vpid, list_id_p->tfile_vfid);
  if (new_page_p == NULL)
    {
      return ER_FAILED;
    }

  list_id_p->page_cnt++;

  QFILE_PUT_TUPLE_COUNT (new_page_p, 0);
  QFILE_PUT_PREV_VPID (new_page_p, &list_id_p->last_vpid);
  LS_PUT_NEXT_VPID (new_page_p);

  QFILE_COPY_VPID (&list_id_p->first_vpid, &new_vpid);
  QFILE_COPY_VPID (&list_id_p->last_vpid, &new_vpid);

  list_id_p->last_pgptr = new_page_p;
  list_id_p->last_offset = QFILE_PAGE_HEADER_SIZE;
  QFILE_PUT_OVERFLOW_VPID_NULL (new_page_p);

  list_id_p->lasttpl_len = 0;
  list_id_p->last_offset += ((0 + list_id_p->last_offset > DB_PAGESIZE) ? DB_PAGESIZE : 0);

  qfile_set_dirty_page (thread_p, new_page_p, DONT_FREE, list_id_p->tfile_vfid);
  return NO_ERROR;
}

/*
 * qfile_destroy_list () -
 *   return: int
 *   list_id(in): List File Identifier
 *
 * Note: All the pages of the list file are deallocated from the query
 *              file, the memory areas for the list file identifier are freed
 *              and the number of pages deallocated is returned. This routine
 *              is basically called for temporarily created list files.
 */
void
qfile_destroy_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p)
{
  if (list_id_p)
    {
      if (list_id_p->tfile_vfid)
	{
	  qmgr_free_list_temp_file (thread_p, list_id_p->query_id, list_id_p->tfile_vfid);
	}
      else
	{
	  /* because qmgr_free_list_temp_file() destroy only FILE_TEMP file */
	  if (!VFID_ISNULL (&list_id_p->temp_vfid))
	    {
	      file_temp_retire (thread_p, &list_id_p->temp_vfid);
	    }
	}

      qfile_clear_list_id (list_id_p);
    }
}

/*
 * xqfile_get_list_file_page () -
 *   return: NO_ERROR or ER_ code
 *   query_id(in):
 *   volid(in): List file page volume identifier
 *   pageid(in): List file page identifier
 *   page_bufp(out): Buffer to contain list file page content
 *   page_sizep(out):
 *
 * Note: This routine is basically called by the C/S communication
 *              routines to fetch and copy the indicated list file page to
 *              the buffer area. The area pointed by the buffer must have
 *              been allocated by the caller and should be big enough to
 *              store a list file page.
 */
int
xqfile_get_list_file_page (THREAD_ENTRY * thread_p, QUERY_ID query_id, VOLID vol_id, PAGEID page_id, char *page_buf_p,
			   int *page_size_p)
{
  QMGR_QUERY_ENTRY *query_entry_p = NULL;
  QFILE_LIST_ID *list_id_p;
  QMGR_TEMP_FILE *tfile_vfid_p;
  VPID vpid, next_vpid;
  PAGE_PTR page_p;
  int one_page_size = DB_PAGESIZE;
  int tran_index;

  assert (NULL_PAGEID < page_id);
  VPID_SET (&vpid, vol_id, page_id);

  *page_size_p = 0;

  if (query_id == NULL_QUERY_ID)
    {
      assert (false);
      return ER_QPROC_UNKNOWN_QUERYID;
    }
  else if (query_id >= SHRT_MAX)
    {
      tfile_vfid_p = (QMGR_TEMP_FILE *) query_id;
      goto get_page;
    }
  else
    {
      tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

      query_entry_p = qmgr_get_query_entry (thread_p, query_id, tran_index);
      if (query_entry_p == NULL)
	{
	  return ER_QPROC_UNKNOWN_QUERYID;
	}

      assert (query_entry_p->errid == NO_ERROR);
      assert (query_entry_p->query_status == QUERY_COMPLETED);

      if (query_entry_p->list_id == NULL)
	{
	  assert (query_entry_p->list_id != NULL);
	  *page_size_p = 0;
	  return NO_ERROR;
	}

      assert (NULL_PAGEID < query_entry_p->list_id->first_vpid.pageid);

      /* unexpected no result */
      if (vol_id == NULL_VOLID && page_id == NULL_PAGEID)
	{
	  assert (vol_id != NULL_VOLID || page_id != NULL_PAGEID);
	  *page_size_p = 0;
	  return NO_ERROR;
	}

      list_id_p = query_entry_p->list_id;
      tfile_vfid_p = list_id_p->tfile_vfid;
    }

get_page:
  /* append pages until a network page is full */
  while ((*page_size_p + DB_PAGESIZE) <= IO_MAX_PAGE_SIZE)
    {
      page_p = qmgr_get_old_page (thread_p, &vpid, tfile_vfid_p);
      if (page_p == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      /* find next page to append */
      QFILE_GET_OVERFLOW_VPID (&next_vpid, page_p);
      if (next_vpid.pageid == NULL_PAGEID)
	{
	  QFILE_GET_NEXT_VPID (&next_vpid, page_p);
	}

      assert (next_vpid.pageid != NULL_PAGEID_IN_PROGRESS);

      if (QFILE_GET_TUPLE_COUNT (page_p) == QFILE_OVERFLOW_TUPLE_COUNT_FLAG
	  || QFILE_GET_OVERFLOW_PAGE_ID (page_p) != NULL_PAGEID)
	{
	  one_page_size = DB_PAGESIZE;
	}
      else
	{
	  one_page_size = (QFILE_GET_LAST_TUPLE_OFFSET (page_p)
			   + QFILE_GET_TUPLE_LENGTH (page_p + QFILE_GET_LAST_TUPLE_OFFSET (page_p)));
	  if (one_page_size < QFILE_PAGE_HEADER_SIZE)
	    {
	      one_page_size = QFILE_PAGE_HEADER_SIZE;
	    }

	  if (one_page_size > DB_PAGESIZE)
	    {
	      one_page_size = DB_PAGESIZE;
	    }
	}

      memcpy ((page_buf_p + *page_size_p), page_p, one_page_size);
      qmgr_free_old_page_and_init (thread_p, page_p, tfile_vfid_p);

      *page_size_p += DB_PAGESIZE;

      /* next page to append does not exists, stop appending */
      if (next_vpid.pageid == NULL_PAGEID)
	{
	  break;
	}

      vpid = next_vpid;
    }

  *page_size_p += one_page_size - DB_PAGESIZE;

  return NO_ERROR;
}

/*
 * qfile_add_item_to_list () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   item(in): Value in disk representation form
 *   item_size(in): Size of the value
 *   list_id(in): List File Identifier
 *
 * Note: The given item is added to the end of the given list file.
 *       The list file must be of a single column.
 */
int
qfile_add_item_to_list (THREAD_ENTRY * thread_p, char *item_p, int item_size, QFILE_LIST_ID * list_id_p)
{
  QFILE_TUPLE tuple;
  int tuple_length, align;
  char *tuple_p;

  tuple_length = QFILE_TUPLE_LENGTH_SIZE + QFILE_TUPLE_VALUE_HEADER_SIZE + item_size;

  align = DB_ALIGN (item_size, MAX_ALIGNMENT) - item_size;
  tuple_length += align;

  if (tuple_length < QFILE_MAX_TUPLE_SIZE_IN_PAGE)
    {
      /* SMALL_TUPLE */

      list_id_p->tpl_descr.item = item_p;
      list_id_p->tpl_descr.item_size = item_size;
      list_id_p->tpl_descr.tpl_size = tuple_length;

      if (qfile_generate_tuple_into_list (thread_p, list_id_p, T_SINGLE_BOUND_ITEM) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      /* BIG_TUPLE */

      tuple = (QFILE_TUPLE) malloc (tuple_length);
      if (tuple == NULL)
	{
	  return ER_FAILED;
	}

      QFILE_PUT_TUPLE_LENGTH (tuple, tuple_length);
      tuple_p = (char *) tuple + QFILE_TUPLE_LENGTH_SIZE;
      QFILE_PUT_TUPLE_VALUE_FLAG (tuple_p, V_BOUND);
      QFILE_PUT_TUPLE_VALUE_LENGTH (tuple_p, item_size + align);
      tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
      memcpy (tuple_p, item_p, item_size);
#if !defined(NDEBUG)
      /* suppress valgrind UMW error */
      memset (tuple_p + item_size, 0, align);
#endif

      if (qfile_add_tuple_to_list (thread_p, list_id_p, tuple) != NO_ERROR)
	{
	  free_and_init (tuple);
	  return ER_FAILED;
	}

      free_and_init (tuple);
    }

  return NO_ERROR;
}

/*
 * qfile_compare_tuple_helper () -
 *   return:
 *   lhs(in):
 *   rhs(in):
 *   types(in):
 */
static int
qfile_compare_tuple_helper (QFILE_TUPLE lhs, QFILE_TUPLE rhs, QFILE_TUPLE_VALUE_TYPE_LIST * types, int *cmp)
{
  char *lhs_tuple_p, *rhs_tuple_p;
  int i, result;

  lhs_tuple_p = (char *) lhs + QFILE_TUPLE_LENGTH_SIZE;
  rhs_tuple_p = (char *) rhs + QFILE_TUPLE_LENGTH_SIZE;

  for (i = 0; i < types->type_cnt; i++)
    {
      result = qfile_compare_tuple_values (lhs_tuple_p, rhs_tuple_p, types->domp[i], cmp);
      if (result != NO_ERROR)
	{
	  return result;
	}

      if (*cmp != 0)
	{
	  return NO_ERROR;
	}

      lhs_tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (lhs_tuple_p);
      rhs_tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (rhs_tuple_p);
    }

  *cmp = 0;
  return NO_ERROR;
}

/*
 * qfile_advance_single () -
 *   return:
 *   next_scan(in):
 *   next_tpl(in):
 *   last_scan(in):
 *   last_tpl(in):
 *   types(in):
 */
static SCAN_CODE
qfile_advance_single (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * next_scan_p, QFILE_TUPLE_RECORD * next_tuple_p,
		      QFILE_LIST_SCAN_ID * last_scan_p, QFILE_TUPLE_RECORD * last_tuple_p,
		      QFILE_TUPLE_VALUE_TYPE_LIST * types)
{
  if (next_scan_p == NULL)
    {
      return S_END;
    }

  return qfile_scan_list_next (thread_p, next_scan_p, next_tuple_p, PEEK);
}

/*
 * qfile_advance_group () -
 *   return:
 *   next_scan(in/out):
 *   next_tpl(out):
 *   last_scan(in/out):
 *   last_tpl(out):
 *   types(in):
 */
static SCAN_CODE
qfile_advance_group (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * next_scan_p, QFILE_TUPLE_RECORD * next_tuple_p,
		     QFILE_LIST_SCAN_ID * last_scan_p, QFILE_TUPLE_RECORD * last_tuple_p,
		     QFILE_TUPLE_VALUE_TYPE_LIST * types)
{
  SCAN_CODE status;
  int error_code, cmp;

  if (next_scan_p == NULL)
    {
      return S_END;
    }

  status = S_SUCCESS;

  switch (last_scan_p->position)
    {
    case S_BEFORE:
      status = qfile_scan_list_next (thread_p, next_scan_p, next_tuple_p, PEEK);
      break;

    case S_ON:
      do
	{
	  status = qfile_scan_list_next (thread_p, next_scan_p, next_tuple_p, PEEK);
	  if (status != S_SUCCESS)
	    {
	      break;
	    }

	  error_code = qfile_compare_tuple_helper (last_tuple_p->tpl, next_tuple_p->tpl, types, &cmp);
	}
      while (error_code == NO_ERROR && cmp == 0);
      break;

    case S_AFTER:
    default:
      status = S_END;
      break;
    }

  if (status == S_SUCCESS)
    {
      QFILE_TUPLE_POSITION next_pos;

      qfile_save_current_scan_tuple_position (next_scan_p, &next_pos);
      status = qfile_jump_scan_tuple_position (thread_p, last_scan_p, &next_pos, last_tuple_p, PEEK);
    }

  return status;
}

/*
 * qfile_add_one_tuple () -
 *   return:
 *   dst(in/out):
 *   lhs(in):
 *   rhs(in):
 */
static int
qfile_add_one_tuple (THREAD_ENTRY * thread_p, QFILE_LIST_ID * dest_list_p, QFILE_TUPLE lhs, QFILE_TUPLE rhs)
{
  return qfile_add_tuple_to_list (thread_p, dest_list_p, lhs);
}

/*
 * qfile_add_two_tuple () -
 *   return:
 *   dst(in):
 *   lhs(in):
 *   rhs(in):
 */
static int
qfile_add_two_tuple (THREAD_ENTRY * thread_p, QFILE_LIST_ID * dest_list_p, QFILE_TUPLE lhs, QFILE_TUPLE rhs)
{
  int error;

  error = qfile_add_tuple_to_list (thread_p, dest_list_p, lhs);
  if (error == NO_ERROR)
    {
      error = qfile_add_tuple_to_list (thread_p, dest_list_p, rhs);
    }

  return error;
}

static int
qfile_advance (THREAD_ENTRY * thread_p, ADVANCE_FUCTION advance_func, QFILE_TUPLE_RECORD * side_p,
	       QFILE_TUPLE_RECORD * last_side_p, QFILE_LIST_SCAN_ID * scan_p, QFILE_LIST_SCAN_ID * last_scan_p,
	       QFILE_LIST_ID * side_file_p, int *have_side_p)
{
  SCAN_CODE scan_result;

  scan_result = (*advance_func) (thread_p, scan_p, side_p, last_scan_p, last_side_p, &side_file_p->type_list);
  switch (scan_result)
    {
    case S_SUCCESS:
      *have_side_p = 1;
      break;
    case S_END:
      *have_side_p = 0;
      break;
    default:
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * qfile_combine_two_list () -
 *   return: QFILE_LIST_ID *, or NULL
 *   lhs_file(in): pointer to a QFILE_LIST_ID for one of the input files
 *   rhs_file(in): pointer to a QFILE_LIST_ID for the other input file, or NULL
 *   flag(in): {QFILE_FLAG_UNION, QFILE_FLAG_DIFFERENCE, QFILE_FLAG_INTERSECT,
 *             QFILE_FLAG_ALL, QFILE_FLAG_DISTINCT}
 *             the kind of combination desired (union, diff, or intersect) and
 *             whether to do 'all' or 'distinct'
 *
 */
QFILE_LIST_ID *
qfile_combine_two_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * lhs_file_p, QFILE_LIST_ID * rhs_file_p, int flag)
{
  QFILE_LIST_ID *dest_list_id_p = NULL;
  QFILE_LIST_SCAN_ID lhs_scan_id, rhs_scan_id, last_lhs_scan_id, last_rhs_scan_id;
  QFILE_LIST_SCAN_ID *lhs_scan_p = NULL, *rhs_scan_p = NULL;
  QFILE_LIST_SCAN_ID *last_lhs_scan_p = NULL, *last_rhs_scan_p = NULL;
  int have_lhs = 0, have_rhs = 0, cmp;
  QFILE_TUPLE_RECORD lhs = { NULL, 0 };
  QFILE_TUPLE_RECORD rhs = { NULL, 0 };
  QFILE_TUPLE_RECORD last_lhs = { NULL, 0 };
  QFILE_TUPLE_RECORD last_rhs = { NULL, 0 };
  QUERY_OPTIONS distinct_or_all;

  ADVANCE_FUCTION advance_func;
  int (*act_left_func) (THREAD_ENTRY * thread_p, QFILE_LIST_ID *, QFILE_TUPLE);
  int (*act_right_func) (THREAD_ENTRY * thread_p, QFILE_LIST_ID *, QFILE_TUPLE);
  int (*act_both_func) (THREAD_ENTRY * thread_p, QFILE_LIST_ID *, QFILE_TUPLE, QFILE_TUPLE);

  advance_func = NULL;
  act_left_func = NULL;
  act_right_func = NULL;
  act_both_func = NULL;

  if (QFILE_IS_FLAG_SET_BOTH (flag, QFILE_FLAG_UNION, QFILE_FLAG_ALL))
    {
      return qfile_union_list (thread_p, lhs_file_p, rhs_file_p, flag);
    }

  if (QFILE_IS_FLAG_SET (flag, QFILE_FLAG_DISTINCT))
    {
      distinct_or_all = Q_DISTINCT;
    }
  else
    {
      distinct_or_all = Q_ALL;
    }

  if (lhs_file_p->tuple_cnt > 1)
    {
      lhs_file_p = qfile_sort_list (thread_p, lhs_file_p, NULL, distinct_or_all, true);
      if (lhs_file_p == NULL)
	{
	  goto error;
	}
    }

  if (qfile_open_list_scan (lhs_file_p, &lhs_scan_id) != NO_ERROR)
    {
      goto error;
    }
  lhs_scan_p = &lhs_scan_id;

  if (rhs_file_p)
    {
      if (rhs_file_p->tuple_cnt > 1)
	{
	  rhs_file_p = qfile_sort_list (thread_p, rhs_file_p, NULL, distinct_or_all, true);
	  if (rhs_file_p == NULL)
	    {
	      goto error;
	    }
	}

      if (qfile_open_list_scan (rhs_file_p, &rhs_scan_id) != NO_ERROR)
	{
	  goto error;
	}

      rhs_scan_p = &rhs_scan_id;
    }

  dest_list_id_p = qfile_open_list (thread_p, &lhs_file_p->type_list, NULL, lhs_file_p->query_id, flag);
  if (dest_list_id_p == NULL)
    {
      goto error;
    }

  if (rhs_file_p && qfile_unify_types (dest_list_id_p, rhs_file_p) != NO_ERROR)
    {
      goto error;
    }

  if (QFILE_IS_FLAG_SET (flag, QFILE_FLAG_INTERSECT))
    {
      act_left_func = NULL;
      act_right_func = NULL;
      act_both_func = qfile_add_one_tuple;
    }
  else if (QFILE_IS_FLAG_SET (flag, QFILE_FLAG_DIFFERENCE))
    {
      act_left_func = qfile_add_tuple_to_list;
      act_right_func = NULL;
      act_both_func = NULL;
    }
  else
    {
      /* QFILE_FLAG_UNION */
      act_left_func = qfile_add_tuple_to_list;
      act_right_func = qfile_add_tuple_to_list;
      act_both_func = qfile_add_one_tuple;
    }

  if (QFILE_IS_FLAG_SET (flag, QFILE_FLAG_DISTINCT))
    {
      advance_func = qfile_advance_group;
      if (qfile_open_list_scan (lhs_file_p, &last_lhs_scan_id) != NO_ERROR)
	{
	  goto error;
	}

      last_lhs_scan_p = &last_lhs_scan_id;
      if (rhs_file_p)
	{
	  if (qfile_open_list_scan (rhs_file_p, &last_rhs_scan_id) != NO_ERROR)
	    {
	      goto error;
	    }
	  last_rhs_scan_p = &last_rhs_scan_id;
	}
    }
  else
    {
      /* QFILE_FLAG_ALL */
      advance_func = qfile_advance_single;
      if (QFILE_IS_FLAG_SET (flag, QFILE_FLAG_UNION))
	{
	  act_both_func = qfile_add_two_tuple;
	}
    }

  while (1)
    {
      if (!have_lhs)
	{
	  if (qfile_advance (thread_p, advance_func, &lhs, &last_lhs, lhs_scan_p, last_lhs_scan_p, lhs_file_p,
			     &have_lhs) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if (!have_rhs)
	{
	  if (qfile_advance (thread_p, advance_func, &rhs, &last_rhs, rhs_scan_p, last_rhs_scan_p, rhs_file_p,
			     &have_rhs) != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if (!have_lhs || !have_rhs)
	{
	  break;
	}

      if (qfile_compare_tuple_helper (lhs.tpl, rhs.tpl, &lhs_file_p->type_list, &cmp) != NO_ERROR)
	{
	  goto error;
	}

      if (cmp < 0)
	{
	  if (act_left_func && act_left_func (thread_p, dest_list_id_p, lhs.tpl) != NO_ERROR)
	    {
	      goto error;
	    }

	  have_lhs = 0;
	}
      else if (cmp == 0)
	{
	  if (act_both_func && act_both_func (thread_p, dest_list_id_p, lhs.tpl, rhs.tpl) != NO_ERROR)
	    {
	      goto error;
	    }

	  have_lhs = 0;
	  have_rhs = 0;
	}
      else
	{
	  if (act_right_func && act_right_func (thread_p, dest_list_id_p, rhs.tpl) != NO_ERROR)
	    {
	      goto error;
	    }
	  have_rhs = 0;
	}
    }

  while (have_lhs)
    {
      if (act_left_func && act_left_func (thread_p, dest_list_id_p, lhs.tpl) != NO_ERROR)
	{
	  goto error;
	}

      if (qfile_advance (thread_p, advance_func, &lhs, &last_lhs, lhs_scan_p, last_lhs_scan_p, lhs_file_p, &have_lhs) !=
	  NO_ERROR)
	{
	  goto error;
	}
    }

  while (have_rhs)
    {
      if (act_right_func && act_right_func (thread_p, dest_list_id_p, rhs.tpl) != NO_ERROR)
	{
	  goto error;
	}

      if (qfile_advance (thread_p, advance_func, &rhs, &last_rhs, rhs_scan_p, last_rhs_scan_p, rhs_file_p, &have_rhs) !=
	  NO_ERROR)
	{
	  goto error;
	}
    }

success:
  if (lhs_scan_p)
    {
      qfile_close_scan (thread_p, lhs_scan_p);
    }
  if (rhs_scan_p)
    {
      qfile_close_scan (thread_p, rhs_scan_p);
    }
  if (last_lhs_scan_p)
    {
      qfile_close_scan (thread_p, last_lhs_scan_p);
    }
  if (last_rhs_scan_p)
    {
      qfile_close_scan (thread_p, last_rhs_scan_p);
    }
  if (lhs_file_p)
    {
      qfile_close_list (thread_p, lhs_file_p);
    }
  if (rhs_file_p)
    {
      qfile_close_list (thread_p, rhs_file_p);
    }
  if (dest_list_id_p)
    {
      qfile_close_list (thread_p, dest_list_id_p);
    }

  return dest_list_id_p;

error:
  if (dest_list_id_p)
    {
      qfile_close_list (thread_p, dest_list_id_p);
      QFILE_FREE_AND_INIT_LIST_ID (dest_list_id_p);
    }
  goto success;
}

/*
 * qfile_copy_tuple_descr_to_tuple () - generate a tuple into a tuple record
 *                                      structure from a tuple descriptor
 *   return: NO_ERROR or error code
 *   thread_p(in): thread
 *   tpl_descr(in): tuple descriptor
 *   tpl_rec(in): tuple record
 *
 * NOTE: tpl_rec's fields will be private_alloc'd by this function and it's the
 * caller's resposability to properly dispose the memory
 */
int
qfile_copy_tuple_descr_to_tuple (THREAD_ENTRY * thread_p, QFILE_TUPLE_DESCRIPTOR * tpl_descr,
				 QFILE_TUPLE_RECORD * tplrec)
{
  char *tuple_p;
  int i, size;

  assert (tpl_descr != NULL && tplrec != NULL);

  /* alloc tuple record with tuple descriptor footprint */
  tplrec->size = tpl_descr->tpl_size;
  tplrec->tpl = (char *) db_private_alloc (thread_p, tplrec->size);
  if (tplrec->tpl == NULL)
    {
      return ER_FAILED;
    }

  /* put length */
  QFILE_PUT_TUPLE_LENGTH (tplrec->tpl, tplrec->size);
  tuple_p = tplrec->tpl + QFILE_TUPLE_LENGTH_SIZE;

  /* build tuple */
  for (i = 0; i < tpl_descr->f_cnt; i++)
    {
      if (qdata_copy_db_value_to_tuple_value (tpl_descr->f_valp[i], !(tpl_descr->clear_f_val_at_clone_decache[i]),
					      tuple_p, &size) != NO_ERROR)
	{
	  /* error has already been set */
	  db_private_free_and_init (thread_p, tplrec->tpl);
	  return ER_FAILED;
	}
      tuple_p += size;

      assert (tuple_p <= tplrec->tpl + tplrec->size);
    }

  /* all ok */
  return NO_ERROR;
}

static int
qfile_copy_tuple (THREAD_ENTRY * thread_p, QFILE_LIST_ID * to_list_id_p, QFILE_LIST_ID * from_list_id_p)
{
  QFILE_LIST_SCAN_ID scan_id;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
  SCAN_CODE qp_scan;

  /* scan through the first list file and add the tuples to the result list file. */
  if (qfile_open_list_scan (from_list_id_p, &scan_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  while (true)
    {
      qp_scan = qfile_scan_list_next (thread_p, &scan_id, &tuple_record, PEEK);
      if (qp_scan != S_SUCCESS)
	{
	  break;
	}

      if (qfile_add_tuple_to_list (thread_p, to_list_id_p, tuple_record.tpl) != NO_ERROR)
	{
	  qfile_close_scan (thread_p, &scan_id);
	  return ER_FAILED;
	}
    }

  qfile_close_scan (thread_p, &scan_id);

  if (qp_scan != S_END)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

static void
qfile_close_and_free_list_file (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id)
{
  qfile_close_list (thread_p, list_id);
  QFILE_FREE_AND_INIT_LIST_ID (list_id);
}

/*
 * qfile_union_list () -
 * 	 return: IST_ID *, or NULL
 *   list_id1(in): First list file identifier
 * 	 list_id2(in): Second list file identifier
 * 	 flag(in):
 *
 * Note: This routine takes the union of two list files by getting the
 *              tuples of the both list files and generates a new list file.
 *              The source list files are not affected.
 */
static QFILE_LIST_ID *
qfile_union_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1_p, QFILE_LIST_ID * list_id2_p, int flag)
{
  QFILE_LIST_ID *result_list_id_p, *base, *tail;

  if (list_id1_p->tuple_cnt == 0)
    {
      base = list_id2_p;
      tail = NULL;
    }
  else if (list_id2_p->tuple_cnt == 0)
    {
      base = list_id1_p;
      tail = NULL;
    }
  else
    {
      base = list_id1_p;
      tail = list_id2_p;
    }

  result_list_id_p = qfile_clone_list_id (base, false);
  if (result_list_id_p == NULL)
    {
      return NULL;
    }

  if (tail != NULL)
    {
      if (qfile_reopen_list_as_append_mode (thread_p, result_list_id_p) != NO_ERROR)
	{
	  goto error;
	}

      if (qfile_unify_types (result_list_id_p, tail) != NO_ERROR)
	{
	  goto error;
	}

      if (qfile_copy_tuple (thread_p, result_list_id_p, tail) != NO_ERROR)
	{
	  goto error;
	}

      qfile_close_list (thread_p, result_list_id_p);
    }

  /* clear base list_id to prevent double free of tfile_vfid */
  qfile_clear_list_id (base);

  return result_list_id_p;

error:
  qfile_close_list (thread_p, result_list_id_p);
  qfile_free_list_id (result_list_id_p);
  return NULL;
}

/*
 * qfile_reallocate_tuple () - reallocates a tuple to the desired size.
 *              If it cant, it sets an error and returns 0
 *   return: nt 1 succes, 0 failure
 *   tplrec(in): tuple descriptor
 * 	 tpl_size(in): desired size
 * 	 file(in):
 *   line(in):
 *
 */
int
qfile_reallocate_tuple (QFILE_TUPLE_RECORD * tuple_record_p, int tuple_size)
{
  QFILE_TUPLE tuple;

  if (tuple_record_p->size == 0)
    {
      tuple_record_p->tpl = (QFILE_TUPLE) db_private_alloc (NULL, tuple_size);
    }
  else
    {
      /*
       * Don't leak the original tuple if we get a malloc failure!
       */
      tuple = (QFILE_TUPLE) db_private_realloc (NULL, tuple_record_p->tpl, tuple_size);
      if (tuple == NULL)
	{
	  db_private_free_and_init (NULL, tuple_record_p->tpl);
	}
      tuple_record_p->tpl = tuple;
    }

  if (tuple_record_p->tpl == NULL)
    {
      return ER_FAILED;
    }

  tuple_record_p->size = tuple_size;

  return NO_ERROR;
}

#if defined (CUBRID_DEBUG)
/*
 * qfile_print_list () - Dump the content of the list file to the standard output
 *   return: none
 *   list_id(in): List File Identifier
 */
void
qfile_print_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p)
{
  QFILE_LIST_SCAN_ID scan_id;
  QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };

  if (!list_id_p || list_id_p->type_list.type_cnt < 0)
    {
      fprintf (stdout, "\n <invalid tuple list> ");
      return;
    }
  if (list_id_p->type_list.type_cnt == 0)
    {
      fprintf (stdout, "\n <empty tuple list> ");
      return;
    }

  if (qfile_open_list_scan (list_id_p, &scan_id) != NO_ERROR)
    {
      return;
    }

  while (qfile_scan_list_next (thread_p, &scan_id, &tuple_record, PEEK) == S_SUCCESS)
    {
      qfile_print_tuple (&list_id_p->type_list, tuple_record.tpl);
    }

  qfile_close_scan (thread_p, &scan_id);
}
#endif

/*
 * Sorting Related Routines
 */

/* qfile_make_sort_key () -
 *   return:
 *   info(in):
 *   key(in):
 *   input_scan(in):
 *   tplrec(in):
 */
SORT_STATUS
qfile_make_sort_key (THREAD_ENTRY * thread_p, SORTKEY_INFO * key_info_p, RECDES * key_record_p,
		     QFILE_LIST_SCAN_ID * input_scan_p, QFILE_TUPLE_RECORD * tuple_record_p)
{
  int i, nkeys, length;
  SORT_REC *sort_record_p;
  char *data;
  SCAN_CODE scan_status;
  char *field_data;
  int field_length, offset;
  SORT_STATUS status;

  scan_status = qfile_scan_list_next (thread_p, input_scan_p, tuple_record_p, PEEK);
  if (scan_status != S_SUCCESS)
    {
      return ((scan_status == S_END) ? SORT_NOMORE_RECS : SORT_ERROR_OCCURRED);
    }

  nkeys = key_info_p->nkeys;
  sort_record_p = (SORT_REC *) key_record_p->data;
  sort_record_p->next = NULL;

  if (key_info_p->use_original)
    {
      /* P_sort_key */

      /* get sort_key body start position, align data to 8 bytes boundary */
      data = &(sort_record_p->s.original.body[0]);
      data = PTR_ALIGN (data, MAX_ALIGNMENT);

      length = CAST_BUFLEN (data - key_record_p->data);	/* i.e, 12 */

      /* STEP 1: build header(tuple_ID) */
      if (length <= key_record_p->area_size)
	{
	  sort_record_p->s.original.pageid = input_scan_p->curr_vpid.pageid;
	  sort_record_p->s.original.volid = input_scan_p->curr_vpid.volid;
	  sort_record_p->s.original.offset = input_scan_p->curr_offset;
	}

      /* STEP 2: build body */
      for (i = 0; i < nkeys; i++)
	{
	  /* Position ourselves at the next field, and find out its length */
	  QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tuple_record_p->tpl, key_info_p->key[i].col, field_data);
	  field_length =
	    ((QFILE_GET_TUPLE_VALUE_FLAG (field_data) == V_BOUND) ? QFILE_GET_TUPLE_VALUE_LENGTH (field_data) : 0);

	  length += QFILE_TUPLE_VALUE_HEADER_SIZE + field_length;

	  if (length <= key_record_p->area_size)
	    {
	      memcpy (data, field_data, QFILE_TUPLE_VALUE_HEADER_SIZE + field_length);
	    }

	  /*
	   * Always pretend that we copied the data, even if we didn't.
	   * That will allow us to find out how big the record really needs
	   * to be.
	   */
	  data += QFILE_TUPLE_VALUE_HEADER_SIZE + field_length;
	}
    }
  else
    {
      /* A_sort_key */

      /* get sort_key body start position, align data to 8 bytes boundary */
      data = (char *) &sort_record_p->s.offset[nkeys];
      data = PTR_ALIGN (data, MAX_ALIGNMENT);

      length = CAST_BUFLEN (data - key_record_p->data);	/* i.e, 4 + 4 * (n - 1) */

      /* STEP 1: build header(offset_MAP) - go on with STEP 2 */

      /* STEP 2: build body */

      for (i = 0; i < nkeys; i++)
	{
	  /* Position ourselves at the next field, and find out its length */
	  QFILE_GET_TUPLE_VALUE_HEADER_POSITION (tuple_record_p->tpl, key_info_p->key[i].col, field_data);
	  field_length =
	    ((QFILE_GET_TUPLE_VALUE_FLAG (field_data) == V_BOUND) ? QFILE_GET_TUPLE_VALUE_LENGTH (field_data) : 0);

	  if (field_length)
	    {
	      /* non-NULL value */

	      offset = CAST_BUFLEN (data - key_record_p->data + QFILE_TUPLE_VALUE_HEADER_SIZE);
	      length = offset + field_length;

	      if (length <= key_record_p->area_size)
		{
		  sort_record_p->s.offset[i] = offset;
		  memcpy (data, field_data, QFILE_TUPLE_VALUE_HEADER_SIZE + field_length);
		}
	      /*
	       * Always pretend that we copied the data, even if we didn't.
	       * That will allow us to find out how big the record really
	       * needs to be.
	       */
	      data += QFILE_TUPLE_VALUE_HEADER_SIZE + field_length;
	    }
	  else
	    {
	      /* do not copy NULL-value field */

	      if (length <= key_record_p->area_size)
		{
		  sort_record_p->s.offset[i] = 0;
		}
	    }
	}
    }

  key_record_p->length = CAST_BUFLEN (data - key_record_p->data);

  if (key_record_p->length <= key_record_p->area_size)
    {
      status = SORT_SUCCESS;
    }
  else
    {
      scan_status = qfile_scan_prev (thread_p, input_scan_p);
      status = ((scan_status == S_ERROR) ? SORT_ERROR_OCCURRED : SORT_REC_DOESNT_FIT);
    }

  return status;
}

/* qfile_generate_sort_tuple () -
 *   return:
 *   info(in):
 *   sort_rec(in):
 *   output_recdes(out):
 */
QFILE_TUPLE
qfile_generate_sort_tuple (SORTKEY_INFO * key_info_p, SORT_REC * sort_record_p, RECDES * output_recdes_p)
{
  int nkeys, size, i;
  char *tuple_p, *field_p;
  char *p;
  int c;
  char *src;
  int len;

  nkeys = key_info_p->nkeys;
  size = QFILE_TUPLE_LENGTH_SIZE;

  for (i = 0; i < nkeys; i++)
    {
      size += QFILE_TUPLE_VALUE_HEADER_SIZE;
      if (sort_record_p->s.offset[i] != 0)
	{
	  p = (char *) sort_record_p + sort_record_p->s.offset[i] - QFILE_TUPLE_VALUE_HEADER_SIZE;
	  size += QFILE_GET_TUPLE_VALUE_LENGTH (p);
	}
    }

  if (output_recdes_p->area_size < size)
    {
      if (output_recdes_p->area_size == 0)
	{
	  tuple_p = (char *) db_private_alloc (NULL, size);
	}
      else
	{
	  tuple_p = (char *) db_private_realloc (NULL, output_recdes_p->data, size);
	}

      if (tuple_p == NULL)
	{
	  return NULL;
	}

      output_recdes_p->data = tuple_p;
      output_recdes_p->area_size = size;
    }

  tuple_p = output_recdes_p->data;
  field_p = tuple_p + QFILE_TUPLE_LENGTH_SIZE;

  for (i = 0; i < nkeys; i++)
    {
      c = key_info_p->key[i].permuted_col;

      if (sort_record_p->s.offset[c] == 0)
	{
	  QFILE_PUT_TUPLE_VALUE_FLAG (field_p, V_UNBOUND);
	  QFILE_PUT_TUPLE_VALUE_LENGTH (field_p, 0);
	}
      else
	{
	  src = (char *) sort_record_p + sort_record_p->s.offset[c] - QFILE_TUPLE_VALUE_HEADER_SIZE;
	  len = QFILE_GET_TUPLE_VALUE_LENGTH (src);
	  memcpy (field_p, src, len + QFILE_TUPLE_VALUE_HEADER_SIZE);
	  field_p += len;
	}

      field_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
    }

  QFILE_PUT_TUPLE_LENGTH (tuple_p, field_p - tuple_p);
  return tuple_p;
}

/*
 * qfile_get_next_sort_item	() -
 *   return: SORT_STATUS
 *   recdes(in): Temporary record descriptor
 *   arg(in): Scan identifier
 *
 * Note: This routine is called by the sorting module to get the next
 * input item to sort. The scan identifier opened on the list file
 * is used to get the next list file tuple and return it to the sorting module
 * within the record descriptor. If the record descriptor area is not big
 * enough to hold the tuple, it is indicated to the sorting module and
 * scan position remains unchanged. The routine is supposed to be called again
 * with a bigger record descriptor. If there are no more tuples left
 * in the list file, or if an error occurs, the sorting module is informed with
 * necessary return codes.
 */
static SORT_STATUS
qfile_get_next_sort_item (THREAD_ENTRY * thread_p, RECDES * recdes_p, void *arg)
{
  SORT_INFO *sort_info_p;
  QFILE_SORT_SCAN_ID *scan_id_p;

  sort_info_p = (SORT_INFO *) arg;
  scan_id_p = sort_info_p->s_id;

  return qfile_make_sort_key (thread_p, &sort_info_p->key_info, recdes_p, scan_id_p->s_id, &scan_id_p->tplrec);
}

/*
 * qfile_put_next_sort_item () -
 *   return: SORT_STATUS
 *   recdes(in): Temporary record descriptor
 *   arg(in): Scan identifier
 *
 * Note: This routine is called by the sorting module to output	the next sorted
 * item.
 *
 * We have two versions of the put_next function:
 * ls_sort_put_next_long and ls_sort_put_next_short.  The long version
 * is building sort keys from records that hold more fields than sort keys.
 * It optimizes by simply keeping a pointer to the original record, and
 * when the sort keys are delivered back from the sort module, it uses
 * the pointer to retrieve the original record with all of the fields and
 * delivers that record to the output listfile.
 *
 * The short version is used when we we're sorting on all of the fields of
 * the record.  In that case there's no point in remembering the original record,
 * and we save the space occupied by the pointer. We also avoid the relatively
 * random traversal of the input file when rendering the output file,
 * since we can reconstruct the input records without actually consulting
 * the input file.
 */
static int
qfile_put_next_sort_item (THREAD_ENTRY * thread_p, const RECDES * recdes_p, void *arg)
{
  SORT_INFO *sort_info_p;
  SORT_REC *key_p;
  int error;
  QFILE_TUPLE_DESCRIPTOR *tuple_descr_p;
  int nkeys, i;
  char *p;
  PAGE_PTR page_p;
  VPID vpid;
  QFILE_LIST_ID *list_id_p;
  char *data;

  error = NO_ERROR;
  sort_info_p = (SORT_INFO *) arg;

  for (key_p = (SORT_REC *) recdes_p->data; key_p && error == NO_ERROR; key_p = key_p->next)
    {
      if (sort_info_p->key_info.use_original)
	{
	  /* P_sort_key */

	  list_id_p = &(sort_info_p->s_id->s_id->list_id);

	  vpid.pageid = key_p->s.original.pageid;
	  vpid.volid = key_p->s.original.volid;

#if 0				/* SortCache */
	  /* check if page is already fixed */
	  if (VPID_EQ (&(sort_info_p->fixed_vpid), &vpid))
	    {
	      /* use cached page pointer */
	      page_p = sort_info_p->fixed_page;
	    }
	  else
	    {
	      /* free currently fixed page */
	      if (sort_info_p->fixed_page != NULL)
		{
		  qmgr_free_old_page_and_init (thread_p, sort_info_p->fixed_page, list_id_p->tfile_vfid);
		}

	      /* fix page and cache fixed vpid */
	      page_p = qmgr_get_old_page (thread_p, &vpid, list_id_p->tfile_vfid);
	      if (page_p == NULL)
		{
		  assert (er_errid () != NO_ERROR);
		  return er_errid ();
		}

	      /* cache page pointer */
	      sort_info_p->fixed_vpid = vpid;
	      sort_info_p->fixed_page = page_p;
	    }
#else /* not SortCache */
	  page_p = qmgr_get_old_page (thread_p, &vpid, list_id_p->tfile_vfid);
	  if (page_p == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }
#endif /* not SortCache */

	  QFILE_GET_OVERFLOW_VPID (&vpid, page_p);
	  if (vpid.pageid == NULL_PAGEID)
	    {
	      /*
	       * This is the normal case of a non-overflow tuple.  We can use
	       * the page image directly, since we know that the tuple resides
	       * entirely on that page.
	       */
	      data = page_p + key_p->s.original.offset;
	      error = qfile_add_tuple_to_list (thread_p, sort_info_p->output_file, data);
	    }
	  else
	    {
	      assert (NULL_PAGEID < vpid.pageid);	/* should not be NULL_PAGEID_IN_PROGRESS */

	      /*
	       * Rats; this tuple requires overflow pages.  We need to copy
	       * all of the pages from the input file to the output file.
	       */
	      error = qfile_add_overflow_tuple_to_list (thread_p, sort_info_p->output_file, page_p, list_id_p);
	    }
#if 1				/* not SortCache */
	  qmgr_free_old_page_and_init (thread_p, page_p, list_id_p->tfile_vfid);
#endif /* not SortCache */
	}
      else
	{
	  /* A_sort_key */

	  nkeys = sort_info_p->key_info.nkeys;	/* get sort_key field number */

	  /* generate tuple descriptor */
	  tuple_descr_p = &(sort_info_p->output_file->tpl_descr);

	  /* determine how big a tuple we'll need */
	  tuple_descr_p->tpl_size = QFILE_TUPLE_LENGTH_SIZE + (QFILE_TUPLE_VALUE_HEADER_SIZE * nkeys);
	  for (i = 0; i < nkeys; i++)
	    {
	      if (key_p->s.offset[i] != 0)
		{
		  /*
		   * Remember, the offset[] value points to the start of the
		   * value's *data* (i.e., after the valflag/vallen nonsense),
		   * and is measured from the start of the sort_rec.
		   */
		  p = (char *) key_p + key_p->s.offset[i] - QFILE_TUPLE_VALUE_HEADER_SIZE;
		  tuple_descr_p->tpl_size += QFILE_GET_TUPLE_VALUE_LENGTH (p);
		}
	    }

	  if (tuple_descr_p->tpl_size < QFILE_MAX_TUPLE_SIZE_IN_PAGE)
	    {
	      /* SMALL QFILE_TUPLE */

	      /* set tuple descriptor */
	      tuple_descr_p->sortkey_info = (void *) (&sort_info_p->key_info);
	      tuple_descr_p->sort_rec = (void *) key_p;

	      /* generate sort_key driven tuple into list file page */
	      error = qfile_generate_tuple_into_list (thread_p, sort_info_p->output_file, T_SORTKEY);
	    }
	  else
	    {
	      /* BIG QFILE_TUPLE */

	      /*
	       * We didn't record the original vpid, and we should just
	       * reconstruct the original record from this sort key (rather
	       * than pressure the page buffer pool by reading in the original
	       * page to get the original tuple).
	       */
	      if (qfile_generate_sort_tuple (&sort_info_p->key_info, key_p, &sort_info_p->output_recdes) == NULL)
		{
		  error = ER_FAILED;
		}
	      else
		{
		  error = qfile_add_tuple_to_list (thread_p, sort_info_p->output_file, sort_info_p->output_recdes.data);
		}
	    }
	}
    }

  return (error == NO_ERROR) ? NO_ERROR : er_errid ();
}

/*
 * qfile_compare_partial_sort_record () -
 *   return: -1, 0, or 1, strcmp-style
 *   pk0(in): Pointer to pointer to first sort record
 *   pk1(in): Pointer to pointer to second sort record
 *   arg(in): Pointer to sort info
 *
 * Note: These routines are used for relative comparisons of two sort
 *       records during sorting.
 */
int
qfile_compare_partial_sort_record (const void *pk0, const void *pk1, void *arg)
{
  SORTKEY_INFO *key_info_p;
  SORT_REC *k0, *k1;
  int i, n;
  int o0, o1;
  int order;
  char *d0, *d1;
  char *fp0, *fp1;		/* sort_key field pointer */

  key_info_p = (SORTKEY_INFO *) arg;
  n = key_info_p->nkeys;
  order = 0;

  k0 = *(SORT_REC **) pk0;
  k1 = *(SORT_REC **) pk1;

  /* get body start position of k0, k1 */
  fp0 = &(k0->s.original.body[0]);
  fp0 = PTR_ALIGN (fp0, MAX_ALIGNMENT);

  fp1 = &(k1->s.original.body[0]);
  fp1 = PTR_ALIGN (fp1, MAX_ALIGNMENT);

  for (i = 0; i < n; i++)
    {
      if (QFILE_GET_TUPLE_VALUE_FLAG (fp0) == V_BOUND)
	{
	  o0 = 1;
	}
      else
	{
	  o0 = 0;		/* NULL */
	}

      if (QFILE_GET_TUPLE_VALUE_FLAG (fp1) == V_BOUND)
	{
	  o1 = 1;
	}
      else
	{
	  o1 = 0;		/* NULL */
	}

      if (o0 && o1)
	{
	  if (key_info_p->key[i].use_cmp_dom)
	    {
	      order = qfile_compare_with_interpolation_domain (fp0, fp1, &key_info_p->key[i], key_info_p);
	    }
	  else
	    {
	      d0 = fp0 + QFILE_TUPLE_VALUE_HEADER_LENGTH;
	      d1 = fp1 + QFILE_TUPLE_VALUE_HEADER_LENGTH;

	      order = (*key_info_p->key[i].sort_f) (d0, d1, key_info_p->key[i].col_dom, 0, 1, NULL);
	    }

	  order = key_info_p->key[i].is_desc ? -order : order;
	}
      else
	{
	  order = qfile_compare_with_null_value (o0, o1, key_info_p->key[i]);
	}

      if (order != 0)
	{
	  break;
	}

      fp0 += QFILE_TUPLE_VALUE_HEADER_LENGTH + QFILE_GET_TUPLE_VALUE_LENGTH (fp0);
      fp1 += QFILE_TUPLE_VALUE_HEADER_LENGTH + QFILE_GET_TUPLE_VALUE_LENGTH (fp1);
    }

  return order;
}

int
qfile_compare_all_sort_record (const void *pk0, const void *pk1, void *arg)
{
  SORTKEY_INFO *key_info_p;
  SORT_REC *k0, *k1;
  int i, n;
  int order;
  int o0, o1;
  char *d0, *d1;

  key_info_p = (SORTKEY_INFO *) arg;
  n = key_info_p->nkeys;
  order = 0;

  k0 = *(SORT_REC **) pk0;
  k1 = *(SORT_REC **) pk1;

  for (i = 0; i < n; i++)
    {
      o0 = k0->s.offset[i];
      o1 = k1->s.offset[i];

      if (o0 && o1)
	{
	  d0 = (char *) k0 + o0;
	  d1 = (char *) k1 + o1;

	  order = (*key_info_p->key[i].sort_f) (d0, d1, key_info_p->key[i].col_dom, 0, 1, NULL);
	  order = key_info_p->key[i].is_desc ? -order : order;
	}
      else
	{
	  order = qfile_compare_with_null_value (o0, o1, key_info_p->key[i]);
	}

      if (order != 0)
	{
	  break;
	}
    }

  return order;
}

/*
 * qfile_compare_with_null_value () -
 *   return: -1, 0, or 1, strcmp-style
 *   o0(in): The first value
 *   o1(in): The second value
 *   key_info(in): Sub-key info
 *
 * Note: These routines are internally used for relative comparisons
 *       of two values which include NULL value.
 */
static int
qfile_compare_with_null_value (int o0, int o1, SUBKEY_INFO key_info)
{
  /* At least one of the values sholud be NULL */
  assert (o0 == 0 || o1 == 0);

  if (o0 == 0 && o1 == 0)
    {
      /* both are unbound */
      return 0;
    }
  else if (o0 == 0)
    {
      /* NULL compare_op !NULL */
      assert (o1 != 0);
      if (key_info.is_nulls_first)
	{
	  return -1;
	}
      else
	{
	  return 1;
	}
    }
  else
    {
      /* !NULL compare_op NULL */
      assert (o1 == 0);
      if (key_info.is_nulls_first)
	{
	  return 1;
	}
      else
	{
	  return -1;
	}
    }
}

/* qfile_get_estimated_pages_for_sorting () -
 *   return:
 *   listid(in):
 *   info(in):
 *
 * Note: Make an estimate of input page count to be passed to the sorting
 *       module.  We want this to be an upper bound, because the sort
 *       package already has a limit on sort buffer size, and sorting
 *       proceeds faster with larger in-memory use.
 */
int
qfile_get_estimated_pages_for_sorting (QFILE_LIST_ID * list_id_p, SORTKEY_INFO * key_info_p)
{
  int prorated_pages, sort_key_size, sort_key_overhead;

  prorated_pages = (int) list_id_p->page_cnt;
  if (key_info_p->use_original == 1)
    {
      /* P_sort_key */

      /*
       * Every Part sort key record will have one int of overhead
       * per field in the key (for the offset vector).
       */
      sort_key_size = (int) offsetof (SORT_REC, s.original.body[0]);
      sort_key_overhead = (int) ceil (((double) (list_id_p->tuple_cnt * sort_key_size)) / DB_PAGESIZE);
    }
  else
    {
      /* A_sort_key */

      /*
       * Every Part sort key record will have one int of overhead
       * per field in the key (for the offset vector).
       */
      sort_key_size =
	(int) offsetof (SORT_REC, s.offset[0]) + sizeof (((SORT_REC *) 0)->s.offset[0]) * key_info_p->nkeys;
      sort_key_overhead = (int) ceil (((double) (list_id_p->tuple_cnt * sort_key_size)) / DB_PAGESIZE);
    }

  return prorated_pages + sort_key_overhead;
}

/* qfile_initialize_sort_key_info () -
 *   return:
 *   info(in):
 *   list(in):
 *   types(in):
 */
SORTKEY_INFO *
qfile_initialize_sort_key_info (SORTKEY_INFO * key_info_p, SORT_LIST * list_p, QFILE_TUPLE_VALUE_TYPE_LIST * types)
{
  int i, n;
  SUBKEY_INFO *subkey;

  if (types == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }

  if (list_p)
    {
      n = qfile_get_sort_list_size (list_p);
    }
  else
    {
      n = types->type_cnt;
    }

  key_info_p->nkeys = n;
  key_info_p->use_original = (n != types->type_cnt);
  key_info_p->error = NO_ERROR;

  if (n <= (int) DIM (key_info_p->default_keys))
    {
      key_info_p->key = key_info_p->default_keys;
    }
  else
    {
      key_info_p->key = (SUBKEY_INFO *) db_private_alloc (NULL, n * sizeof (SUBKEY_INFO));
      if (key_info_p->key == NULL)
	{
	  return NULL;
	}
    }

  if (list_p)
    {
      SORT_LIST *p;
      for (i = 0, p = list_p; p; i++, p = p->next)
	{
	  assert_release (p->pos_descr.pos_no >= 0);
	  assert_release (p->pos_descr.dom != NULL);

	  subkey = &key_info_p->key[i];
	  subkey->col = p->pos_descr.pos_no;
	  subkey->col_dom = p->pos_descr.dom;
	  subkey->cmp_dom = NULL;
	  subkey->use_cmp_dom = false;

	  if (p->pos_descr.dom->type->id == DB_TYPE_VARIABLE)
	    {
	      subkey->sort_f = types->domp[i]->type->get_data_cmpdisk_function ();
	    }
	  else
	    {
	      subkey->sort_f = p->pos_descr.dom->type->get_data_cmpdisk_function ();
	    }

	  subkey->is_desc = (p->s_order == S_ASC) ? 0 : 1;
	  subkey->is_nulls_first = (p->s_nulls == S_NULLS_LAST) ? 0 : 1;

	  if (key_info_p->use_original)
	    {
	      key_info_p->key[i].permuted_col = i;
	    }
	  else
	    {
	      key_info_p->key[p->pos_descr.pos_no].permuted_col = i;
	    }
	}
    }
  else
    {
      for (i = 0; i < n; i++)
	{
	  SUBKEY_INFO *subkey;
	  subkey = &key_info_p->key[i];
	  subkey->col = i;
	  subkey->permuted_col = i;
	  subkey->col_dom = types->domp[i];
	  subkey->cmp_dom = NULL;
	  subkey->use_cmp_dom = false;
	  subkey->sort_f = types->domp[i]->type->get_data_cmpdisk_function ();
	  subkey->is_desc = 0;
	  subkey->is_nulls_first = 1;
	}
    }

  return key_info_p;
}

/* qfile_clear_sort_key_info () -
 *   return:
 *   info(in):
 */
void
qfile_clear_sort_key_info (SORTKEY_INFO * key_info_p)
{
  if (!key_info_p)
    {
      return;
    }

  if (key_info_p->key && key_info_p->key != key_info_p->default_keys)
    {
      db_private_free_and_init (NULL, key_info_p->key);
    }

  key_info_p->key = NULL;
  key_info_p->nkeys = 0;
}

/* qfile_initialize_sort_info () -
 *   return:
 *   info(in):
 *   listid(in):
 *   sort_list(in):
 */
static SORT_INFO *
qfile_initialize_sort_info (SORT_INFO * sort_info_p, QFILE_LIST_ID * list_id_p, SORT_LIST * sort_list_p)
{
  sort_info_p->key_info.key = NULL;
#if 0				/* SortCache */
  VPID_SET_NULL (&(sort_info_p->fixed_vpid));
  sort_info_p->fixed_page = NULL;
#endif /* SortCache */
  sort_info_p->output_recdes.data = NULL;
  sort_info_p->output_recdes.area_size = 0;
  if (qfile_initialize_sort_key_info (&sort_info_p->key_info, sort_list_p, &list_id_p->type_list) == NULL)
    {
      return NULL;
    }

  return sort_info_p;
}

/* qfile_clear_sort_info () -
 *   return: none
 *   info(in): Pointer to info block to be initialized
 *
 * Note: Free all internal structures in the given SORT_INFO block.
 */
static void
qfile_clear_sort_info (SORT_INFO * sort_info_p)
{
#if 0				/* SortCache */
  QFILE_LIST_ID *list_idp;

  list_idp = &(sort_info_p->s_id->s_id->list_id);

  if (sort_info_p->fixed_page != NULL)
    {
      qmgr_free_old_page_and_init (thread_p, sort_info_p->fixed_page, list_idp->tfile_vfid);
    }
#endif /* SortCache */

  qfile_clear_sort_key_info (&sort_info_p->key_info);

  if (sort_info_p->output_recdes.data)
    {
      db_private_free_and_init (NULL, sort_info_p->output_recdes.data);
    }

  sort_info_p->output_recdes.data = NULL;
  sort_info_p->output_recdes.area_size = 0;
}

/*
 * qfile_sort_list_with_func () -
 *   return: QFILE_LIST_ID *, or NULL
 *   list_id(in): Source list file identifier
 *   sort_list(in): List of comparison items
 *   option(in):
 *   ls_flag(in):
 *   get_fn(in):
 *   put_fn(in):
 *   cmp_fn(in):
 *   extra_arg(in):
 *   limit(in):
 *   do_close(in):
 */
QFILE_LIST_ID *
qfile_sort_list_with_func (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, SORT_LIST * sort_list_p,
			   QUERY_OPTIONS option, int flag, SORT_GET_FUNC * get_func, SORT_PUT_FUNC * put_func,
			   SORT_CMP_FUNC * cmp_func, void *extra_arg, int limit, bool do_close)
{
  QFILE_LIST_ID *srlist_id;
  QFILE_LIST_SCAN_ID t_scan_id;
  QFILE_SORT_SCAN_ID s_scan_id;
  SORT_INFO info;
  int sort_result, estimated_pages;
  SORT_DUP_OPTION dup_option;

  srlist_id = qfile_open_list (thread_p, &list_id_p->type_list, sort_list_p, list_id_p->query_id, flag);
  if (srlist_id == NULL)
    {
      return NULL;
    }

  /* open a scan on the unsorted list file */
  if (qfile_open_list_scan (list_id_p, &t_scan_id) != NO_ERROR)
    {
      qfile_close_and_free_list_file (thread_p, srlist_id);
      return NULL;
    }

  if (qfile_initialize_sort_info (&info, list_id_p, sort_list_p) == NULL)
    {
      qfile_close_scan (thread_p, &t_scan_id);
      qfile_close_and_free_list_file (thread_p, srlist_id);
      return NULL;
    }

  info.s_id = &s_scan_id;
  info.output_file = srlist_id;
  info.extra_arg = extra_arg;

  if (get_func == NULL)
    {
      get_func = &qfile_get_next_sort_item;
    }

  if (put_func == NULL)
    {
      put_func = &qfile_put_next_sort_item;
    }

  if (cmp_func == NULL)
    {
      if (info.key_info.use_original == 1)
	{
	  cmp_func = &qfile_compare_partial_sort_record;
	}
      else
	{
	  cmp_func = &qfile_compare_all_sort_record;
	}
    }

  s_scan_id.s_id = &t_scan_id;
  s_scan_id.tplrec.size = 0;
  s_scan_id.tplrec.tpl = (char *) NULL;

  estimated_pages = qfile_get_estimated_pages_for_sorting (list_id_p, &info.key_info);

  dup_option = ((option == Q_DISTINCT) ? SORT_ELIM_DUP : SORT_DUP);

  sort_result =
    sort_listfile (thread_p, NULL_VOLID, estimated_pages, get_func, &info, put_func, &info, cmp_func, &info.key_info,
		   dup_option, limit, srlist_id->tfile_vfid->tde_encrypted);

  if (sort_result < 0)
    {
#if 0				/* SortCache */
      qfile_clear_sort_info (&info);
      qfile_close_scan (&t_scan_id);
#else /* not SortCache */
      qfile_close_scan (thread_p, &t_scan_id);
      qfile_clear_sort_info (&info);
#endif /* not SortCache */
      qfile_close_list (thread_p, list_id_p);
      qfile_destroy_list (thread_p, list_id_p);
      qfile_close_and_free_list_file (thread_p, srlist_id);
      return NULL;
    }

  if (do_close)
    {
      qfile_close_list (thread_p, srlist_id);
    }

#if 0				/* SortCache */
  qfile_clear_sort_info (&info);
  qfile_close_scan (&t_scan_id);
#else /* not SortCache */
  qfile_close_scan (thread_p, &t_scan_id);
  qfile_clear_sort_info (&info);
#endif /* not SortCache */

  qfile_close_list (thread_p, list_id_p);
  qfile_destroy_list (thread_p, list_id_p);
  qfile_copy_list_id (list_id_p, srlist_id, true);
  QFILE_FREE_AND_INIT_LIST_ID (srlist_id);

  return list_id_p;
}

/*
 * qfile_sort_list () -
 *   return: QFILE_LIST_ID *, or NULL
 *   list_id(in): Source list file identifier
 *   sort_list(in): List of comparison items
 *   option(in):
 *   do_close(in);
 *
 * Note: This routine sorts the specified list file tuples according
 *       to the list of comparison items and generates a sorted list
 *       file. The source list file is not affected by the routine.
 */
QFILE_LIST_ID *
qfile_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, SORT_LIST * sort_list_p, QUERY_OPTIONS option,
		 bool do_close)
{
  int ls_flag;

  if (sort_list_p && qfile_is_sort_list_covered (list_id_p->sort_list, sort_list_p) == true)
    {
      /* no need to sort here */
      return list_id_p;
    }

  ls_flag = (option == Q_DISTINCT) ? QFILE_FLAG_DISTINCT : QFILE_FLAG_ALL;

  return qfile_sort_list_with_func (thread_p, list_id_p, sort_list_p, option, ls_flag, NULL, NULL, NULL, NULL,
				    NO_SORT_LIMIT, do_close);
}

/*
 * qfile_copy_list_pages () -
 *   return:
 *   old_first_vpidp(in)        :
 *   old_tfile_vfidp(in)        :
 *   new_first_vpidp(in)        :
 *   new_last_vpidp(in) :
 *   new_tfile_vfidp(in)        :
 */
static int
qfile_copy_list_pages (THREAD_ENTRY * thread_p, VPID * old_first_vpid_p, QMGR_TEMP_FILE * old_tfile_vfid_p,
		       VPID * new_first_vpid_p, VPID * new_last_vpid_p, QMGR_TEMP_FILE * new_tfile_vfid_p)
{
  PAGE_PTR old_page_p, old_ovfl_page_p, prev_page_p, new_page_p, new_ovfl_page_p;
  VPID old_next_vpid, old_ovfl_vpid, prev_vpid, new_ovfl_vpid;

  old_page_p = qmgr_get_old_page (thread_p, old_first_vpid_p, old_tfile_vfid_p);
  if (old_page_p == NULL)
    {
      return ER_FAILED;
    }

  new_page_p = qmgr_get_new_page (thread_p, new_first_vpid_p, new_tfile_vfid_p);
  if (new_page_p == NULL)
    {
      qmgr_free_old_page_and_init (thread_p, old_page_p, old_tfile_vfid_p);
      return ER_FAILED;
    }

  *new_last_vpid_p = *new_first_vpid_p;

  while (true)
    {
      (void) memcpy (new_page_p, old_page_p, DB_PAGESIZE);

      QFILE_GET_OVERFLOW_VPID (&old_ovfl_vpid, old_page_p);
      prev_page_p = new_page_p;
      new_ovfl_page_p = NULL;

      while (!VPID_ISNULL (&old_ovfl_vpid))
	{
	  old_ovfl_page_p = qmgr_get_old_page (thread_p, &old_ovfl_vpid, old_tfile_vfid_p);
	  if (old_ovfl_page_p == NULL)
	    {
	      qmgr_free_old_page_and_init (thread_p, old_page_p, old_tfile_vfid_p);
	      qfile_set_dirty_page (thread_p, new_page_p, FREE, new_tfile_vfid_p);
	      return ER_FAILED;
	    }

	  new_ovfl_page_p = qmgr_get_new_page (thread_p, &new_ovfl_vpid, new_tfile_vfid_p);

	  if (new_ovfl_page_p == NULL)
	    {
	      qmgr_free_old_page_and_init (thread_p, old_ovfl_page_p, old_tfile_vfid_p);
	      qmgr_free_old_page_and_init (thread_p, old_page_p, old_tfile_vfid_p);
	      qfile_set_dirty_page (thread_p, new_page_p, FREE, new_tfile_vfid_p);
	      return ER_FAILED;
	    }

	  (void) memcpy (new_ovfl_page_p, old_ovfl_page_p, DB_PAGESIZE);

	  QFILE_GET_OVERFLOW_VPID (&old_ovfl_vpid, old_ovfl_page_p);
	  qmgr_free_old_page_and_init (thread_p, old_ovfl_page_p, old_tfile_vfid_p);

	  QFILE_PUT_OVERFLOW_VPID (prev_page_p, &new_ovfl_vpid);
	  qfile_set_dirty_page (thread_p, prev_page_p, FREE, new_tfile_vfid_p);

	  prev_page_p = new_ovfl_page_p;
	}

      if (new_ovfl_page_p)
	{
	  qfile_set_dirty_page (thread_p, new_ovfl_page_p, FREE, new_tfile_vfid_p);
	}

      QFILE_GET_NEXT_VPID (&old_next_vpid, old_page_p);
      qmgr_free_old_page_and_init (thread_p, old_page_p, old_tfile_vfid_p);

      if (VPID_ISNULL (&old_next_vpid))
	{
	  qfile_set_dirty_page (thread_p, new_page_p, FREE, new_tfile_vfid_p);
	  new_page_p = NULL;
	  break;
	}

      old_page_p = qmgr_get_old_page (thread_p, &old_next_vpid, old_tfile_vfid_p);
      prev_page_p = new_page_p;
      prev_vpid = *new_last_vpid_p;

      if (old_page_p == NULL)
	{
	  qfile_set_dirty_page (thread_p, prev_page_p, FREE, new_tfile_vfid_p);
	  return ER_FAILED;
	}

      new_page_p = qmgr_get_new_page (thread_p, new_last_vpid_p, new_tfile_vfid_p);
      if (new_page_p == NULL)
	{
	  qmgr_free_old_page_and_init (thread_p, old_page_p, old_tfile_vfid_p);
	  qfile_set_dirty_page (thread_p, prev_page_p, FREE, new_tfile_vfid_p);
	  return ER_FAILED;
	}

      QFILE_PUT_PREV_VPID (new_page_p, &prev_vpid);
      QFILE_PUT_NEXT_VPID (prev_page_p, new_last_vpid_p);

      qfile_set_dirty_page (thread_p, prev_page_p, FREE, new_tfile_vfid_p);
    }

  if (new_page_p)
    {
      qfile_set_dirty_page (thread_p, prev_page_p, FREE, new_tfile_vfid_p);
    }

  return NO_ERROR;
}

/*
 * qfile_duplicate_list () -
 *   return:
 *   list_id(in):
 *   flag(in):
 *
 * Note: This routine duplicates the specified list file.
 *       The source list file is not affected by the routine.
 */
QFILE_LIST_ID *
qfile_duplicate_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, int flag)
{
  QFILE_LIST_ID *dup_list_id_p;

  dup_list_id_p = qfile_open_list (thread_p, &list_id_p->type_list, NULL, list_id_p->query_id, flag);
  if (dup_list_id_p == NULL)
    {
      return NULL;
    }

  if (qfile_copy_list_pages (thread_p, &list_id_p->first_vpid, list_id_p->tfile_vfid, &dup_list_id_p->first_vpid,
			     &dup_list_id_p->last_vpid, dup_list_id_p->tfile_vfid) != NO_ERROR)
    {
      qfile_destroy_list (thread_p, dup_list_id_p);
      QFILE_FREE_AND_INIT_LIST_ID (dup_list_id_p);
      return NULL;
    }

  dup_list_id_p->tuple_cnt = list_id_p->tuple_cnt;
  dup_list_id_p->page_cnt = list_id_p->page_cnt;

  return dup_list_id_p;
}

/*
 * List File Scan Routines
 */

/*
 * qfile_get_tuple () -
 *   return:
 *   first_page(in):
 *   tuplep(in):
 *   tplrec(in):
 *   list_idp(in):
 */
int
qfile_get_tuple (THREAD_ENTRY * thread_p, PAGE_PTR first_page_p, QFILE_TUPLE tuple, QFILE_TUPLE_RECORD * tuple_record_p,
		 QFILE_LIST_ID * list_id_p)
{
  VPID ovfl_vpid;
  char *tuple_p;
  int offset;
  int tuple_length, tuple_page_size;
  int max_tuple_page_size;
  PAGE_PTR page_p;

  page_p = first_page_p;
  tuple_length = QFILE_GET_TUPLE_LENGTH (tuple);

  if (tuple_record_p->size < tuple_length)
    {
      if (qfile_reallocate_tuple (tuple_record_p, tuple_length) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  tuple_p = (char *) tuple_record_p->tpl;

  if (QFILE_GET_OVERFLOW_PAGE_ID (page_p) == NULL_PAGEID)
    {
      /* tuple is inside the page */
      memcpy (tuple_p, tuple, tuple_length);
      return NO_ERROR;
    }
  else
    {
      /* tuple has overflow pages */
      offset = 0;
      max_tuple_page_size = qfile_Max_tuple_page_size;

      do
	{
	  QFILE_GET_OVERFLOW_VPID (&ovfl_vpid, page_p);
	  tuple_page_size = MIN (tuple_length - offset, max_tuple_page_size);

	  memcpy (tuple_p, (char *) page_p + QFILE_PAGE_HEADER_SIZE, tuple_page_size);

	  tuple_p += tuple_page_size;
	  offset += tuple_page_size;

	  if (page_p != first_page_p)
	    {
	      qmgr_free_old_page_and_init (thread_p, page_p, list_id_p->tfile_vfid);
	    }

	  if (ovfl_vpid.pageid != NULL_PAGEID)
	    {
	      page_p = qmgr_get_old_page (thread_p, &ovfl_vpid, list_id_p->tfile_vfid);
	      if (page_p == NULL)
		{
		  return ER_FAILED;
		}
	    }
	}
      while (ovfl_vpid.pageid != NULL_PAGEID);
    }

  return NO_ERROR;
}

/*
 * qfile_get_tuple_from_current_list () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   s_id(in): Scan identifier
 *   tplrec(in): Tuple record descriptor
 *
 * Note: Fetch the current tuple of the given scan identifier into the
 *       tuple descriptor.
 */
static int
qfile_get_tuple_from_current_list (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p,
				   QFILE_TUPLE_RECORD * tuple_record_p)
{
  PAGE_PTR page_p;
  QFILE_TUPLE tuple;

  page_p = scan_id_p->curr_pgptr;
  tuple = (char *) page_p + scan_id_p->curr_offset;

  return qfile_get_tuple (thread_p, page_p, tuple, tuple_record_p, &scan_id_p->list_id);
}

/*
 * qfile_scan_next  () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   s_id(in/out): Scan identifier
 *
 * Note: The scan is moved to the next scan item. If there are no more
 *       scan items, S_END is returned.  If an error occurs,
 *       S_ERROR is returned.
 *
 * Note: The scan identifier must be of type LIST FILE scan identifier.
 */
static SCAN_CODE
qfile_scan_next (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p)
{
  PAGE_PTR page_p, next_page_p;
  VPID next_vpid;

  if (scan_id_p->position == S_BEFORE)
    {
      if (scan_id_p->list_id.tuple_cnt > 0)
	{
	  page_p = qmgr_get_old_page (thread_p, &scan_id_p->list_id.first_vpid, scan_id_p->list_id.tfile_vfid);
	  if (page_p == NULL)
	    {
	      return S_ERROR;
	    }

	  QFILE_COPY_VPID (&scan_id_p->curr_vpid, &scan_id_p->list_id.first_vpid);
	  scan_id_p->curr_pgptr = page_p;
	  scan_id_p->curr_offset = QFILE_PAGE_HEADER_SIZE;
	  scan_id_p->curr_tpl = (char *) scan_id_p->curr_pgptr + QFILE_PAGE_HEADER_SIZE;
	  scan_id_p->curr_tplno = 0;
	  scan_id_p->position = S_ON;
	  return S_SUCCESS;
	}
      else
	{
	  return S_END;
	}
    }
  else if (scan_id_p->position == S_ON)
    {
      if (scan_id_p->curr_tplno < QFILE_GET_TUPLE_COUNT (scan_id_p->curr_pgptr) - 1)
	{
	  scan_id_p->curr_offset += QFILE_GET_TUPLE_LENGTH (scan_id_p->curr_tpl);
	  scan_id_p->curr_tpl += QFILE_GET_TUPLE_LENGTH (scan_id_p->curr_tpl);
	  scan_id_p->curr_tplno++;
	  return S_SUCCESS;
	}
      else if (qfile_has_next_page (scan_id_p->curr_pgptr))
	{
	  QFILE_GET_NEXT_VPID (&next_vpid, scan_id_p->curr_pgptr);
	  next_page_p = qmgr_get_old_page (thread_p, &next_vpid, scan_id_p->list_id.tfile_vfid);
	  if (next_page_p == NULL)
	    {
	      return S_ERROR;
	    }

	  qmgr_free_old_page_and_init (thread_p, scan_id_p->curr_pgptr, scan_id_p->list_id.tfile_vfid);
	  QFILE_COPY_VPID (&scan_id_p->curr_vpid, &next_vpid);
	  scan_id_p->curr_pgptr = next_page_p;
	  scan_id_p->curr_tplno = 0;
	  scan_id_p->curr_offset = QFILE_PAGE_HEADER_SIZE;
	  scan_id_p->curr_tpl = (char *) scan_id_p->curr_pgptr + QFILE_PAGE_HEADER_SIZE;
	  return S_SUCCESS;
	}
      else
	{
	  scan_id_p->position = S_AFTER;

	  if (!scan_id_p->keep_page_on_finish)
	    {
	      scan_id_p->curr_vpid.pageid = NULL_PAGEID;
	      qmgr_free_old_page_and_init (thread_p, scan_id_p->curr_pgptr, scan_id_p->list_id.tfile_vfid);
	    }

	  return S_END;
	}
    }
  else if (scan_id_p->position == S_AFTER)
    {
      return S_END;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
      return S_ERROR;
    }
}

/*
 * qfile_scan_prev () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   s_id(in/out): Scan identifier
 *
 * Note: The scan is moved to previous scan item. If there are no more
 *       scan items, S_END is returned.  If an error occurs,
 *       S_ERROR is returned.
 */
static SCAN_CODE
qfile_scan_prev (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p)
{
  PAGE_PTR page_p, prev_page_p;
  VPID prev_vpid;

  if (scan_id_p->position == S_BEFORE)
    {
      return S_END;
    }
  else if (scan_id_p->position == S_ON)
    {
      if (scan_id_p->curr_tplno > 0)
	{
	  scan_id_p->curr_offset -= QFILE_GET_PREV_TUPLE_LENGTH (scan_id_p->curr_tpl);
	  scan_id_p->curr_tpl -= QFILE_GET_PREV_TUPLE_LENGTH (scan_id_p->curr_tpl);
	  scan_id_p->curr_tplno--;
	  return S_SUCCESS;
	}
      else if (QFILE_GET_PREV_PAGE_ID (scan_id_p->curr_pgptr) != NULL_PAGEID)
	{
	  QFILE_GET_PREV_VPID (&prev_vpid, scan_id_p->curr_pgptr);
	  prev_page_p = qmgr_get_old_page (thread_p, &prev_vpid, scan_id_p->list_id.tfile_vfid);
	  if (prev_page_p == NULL)
	    {
	      return S_ERROR;
	    }

	  qmgr_free_old_page_and_init (thread_p, scan_id_p->curr_pgptr, scan_id_p->list_id.tfile_vfid);
	  QFILE_COPY_VPID (&scan_id_p->curr_vpid, &prev_vpid);
	  scan_id_p->curr_pgptr = prev_page_p;
	  scan_id_p->curr_tplno = QFILE_GET_TUPLE_COUNT (prev_page_p) - 1;
	  scan_id_p->curr_offset = QFILE_GET_LAST_TUPLE_OFFSET (prev_page_p);
	  scan_id_p->curr_tpl = (char *) scan_id_p->curr_pgptr + scan_id_p->curr_offset;
	  return S_SUCCESS;
	}
      else
	{
	  scan_id_p->position = S_BEFORE;
	  scan_id_p->curr_vpid.pageid = NULL_PAGEID;
	  qmgr_free_old_page_and_init (thread_p, scan_id_p->curr_pgptr, scan_id_p->list_id.tfile_vfid);
	  return S_END;
	}
    }
  else if (scan_id_p->position == S_AFTER)
    {

      if (VPID_ISNULL (&scan_id_p->list_id.first_vpid))
	{
	  return S_END;
	}
      page_p = qmgr_get_old_page (thread_p, &scan_id_p->list_id.last_vpid, scan_id_p->list_id.tfile_vfid);
      if (page_p == NULL)
	{
	  return S_ERROR;
	}

      scan_id_p->position = S_ON;
      QFILE_COPY_VPID (&scan_id_p->curr_vpid, &scan_id_p->list_id.last_vpid);
      scan_id_p->curr_pgptr = page_p;
      scan_id_p->curr_tplno = QFILE_GET_TUPLE_COUNT (page_p) - 1;
      scan_id_p->curr_offset = QFILE_GET_LAST_TUPLE_OFFSET (page_p);
      scan_id_p->curr_tpl = (char *) scan_id_p->curr_pgptr + scan_id_p->curr_offset;
      return S_SUCCESS;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
      return S_ERROR;
    }
}

/*
 * qfile_save_current_scan_tuple_position () -
 *   return:
 *   s_id(in): Scan identifier
 *   ls_tplpos(in/out): Set to contain current scan tuple position
 *
 * Note: Save current scan tuple position information.
 */
void
qfile_save_current_scan_tuple_position (QFILE_LIST_SCAN_ID * scan_id_p, QFILE_TUPLE_POSITION * tuple_position_p)
{
  tuple_position_p->status = scan_id_p->status;
  tuple_position_p->position = scan_id_p->position;
  tuple_position_p->vpid.pageid = scan_id_p->curr_vpid.pageid;
  tuple_position_p->vpid.volid = scan_id_p->curr_vpid.volid;
  tuple_position_p->offset = scan_id_p->curr_offset;
  tuple_position_p->tpl = scan_id_p->curr_tpl;
  tuple_position_p->tplno = scan_id_p->curr_tplno;
}

static SCAN_CODE
qfile_retrieve_tuple (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p, QFILE_TUPLE_RECORD * tuple_record_p,
		      int peek)
{
  int tuple_size;

  if (QFILE_GET_OVERFLOW_PAGE_ID (scan_id_p->curr_pgptr) == NULL_PAGEID)
    {
      if (peek)
	{
	  tuple_record_p->tpl = scan_id_p->curr_tpl;
	}
      else
	{
	  tuple_size = QFILE_GET_TUPLE_LENGTH (scan_id_p->curr_tpl);
	  if (tuple_record_p->size < tuple_size)
	    {
	      if (qfile_reallocate_tuple (tuple_record_p, tuple_size) != NO_ERROR)
		{
		  return S_ERROR;
		}
	    }
	  memcpy (tuple_record_p->tpl, scan_id_p->curr_tpl, tuple_size);
	}
    }
  else
    {
      /* tuple has overflow pages */
      if (peek)
	{
	  if (qfile_get_tuple_from_current_list (thread_p, scan_id_p, &scan_id_p->tplrec) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	  tuple_record_p->tpl = scan_id_p->tplrec.tpl;
	}
      else
	{
	  if (qfile_get_tuple_from_current_list (thread_p, scan_id_p, tuple_record_p) != NO_ERROR)
	    {
	      return S_ERROR;
	    }
	}
    }

  return S_SUCCESS;
}

/*
 * qfile_jump_scan_tuple_position() -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   s_id(in/out): Scan identifier
 *   ls_tplpos(in): Scan tuple position
 *   tplrec(in/out): Tuple record descriptor
 *   peek(in): Peek or Copy Tuple
 *
 * Note: Jump to the given list file scan position and fetch the tuple
 *       to the given tuple descriptor, either by peeking or by
 *       copying depending on the value of the peek parameter.
 *
 * Note: Saved scan can only be on "ON" or "AFTER" positions.
 */
SCAN_CODE
qfile_jump_scan_tuple_position (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p,
				QFILE_TUPLE_POSITION * tuple_position_p, QFILE_TUPLE_RECORD * tuple_record_p, int peek)
{
  PAGE_PTR page_p;

  if (tuple_position_p->position == S_ON)
    {
      if (scan_id_p->position == S_ON)
	{
	  if (scan_id_p->curr_vpid.pageid != tuple_position_p->vpid.pageid
	      || scan_id_p->curr_vpid.volid != tuple_position_p->vpid.volid)
	    {
	      page_p = qmgr_get_old_page (thread_p, &tuple_position_p->vpid, scan_id_p->list_id.tfile_vfid);
	      if (page_p == NULL)
		{
		  return S_ERROR;
		}

	      qmgr_free_old_page_and_init (thread_p, scan_id_p->curr_pgptr, scan_id_p->list_id.tfile_vfid);

	      QFILE_COPY_VPID (&scan_id_p->curr_vpid, &tuple_position_p->vpid);
	      scan_id_p->curr_pgptr = page_p;
	    }
	}
      else
	{
	  page_p = qmgr_get_old_page (thread_p, &tuple_position_p->vpid, scan_id_p->list_id.tfile_vfid);
	  if (page_p == NULL)
	    {
	      return S_ERROR;
	    }

	  QFILE_COPY_VPID (&scan_id_p->curr_vpid, &tuple_position_p->vpid);
	  scan_id_p->curr_pgptr = page_p;
	}
    }
  else
    {
      if (scan_id_p->position == S_ON)
	{
	  qmgr_free_old_page_and_init (thread_p, scan_id_p->curr_pgptr, scan_id_p->list_id.tfile_vfid);
	}
    }

  scan_id_p->status = tuple_position_p->status;
  scan_id_p->position = tuple_position_p->position;
  scan_id_p->curr_offset = tuple_position_p->offset;
  scan_id_p->curr_tpl = (char *) scan_id_p->curr_pgptr + scan_id_p->curr_offset;
  scan_id_p->curr_tplno = tuple_position_p->tplno;

  if (scan_id_p->position == S_ON)
    {
      return qfile_retrieve_tuple (thread_p, scan_id_p, tuple_record_p, peek);
    }
  else if (scan_id_p->position == S_BEFORE || scan_id_p->position == S_AFTER)
    {
      return S_END;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
      return S_ERROR;
    }
}

/*
 * qfile_start_scan_fix () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   s_id(in/out): Scan identifier
 *
 * Note: Start a scan operation which will keep the accessed list file
 *       pages fixed in the buffer pool. The routine starts the scan
 *       operation either from the beginning or from the last point
 *       scan fix ended.
 */
int
qfile_start_scan_fix (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p)
{
  if (scan_id_p->position == S_ON && !scan_id_p->curr_pgptr)
    {
      scan_id_p->curr_pgptr = qmgr_get_old_page (thread_p, &scan_id_p->curr_vpid, scan_id_p->list_id.tfile_vfid);
      if (scan_id_p->curr_pgptr == NULL)
	{
	  return ER_FAILED;
	}

      scan_id_p->curr_tpl = (char *) scan_id_p->curr_pgptr + scan_id_p->curr_offset;
    }
  else
    {
      scan_id_p->status = S_STARTED;
      scan_id_p->position = S_BEFORE;
    }

  return NO_ERROR;
}

/*
 * qfile_open_list_scan () -
 *   return: int (NO_ERROR or ER_FAILED)
 *   list_id(in): List identifier
 *   s_id(in/out): Scan identifier
 *
 * Note: A scan identifier is created to scan through the given list of tuples.
 */
int
qfile_open_list_scan (QFILE_LIST_ID * list_id_p, QFILE_LIST_SCAN_ID * scan_id_p)
{
  scan_id_p->status = S_OPENED;
  scan_id_p->position = S_BEFORE;
  scan_id_p->keep_page_on_finish = 0;
  scan_id_p->curr_vpid.pageid = NULL_PAGEID;
  scan_id_p->curr_vpid.volid = NULL_VOLID;
  QFILE_CLEAR_LIST_ID (&scan_id_p->list_id);

  if (qfile_copy_list_id (&scan_id_p->list_id, list_id_p, true) != NO_ERROR)
    {
      return ER_FAILED;
    }

  scan_id_p->tplrec.size = 0;
  scan_id_p->tplrec.tpl = NULL;

  return NO_ERROR;
}

/*
 * qfile_scan_list () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   s_id(in): Scan identifier
 *   tplrec(in/out): Tuple record descriptor
 *   peek(in): Peek or Copy Tuple
 *
 * Note: The regular LIST FILE scan is moved to the next scan tuple and
 *       the tuple is fetched to the tuple record descriptor. If there
 *       are no more scan items(tuples), S_END is returned. If
 *       an error occurs, S_ERROR is returned. If peek is true,
 *       the tplrec->tpl pointer is directly set to point to scan list
 *       file page, otherwise the tuple content is copied to the
 *       tplrec tuple area. If the area inside the tplrec is not enough
 *       it is reallocated by this routine.
 *
 * Note1: The pointer set by a PEEK operation is valid until another scan
 *        operation on the list file or until scan is closed.
 *
 * Note2: When the PEEK is specified, the area pointed by the tuple must not
 *        be modified by the caller.
 */
static SCAN_CODE
qfile_scan_list (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p,
		 SCAN_CODE (*scan_func) (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID *),
		 QFILE_TUPLE_RECORD * tuple_record_p, int peek)
{
  SCAN_CODE qp_scan;

  qp_scan = (*scan_func) (thread_p, scan_id_p);
  if (qp_scan == S_SUCCESS)
    {
      qp_scan = qfile_retrieve_tuple (thread_p, scan_id_p, tuple_record_p, peek);
    }

  return qp_scan;
}

/*
 * qfile_scan_list_next () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   s_id(in): Scan identifier
 *   tplrec(in/out): Tuple record descriptor
 *   peek(in): Peek or Copy Tuple
 */
SCAN_CODE
qfile_scan_list_next (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p, QFILE_TUPLE_RECORD * tuple_record_p,
		      int peek)
{
  return qfile_scan_list (thread_p, scan_id_p, qfile_scan_next, tuple_record_p, peek);
}

/*
 * qfile_scan_list_prev () -
 *   return: SCAN_CODE (S_SUCCESS, S_END, S_ERROR)
 *   s_id(in): Scan identifier
 *   tplrec(in/out): Tuple record descriptor
 *   peek(in): Peek or Copy Tuple
 */
SCAN_CODE
qfile_scan_list_prev (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p, QFILE_TUPLE_RECORD * tuple_record_p,
		      int peek)
{
  return qfile_scan_list (thread_p, scan_id_p, qfile_scan_prev, tuple_record_p, peek);
}

/*
 * qfile_end_scan_fix () -
 *   return:
 *   s_id(in/out)   : Scan identifier
 *
 * Note: End a scan fix operation by freeing the current scan page pointer.
 */
void
qfile_end_scan_fix (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p)
{
  if (scan_id_p->position == S_ON && scan_id_p->curr_pgptr)
    {
      qmgr_free_old_page_and_init (thread_p, scan_id_p->curr_pgptr, scan_id_p->list_id.tfile_vfid);
    }
  else
    {
      scan_id_p->status = S_ENDED;
      scan_id_p->position = S_AFTER;
    }
}

/*
 * qfile_close_scan () -
 *   return:
 *   s_id(in)   : Scan identifier
 *
 * Note: The scan identifier is closed and allocated areas and page
 *       buffers are freed.
 */
void
qfile_close_scan (THREAD_ENTRY * thread_p, QFILE_LIST_SCAN_ID * scan_id_p)
{
  if (scan_id_p->status == S_CLOSED)
    {
      return;
    }

  if ((scan_id_p->position == S_ON || (scan_id_p->position == S_AFTER && scan_id_p->keep_page_on_finish))
      && scan_id_p->curr_pgptr)
    {
      qmgr_free_old_page_and_init (thread_p, scan_id_p->curr_pgptr, scan_id_p->list_id.tfile_vfid);
    }

  if (scan_id_p->tplrec.tpl != NULL)
    {
      db_private_free_and_init (thread_p, scan_id_p->tplrec.tpl);
      scan_id_p->tplrec.size = 0;
    }

  qfile_clear_list_id (&scan_id_p->list_id);

  scan_id_p->status = S_CLOSED;
}

#if defined(SERVER_MODE)
/*
 * qfile_compare_tran_id () -
 *   return:
 *   t1(in)     :
 *   t2(in)     :
 */
static int
qfile_compare_tran_id (const void *t1, const void *t2)
{
  return *((int *) t1) - *((int *) t2);
}
#endif /* SERVER_MODE */

/*
 * qfile_hash_db_value_array () - Hash an array of DB_VALUE (DB_VALUE_ARRAY type)
 *   return:
 *   key(in)    :
 *   htsize(in) :
 */
static unsigned int
qfile_hash_db_value_array (const void *key, unsigned int htsize)
{
  unsigned int hash = 0;
  int i;
  const DB_VALUE_ARRAY *array = (DB_VALUE_ARRAY *) key;
  const DB_VALUE *val;

  if (key != NULL && array->size > 0)
    {
      for (i = 0, val = array->vals; i < array->size; i++, val++)
	{
	  hash |= mht_valhash (val, htsize);
	  hash <<= 8;
	}
      hash |= array->size;
    }

  return (hash % htsize);
}

/*
 * qfile_compare_equal_db_value_array () - Compare two arrays of DB_VALUE
 *                                   (DB_VALUE_ARRAY type)
 *   return:
 *   key1(in)   :
 *   key2(in)   :
 */
static int
qfile_compare_equal_db_value_array (const void *key1, const void *key2)
{
  int i;
  const DB_VALUE_ARRAY *array1 = (DB_VALUE_ARRAY *) key1, *array2 = (DB_VALUE_ARRAY *) key2;
  const DB_VALUE *val1, *val2;

  if (key1 == key2 || (array1->size == 0 && array2->size == 0))
    {
      return true;
    }

  if (key1 == NULL || key2 == NULL || array1->size != array2->size)
    {
      return false;
    }

  for (i = 0, val1 = array1->vals, val2 = array2->vals; i < array1->size; i += 3, val1 += 3, val2 += 3)
    {
      if (mht_compare_dbvalues_are_equal (val1, val2) == false)
	{
	  return false;
	}
    }

  for (i = 1, val1 = array1->vals + 1, val2 = array2->vals + 1; i < array1->size; i += 3, val1 += 3, val2 += 3)
    {
      if (mht_compare_dbvalues_are_equal (val1, val2) == false)
	{
	  return false;
	}
    }

  for (i = 2, val1 = array1->vals + 2, val2 = array2->vals + 2; i < array1->size; i += 3, val1 += 3, val2 += 3)
    {
      if (mht_compare_dbvalues_are_equal (val1, val2) == false)
	{
	  return false;
	}
    }

  return true;
}

/*
 * qfile_initialize_list_cache () - Initialize list cache
 *   return:
 */
int
qfile_initialize_list_cache (THREAD_ENTRY * thread_p)
{
  unsigned int i;
  int n;
  QFILE_POOLED_LIST_CACHE_ENTRY *pent;

  if (QFILE_IS_LIST_CACHE_DISABLED)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_LIST_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* array of memory hash tables for list cache; pool for list_ht of QFILE_LIST_CACHE_ENTRY */
  if (qfile_List_cache.list_hts)
    {
      /* if the hash table already exist, clear it out */
      for (i = 0; i < qfile_List_cache.n_hts; i++)
	{
	  (void) mht_map_no_key (thread_p, qfile_List_cache.list_hts[i], qfile_free_list_cache_entry,
				 qfile_List_cache.list_hts[i]);
	  (void) mht_clear (qfile_List_cache.list_hts[i], NULL, NULL);
	  qfile_List_cache.free_ht_list[i] = i + 1;
	}
      qfile_List_cache.free_ht_list[i - 1] = -1;
    }
  else
    {
      /* create */
      qfile_List_cache.n_hts = prm_get_integer_value (PRM_ID_XASL_CACHE_MAX_ENTRIES) + 10;
      qfile_List_cache.list_hts = (MHT_TABLE **) calloc (qfile_List_cache.n_hts, sizeof (MHT_TABLE *));
      qfile_List_cache.free_ht_list = (int *) calloc (qfile_List_cache.n_hts, sizeof (int));
      if (qfile_List_cache.list_hts == NULL)
	{
	  goto error;
	}

      for (i = 0; i < qfile_List_cache.n_hts; i++)
	{
	  qfile_List_cache.list_hts[i] =
	    mht_create ("list file cache (DB_VALUE list)", qfile_List_cache.n_hts,
			qfile_hash_db_value_array, qfile_compare_equal_db_value_array);
	  qfile_List_cache.free_ht_list[i] = i + 1;
	  if (qfile_List_cache.list_hts[i] == NULL)
	    {
	      goto error;
	    }
	}
      qfile_List_cache.free_ht_list[i - 1] = -1;
    }

  qfile_List_cache.n_entries = 0;
  qfile_List_cache.n_pages = 0;
  qfile_List_cache.lookup_counter = 0;
  qfile_List_cache.hit_counter = 0;
  qfile_List_cache.miss_counter = 0;
  qfile_List_cache.full_counter = 0;

  /* list cache entry pool */
  if (qfile_List_cache_entry_pool.pool)
    {
      free_and_init (qfile_List_cache_entry_pool.pool);
    }

  qfile_List_cache_entry_pool.n_entries = prm_get_integer_value (PRM_ID_LIST_MAX_QUERY_CACHE_ENTRIES) + 10;
  qfile_List_cache_entry_pool.pool =
    (QFILE_POOLED_LIST_CACHE_ENTRY *) calloc (qfile_List_cache_entry_pool.n_entries,
					      sizeof (QFILE_POOLED_LIST_CACHE_ENTRY));
  if (qfile_List_cache_entry_pool.pool == NULL)
    {
      goto error;
    }

  qfile_List_cache_entry_pool.free_list = 0;
  for (pent = qfile_List_cache_entry_pool.pool, n = 0; pent && n < qfile_List_cache_entry_pool.n_entries - 1;
       pent++, n++)
    {
      pent->s.next = n + 1;
    }

  if (pent != NULL)
    {
      pent->s.next = -1;
    }

  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);

  return NO_ERROR;

error:
  if (qfile_List_cache_entry_pool.pool)
    {
      free_and_init (qfile_List_cache_entry_pool.pool);
    }
  qfile_List_cache_entry_pool.n_entries = 0;
  qfile_List_cache_entry_pool.free_list = -1;

  if (qfile_List_cache.list_hts)
    {
      for (i = 0; i < qfile_List_cache.n_hts && qfile_List_cache.list_hts[i]; i++)
	{
	  mht_destroy (qfile_List_cache.list_hts[i]);
	}
      free_and_init (qfile_List_cache.list_hts);
      free_and_init (qfile_List_cache.free_ht_list);
    }
  qfile_List_cache.n_hts = 0;

  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);

  return ER_FAILED;
}

/*
 * qfile_finalize_list_cache () - Finalize list cache
 *   return:
 */
int
qfile_finalize_list_cache (THREAD_ENTRY * thread_p)
{
  unsigned int i;

  if (QFILE_IS_LIST_CACHE_DISABLED)
    {
      return NO_ERROR;
    }

  if (csect_enter (thread_p, CSECT_QPROC_LIST_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* array of memory hash tables for list cache; pool for list_ht of QFILE_LIST_CACHE_ENTRY */
  if (qfile_List_cache.list_hts)
    {
      for (i = 0; i < qfile_List_cache.n_hts; i++)
	{
	  bool invalidate = true;
	  (void) mht_map_no_key (thread_p, qfile_List_cache.list_hts[i], qfile_end_use_of_list_cache_entry_local,
				 &invalidate);
	  mht_destroy (qfile_List_cache.list_hts[i]);
	}
      free_and_init (qfile_List_cache.list_hts);
    }

  if (qfile_List_cache.free_ht_list)
    {
      free_and_init (qfile_List_cache.free_ht_list);
    }

  /* list cache entry pool */
  if (qfile_List_cache_entry_pool.pool)
    {
      free_and_init (qfile_List_cache_entry_pool.pool);
    }

  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);

  return NO_ERROR;
}

/*
 * qfile_clear_list_cache () - Clear out list cache hash table
 *   return:
 *   list_ht_no(in)     :
 */
int
qfile_clear_list_cache (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xcache_entry, bool invalidate)
{
  int rc;
  int cnt;
  int list_ht_no;

  if (csect_enter (thread_p, CSECT_QPROC_LIST_CACHE, INF_WAIT) != NO_ERROR)
    {
      goto end;
    }

  if (QFILE_IS_LIST_CACHE_DISABLED || xcache_entry->list_ht_no < 0)
    {
      goto end;
    }

  if (qfile_List_cache.n_hts == 0)
    {
      goto end;
    }

  list_ht_no = xcache_entry->list_ht_no;

  if (qfile_get_list_cache_number_of_entries (list_ht_no) == 0)
    {
      /* if no entries, to invalidate free the entry here */
      if (invalidate)
	{
	  xcache_entry->list_ht_no = -1;
	  qcache_free_ht_no (thread_p, list_ht_no);
	}
      goto end;
    }

  cnt = 0;
  do
    {
      rc =
	mht_map_no_key (thread_p, qfile_List_cache.list_hts[list_ht_no], qfile_end_use_of_list_cache_entry_local,
			&invalidate);
      if (rc != NO_ERROR)
	{
	  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);
	  thread_sleep (10);	/* 10 msec */
	  if (csect_enter (thread_p, CSECT_QPROC_LIST_CACHE, INF_WAIT) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	}
    }
  while (rc != NO_ERROR && cnt++ < 10);

  if (rc != NO_ERROR)
    {
      /* unhappy condition; if this happens, memory leak will occurrs */
      er_log_debug (ARG_FILE_LINE, "ls_clear_list_cache: failed to delete all entries\n");
    }

end:
  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);

  return NO_ERROR;
}

/*
 * qfile_allocate_list_cache_entry () - Allocate the entry or get one from the pool
 *   return:
 *   req_size(in)       :
 */
static QFILE_LIST_CACHE_ENTRY *
qfile_allocate_list_cache_entry (int req_size)
{
  /* this function should be called within CSECT_QPROC_LIST_CACHE */
  QFILE_POOLED_LIST_CACHE_ENTRY *pent = NULL;

  if (req_size > RESERVED_SIZE_FOR_LIST_CACHE_ENTRY || qfile_List_cache_entry_pool.free_list == -1)
    {
      /* malloc from the heap if required memory size is bigger than reserved, or the pool is exhausted */
      pent = (QFILE_POOLED_LIST_CACHE_ENTRY *) malloc (req_size + ADDITION_FOR_POOLED_LIST_CACHE_ENTRY);
      if (pent != NULL)
	{
	  /* mark as to be freed rather than returning back to the pool */
	  pent->s.next = -2;
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE, "ls_alloc_list_cache_ent: allocation failed\n");
	}
    }
  else
    {
      /* get one from the pool */
      assert ((qfile_List_cache_entry_pool.free_list <= qfile_List_cache_entry_pool.n_entries)
	      && (qfile_List_cache_entry_pool.free_list >= 0));
      pent = &qfile_List_cache_entry_pool.pool[qfile_List_cache_entry_pool.free_list];

      assert (pent->s.next <= qfile_List_cache_entry_pool.n_entries && pent->s.next >= -1);
      qfile_List_cache_entry_pool.free_list = pent->s.next;
      pent->s.next = -1;
    }

  /* initialize */
  if (pent)
    {
      assert (sizeof (pent->s.entry) >= sizeof (QFILE_LIST_CACHE_ENTRY));

      (void) memset ((void *) &pent->s.entry, 0, sizeof (QFILE_LIST_CACHE_ENTRY));
    }
  else
    {
      return NULL;
    }

  return &pent->s.entry;
}

/*
 * qfile_free_list_cache_entry () -  Remove the entry from the hash and free it
 *                              Can be used by mht_map_no_key() function
 *   return:
 *   data(in)   :
 *   args(in)   :
 */
static int
qfile_free_list_cache_entry (THREAD_ENTRY * thread_p, void *data, void *args)
{
  /* this function should be called within CSECT_QPROC_LIST_CACHE */
  QFILE_POOLED_LIST_CACHE_ENTRY *pent;
  QFILE_LIST_CACHE_ENTRY *lent = (QFILE_LIST_CACHE_ENTRY *) data;
  HL_HEAPID old_pri_heap_id;
  int i;
#if !defined (NDEBUG)
  int idx;
#endif

  if (data == NULL)
    {
      return ER_FAILED;
    }

  /*
   * Clear out parameter values. (DB_VALUE containers)
   * Remind that the parameter values are cloned in global heap context(0)
   */
  old_pri_heap_id = db_change_private_heap (thread_p, 0);
  for (i = 0; i < lent->param_values.size; i++)
    {
      (void) pr_clear_value (&lent->param_values.vals[i]);
    }
  (void) db_change_private_heap (thread_p, old_pri_heap_id);

  /* if this entry is from the pool return it, else free it */
  pent = POOLED_LIST_CACHE_ENTRY_FROM_LIST_CACHE_ENTRY (lent);
  if (pent->s.next == -2)
    {
      free_and_init (pent);
    }
  else
    {
      /* return it back to the pool */
      (void) memset (&pent->s.entry, 0, sizeof (QFILE_LIST_CACHE_ENTRY));
      pent->s.next = qfile_List_cache_entry_pool.free_list;

#if !defined (NDEBUG)
      idx = (int) (pent - qfile_List_cache_entry_pool.pool);
      assert (idx <= qfile_List_cache_entry_pool.n_entries && idx >= 0);
#endif

      qfile_List_cache_entry_pool.free_list = CAST_BUFLEN (pent - qfile_List_cache_entry_pool.pool);
    }

  return NO_ERROR;
}

/*
 * qfile_print_list_cache_entry () - Print the entry
 *                              Will be used by mht_dump() function
 *   return:
 *   fp(in)     :
 *   key(in)    :
 *   data(in)   :
 *   args(in)   :
 */
static int
qfile_print_list_cache_entry (THREAD_ENTRY * thread_p, FILE * fp, const void *key, void *data, void *args)
{
  QFILE_LIST_CACHE_ENTRY *ent = (QFILE_LIST_CACHE_ENTRY *) data;
  int i;
  char str[20];
  TP_DOMAIN **d;
  time_t tmp_time;
  struct tm *c_time_struct, tm_val;

  if (!ent)
    {
      return false;
    }
  if (fp == NULL)
    {
      fp = stdout;
    }

  fprintf (fp, "LIST_CACHE_ENTRY (%p) {\n", data);
  fprintf (fp, "  param_values = [");

  for (i = 0; i < ent->param_values.size; i++)
    {
      fprintf (fp, " ");
      db_fprint_value (fp, &ent->param_values.vals[i]);
    }

  fprintf (fp, " ]\n");
  fprintf (fp, "  list_id = { type_list { %d", ent->list_id.type_list.type_cnt);

  for (i = 0, d = ent->list_id.type_list.domp; i < ent->list_id.type_list.type_cnt && d && *d; i++, d++)
    {
      fprintf (fp, " %s/%d", (*d)->type->name, TP_DOMAIN_TYPE ((*d)));
    }

  fprintf (fp,
	   " } tuple_cnt %d page_cnt %d first_vpid { %d %d } last_vpid { %d %d } lasttpl_len %d query_id %lld  "
	   " temp_vfid { %d %d } }\n", ent->list_id.tuple_cnt, ent->list_id.page_cnt, ent->list_id.first_vpid.pageid,
	   ent->list_id.first_vpid.volid, ent->list_id.last_vpid.pageid, ent->list_id.last_vpid.volid,
	   ent->list_id.lasttpl_len, (long long) ent->list_id.query_id, ent->list_id.temp_vfid.fileid,
	   ent->list_id.temp_vfid.volid);

#if defined(SERVER_MODE)
  fprintf (fp, "  tran_isolation = %d\n", ent->tran_isolation);
  fprintf (fp, "  tran_index_array = [");

  for (i = 0; (unsigned int) i < ent->last_ta_idx; i++)
    {
      fprintf (fp, " %d", ent->tran_index_array[i]);
    }

  fprintf (fp, " ]\n");
  fprintf (fp, "  last_ta_idx = %lld\n", (long long) ent->last_ta_idx);
#endif /* SERVER_MODE */

  fprintf (fp, "  query_string = %s\n", ent->query_string);

  tmp_time = ent->time_created.tv_sec;
  c_time_struct = localtime_r (&tmp_time, &tm_val);
  if (c_time_struct == NULL)
    {
      fprintf (fp, "  ent->time_created.tv_sec is invalid (%ld)\n", ent->time_last_used.tv_sec);
    }
  else
    {
      (void) strftime (str, sizeof (str), "%x %X", c_time_struct);
      fprintf (fp, "  time_created = %s.%d\n", str, (int) ent->time_created.tv_usec);
    }

  tmp_time = ent->time_last_used.tv_sec;
  c_time_struct = localtime_r (&tmp_time, &tm_val);
  if (c_time_struct == NULL)
    {
      fprintf (fp, "  ent->time_last_used.tv_sec is invalid (%ld)\n", ent->time_last_used.tv_sec);
    }
  else
    {
      (void) strftime (str, sizeof (str), "%x %X", c_time_struct);
      fprintf (fp, "  time_last_used = %s.%d\n", str, (int) ent->time_last_used.tv_usec);

      fprintf (fp, "  ref_count = %d\n", ent->ref_count);
      fprintf (fp, "  deletion_marker = %s\n", (ent->deletion_marker) ? "true" : "false");
      fprintf (fp, "  invalidate = %s\n", (ent->invalidate) ? "true" : "false");
      fprintf (fp, "}\n");
    }

  return true;
}

/*
 * qfile_dump_list_cache_internal () -
 *   return:
 *   fp(in)     :
 */
int
qfile_dump_list_cache_internal (THREAD_ENTRY * thread_p, FILE * fp)
{
  unsigned int i;

  if (csect_enter (thread_p, CSECT_QPROC_LIST_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (!fp)
    {
      fp = stdout;
    }

  fprintf (fp,
	   "LIST_CACHE {\n  n_hts %d\n  n_entries %d  n_pages %d\n"
	   "  lookup_counter %d\n  hit_counter %d\n  miss_counter %d\n  full_counter %d\n}\n",
	   qfile_List_cache.n_hts, qfile_List_cache.n_entries, qfile_List_cache.n_pages,
	   qfile_List_cache.lookup_counter, qfile_List_cache.hit_counter, qfile_List_cache.miss_counter,
	   qfile_List_cache.full_counter);

  for (i = 0; i < qfile_List_cache.n_hts; i++)
    {
      if (mht_count (qfile_List_cache.list_hts[i]) > 0)
	{
	  fprintf (fp, "\nlist_hts[%d] %p\n", i, (void *) qfile_List_cache.list_hts[i]);
	  (void) mht_dump (thread_p, fp, qfile_List_cache.list_hts[i], true, qfile_print_list_cache_entry, NULL);
	}
    }

  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);

  return NO_ERROR;
}

#if defined (CUBRID_DEBUG)
/*
 * qfile_dump_list_cache () -
 *   return:
 *   fname(in)  :
 */
int
qfile_dump_list_cache (THREAD_ENTRY * thread_p, const char *fname)
{
  int rc;
  FILE *fp;

  if (QFILE_IS_LIST_CACHE_DISABLED)
    {
      return ER_FAILED;
    }

  if (qfile_List_cache.n_hts == 0 || !qfile_List_cache.list_hts)
    {
      return ER_FAILED;
    }

  fp = (fname) ? fopen (fname, "a") : stdout;
  if (!fp)
    {
      fp = stdout;
    }

  rc = qfile_dump_list_cache_internal (thread_p, fp);

  if (fp != stdout)
    {
      fclose (fp);
    }

  return rc;
}
#endif

/*
 * qfile_delete_list_cache_entry () - Delete a list cache entry
 *                               Can be used by mht_map_no_key() function
 *   return:
 *   data(in/out)   :
 *   args(in)   :
 */
static int
qfile_delete_list_cache_entry (THREAD_ENTRY * thread_p, void *data)
{
  /* this function should be called within CSECT_QPROC_LIST_CACHE */
  QFILE_LIST_CACHE_ENTRY *lent = (QFILE_LIST_CACHE_ENTRY *) data;
  int error_code = ER_FAILED;
  bool invalidate;
  int ht_no;

  if (data == NULL || lent->list_ht_no < 0)
    {
      return ER_FAILED;
    }

  invalidate = lent->invalidate;
  ht_no = lent->list_ht_no;

  /* update counter */
  qfile_List_cache.n_entries--;
  qfile_List_cache.n_pages -= lent->list_id.page_cnt;

  /* remove the entry from the hash table */
  if (mht_rem2 (qfile_List_cache.list_hts[lent->list_ht_no], &lent->param_values, lent, NULL, NULL) != NO_ERROR)
    {
      if (!lent->deletion_marker)
	{
	  char *s = NULL;

	  if (lent->param_values.size > 0)
	    {
	      s = pr_valstring (&lent->param_values.vals[0]);
	    }

	  er_log_debug (ARG_FILE_LINE,
			"ls_delete_list_cache_ent: mht_rem failed for param_values { %d %s ...}\n",
			lent->param_values.size, s ? s : "(null)");
	  if (s)
	    {
	      db_private_free (thread_p, s);
	    }
	}
    }

  /* destroy the temp file of XASL_ID */
  if (!VFID_ISNULL (&lent->list_id.temp_vfid)
      && file_temp_retire_preserved (thread_p, &lent->list_id.temp_vfid) != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "ls_delete_list_cache_ent: fl_destroy failed for vfid { %d %d }\n",
		    lent->list_id.temp_vfid.fileid, lent->list_id.temp_vfid.volid);
    }

  /* clear list_id */
  qfile_update_qlist_count (thread_p, &lent->list_id, 1);
  qfile_clear_list_id (&lent->list_id);

  /* to check if it's the last list cache entry of the hash table */
  if (invalidate && qfile_get_list_cache_number_of_entries (ht_no) == 0)
    {
      /* this hash table has no entries and invalidated
       * it needs to free
       */
      lent->xcache_entry->list_ht_no = -1;
      qcache_free_ht_no (thread_p, ht_no);
    }

  error_code = qfile_free_list_cache_entry (thread_p, lent, NULL);

  return error_code;
}

/*
 * qfile_end_use_of_list_cache_entry_local ()
 *   return:
 *   data(in)   : a list cached entry
 *   args(in)   : invalidate is true if the xcache entry is erased
 */
static int
qfile_end_use_of_list_cache_entry_local (THREAD_ENTRY * thread_p, void *data, void *args)
{
  QFILE_LIST_CACHE_ENTRY *lent = (QFILE_LIST_CACHE_ENTRY *) data;

  if (lent == NULL)
    {
      return ER_FAILED;
    }

  if (lent->invalidate == false)
    {
      lent->invalidate = *((bool *) args);
    }

  return qfile_end_use_of_list_cache_entry (thread_p, lent, true);
}

/*
 * qfile_lookup_list_cache_entry () - Lookup the list cache with the parameter
 * values (DB_VALUE array) bound to the query
 *   return:
 *   list_ht_no(in)     :
 *   params(in) :
 *
 * Note: Look up the hash table to get the cached result with the parameter
 *       values as the key.
 */
QFILE_LIST_CACHE_ENTRY *
qfile_lookup_list_cache_entry (THREAD_ENTRY * thread_p, XASL_CACHE_ENTRY * xasl, const DB_VALUE_ARRAY * params,
			       bool * result_cached)
{
  QFILE_LIST_CACHE_ENTRY *lent;
  int tran_index;
#if defined(SERVER_MODE)
  TRAN_ISOLATION tran_isolation;
#if defined(WINDOWS)
  unsigned int num_elements;
#else
  size_t num_elements;
#endif
#endif /* SERVER_MODE */
#if defined (SERVER_MODE) && !defined (NDEBUG)
  size_t i_idx, num_active_users;
#endif

  bool new_tran = true;

  *result_cached = false;

  if (QFILE_IS_LIST_CACHE_DISABLED)
    {
      return NULL;
    }

  if (qfile_List_cache.n_hts == 0)
    {
      return NULL;
    }

  if (csect_enter (thread_p, CSECT_QPROC_LIST_CACHE, INF_WAIT) != NO_ERROR)
    {
      return NULL;
    }

  if (xasl->list_ht_no < 0)
    {
      if ((xasl->list_ht_no = qcache_get_new_ht_no (thread_p)) < 0)
	{
	  goto end;
	}
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  /* look up the hash table with the key */
  lent = (QFILE_LIST_CACHE_ENTRY *) mht_get (qfile_List_cache.list_hts[xasl->list_ht_no], params);
  qfile_List_cache.lookup_counter++;	/* counter */

  if (lent)
    {
      unsigned int i;

      /* check if it is marked to be deleted */
      if (lent->deletion_marker)
	{
#if defined(SERVER_MODE)
	  if (lent->last_ta_idx == 0)
#endif
	    {
	      qfile_delete_list_cache_entry (thread_p, lent);
	    }
	  goto end;
	}

      *result_cached = true;

#if defined(SERVER_MODE)
      /* check if the cache is owned by me */
      for (i = 0; i < lent->last_ta_idx; i++)
	{
	  if (lent->tran_index_array[i] == tran_index)
	    {
	      new_tran = false;
	      break;
	    }
	}

      /* finally, we found an useful cache entry to reuse */
      if (new_tran)
	{
	  /* record my transaction id into the entry and adjust timestamp and reference counter */
	  lent->uncommitted_marker = true;
	  if (lent->last_ta_idx < (size_t) MAX_NTRANS)
	    {
	      num_elements = (int) lent->last_ta_idx;
	      (void) lsearch (&tran_index, lent->tran_index_array, &num_elements, sizeof (int), qfile_compare_tran_id);
	      lent->last_ta_idx = num_elements;
	    }
#if !defined (NDEBUG)
	  for (i_idx = 0, num_active_users = 0; i_idx < lent->last_ta_idx; i_idx++)
	    {
	      if (lent->tran_index_array[i_idx] > 0)
		{
		  num_active_users++;
		}
	    }
	  assert (lent->last_ta_idx == num_active_users);
#endif
	}
#endif /* SERVER_MODE */

      (void) gettimeofday (&lent->time_last_used, NULL);
      lent->ref_count++;
    }

end:

  if (*result_cached)
    {
      qfile_List_cache.hit_counter++;	/* counter */
    }
  else
    {
      qfile_List_cache.miss_counter++;	/* counter */
    }

  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);

  if (*result_cached)
    {
      return lent;
    }

  return NULL;
}

static bool
qfile_is_early_time (struct timeval *a, struct timeval *b)
{
  if (a->tv_sec == b->tv_sec)
    {
      return a->tv_usec < b->tv_usec;
    }
  else
    {
      return a->tv_sec < b->tv_sec;
    }
}

/*
 * qfile_update_list_cache_entry () - Update list cache entry if exist or create new
 *                               one
 *   return:
 *   list_ht_no_ptr(in/out) :
 *   params(in) :
 *   list_id(in)        :
 *   query_string(in)   :
 *
 * Note: Put the query result into the proper hash table with the key of
 *       the parameter values (DB_VALUE array) and the data of LIST ID.
 *       If there already exists the entry with the same key, update its data.
 *       As a side effect, the given 'list_hash_no' will be change if it was -1.
 */
QFILE_LIST_CACHE_ENTRY *
qfile_update_list_cache_entry (THREAD_ENTRY * thread_p, int list_ht_no, const DB_VALUE_ARRAY * params,
			       const QFILE_LIST_ID * list_id, XASL_CACHE_ENTRY * xasl)
{
  QFILE_LIST_CACHE_ENTRY *lent, *old, **p, **q, **r;
  MHT_TABLE *ht;
  int tran_index;
#if defined(SERVER_MODE)
  TRAN_ISOLATION tran_isolation;
#if defined(WINDOWS)
  unsigned int num_elements;
#else
  size_t num_elements;
#endif
#if !defined (NDEBUG)
  size_t i_idx, num_active_users;
#endif
#endif /* SERVER_MODE */
  unsigned int n;
  HL_HEAPID old_pri_heap_id;
  int i, j, k;
  int alloc_size;

  if (QFILE_IS_LIST_CACHE_DISABLED)
    {
      return NULL;
    }
  if (qfile_List_cache.n_hts == 0)
    {
      return NULL;
    }

  if (csect_enter (thread_p, CSECT_QPROC_LIST_CACHE, INF_WAIT) != NO_ERROR)
    {
      return NULL;
    }

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
#if defined(SERVER_MODE)
  tran_isolation = logtb_find_isolation (tran_index);
#endif /* SERVER_MODE */

  /*
   * The other competing thread which is running the same query
   * already updated this entry after that this and the thread had failed
   * to find the query in the cache.
   * The other case is that the query could be marked not to look up
   * the cache but update the cache with its result.
   * Then, try to delete the previous one and insert new one.
   * If fail to delete, leave the cache without touch.
   */

  ht = qfile_List_cache.list_hts[list_ht_no];
  do
    {
      /* check again whether the entry is in the cache */
      lent = (QFILE_LIST_CACHE_ENTRY *) mht_get (ht, params);
      if (lent == NULL)
	{
	  break;
	}

#if defined(SERVER_MODE)
      /* check in-use by other transaction */
      if (lent->last_ta_idx > 0)
	{
	  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);
	  return lent;
	}

      /* the entry that is in the cache is same with mine; do not duplicate the cache entry */
      /* record my transaction id into the entry and adjust timestamp and reference counter */
      if (lent->last_ta_idx < (size_t) MAX_NTRANS)
	{
	  num_elements = (int) lent->last_ta_idx;
	  (void) lsearch (&tran_index, lent->tran_index_array, &num_elements, sizeof (int), qfile_compare_tran_id);
	  lent->last_ta_idx = num_elements;
	}

#if !defined (NDEBUG)
      for (i_idx = 0, num_active_users = 0; i_idx < lent->last_ta_idx; i_idx++)
	{
	  if (lent->tran_index_array[i_idx] > 0)
	    {
	      num_active_users++;
	    }
	}

      assert (lent->last_ta_idx == num_active_users);
#endif

      (void) gettimeofday (&lent->time_last_used, NULL);
      lent->ref_count++;

#endif /* SERVER_MODE */
    }
  while (0);

  /* return with the one from the cache */
  if (lent != NULL)
    {
      goto end;
    }

#if defined(SERVER_MODE)
  /* check the number of list cache entries */
  if (qfile_List_cache.n_entries >= prm_get_integer_value (PRM_ID_LIST_MAX_QUERY_CACHE_ENTRIES)
      || qfile_List_cache.n_pages >= prm_get_integer_value (PRM_ID_LIST_MAX_QUERY_CACHE_PAGES))
    {
      if (qfile_list_cache_cleanup (thread_p) != NO_ERROR)
	{
	  goto end;
	}
    }
#endif

  /* make new QFILE_LIST_CACHE_ENTRY */

  /* get new entry from the QFILE_LIST_CACHE_ENTRY_POOL */
  alloc_size = qfile_get_list_cache_entry_size_for_allocate (params->size);
  lent = qfile_allocate_list_cache_entry (alloc_size);
  if (lent == NULL)
    {
      goto end;
    }

  lent->list_ht_no = list_ht_no;
#if defined(SERVER_MODE)
  lent->uncommitted_marker = true;
  lent->tran_isolation = tran_isolation;
  lent->last_ta_idx = 0;
  lent->tran_index_array =
    (int *) memset (qfile_get_list_cache_entry_tran_index_array (lent), 0, MAX_NTRANS * sizeof (int));
#endif /* SERVER_MODE */
  lent->param_values.size = params->size;
  lent->param_values.vals = qfile_get_list_cache_entry_param_values (lent);

  /*
   * Copy parameter values. (DB_VALUE containers)
   * Changing private heap to the global one (0, malloc/free) is
   * needed because cloned db values last beyond request processing time
   * boundary.
   */
  old_pri_heap_id = db_change_private_heap (thread_p, 0);
  for (i = 0; i < lent->param_values.size; i++)
    {
      /* (void) pr_clear_value(&(lent->param_values.vals[i])); */
      (void) pr_clone_value (&params->vals[i], &lent->param_values.vals[i]);
    }
  (void) db_change_private_heap (thread_p, old_pri_heap_id);

  /* copy the QFILE_LIST_ID */
  if (qfile_copy_list_id (&lent->list_id, list_id, false) != NO_ERROR)
    {
      qfile_delete_list_cache_entry (thread_p, lent);
      lent = NULL;
      goto end;
    }
  lent->list_id.tfile_vfid = NULL;
  lent->query_string = xasl->sql_info.sql_hash_text;
  (void) gettimeofday (&lent->time_created, NULL);
  (void) gettimeofday (&lent->time_last_used, NULL);
  lent->ref_count = 0;
  lent->deletion_marker = false;
  lent->invalidate = false;
  lent->xcache_entry = xasl;

  /* record my transaction id into the entry */
#if defined(SERVER_MODE)
  if (lent->last_ta_idx < (size_t) MAX_NTRANS)
    {
      num_elements = (int) lent->last_ta_idx;
      (void) lsearch (&tran_index, lent->tran_index_array, &num_elements, sizeof (int), qfile_compare_tran_id);
      lent->last_ta_idx = num_elements;
    }

#if !defined (NDEBUG)
  for (i_idx = 0, num_active_users = 0; i_idx < lent->last_ta_idx; i_idx++)
    {
      if (lent->tran_index_array[i_idx] > 0)
	{
	  num_active_users++;
	}
    }

  assert (lent->last_ta_idx == num_active_users);
#endif

#endif /* SERVER_MODE */

  /* insert (or update) the entry into the hash table */
  if (mht_put_new (ht, &lent->param_values, lent) == NULL)
    {
      char *s;

      s = ((lent->param_values.size > 0) ? pr_valstring (&lent->param_values.vals[0]) : NULL);
      er_log_debug (ARG_FILE_LINE, "ls_update_list_cache_ent: mht_rem failed for param_values { %d %s ...}\n",
		    lent->param_values.size, s ? s : "(null)");
      if (s)
	{
	  db_private_free (thread_p, s);
	}
      qfile_delete_list_cache_entry (thread_p, lent);
      lent = NULL;
      goto end;
    }

  /* update counter */
  qfile_List_cache.n_entries++;
  qfile_List_cache.n_pages += lent->list_id.page_cnt;

end:
  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);

  return lent;
}

/*
 * qfile_end_use_of_list_cache_entry () - End use of list cache entry
 *   return:
 *   lent(in/out)   :
 *   marker(in) :
 */
int
qfile_end_use_of_list_cache_entry (THREAD_ENTRY * thread_p, QFILE_LIST_CACHE_ENTRY * lent, bool marker)
{
  int tran_index;
  bool invalidate = false;
#if defined(SERVER_MODE)
  int *p, *r;
#if defined(WINDOWS)
  unsigned int num_elements;
#else
  size_t num_elements;
#endif
#endif /* SERVER_MODE */
#if defined (SERVER_MODE) && !defined (NDEBUG)
  size_t i_idx, num_active_users;
#endif

  if (QFILE_IS_LIST_CACHE_DISABLED)
    {
      return ER_FAILED;
    }
  if (lent == NULL || qfile_List_cache.n_hts == 0)
    {
      return ER_FAILED;
    }

  if (csect_enter (thread_p, CSECT_QPROC_LIST_CACHE, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

#if defined(SERVER_MODE)
  /* remove my transaction id from the entry and do compaction */
  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  r = &lent->tran_index_array[lent->last_ta_idx];
  do
    {
      /* find my tran_id */
      num_elements = (int) lent->last_ta_idx;
      p = (int *) lfind (&tran_index, lent->tran_index_array, &num_elements, sizeof (int), qfile_compare_tran_id);
      lent->last_ta_idx = num_elements;
      if (p)
	{
	  *p = 0;
	  /* do compaction */
	  if (p + 1 < r)
	    {
	      (void) memcpy (p, p + 1, sizeof (int) * (r - p - 1));
	    }
	  lent->last_ta_idx--;
	  r--;
	}
    }
  while (p && p < r);

  if (lent->last_ta_idx == 0)
    {
      lent->uncommitted_marker = false;
    }

#if !defined (NDEBUG)
  for (i_idx = 0, num_active_users = 0; i_idx < lent->last_ta_idx; i_idx++)
    {
      if (lent->tran_index_array[i_idx] > 0)
	{
	  num_active_users++;
	}
    }

  assert (lent->last_ta_idx == num_active_users);
#endif

#endif /* SERVER_MODE */

  /* if this entry will be deleted */
#if defined(SERVER_MODE)
  if (lent->last_ta_idx == 0)
#endif
    {
      /* to check if it's the last transaction using the lent */
      if (marker || lent->deletion_marker)
	{
	  qfile_delete_list_cache_entry (thread_p, lent);
	}
    }
#if defined(SERVER_MODE)
  else
    {
      /* to avoid resetting the deletion_marker
       * that has already been set by other transaction */
      if (lent->deletion_marker == false)
	{
	  lent->deletion_marker = marker;
	}
    }
#endif

  csect_exit (thread_p, CSECT_QPROC_LIST_CACHE);

  return NO_ERROR;
}

static int
qfile_get_list_cache_entry_size_for_allocate (int nparam)
{
#if defined(SERVER_MODE)
  return sizeof (QFILE_LIST_CACHE_ENTRY)	/* space for structure */
    + sizeof (int) * MAX_NTRANS	/* space for tran_index_array */
    + sizeof (DB_VALUE) * nparam;	/* space for param_values.vals */
#else /* SERVER_MODE */
  return sizeof (QFILE_LIST_CACHE_ENTRY)	/* space for structure */
    + sizeof (DB_VALUE) * nparam;	/* space for param_values.vals */
#endif /* SERVER_MODE */
}

#if defined(SERVER_MODE)
static int *
qfile_get_list_cache_entry_tran_index_array (QFILE_LIST_CACHE_ENTRY * ent)
{
  return (int *) ((char *) ent + sizeof (QFILE_LIST_CACHE_ENTRY));
}
#endif /* SERVER_MODE */

static DB_VALUE *
qfile_get_list_cache_entry_param_values (QFILE_LIST_CACHE_ENTRY * ent)
{
#if defined(SERVER_MODE)
  return (DB_VALUE *) ((char *) ent + sizeof (QFILE_LIST_CACHE_ENTRY) + sizeof (TRANID) * MAX_NTRANS);
#else /* SERVER_MODE */
  return (DB_VALUE *) ((char *) ent + sizeof (QFILE_LIST_CACHE_ENTRY));
#endif /* SERVER_MODE */
}

/*
 * qfile_add_tuple_get_pos_in_list () - The given tuple is added to the end of
 *    the list file. The position in the list file is returned.
 *  return:
 *  list_id(in):
 *  tuple(in):
 *  tuple_pos(out):
 */
int
qfile_add_tuple_get_pos_in_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, QFILE_TUPLE tuple,
				 QFILE_TUPLE_POSITION * tuple_pos)
{
  PAGE_PTR cur_page_p, new_page_p, prev_page_p;
  int tuple_length;
  char *page_p, *tuple_p;
  int offset, tuple_page_size;

  if (list_id_p == NULL)
    {
      return ER_FAILED;
    }

  QFILE_CHECK_LIST_FILE_IS_CLOSED (list_id_p);

  cur_page_p = list_id_p->last_pgptr;
  tuple_length = QFILE_GET_TUPLE_LENGTH (tuple);

  if (qfile_allocate_new_page_if_need (thread_p, list_id_p, &cur_page_p, tuple_length, false) != NO_ERROR)
    {
      return ER_FAILED;
    }

  page_p = (char *) cur_page_p + list_id_p->last_offset;
  tuple_page_size = MIN (tuple_length, qfile_Max_tuple_page_size);
  memcpy (page_p, tuple, tuple_page_size);

  /* get the information for our QFILE_TUPLE_POSITION */
  if (tuple_pos)
    {
      tuple_pos->offset = list_id_p->last_offset;
      tuple_pos->tpl = page_p;
      tuple_pos->tplno = list_id_p->tuple_cnt;
      tuple_pos->vpid = list_id_p->last_vpid;
    }

  qfile_add_tuple_to_list_id (list_id_p, page_p, tuple_length, tuple_page_size);

  prev_page_p = cur_page_p;
  for (offset = tuple_page_size, tuple_p = (char *) tuple + offset; offset < tuple_length;
       offset += tuple_page_size, tuple_p += tuple_page_size)
    {
      new_page_p =
	qfile_allocate_new_ovf_page (thread_p, list_id_p, cur_page_p, prev_page_p, tuple_length, offset,
				     &tuple_page_size);
      if (new_page_p == NULL)
	{
	  if (prev_page_p != cur_page_p)
	    {
	      qfile_set_dirty_page (thread_p, prev_page_p, FREE, list_id_p->tfile_vfid);
	    }
	  return ER_FAILED;
	}

      memcpy ((char *) new_page_p + QFILE_PAGE_HEADER_SIZE, tuple_p, tuple_page_size);

      prev_page_p = new_page_p;
    }

  if (prev_page_p != cur_page_p)
    {
      QFILE_PUT_OVERFLOW_VPID_NULL (prev_page_p);
      qfile_set_dirty_page (thread_p, prev_page_p, FREE, list_id_p->tfile_vfid);
    }

  qfile_set_dirty_page (thread_p, cur_page_p, DONT_FREE, list_id_p->tfile_vfid);

  return NO_ERROR;
}

/*
 * qfile_has_next_page() - returns whether the page has the next page or not.
 *   If false, that means the page is the last page of the list file.
 *  return: true/false
 *  page_p(in):
 */
bool
qfile_has_next_page (PAGE_PTR page_p)
{
  return (QFILE_GET_NEXT_PAGE_ID (page_p) != NULL_PAGEID && QFILE_GET_NEXT_PAGE_ID (page_p) != NULL_PAGEID_IN_PROGRESS);
}

/*
 * qfile_update_domains_on_type_list() - Update domain pointers belongs to
 *   type list of a given list file
 *  return: error code
 *
 */
int
qfile_update_domains_on_type_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, valptr_list_node * valptr_list_p)
{
  REGU_VARIABLE_LIST reg_var_p;
  int i, count = 0;

  assert (list_id_p != NULL);

  list_id_p->is_domain_resolved = true;

  reg_var_p = valptr_list_p->valptrp;

  for (i = 0; i < valptr_list_p->valptr_cnt; i++, reg_var_p = reg_var_p->next)
    {
      if (REGU_VARIABLE_IS_FLAGED (&reg_var_p->value, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  continue;
	}

      if (count >= list_id_p->type_list.type_cnt)
	{
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  goto exit_on_error;
	}

      if (TP_DOMAIN_TYPE (list_id_p->type_list.domp[count]) == DB_TYPE_VARIABLE)
	{
	  if (TP_DOMAIN_TYPE (reg_var_p->value.domain) == DB_TYPE_VARIABLE)
	    {
	      /* In this case, we cannot resolve the value's domain. We will try to do for the next tuple. */
	      if (list_id_p->is_domain_resolved)
		{
		  list_id_p->is_domain_resolved = false;
		}
	    }
	  else
	    {
	      list_id_p->type_list.domp[count] = reg_var_p->value.domain;
	    }
	}

      if (list_id_p->type_list.domp[count]->collation_flag != TP_DOMAIN_COLL_NORMAL)
	{
	  if (reg_var_p->value.domain->collation_flag != TP_DOMAIN_COLL_NORMAL)
	    {
	      /* In this case, we cannot resolve the value's domain. We will try to do for the next tuple. */
	      if (list_id_p->is_domain_resolved)
		{
		  list_id_p->is_domain_resolved = false;
		}
	    }
	  else
	    {
	      list_id_p->type_list.domp[count] = reg_var_p->value.domain;
	    }
	}

      count++;
    }

  /* The number of columns should be same. */
  if (count != list_id_p->type_list.type_cnt)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      goto exit_on_error;
    }

  return NO_ERROR;

exit_on_error:

  list_id_p->is_domain_resolved = false;
  return ER_FAILED;
}

/*
 * qfile_set_tuple_column_value() - Set column value for a fixed size column
 *				    of a tuple inside a list file (in-place)
 *  return: error code
 *  list_id_p(in): list file id
 *  curr_page_p(in): use this only with curr_pgptr of your open scanner!
 *  vpid_p(in): real VPID
 *  tuple_p(in): pointer to the tuple inside the page
 *  col_num(in): column number
 *  value_p(in): new column value
 *  domain_p(in): column domain
 *
 *  Note: Caller is responsible to ensure that the size of the column data
 *        is fixed!
 */
int
qfile_set_tuple_column_value (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id_p, PAGE_PTR curr_page_p, VPID * vpid_p,
			      QFILE_TUPLE tuple_p, int col_num, DB_VALUE * value_p, TP_DOMAIN * domain_p)
{
  PAGE_PTR page_p;
  QFILE_TUPLE_VALUE_FLAG flag;
  QFILE_TUPLE_RECORD tuple_rec = { NULL, 0 };
  PR_TYPE *pr_type;
  OR_BUF buf;
  char *ptr;
  int length;
  int error = NO_ERROR;

  pr_type = domain_p->type;
  if (pr_type == NULL)
    {
      return ER_FAILED;
    }

  /* get a pointer to the page */
  if (curr_page_p != NULL)
    {
      page_p = curr_page_p;
    }
  else
    {
      page_p = qmgr_get_old_page (thread_p, vpid_p, list_id_p->tfile_vfid);
    }
  if (page_p == NULL)
    {
      return ER_FAILED;
    }

  /* locate and update tuple value inside the page or in overflow pages */
  if (QFILE_GET_OVERFLOW_PAGE_ID (page_p) == NULL_PAGEID)
    {
      /* tuple is inside the page, locate column and set the new value */
      flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_p, col_num, &ptr, &length);
      if (flag == V_BOUND)
	{
	  OR_BUF_INIT (buf, ptr, length);

	  if (pr_type->data_writeval (&buf, value_p) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto cleanup;
	    }
	}
      else
	{
	  error = ER_FAILED;
	  goto cleanup;
	}
      qfile_set_dirty_page (thread_p, page_p, DONT_FREE, list_id_p->tfile_vfid);
    }
  else
    {
      /* tuple is in overflow pages */
      if (curr_page_p)
	{
	  /* tuple_p is not a tuple pointer inside the current page, it is a copy made by qfile_scan_list_next(), so
	   * avoid fetching it twice, and make sure it doesn't get freed at cleanup stage of this function. For
	   * reference see how qfile_retrieve_tuple() handles overflow pages. */
	  tuple_rec.tpl = tuple_p;
	  tuple_rec.size = QFILE_GET_TUPLE_LENGTH (tuple_p);
	}
      else
	{
	  if (qfile_get_tuple (thread_p, page_p, tuple_p, &tuple_rec, list_id_p) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto cleanup;
	    }
	}

      /* locate column and set the new value */
      flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_rec.tpl, col_num, &ptr, &length);
      if (flag == V_BOUND)
	{
	  OR_BUF_INIT (buf, ptr, length);

	  if (pr_type->data_writeval (&buf, value_p) != NO_ERROR)
	    {
	      error = ER_FAILED;
	      goto cleanup;
	    }
	}
      else
	{
	  error = ER_FAILED;
	  goto cleanup;
	}

      /* flush the tuple back into overflow pages */
      if (qfile_overwrite_tuple (thread_p, page_p, tuple_p, &tuple_rec, list_id_p) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}
      qfile_set_dirty_page (thread_p, page_p, DONT_FREE, list_id_p->tfile_vfid);
    }

cleanup:
  /* free the tuple record if used */
  if (tuple_rec.tpl != NULL && tuple_rec.tpl != tuple_p)
    {
      db_private_free_and_init (NULL, tuple_rec.tpl);
    }
  /* free the page */
  if (page_p != curr_page_p)
    {
      qmgr_free_old_page_and_init (thread_p, page_p, list_id_p->tfile_vfid);
    }

  return error;
}

/*
 * qfile_overwrite_tuple () - Overwrite a tuple inside a list file with a tuple
 *                            record of the same size
 *  return: error code
 *  first_page_p(in): pointer to the first page where the tuple is located
 *  tuple(in): pointer to the tuple to be overwritten
 *  tuple_record_p(in): tuple record to overwrite with
 *  list_id_p(in): list file id
 *
 *  Note: The caller should use responsibly this function!
 */
int
qfile_overwrite_tuple (THREAD_ENTRY * thread_p, PAGE_PTR first_page_p, QFILE_TUPLE tuple,
		       QFILE_TUPLE_RECORD * tuple_record_p, QFILE_LIST_ID * list_id_p)
{
  VPID ovfl_vpid;
  char *tuple_p;
  int offset;
  int tuple_length, tuple_page_size;
  int max_tuple_page_size;
  PAGE_PTR page_p;

  page_p = first_page_p;
  tuple_length = QFILE_GET_TUPLE_LENGTH (tuple);

  /* sanity check */
  if (tuple_length != tuple_record_p->size)
    {
      assert (false);
      return ER_FAILED;
    }

  tuple_p = (char *) tuple_record_p->tpl;

  if (QFILE_GET_OVERFLOW_PAGE_ID (page_p) == NULL_PAGEID)
    {
      /* tuple is inside the page */
      memcpy (tuple, tuple_p, tuple_length);
      return NO_ERROR;
    }
  else
    {
      /* tuple has overflow pages */
      offset = 0;
      max_tuple_page_size = qfile_Max_tuple_page_size;

      do
	{
	  QFILE_GET_OVERFLOW_VPID (&ovfl_vpid, page_p);
	  tuple_page_size = MIN (tuple_length - offset, max_tuple_page_size);

	  memcpy ((char *) page_p + QFILE_PAGE_HEADER_SIZE, tuple_p, tuple_page_size);

	  tuple_p += tuple_page_size;
	  offset += tuple_page_size;

	  if (page_p != first_page_p)
	    {
	      qfile_set_dirty_page (thread_p, page_p, FREE, list_id_p->tfile_vfid);
	    }

	  if (ovfl_vpid.pageid != NULL_PAGEID)
	    {
	      page_p = qmgr_get_old_page (thread_p, &ovfl_vpid, list_id_p->tfile_vfid);
	      if (page_p == NULL)
		{
		  return ER_FAILED;
		}
	    }
	}
      while (ovfl_vpid.pageid != NULL_PAGEID);
    }

  return NO_ERROR;
}

/*
 * qfile_compare_with_interpolation_domain () -
 *  return: compare result
 *  fp0(in):
 *  fp1(in):
 *  subkey(in):
 *
 *  NOTE: median analytic function sort string in different domain
 */
static int
qfile_compare_with_interpolation_domain (char *fp0, char *fp1, SUBKEY_INFO * subkey, SORTKEY_INFO * key_info)
{
  int order = 0;
  DB_VALUE val0, val1;
  OR_BUF buf0, buf1;
  TP_DOMAIN *cast_domain = NULL;
  TP_DOMAIN_STATUS status = DOMAIN_COMPATIBLE;
  int error = NO_ERROR;
  char *d0, *d1;

  assert (fp0 != NULL && fp1 != NULL && subkey != NULL && key_info != NULL);

  db_make_null (&val0);
  db_make_null (&val1);

  d0 = fp0 + QFILE_TUPLE_VALUE_HEADER_LENGTH;
  d1 = fp1 + QFILE_TUPLE_VALUE_HEADER_LENGTH;

  if (subkey->cmp_dom == NULL)
    {
      /* get the proper domain NOTE: col_dom is string type.  See qexec_initialize_analytic_state */
      pr_clear_value (&val0);

      OR_BUF_INIT (buf0, d0, QFILE_GET_TUPLE_VALUE_LENGTH (fp0));
      error =
	subkey->col_dom->type->data_readval (&buf0, &val0, subkey->col_dom, QFILE_GET_TUPLE_VALUE_LENGTH (fp0), false,
					     NULL, 0);
      if (error != NO_ERROR || DB_IS_NULL (&val0))
	{
	  goto end;
	}

      error = qdata_update_interpolation_func_value_and_domain (&val0, &val0, &cast_domain);
      if (error != NO_ERROR)
	{
	  subkey->cmp_dom = NULL;
	  goto end;
	}
      else
	{
	  subkey->cmp_dom = cast_domain;
	}
    }

  /* cast to proper domain, then compare */
  pr_clear_value (&val0);
  pr_clear_value (&val1);

  OR_BUF_INIT (buf0, d0, QFILE_GET_TUPLE_VALUE_LENGTH (fp0));
  OR_BUF_INIT (buf1, d1, QFILE_GET_TUPLE_VALUE_LENGTH (fp1));
  error =
    subkey->col_dom->type->data_readval (&buf0, &val0, subkey->col_dom, QFILE_GET_TUPLE_VALUE_LENGTH (fp0), false,
					 NULL, 0);
  if (error != NO_ERROR)
    {
      goto end;
    }

  error =
    subkey->col_dom->type->data_readval (&buf1, &val1, subkey->col_dom, QFILE_GET_TUPLE_VALUE_LENGTH (fp1), false,
					 NULL, 0);
  if (error != NO_ERROR)
    {
      goto end;
    }

  cast_domain = subkey->cmp_dom;
  status = tp_value_cast (&val0, &val0, cast_domain, false);
  if (status != DOMAIN_COMPATIBLE)
    {
      error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
      goto end;
    }

  status = tp_value_cast (&val1, &val1, cast_domain, false);
  if (status != DOMAIN_COMPATIBLE)
    {
      error = ER_ARG_CAN_NOT_BE_CASTED_TO_DESIRED_DOMAIN;
      goto end;
    }

  /* compare */
  order = cast_domain->type->cmpval (&val0, &val1, 0, 1, NULL, cast_domain->collation_id);

end:

  pr_clear_value (&val0);
  pr_clear_value (&val1);

  /* record error */
  if (error != NO_ERROR)
    {
      key_info->error = error;
    }

  return order;
}

void
qfile_update_qlist_count (THREAD_ENTRY * thread_p, const QFILE_LIST_ID * list_p, int inc)
{
#if defined (SERVER_MODE)
  if (list_p != NULL && list_p->type_list.type_cnt != 0)
    {
      thread_p->m_qlist_count += inc;
      if (prm_get_bool_value (PRM_ID_LOG_QUERY_LISTS))
	{
	  er_print_callstack (ARG_FILE_LINE, "update qlist_count by %d to %d\n", inc, thread_p->m_qlist_count);
	}
    }
#endif // SERVER_MODE
}

int
qfile_get_list_cache_number_of_entries (int ht_no)
{
  assert_release (ht_no >= 0);

  return (qfile_List_cache.list_hts[ht_no]->nentries);
}

bool
qfile_has_no_cache_entries ()
{
  return (qfile_List_cache.n_entries == 0);
}
