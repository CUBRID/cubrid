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
 * lock_free.h : Lock-free structures interface.
 */

#ifndef _LOCK_FREE_H_
#define _LOCK_FREE_H_

#include "dbtype_def.h"
#include "lockfree_bitmap.hpp"
#include "porting.h"

#include <cassert>
#if !defined (WINDOWS)
#include <pthread.h>
#endif

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

  /* maximum alloc cnt */
  int max_alloc_cnt;

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

#define LF_ENTRY_DESCRIPTOR_MAX_ALLOC 2147483647	/* MAX INT */
#define LF_ENTRY_DESCRIPTOR_INITIALIZER { 0, 0, 0, 0, 0, 0, LF_ENTRY_DESCRIPTOR_MAX_ALLOC, NULL, NULL, NULL, \
					  NULL, NULL, NULL, NULL, NULL}

/*
 * Lock free transaction based memory garbage collector
 */
#define LF_NULL_TRANSACTION_ID	      ULONG_MAX

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
  { NULL, 0, {}, 0, 0, 100, 0, NULL }

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

#if defined (UNITTEST_LF)
extern void lf_reset_counters (void);
#endif /* UNITTEST_LF */

// C++ style lock-free hash
// *INDENT-OFF*
template <class Key, class T>
class lf_hash_table_cpp
{
  public:
    class iterator;

    lf_hash_table_cpp ();

    void init (lf_tran_system &transys, int hash_size, int freelist_block_count, int freelist_block_size,
               lf_entry_descriptor &edes);
    void destroy ();

    T *find (lf_tran_entry *t_entry, Key &key);
    bool find_or_insert (lf_tran_entry *t_entry, Key &key, T *&t);
    bool insert (lf_tran_entry *t_entry, Key &key, T *&t);
    bool insert_given (lf_tran_entry *t_entry, Key &key, T *&t);
    bool erase (lf_tran_entry *t_entry, Key &key);
    bool erase_locked (lf_tran_entry *t_entry, Key &key, T *&t);

    void unlock (lf_tran_entry *t_entry, T *&t);

    void clear (lf_tran_entry *t_entry);

    T *freelist_claim (lf_tran_entry *t_entry);
    void freelist_retire (lf_tran_entry *t_entry, T *&t);

    void start_tran (lf_tran_entry *t_entry);
    void end_tran (lf_tran_entry *t_entry);

    size_t get_size () const;
    size_t get_element_count () const;
    size_t get_alloc_element_count () const;

    lf_hash_table &get_hash_table ();
    lf_freelist &get_freelist ();

  private:
    pthread_mutex_t *get_pthread_mutex (T *t);
    template <typename F>
    bool generic_insert (F &ins_func, lf_tran_entry *t_entry, Key &key, T *&t);

    lf_freelist m_freelist;
    lf_hash_table m_hash;
};

template <class Key, class T>
class lf_hash_table_cpp<Key, T>::iterator
{
  public:
    iterator () = default;
    iterator (lf_tran_entry *t_entry, lf_hash_table_cpp & hash);
    ~iterator ();

    T *iterate ();
    void restart ();

  private:
    lf_hash_table_iterator m_iter;
    T *m_crt_val;
};

//
// implementation
//

//
// lf_hash_table_cpp
//
template <class Key, class T>
lf_hash_table_cpp<Key, T>::lf_hash_table_cpp ()
  : m_freelist LF_FREELIST_INITIALIZER
  , m_hash LF_HASH_TABLE_INITIALIZER
{
}

template <class Key, class T>
void
lf_hash_table_cpp<Key, T>::init (lf_tran_system &transys, int hash_size, int freelist_block_count,
                                 int freelist_block_size, lf_entry_descriptor &edesc)
{
  if (lf_freelist_init (&m_freelist, freelist_block_count, freelist_block_size, &edesc, &transys) != NO_ERROR)
    {
      assert (false);
      return;
    }
  if (lf_hash_init (&m_hash, &m_freelist, hash_size, &edesc) != NO_ERROR)
    {
      assert (false);
      return;
    }
}

template <class Key, class T>
void
lf_hash_table_cpp<Key, T>::destroy ()
{
  lf_hash_destroy (&m_hash);
  lf_freelist_destroy (&m_freelist);
}

template <class Key, class T>
pthread_mutex_t *
lf_hash_table_cpp<Key, T>::get_pthread_mutex (T *t)
{
  assert (m_freelist.entry_desc->using_mutex);
  return (pthread_mutex_t *) (((char *) t) + m_freelist.entry_desc->of_mutex);
}

template <class Key, class T>
T *
lf_hash_table_cpp<Key, T>::find (lf_tran_entry *t_entry, Key &key)
{
  T *ret = NULL;
  if (lf_hash_find (t_entry, &m_hash, &key, (void **) (&ret)) != NO_ERROR)
    {
      assert (false);
    }
  return ret;
}

template <class Key, class T>
template <typename F>
bool
lf_hash_table_cpp<Key, T>::generic_insert (F &ins_func, lf_tran_entry *t_entry, Key &key, T *&t)
{
  int inserted = 0;
  if (ins_func (t_entry, &m_hash, &key, reinterpret_cast<void **> (&t), &inserted) != NO_ERROR)
    {
      assert (false);
    }
  return inserted != 0;
}

template <class Key, class T>
bool
lf_hash_table_cpp<Key, T>::find_or_insert (lf_tran_entry *t_entry, Key &key, T *&t)
{
  return generic_insert (lf_hash_find_or_insert, t_entry, key, t);
}

template <class Key, class T>
bool
lf_hash_table_cpp<Key, T>::insert (lf_tran_entry *t_entry, Key &key, T *&t)
{
  return generic_insert (lf_hash_insert, t_entry, key, t);
}

template <class Key, class T>
bool
lf_hash_table_cpp<Key, T>::insert_given (lf_tran_entry *t_entry, Key &key, T *&t)
{
  return generic_insert (lf_hash_insert_given, t_entry, key, t);
}

template <class Key, class T>
bool
lf_hash_table_cpp<Key, T>::erase (lf_tran_entry *t_entry, Key &key)
{
  int success = 0;
  if (lf_hash_delete (t_entry, &m_hash, &key, &success) != NO_ERROR)
    {
      assert (false);
    }
  return success != 0;
}

template <class Key, class T>
bool
lf_hash_table_cpp<Key, T>::erase_locked (lf_tran_entry *t_entry, Key &key, T *&t)
{
  int success = 0;
  if (lf_hash_delete_already_locked (t_entry, &m_hash, &key, t, &success) != NO_ERROR)
    {
      assert (false);
      pthread_mutex_unlock (get_pthread_mutex (t));
    }
  if (success != 0)
    {
      t = NULL;
    }
  return success != 0;
}

template <class Key, class T>
void
lf_hash_table_cpp<Key, T>::unlock (lf_tran_entry *t_entry, T *&t)
{
  assert (t != NULL);
  if (m_freelist.entry_desc->using_mutex)
    {
      pthread_mutex_unlock (get_pthread_mutex (t));
    }
  else
    {
      lf_tran_end_with_mb (t_entry);
    }
  t = NULL;
}

template <class Key, class T>
void
lf_hash_table_cpp<Key, T>::clear (lf_tran_entry *t_entry)
{
  lf_hash_clear (t_entry, &m_hash);
}

template <class Key, class T>
T *
lf_hash_table_cpp<Key, T>::freelist_claim (lf_tran_entry *t_entry)
{
  return (T *) lf_freelist_claim (t_entry, &m_freelist);
}

template <class Key, class T>
void
lf_hash_table_cpp<Key, T>::freelist_retire (lf_tran_entry *t_entry, T *&t)
{
  lf_freelist_retire (t_entry, &m_freelist, t);
  t = NULL;
}

template <class Key, class T>
void
lf_hash_table_cpp<Key, T>::start_tran (lf_tran_entry *t_entry)
{
  lf_tran_start_with_mb (t_entry, false);
}

template <class Key, class T>
void
lf_hash_table_cpp<Key, T>::end_tran (lf_tran_entry *t_entry)
{
  lf_tran_end_with_mb (t_entry);
}

template <class Key, class T>
size_t
lf_hash_table_cpp<Key, T>::get_size () const
{
  assert (m_hash.hash_size > 0);
  return (size_t) m_hash.hash_size;
}

template <class Key, class T>
size_t
lf_hash_table_cpp<Key, T>::get_element_count () const
{
  int alloc_count = m_freelist.alloc_cnt;
  int unused_count = m_freelist.available_cnt + m_freelist.retired_cnt;
  if (alloc_count > unused_count)
    {
      return static_cast<size_t> (alloc_count - unused_count);
    }
  else
    {
      return 0;
    }
}

template <class Key, class T>
size_t
lf_hash_table_cpp<Key, T>::get_alloc_element_count () const
{
  return m_freelist.alloc_cnt;
}

template <class Key, class T>
lf_hash_table &
lf_hash_table_cpp<Key, T>::get_hash_table ()
{
  return m_hash;
}

template <class Key, class T>
lf_freelist &
lf_hash_table_cpp<Key, T>::get_freelist ()
{
  return m_freelist;
}

//
// lf_hash_table_cpp::iterator
//

template <class Key, class T>
lf_hash_table_cpp<Key, T>::iterator::iterator (lf_tran_entry *t_entry, lf_hash_table_cpp & hash)
  : m_iter ()
  , m_crt_val (NULL)
{
  lf_hash_create_iterator (&m_iter, t_entry, &hash.m_hash);
}

template <class Key, class T>
lf_hash_table_cpp<Key, T>::iterator::~iterator ()
{
}

template <class Key, class T>
T *
lf_hash_table_cpp<Key, T>::iterator::iterate ()
{
  return static_cast<T *> (lf_hash_iterate (&m_iter));
}

template <class Key, class T>
void
lf_hash_table_cpp<Key, T>::iterator::restart ()
{
  if (m_iter.tran_entry->transaction_id != LF_NULL_TRANSACTION_ID)
    {
      lf_tran_end_with_mb (m_iter.tran_entry);
    }
  m_crt_val = NULL;
}

// *INDENT-ON*

#endif /* _LOCK_FREE_H_ */
