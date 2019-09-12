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
 * lock_free.h : Lock-free structures interface.
 */

#ifndef _LOCK_FREE_H_
#define _LOCK_FREE_H_

#include "porting.h"
#include "dbtype_def.h"

/*
 * Some common hash, copy and compare functions
 */
extern unsigned int lf_callback_vpid_hash (void *vpid, int htsize);
extern int lf_callback_vpid_compare (void *vpid_1, void *vpid_2);
extern int lf_callback_vpid_copy (void *src, void *dest);

/*
 * Volatile access to a variable
 */
#define VOLATILE_ACCESS(v,t)		(*((t volatile *) &(v)))

/*
 * Entry descriptor
 */
typedef void *(*LF_ENTRY_ALLOC_FUNC) ();
typedef int (*LF_ENTRY_FREE_FUNC) (void *);
typedef int (*LF_ENTRY_INITIALIZE_FUNC) (void *);
typedef int (*LF_ENTRY_UNINITIALIZE_FUNC) (void *);
typedef int (*LF_ENTRY_KEY_COPY_FUNC) (void *src, void *dest);
typedef int (*LF_ENTRY_KEY_COMPARE_FUNC) (void *key1, void *key2);
typedef unsigned int (*LF_ENTRY_HASH_FUNC) (void *key, int htsize);
typedef int (*LF_ENTRY_DUPLICATE_KEY_HANDLER) (void *key, void *existing);

#define LF_EM_NOT_USING_MUTEX		      0
#define LF_EM_USING_MUTEX		      1

typedef struct lf_entry_descriptor LF_ENTRY_DESCRIPTOR;
struct lf_entry_descriptor
{
  /* offset of "next" pointer used in local lists */
  unsigned int of_local_next;

  /* offset of "next" pointer */
  unsigned int of_next;

  /* offset of transaction id of delete operation */
  unsigned int of_del_tran_id;

  /* offset of key */
  unsigned int of_key;

  /* offset of entry mutex */
  unsigned int of_mutex;

  /* does entry have mutex */
  int using_mutex;

  /* allocation callback */
  LF_ENTRY_ALLOC_FUNC f_alloc;

  /* deallocation callback */
  LF_ENTRY_FREE_FUNC f_free;

  /* initialization callback; can be NULL */
  LF_ENTRY_INITIALIZE_FUNC f_init;

  /* uninitialization callback; can be NULL */
  LF_ENTRY_UNINITIALIZE_FUNC f_uninit;

  /* copy function for keys */
  LF_ENTRY_KEY_COPY_FUNC f_key_copy;

  /* compare function for keys */
  LF_ENTRY_KEY_COMPARE_FUNC f_key_cmp;

  /* hash function for keys */
  LF_ENTRY_HASH_FUNC f_hash;

  /* callback for lf_insert with existing key */
  /* NOTE: when NULL, lf_insert will spin until existing entry is deleted */
  LF_ENTRY_DUPLICATE_KEY_HANDLER f_duplicate;
};

#define LF_ENTRY_DESCRIPTOR_INITIALIZER { 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, \
					  NULL, NULL, NULL, NULL, NULL}

/*
 * Lock free transaction based memory garbage collector
 */
#define LF_NULL_TRANSACTION_ID	      ULONG_MAX
#define LF_BITFIELD_WORD_SIZE    (int) (sizeof (unsigned int) * 8)

typedef struct lf_tran_system LF_TRAN_SYSTEM;
typedef struct lf_tran_entry LF_TRAN_ENTRY;

struct lf_tran_entry
{
  /* last ID for which a cleanup of retired_list was performed */
  UINT64 last_cleanup_id;

  /* id of current transaction */
  UINT64 transaction_id;

  /* list of retired node for attached thread */
  void *retired_list;

  /* temp entry - for find_and_insert operations, to avoid unnecessary ops */
  void *temp_entry;

  /* attached transaction system */
  LF_TRAN_SYSTEM *tran_system;

  /* entry in transaction system */
  int entry_idx;

  /* Was transaction ID incremented? */
  bool did_incr;

#if defined (UNITTEST_LF)
  /* Debug */
  pthread_mutex_t *locked_mutex;
  int locked_mutex_line;
#endif				/* UNITTEST_LF */
};

#define LF_TRAN_ENTRY_INITIALIZER     { 0, LF_NULL_TRANSACTION_ID, NULL, NULL, NULL, -1, false }

enum lf_bitmap_style
{
  LF_BITMAP_ONE_CHUNK = 0,
  LF_BITMAP_LIST_OF_CHUNKS
};
typedef enum lf_bitmap_style LF_BITMAP_STYLE;

typedef struct lf_bitmap LF_BITMAP;
struct lf_bitmap
{
  /* bitfield for entries array */
  unsigned int *bitfield;

  /* capacity count */
  int entry_count;

  /* current used count */
  int entry_count_in_use;

  /* style */
  LF_BITMAP_STYLE style;

  /* threshold for usage */
  float usage_threshold;

  /* the start chunk index for round-robin */
  unsigned int start_idx;
};

#define LF_BITMAP_FULL_USAGE_RATIO (1.0f)
#define LF_BITMAP_95PERCENTILE_USAGE_RATIO (0.95f)

#if defined (SERVER_MODE)
#define LF_AREA_BITMAP_USAGE_RATIO LF_BITMAP_95PERCENTILE_USAGE_RATIO
#else
#define LF_AREA_BITMAP_USAGE_RATIO LF_BITMAP_FULL_USAGE_RATIO
#endif

#define LF_BITMAP_IS_FULL(bitmap)                              \
  (((float)VOLATILE_ACCESS((bitmap)->entry_count_in_use, int)) \
        / (bitmap)->entry_count >= (bitmap)->usage_threshold)

#define LF_BITMAP_COUNT_ALIGN(count) \
    (((count) + (LF_BITFIELD_WORD_SIZE) - 1) & ~((LF_BITFIELD_WORD_SIZE) - 1))

struct lf_tran_system
{
  /* pointer array to thread dtran entries */
  LF_TRAN_ENTRY *entries;

  /* capacity */
  int entry_count;

  /* lock-free bitmap */
  LF_BITMAP lf_bitmap;

  /* global delete ID for all delete operations */
  UINT64 global_transaction_id;

  /* minimum curr_delete_id of all used LF_DTRAN_ENTRY entries */
  UINT64 min_active_transaction_id;

  /* number of transactions between computing min_active_transaction_id */
  int mati_refresh_interval;

  /* current used count */
  int used_entry_count;

  /* entry descriptor */
  LF_ENTRY_DESCRIPTOR *entry_desc;
};

#define LF_TRAN_SYSTEM_INITIALIZER \
  { NULL, 0, {NULL, 0, 0, LF_BITMAP_ONE_CHUNK, 1.0f, 0}, 0, 0, 100, 0, NULL }

#define LF_TRAN_CLEANUP_NECESSARY(e) ((e)->tran_system->min_active_transaction_id > (e)->last_cleanup_id)

extern int lf_tran_system_init (LF_TRAN_SYSTEM * sys, int max_threads);
extern void lf_tran_system_destroy (LF_TRAN_SYSTEM * sys);

extern LF_TRAN_ENTRY *lf_tran_request_entry (LF_TRAN_SYSTEM * sys);
extern void lf_tran_return_entry (LF_TRAN_ENTRY * entry);
extern void lf_tran_destroy_entry (LF_TRAN_ENTRY * entry);
extern void lf_tran_compute_minimum_transaction_id (LF_TRAN_SYSTEM * sys);

extern void lf_tran_start (LF_TRAN_ENTRY * entry, bool incr);
extern void lf_tran_end (LF_TRAN_ENTRY * entry);
/* TODO: Investigate memory barriers. First of all, I need to check if it breaks the inlining of lf_tran_start and
 *	 lf_tran_end functions. Second of all, full memory barriers might not be necessary.
 */
#define lf_tran_start_with_mb(entry, incr) lf_tran_start (entry, incr); MEMORY_BARRIER ()
#define lf_tran_end_with_mb(entry) MEMORY_BARRIER (); lf_tran_end (entry)

/*
 * Global lock free transaction system declarations
 */
extern LF_TRAN_SYSTEM spage_saving_Ts;
extern LF_TRAN_SYSTEM obj_lock_res_Ts;
extern LF_TRAN_SYSTEM obj_lock_ent_Ts;
extern LF_TRAN_SYSTEM catalog_Ts;
extern LF_TRAN_SYSTEM sessions_Ts;
extern LF_TRAN_SYSTEM free_sort_list_Ts;
extern LF_TRAN_SYSTEM global_unique_stats_Ts;
extern LF_TRAN_SYSTEM hfid_table_Ts;
extern LF_TRAN_SYSTEM xcache_Ts;
extern LF_TRAN_SYSTEM fpcache_Ts;
extern LF_TRAN_SYSTEM dwb_slots_Ts;

extern int lf_initialize_transaction_systems (int max_threads);
extern void lf_destroy_transaction_systems (void);

/*
 * Lock free stack
 */
extern int lf_stack_push (void **top, void *entry, LF_ENTRY_DESCRIPTOR * edesc);
extern void *lf_stack_pop (void **top, LF_ENTRY_DESCRIPTOR * edesc);

/*
 * Lock free freelist
 */
typedef struct lf_freelist LF_FREELIST;
struct lf_freelist
{
  /* available stack (i.e. entries that can be safely reclaimed) */
  void *available;

  /* allocation block size */
  int block_size;

  /* entry counters */
  int alloc_cnt;
  int available_cnt;
  int retired_cnt;

  /* entry descriptor */
  LF_ENTRY_DESCRIPTOR *entry_desc;

  /* transaction system */
  LF_TRAN_SYSTEM *tran_system;
};

#define LF_FREELIST_INITIALIZER \
  { NULL, 0, 0, 0, 0, NULL, NULL }

extern int lf_freelist_init (LF_FREELIST * freelist, int initial_blocks, int block_size, LF_ENTRY_DESCRIPTOR * edesc,
			     LF_TRAN_SYSTEM * tran_system);
extern void lf_freelist_destroy (LF_FREELIST * freelist);

extern void *lf_freelist_claim (LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist);
extern int lf_freelist_retire (LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist, void *entry);
extern int lf_freelist_transport (LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist);

/*
 * Lock free insert-only list based dictionary
 * NOTE: This list does not use a LF_TRAN_SYSTEM nor a LF_FREELIST.
 */
extern int lf_io_list_find (void **list_p, void *key, LF_ENTRY_DESCRIPTOR * edesc, void **entry);
extern int lf_io_list_find_or_insert (void **list_p, void *new_entry, LF_ENTRY_DESCRIPTOR * edesc, void **entry);

/*
 * Lock free linked list based dictionary
 */
#define LF_LIST_BF_NONE			  0x0

/* flags that can be given to lf_list_* functions */
#define LF_LIST_BF_RETURN_ON_RESTART	  ((int) 0x01)
#define LF_LIST_BF_RESTART_ON_DUPLICATE	  ((int) 0x02)	/* Not used for now. */
#define LF_LIST_BF_INSERT_GIVEN		  ((int) 0x04)
#define LF_LIST_BF_FIND_OR_INSERT	  ((int) 0x08)
#define LF_LIST_BF_LOCK_ON_DELETE	  ((int) 0x10)
#define LF_LIST_BF_IS_FLAG_SET(bf, flag) ((*(bf) & (flag)) != 0)
#define LF_LIST_BF_SET_FLAG(bf, flag) (*(bf) = *(bf) | (flag))

/* responses to flags from lf_list_* functions */
#define LF_LIST_BR_RESTARTED		  ((int) 0x100)
#define LF_LIST_BR_DUPLICATE		  ((int) 0x200)	/* Not used for now. */
#define LF_LIST_BR_IS_FLAG_SET(br, flag) ((*(br) & (flag)))
#define LF_LIST_BR_SET_FLAG(br, flag) (*(br) = *(br) | (flag))

extern int lf_list_find (LF_TRAN_ENTRY * tran, void **list_p, void *key, int *behavior_flags,
			 LF_ENTRY_DESCRIPTOR * edesc, void **entry);
extern int lf_list_delete (LF_TRAN_ENTRY * tran, void **list_p, void *key, void *locked_entry, int *behavior_flags,
			   LF_ENTRY_DESCRIPTOR * edesc, LF_FREELIST * freelist, int *success);
/* TODO: Add lf_list_insert functions. So far, they are only used for lf_hash_insert. */

/*
 * Lock free hash table
 */
typedef struct lf_hash_table LF_HASH_TABLE;
struct lf_hash_table
{
  /* table buckets */
  void **buckets;

  /* backbuffer */
  void **backbuffer;

  /* backbuffer mutex */
  pthread_mutex_t backbuffer_mutex;

  /* size of hash table */
  unsigned int hash_size;

  /* freelist for memory reuse */
  LF_FREELIST *freelist;

  /* entry descriptor */
  LF_ENTRY_DESCRIPTOR *entry_desc;
};

#define LF_HASH_TABLE_INITIALIZER \
  { NULL, NULL, PTHREAD_MUTEX_INITIALIZER, 0, NULL, NULL }

extern int lf_hash_init (LF_HASH_TABLE * table, LF_FREELIST * freelist, unsigned int hash_size,
			 LF_ENTRY_DESCRIPTOR * edesc);
extern void lf_hash_destroy (LF_HASH_TABLE * table);

extern int lf_hash_find (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry);
extern int lf_hash_find_or_insert (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry, int *inserted);
extern int lf_hash_insert (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry, int *inserted);
extern int lf_hash_insert_given (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry, int *inserted);
extern int lf_hash_delete (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, int *success);
extern int lf_hash_delete_already_locked (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void *locked_entry,
					  int *success);
extern void lf_hash_clear (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table);

/*
 * Lock free hash table iterator
 */
typedef struct lf_hash_table_iterator LF_HASH_TABLE_ITERATOR;
struct lf_hash_table_iterator
{
  /* hash table we iterate on */
  LF_HASH_TABLE *hash_table;

  /* current bucket index */
  int bucket_index;

  /* current entry */
  void *curr;

  /* transaction entry to use */
  LF_TRAN_ENTRY *tran_entry;
};

extern void lf_hash_create_iterator (LF_HASH_TABLE_ITERATOR * iterator, LF_TRAN_ENTRY * tran_entry,
				     LF_HASH_TABLE * table);
extern void *lf_hash_iterate (LF_HASH_TABLE_ITERATOR * it);

/* lock free bitmap */
extern int lf_bitmap_init (LF_BITMAP * bitmap, LF_BITMAP_STYLE style, int entries_cnt, float usage_threshold);
extern void lf_bitmap_destroy (LF_BITMAP * bitmap);
extern int lf_bitmap_get_entry (LF_BITMAP * bitmap);
extern void lf_bitmap_free_entry (LF_BITMAP * bitmap, int entry_idx);

#if defined (UNITTEST_LF)
extern void lf_reset_counters (void);
#endif /* UNITTEST_LF */

#endif /* _LOCK_FREE_H_ */
