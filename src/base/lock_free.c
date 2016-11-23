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
 * lock_free.c : Lock-free structures.
 */

#include "config.h"

#if !defined (WINDOWS)
#include <pthread.h>
#endif
#include <assert.h>

#include "porting.h"
#include "lock_free.h"
#include "error_manager.h"
#include "error_code.h"
#include "memory_alloc.h"

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)   0
#define pthread_mutex_trylock(a)   0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* not SERVER_MODE */

/*
 * Global lock free transaction systems systems
 */
LF_TRAN_SYSTEM spage_saving_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM obj_lock_res_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM obj_lock_ent_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM catalog_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM sessions_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM free_sort_list_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM global_unique_stats_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM hfid_table_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM xcache_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM fpcache_Ts = LF_TRAN_SYSTEM_INITIALIZER;

static bool tran_systems_initialized = false;

/*
 * Macro definitions
 */
#define OF_GET_REF(p,o)		(void * volatile *) (((char *)(p)) + (o))
#define OF_GET_PTR(p,o)		(void *) (((char *)(p)) + (o))
#define OF_GET_PTR_DEREF(p,o)	(*OF_GET_REF (p,o))


static INT64 lf_hash_size = 0;

static INT64 lf_inserts = 0;
static INT64 lf_inserts_restart = 0;
static INT64 lf_list_inserts = 0;
static INT64 lf_list_inserts_found = 0;
static INT64 lf_list_inserts_save_temp_1 = 0;
static INT64 lf_list_inserts_save_temp_2 = 0;
static INT64 lf_list_inserts_claim = 0;
static INT64 lf_list_inserts_fail_link = 0;
static INT64 lf_list_inserts_success_link = 0;

static INT64 lf_deletes = 0;
static INT64 lf_deletes_restart = 0;
static INT64 lf_list_deletes = 0;
static INT64 lf_list_deletes_found = 0;
static INT64 lf_list_deletes_fail_mark_next = 0;
static INT64 lf_list_deletes_fail_unlink = 0;
static INT64 lf_list_deletes_success_unlink = 0;
static INT64 lf_list_deletes_not_found = 0;
static INT64 lf_list_deletes_not_match = 0;

static INT64 lf_clears = 0;

static INT64 lf_retires = 0;
static INT64 lf_claims = 0;
static INT64 lf_claims_temp = 0;
static INT64 lf_transports = 0;
static INT64 lf_temps = 0;

#if defined (UNITTEST_LF)
#define LF_UNITTEST_INC(lf_stat, incval) ATOMIC_INC_64 (lf_stat, incval)
#else /* !UNITTEST_LF */
#define LF_UNITTEST_INC(lf_stat, incval)
#endif

#if defined (UNITTEST_LF) || defined (UNITTEST_CQ)
#if defined (NDEBUG)
/* Abort when calling assert even if it is not debug */
#define assert(cond) if (!(cond)) abort ()
#define assert_release(cond) if (!(cond)) abort ()
#endif /* NDEBUG */
#endif /* UNITTEST_LF || UNITTEST_CQ */

static int lf_list_insert_internal (LF_TRAN_ENTRY * tran, void **list_p, void *key, int *behavior_flags,
				    LF_ENTRY_DESCRIPTOR * edesc, LF_FREELIST * freelist, void **entry, int *inserted);
static int lf_hash_insert_internal (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, int bflags, void **entry,
				    int *inserted);
static int lf_hash_delete_internal (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void *locked_entry,
				    int bflags, int *success);

/*
 * lf_callback_vpid_hash () - hash a VPID
 *   returns: hash value
 *   vpid(in): VPID to hash
 *   htsize(in): hash table size
 */
unsigned int
lf_callback_vpid_hash (void *vpid, int htsize)
{
  VPID *lvpid = (VPID *) vpid;

  return ((lvpid->pageid | ((unsigned int) lvpid->volid) << 24) % htsize);
}

/*
 * lf_callback_vpid_compare () - compare two vpids
 *   returns: 0 if equal, non-zero otherwise
 *   vpid_1(in): first VPID
 *   vpid_2(in): second vpid
 */
int
lf_callback_vpid_compare (void *vpid_1, void *vpid_2)
{
  VPID *lvpid_1 = (VPID *) vpid_1;
  VPID *lvpid_2 = (VPID *) vpid_2;

  return !((lvpid_1->pageid == lvpid_2->pageid) && (lvpid_1->volid == lvpid_2->volid));
}

/*
 * lf_callback_vpid_copy () - copy a vpid
 *   returns: error code or NO_ERROR
 *   src(in): source VPID
 *   dest(in): destination to copy to
 */
int
lf_callback_vpid_copy (void *src, void *dest)
{
  ((VPID *) dest)->pageid = ((VPID *) src)->pageid;
  ((VPID *) dest)->volid = ((VPID *) src)->volid;

  return NO_ERROR;
}

/*
 * lf_tran_system_init () - initialize a transaction system
 *   returns: error code or NO_ERROR
 *   sys(in): tran system to initialize
 *   max_threads(in): maximum number of threads that will use this system
 */
int
lf_tran_system_init (LF_TRAN_SYSTEM * sys, int max_threads)
{
  int i;
  int error = NO_ERROR;

  assert (sys != NULL);

  sys->entry_count = LF_BITMAP_COUNT_ALIGN (max_threads);
  error = lf_bitmap_init (&sys->lf_bitmap, LF_BITMAP_ONE_CHUNK, sys->entry_count, LF_BITMAP_FULL_USAGE_RATIO);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* initialize entry array */
  sys->entries = (LF_TRAN_ENTRY *) malloc (sizeof (LF_TRAN_ENTRY) * sys->entry_count);
  if (sys->entries == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LF_TRAN_ENTRY) * sys->entry_count);

      sys->entry_count = 0;
      lf_bitmap_destroy (&sys->lf_bitmap);
      return ER_FAILED;
    }

  memset (sys->entries, 0, sys->entry_count * sizeof (LF_TRAN_ENTRY));

  for (i = 0; i < sys->entry_count; i++)
    {
      sys->entries[i].tran_system = sys;
      sys->entries[i].entry_idx = i;
      sys->entries[i].transaction_id = LF_NULL_TRANSACTION_ID;
      sys->entries[i].did_incr = false;
      sys->entries[i].last_cleanup_id = 0;
      sys->entries[i].retired_list = NULL;
      sys->entries[i].temp_entry = NULL;

#if defined (UNITTEST_LF)
      sys->entries[i].locked_mutex = NULL;
      sys->entries[i].locked_mutex_line = -1;
#endif /* UNITTEST_LF */
    }

  sys->used_entry_count = 0;
  sys->global_transaction_id = 0;
  sys->min_active_transaction_id = 0;
  sys->mati_refresh_interval = 100;

  return NO_ERROR;
}

/*
 * lf_tran_system_destroy () - destroy a tran system
 *   sys(in): tran system
 *
 * NOTE: If (edesc == NULL) then there may be some memory leaks. Make sure the
 * function is called with NULL edesc only at shutdown, or after collecting all
 * retired entries.
 */
void
lf_tran_system_destroy (LF_TRAN_SYSTEM * sys)
{
  int i;

  assert (sys != NULL);

  if (sys->entry_desc != NULL)
    {
      for (i = 0; i < sys->entry_count; i++)
	{
	  lf_tran_destroy_entry (&sys->entries[i]);
	}
    }
  sys->entry_count = 0;

  if (sys->entries != NULL)
    {
      free_and_init (sys->entries);
    }

  lf_bitmap_destroy (&sys->lf_bitmap);

  return;
}

/*
 * lf_dtran_request_entry () - request a tran "entry"
 *   returns: entry or NULL on error
 *   sys(in): tran system
 */
LF_TRAN_ENTRY *
lf_tran_request_entry (LF_TRAN_SYSTEM * sys)
{
  LF_TRAN_ENTRY *entry;
  int entry_idx;

  assert (sys != NULL);
  assert (sys->entry_count > 0);
  assert (sys->entries != NULL);

  entry_idx = lf_bitmap_get_entry (&sys->lf_bitmap);
  if (entry_idx < 0)
    {
      assert (false);
      return NULL;
    }

  /* done; clear and serve */
  ATOMIC_INC_32 (&sys->used_entry_count, 1);
  entry = &sys->entries[entry_idx];

  assert (entry->transaction_id == LF_NULL_TRANSACTION_ID);

#if defined (UNITTEST_LF)
  entry->locked_mutex = NULL;
  entry->locked_mutex_line = -1;
#endif /* UNITTEST_LF */

  return entry;
}

/*
 * lf_tran_return_entry () - return a previously requested entry
 *   returns: error code or NO_ERROR
 *   sys(in): tran system
 *   entry(in): tran entry
 *
 * NOTE: Only entries requested from this system should be returned.
 */
int
lf_tran_return_entry (LF_TRAN_ENTRY * entry)
{
  LF_TRAN_SYSTEM *sys;
  int error = NO_ERROR;

  assert (entry != NULL);
  assert (entry->entry_idx >= 0);
  assert (entry->transaction_id == LF_NULL_TRANSACTION_ID);
  assert (entry->tran_system != NULL);

  /* fetch system */
  sys = entry->tran_system;

  /* clear bitfield so slot may be reused */
  error = lf_bitmap_free_entry (&sys->lf_bitmap, entry->entry_idx);
  if (error != NO_ERROR)
    {
      return error;
    }

  /* decrement use counter */
  ATOMIC_INC_32 (&sys->used_entry_count, -1);

  /* all ok */
  return NO_ERROR;
}

/*
 * lf_tran_destroy_entry () - destroy a tran entry
 **  return : NULL 
 *   entry(in): tran entry
 */

void
lf_tran_destroy_entry (LF_TRAN_ENTRY * entry)
{
  LF_ENTRY_DESCRIPTOR *edesc = NULL;

  assert (entry != NULL);
  assert (entry->tran_system != NULL);

  edesc = entry->tran_system->entry_desc;
  if (edesc != NULL)
    {
      void *curr = entry->retired_list, *next = NULL;
      while (curr != NULL)
	{
	  next = (void *) OF_GET_PTR_DEREF (curr, edesc->of_local_next);
	  if (edesc->f_uninit != NULL)
	    {
	      edesc->f_uninit (curr);
	    }
	  edesc->f_free (curr);
	  curr = next;
	}

      entry->retired_list = NULL;
    }
}

/*
 * lf_dtran_compute_minimum_delete_id () - compute minimum delete id of all
 *					   used entries of a tran system
 *   return: error code or NO_ERROR
 *   sys(in): tran system
 */
void
lf_tran_compute_minimum_transaction_id (LF_TRAN_SYSTEM * sys)
{
  UINT64 minvalue = LF_NULL_TRANSACTION_ID;
  int i, j;

  /* determine minimum value of all min_cdi fields in system entries */
  for (i = 0; i < sys->entry_count / LF_BITFIELD_WORD_SIZE; i++)
    {
      if (sys->lf_bitmap.bitfield[i])
	{
	  for (j = 0; j < LF_BITFIELD_WORD_SIZE; j++)
	    {
	      int pos = i * LF_BITFIELD_WORD_SIZE + j;
	      UINT64 fetch = sys->entries[pos].transaction_id;

	      if (minvalue > fetch)
		{
		  minvalue = fetch;
		}
	    }
	}
    }

  /* store new minimum for later fetching */
  ATOMIC_TAS_64 (&sys->min_active_transaction_id, minvalue);
}

/*
 * lf_tran_start_op () - start operation in entry
 *   returns: error code or NO_ERROR
 *   entry(in): tran entry
 *   incr(in): increment global counter?
 */
void
lf_tran_start (LF_TRAN_ENTRY * entry, bool incr)
{
  LF_TRAN_SYSTEM *sys;

  assert (entry != NULL);
  assert (entry->tran_system != NULL);
  assert (entry->transaction_id == LF_NULL_TRANSACTION_ID || (!entry->did_incr && incr));

  sys = entry->tran_system;

  if (incr && !entry->did_incr)
    {
      entry->transaction_id = ATOMIC_INC_64 (&sys->global_transaction_id, 1);
      entry->did_incr = true;

      if (entry->transaction_id % entry->tran_system->mati_refresh_interval == 0)
	{
	  lf_tran_compute_minimum_transaction_id (entry->tran_system);
	}
    }
  else
    {
      entry->transaction_id = VOLATILE_ACCESS (sys->global_transaction_id, UINT64);
    }
}

/*
 * lf_tran_end_op () - end operation in entry
 *   returns: error code or NO_ERROR
 *   sys(in): tran system
 *   entry(in): tran entry
 */
void
lf_tran_end (LF_TRAN_ENTRY * entry)
{
  /* maximum value of domain */
  assert (entry->transaction_id != LF_NULL_TRANSACTION_ID);
  entry->transaction_id = LF_NULL_TRANSACTION_ID;
  entry->did_incr = false;
}

/*
 * lf_initialize_transaction_systems () - initialize global transaction systems
 */
int
lf_initialize_transaction_systems (int max_threads)
{
  if (tran_systems_initialized)
    {
      lf_destroy_transaction_systems ();
      /* reinitialize */
    }

  if (lf_tran_system_init (&spage_saving_Ts, max_threads) != NO_ERROR)
    {
      goto error;
    }
  if (lf_tran_system_init (&obj_lock_res_Ts, max_threads) != NO_ERROR)
    {
      goto error;
    }
  if (lf_tran_system_init (&obj_lock_ent_Ts, max_threads) != NO_ERROR)
    {
      goto error;
    }
  if (lf_tran_system_init (&catalog_Ts, max_threads) != NO_ERROR)
    {
      goto error;
    }
  if (lf_tran_system_init (&sessions_Ts, max_threads) != NO_ERROR)
    {
      goto error;
    }
  if (lf_tran_system_init (&free_sort_list_Ts, max_threads) != NO_ERROR)
    {
      goto error;
    }
  if (lf_tran_system_init (&global_unique_stats_Ts, max_threads) != NO_ERROR)
    {
      goto error;
    }

  if (lf_tran_system_init (&hfid_table_Ts, max_threads) != NO_ERROR)
    {
      goto error;
    }

  if (lf_tran_system_init (&xcache_Ts, max_threads) != NO_ERROR)
    {
      /* TODO: Could we not use an array for tran systems? */
      goto error;
    }

  if (lf_tran_system_init (&fpcache_Ts, max_threads) != NO_ERROR)
    {
      /* TODO: Could we not use an array for tran systems? */
      goto error;
    }

  tran_systems_initialized = true;
  return NO_ERROR;

error:
  lf_destroy_transaction_systems ();
  return ER_FAILED;
}

/*
 * lf_destroy_transaction_systems () - destroy global transaction systems
 */
void
lf_destroy_transaction_systems (void)
{
  lf_tran_system_destroy (&spage_saving_Ts);
  lf_tran_system_destroy (&obj_lock_res_Ts);
  lf_tran_system_destroy (&obj_lock_ent_Ts);
  lf_tran_system_destroy (&catalog_Ts);
  lf_tran_system_destroy (&sessions_Ts);
  lf_tran_system_destroy (&free_sort_list_Ts);
  lf_tran_system_destroy (&global_unique_stats_Ts);
  lf_tran_system_destroy (&hfid_table_Ts);
  lf_tran_system_destroy (&xcache_Ts);
  lf_tran_system_destroy (&fpcache_Ts);

  tran_systems_initialized = false;
}

/*
 * lf_stack_push () - push an entry on a lock free stack
 *   returns: error code or NO_ERROR
 *   top(in/out): top of stack
 *   entry(in): entry to push
 *   edesc(in): descriptor for entry
 */
int
lf_stack_push (void **top, void *entry, LF_ENTRY_DESCRIPTOR * edesc)
{
  void *rtop = NULL;
  assert (top != NULL && entry != NULL && edesc != NULL);

  do
    {
      rtop = *((void *volatile *) top);
      OF_GET_PTR_DEREF (entry, edesc->of_local_next) = rtop;
    }
  while (!ATOMIC_CAS_ADDR (top, rtop, entry));

  /* done */
  return NO_ERROR;
}

/*
 * lf_stack_pop () - pop an entry off a lock free stack
 *   returns: entry or NULL on empty stack
 *   top(in/out): top of stack
 *   edesc(in): descriptor for entry
 *
 * NOTE: This function is vulnerable to an ABA problem in the following
 * scenario:
 *   (1) Thread_1 executes POP and is suspended exactly after MARK
 *   (2) Thread_2 executes: t := POP, POP, PUSH (t)
 *   (3) Thread_1 is continued; CAS will succeed but prev is pointing to the
 *       wrong address
 */
void *
lf_stack_pop (void **top, LF_ENTRY_DESCRIPTOR * edesc)
{
  void *rtop = NULL, *prev = NULL;
  assert (top != NULL && edesc != NULL);

  while (true)
    {
      rtop = *top;
      if (rtop == NULL)
	{
	  /* at this time the stack is empty */
	  return NULL;
	}

      prev = OF_GET_PTR_DEREF (rtop, edesc->of_local_next);	/* MARK */
      if (ATOMIC_CAS_ADDR (top, rtop, prev))
	{
	  /* clear link */
	  OF_GET_PTR_DEREF (rtop, edesc->of_local_next) = NULL;

	  /* return success */
	  return rtop;
	}
    }

  /* impossible case */
  assert (false);
  return NULL;
}

/*
 * lf_freelist_alloc_block () - allocate a block of entries in the freelist
 *   returns: error code or NO_ERROR
 *   freelist(in): freelist to allocate to
 */
static int
lf_freelist_alloc_block (LF_FREELIST * freelist)
{
  void *head = NULL, *tail = NULL, *new_entry = NULL, *top = NULL;
  LF_ENTRY_DESCRIPTOR *edesc;
  int i;

  assert (freelist != NULL && freelist->entry_desc != NULL);
  edesc = freelist->entry_desc;

  /* allocate a block */
  for (i = 0; i < freelist->block_size; i++)
    {
      new_entry = edesc->f_alloc ();
      if (new_entry == NULL)
	{
	  /* we use a decoy size since we don't know it */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) 1);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      /* of_prev of new entry points to tail; new entry becomes tail */
      OF_GET_PTR_DEREF (new_entry, edesc->of_local_next) = tail;
      tail = new_entry;

      /* store first entry as head */
      if (head == NULL)
	{
	  head = new_entry;
	}
    }

  /* append block to freelist */
  do
    {
      top = VOLATILE_ACCESS (freelist->available, void *);
      OF_GET_PTR_DEREF (head, edesc->of_local_next) = top;
    }
  while (!ATOMIC_CAS_ADDR (&freelist->available, top, tail));

  /* increment allocated count */
  ATOMIC_INC_32 (&freelist->alloc_cnt, freelist->block_size);
  ATOMIC_INC_32 (&freelist->available_cnt, freelist->block_size);

  /* operation successful, block appended */
  return NO_ERROR;
}

/*
 * lf_freelist_init () - initialize a freelist
 *   returns: error code or NO_ERROR
 *   freelist(in): freelist to initialize
 *   initial_blocks(in): number of blocks to allocate at initialisation
 *   block_size(in): number of entries allocated in a block
 */
int
lf_freelist_init (LF_FREELIST * freelist, int initial_blocks, int block_size, LF_ENTRY_DESCRIPTOR * edesc,
		  LF_TRAN_SYSTEM * tran_system)
{
  int i;

  assert (freelist != NULL && edesc != NULL);
  assert (tran_system != NULL);
  assert (initial_blocks >= 1);

  if (freelist->available != NULL)
    {
      /* already initialized */
      return NO_ERROR;
    }

  /* initialize fields */
  freelist->available = NULL;

  freelist->available_cnt = 0;
  freelist->retired_cnt = 0;
  freelist->alloc_cnt = 0;

  freelist->block_size = block_size;
  freelist->entry_desc = edesc;
  freelist->tran_system = tran_system;

  tran_system->entry_desc = edesc;

  for (i = 0; i < initial_blocks; i++)
    {
      if (lf_freelist_alloc_block (freelist) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * lf_freelist_destroy () - destroy the freelist
 *   freelist(in): freelist
 */
void
lf_freelist_destroy (LF_FREELIST * freelist)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  void *entry, *next;

  assert (freelist != NULL);

  if (freelist->available == NULL)
    {
      return;
    }

  edesc = freelist->entry_desc;
  entry = freelist->available;
  if (entry != NULL)
    {
      do
	{
	  /* save next entry */
	  next = OF_GET_PTR_DEREF (entry, edesc->of_local_next);

	  /* discard current entry */
	  edesc->f_free (entry);

	  /* advance */
	  entry = next;
	}
      while (entry != NULL);
    }

  freelist->available = NULL;
}

/*
 * lf_freelist_claim () - claim an entry from the available list
 *   returns: entry or NULL on error
 *   tran_entry(in): lock free transaction entry
 *   freelist(in): freelist to claim from
 */
void *
lf_freelist_claim (LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  void *entry;
  bool local_tran = false;

  assert (tran_entry != NULL);
  assert (freelist != NULL);
  assert (freelist->entry_desc != NULL);

  edesc = freelist->entry_desc;

  LF_UNITTEST_INC (&lf_claims, 1);

  /* first check temporary entry */
  if (tran_entry->temp_entry != NULL)
    {
      entry = tran_entry->temp_entry;
      tran_entry->temp_entry = NULL;
      OF_GET_PTR_DEREF (entry, edesc->of_next) = NULL;

      LF_UNITTEST_INC (&lf_claims_temp, 1);
      LF_UNITTEST_INC (&lf_temps, -1);
      return entry;
    }

  /* We need a transaction. Careful: we cannot increment transaction ID! */
  if (tran_entry->transaction_id == LF_NULL_TRANSACTION_ID)
    {
      local_tran = true;
      lf_tran_start_with_mb (tran_entry, false);
    }

  /* clean retired list, if possible */
  if (LF_TRAN_CLEANUP_NECESSARY (tran_entry))
    {
      if (lf_freelist_transport (tran_entry, freelist) != NO_ERROR)
	{
	  if (local_tran)
	    {
	      lf_tran_end_with_mb (tran_entry);
	    }
	  return NULL;
	}
    }

  /* claim an entry */
  while (true)
    {
      /* try to get a new entry form the safe stack */
      entry = lf_stack_pop (&freelist->available, edesc);

      if (entry != NULL)
	{
	  /* adjust counter */
	  ATOMIC_INC_32 (&freelist->available_cnt, -1);

	  if ((edesc->f_init != NULL) && (edesc->f_init (entry) != NO_ERROR))
	    {
	      /* can't initialize it */
	      if (local_tran)
		{
		  lf_tran_end_with_mb (tran_entry);
		}
	      return NULL;
	    }

	  /* initialize next */
	  OF_GET_PTR_DEREF (entry, edesc->of_next) = NULL;

	  /* done! */
	  if (local_tran)
	    {
	      lf_tran_end_with_mb (tran_entry);
	    }
	  return entry;
	}
      else
	{
	  /* NOTE: as you can see, more than one thread can start allocating a new freelist_entry block at the same
	   * time; this behavior is acceptable given that the freelist has a _low_ enough value of block_size; it sure
	   * beats synchronizing the operations */
	  if (lf_freelist_alloc_block (freelist) != NO_ERROR)
	    {
	      if (local_tran)
		{
		  lf_tran_end_with_mb (tran_entry);
		}
	      return NULL;
	    }

	  /* retry a stack pop */
	  continue;
	}
    }

  /* impossible! */
  assert (false);
  if (local_tran)
    {
      lf_tran_end_with_mb (tran_entry);
    }
  return NULL;
}

/*
 * lf_freelist_retire () - retire an entry
 *   returns: error code or NO_ERROR
 *   tran_entry(in): tran entry to store local retired entries
 *   freelist(in): freelist to use
 *   entry(in): entry to retire
 */
int
lf_freelist_retire (LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist, void *entry)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  UINT64 *tran_id;
  int ret = NO_ERROR;
  bool local_tran = false;

  assert (entry != NULL && tran_entry != NULL);
  assert (freelist != NULL);
  edesc = freelist->entry_desc;
  assert (edesc != NULL);

  /* see if local transaction is required */
  if (tran_entry->transaction_id == LF_NULL_TRANSACTION_ID)
    {
      local_tran = true;
      lf_tran_start_with_mb (tran_entry, true);
    }
  else if (!tran_entry->did_incr)
    {
      lf_tran_start_with_mb (tran_entry, true);
    }

  /* do a retired list cleanup, if possible */
  if (LF_TRAN_CLEANUP_NECESSARY (tran_entry))
    {
      if (lf_freelist_transport (tran_entry, freelist) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  /* set transaction index of entry */
  tran_id = (UINT64 *) OF_GET_PTR (entry, edesc->of_del_tran_id);
  *tran_id = tran_entry->transaction_id;

  /* push to local list */
  ret = lf_stack_push (&tran_entry->retired_list, entry, edesc);
  if (ret == NO_ERROR)
    {
      /* for stats purposes */
      ATOMIC_INC_32 (&freelist->retired_cnt, 1);

      LF_UNITTEST_INC (&lf_retires, 1);
    }
  else
    {
      assert (false);
    }

  /* end local transaction */
  if (local_tran)
    {
      lf_tran_end_with_mb (tran_entry);
    }

  return ret;
}

/*
 * lf_freelist_transport () - transport local retired entries to freelist
 *   returns: error code or NO_ERROR
 *   tran_entry(in): transaction entry to use
 *   freelist(in): freelist
 */
int
lf_freelist_transport (LF_TRAN_ENTRY * tran_entry, LF_FREELIST * freelist)
{
  LF_TRAN_SYSTEM *tran_system;
  LF_ENTRY_DESCRIPTOR *edesc;
  UINT64 min_tran_id = 0;
  int transported_count = 0;
  void *list = NULL, *list_trailer = NULL;
  void *aval_first = NULL, *aval_last = NULL;
  void *old_head;

  assert (freelist != NULL);
  assert (tran_entry != NULL);
  tran_system = tran_entry->tran_system;
  assert (tran_system != NULL);
  edesc = freelist->entry_desc;
  assert (edesc != NULL);

  /* check if cleanup is actually necessary */
  min_tran_id = tran_system->min_active_transaction_id;
  if (min_tran_id <= tran_entry->last_cleanup_id)
    {
      /* nothing to do */
      return NO_ERROR;
    }

  /* walk private list and unlink old entries */
  for (list = tran_entry->retired_list; list != NULL; list = OF_GET_PTR_DEREF (list, edesc->of_local_next))
    {

      if (aval_first == NULL)
	{
	  UINT64 *del_id = (UINT64 *) OF_GET_PTR (list, edesc->of_del_tran_id);
	  assert (del_id != NULL);

	  if (*del_id < min_tran_id)
	    {
	      /* found first reusable entry - since list is ordered by descending transaction id, entries that follow
	       * are also reusable */
	      aval_first = list;
	      aval_last = list;

	      /* uninit the reusable entry */
	      if (edesc->f_uninit != NULL)
		{
		  edesc->f_uninit (list);
		}

	      transported_count = 1;
	    }
	  else
	    {
	      /* save trailer */
	      list_trailer = list;
	    }
	}
      else
	{
	  /* continue until we get to tail */
	  aval_last = list;

	  /* uninit the reusable entry */
	  if (edesc->f_uninit != NULL)
	    {
	      edesc->f_uninit (list);
	    }

	  transported_count++;
	}
    }

  if (aval_first != NULL)
    {
      if (list_trailer != NULL)
	{
	  /* unlink found part of list */
	  OF_GET_PTR_DEREF (list_trailer, edesc->of_local_next) = NULL;
	}
      else
	{
	  /* whole list can be reused */
	  tran_entry->retired_list = NULL;
	}

      /* make sure we don't append an unlinked sublist */
      MEMORY_BARRIER ();

      /* link part of list to available */
      do
	{
	  old_head = VOLATILE_ACCESS (freelist->available, void *);
	  OF_GET_PTR_DEREF (aval_last, edesc->of_local_next) = old_head;
	}
      while (!ATOMIC_CAS_ADDR (&freelist->available, old_head, aval_first));

      /* update counters */
      ATOMIC_INC_32 (&freelist->available_cnt, transported_count);
      ATOMIC_INC_32 (&freelist->retired_cnt, -transported_count);

      LF_UNITTEST_INC (&lf_transports, transported_count);
    }

  /* register cleanup */
  tran_entry->last_cleanup_id = min_tran_id;

  /* all ok */
  return NO_ERROR;
}

/*
 * lf_io_list_find () - find in an insert-only list
 *   returns: error code or NO_ERROR
 *   list_p(in): address of list head
 *   key(in): key to search for
 *   edesc(in): entry descriptor
 *   entry(out): found entry or NULL
 */
int
lf_io_list_find (void **list_p, void *key, LF_ENTRY_DESCRIPTOR * edesc, void **entry)
{
  pthread_mutex_t *entry_mutex;
  void **curr_p;
  void *curr;
  int rv;

  assert (list_p != NULL && edesc != NULL);
  assert (key != NULL && entry != NULL);

  /* by default, not found */
  (*entry) = NULL;

  curr_p = list_p;
  curr = *((void *volatile *) curr_p);

  /* search */
  while (curr != NULL)
    {
      if (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0)
	{
	  /* found! */
	  if (edesc->using_mutex)
	    {
	      /* entry has a mutex protecting it's members; lock it */
	      entry_mutex = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);
	      rv = pthread_mutex_lock (entry_mutex);
	    }

	  (*entry) = curr;
	  return NO_ERROR;
	}

      /* advance */
      curr_p = (void **) OF_GET_REF (curr, edesc->of_next);
      curr = *((void *volatile *) curr_p);
    }

  /* all ok but not found */
  return NO_ERROR;
}

/*
 * lf_io_list_find_or_insert () - find an entry in an insert only list or insert
 *				  new entry if not found
 *   returns: error code or NO_ERROR
 *   list_p(in): address of list head
 *   new_entry(in): new entry to insert if entry with same key does not exist
 *   edesc(in): entry descriptor
 *   entry(out): found entry or "new_entry" if inserted
 *
 * NOTE: key is extracted from new_entry
 */
int
lf_io_list_find_or_insert (void **list_p, void *new_entry, LF_ENTRY_DESCRIPTOR * edesc, void **entry)
{
  pthread_mutex_t *entry_mutex;
  void **curr_p;
  void *curr;
  void *key;
  int rv;

  assert (list_p != NULL && edesc != NULL);
  assert (new_entry != NULL && entry != NULL);

  /* extract key from new entry */
  key = OF_GET_PTR (new_entry, edesc->of_key);

  /* by default, not found */
  (*entry) = NULL;

restart_search:
  curr_p = list_p;
  curr = *((void *volatile *) curr_p);

  /* search */
  while (curr_p != NULL)
    {
      /* is this the droid we are looking for? */
      if (curr != NULL)
	{
	  if (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0)
	    {
	      /* found! */
	      if (edesc->using_mutex)
		{
		  /* entry has a mutex protecting it's members; lock it */
		  entry_mutex = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);
		  rv = pthread_mutex_lock (entry_mutex);
		}

	      (*entry) = curr;
	      return NO_ERROR;
	    }

	  /* advance */
	  curr_p = (void **) OF_GET_REF (curr, edesc->of_next);
	  curr = *((void *volatile *) curr_p);
	}
      else
	{
	  /* end of bucket, we must insert */
	  (*entry) = new_entry;
	  if (edesc->using_mutex)
	    {
	      /* entry has a mutex protecting it's members; lock it */
	      entry_mutex = (pthread_mutex_t *) OF_GET_PTR ((*entry), edesc->of_mutex);
	      rv = pthread_mutex_lock (entry_mutex);
	    }

	  /* attempt an add */
	  if (!ATOMIC_CAS_ADDR (curr_p, NULL, (*entry)))
	    {
	      if (edesc->using_mutex)
		{
		  /* link failed, unlock mutex */
		  entry_mutex = (pthread_mutex_t *) OF_GET_PTR ((*entry), edesc->of_mutex);
		  pthread_mutex_unlock (entry_mutex);
		}

	      goto restart_search;
	    }

	  /* done! */
	  return NO_ERROR;
	}
    }

  /* impossible case */
  assert (false);
  return ER_FAILED;
}

/*
 * lf_list_find () - find an entry in list
 *   returns: error code or NO_ERROR
 *   tran(in): lock free transaction system
 *   list_p(in): address of list head
 *   key(in): key to search for
 *   behavior_flags(in/out): flags that control restart behavior
 *   edesc(in): entry descriptor
 *   entry(out): entry (if found) or NULL
 */
int
lf_list_find (LF_TRAN_ENTRY * tran, void **list_p, void *key, int *behavior_flags, LF_ENTRY_DESCRIPTOR * edesc,
	      void **entry)
{
  pthread_mutex_t *entry_mutex;
  void **curr_p;
  void *curr;
  int rv;

  assert (tran != NULL);
  assert (list_p != NULL && edesc != NULL);
  assert (key != NULL && entry != NULL);

  /* by default, not found */
  (*entry) = NULL;

  lf_tran_start_with_mb (tran, false);

restart_search:
  curr_p = list_p;
  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));

  /* search */
  while (curr != NULL)
    {
      if (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0)
	{
	  /* found! */
	  if (edesc->using_mutex)
	    {
	      /* entry has a mutex protecting it's members; lock it */
	      entry_mutex = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);
	      rv = pthread_mutex_lock (entry_mutex);

	      /* mutex has been locked, no need to keep transaction */
	      lf_tran_end_with_mb (tran);

	      if (ADDR_HAS_MARK (OF_GET_PTR_DEREF (curr, edesc->of_next)))
		{
		  /* while waiting for lock, somebody else deleted the entry; restart the search */
		  pthread_mutex_unlock (entry_mutex);

		  if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		    {
		      *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		      return NO_ERROR;
		    }
		  else
		    {
		      goto restart_search;
		    }
		}
	    }

	  (*entry) = curr;
	  return NO_ERROR;
	}

      /* advance */
      curr_p = (void **) OF_GET_REF (curr, edesc->of_next);
      curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));
    }

  /* all ok but not found */
  lf_tran_end_with_mb (tran);
  return NO_ERROR;
}

/*
 * lf_list_insert_internal () - insert an entry into latch-free list.
 *   returns: error code or NO_ERROR
 *   tran(in): lock free transaction system
 *   list_p(in): address of list head
 *   key(in): key to search for or insert
 *   behavior_flags(in/out): flags that control restart behavior
 *   edesc(in): entry descriptor
 *   freelist(in): freelist to fetch new entries from
 *   entry(in/out): found entry or inserted entry
 *   inserted(out): returns 1 if inserted, 0 if found or not inserted.
 *
 * Behavior flags:
 *
 * LF_LIST_BF_RETURN_ON_RESTART - When insert fails because last entry in bucket was deleted, if this flag is set,
 *				  then the operation is restarted from here, instead of looping inside
 *				  lf_list_insert_internal (as a consequence, hash key is recalculated).
 *				  NOTE: Currently, this flag is always used (I must find out why).
 *
 * LF_LIST_BF_INSERT_GIVEN	- If this flag is set, the caller means to force its own entry into hash table.
 *				  When the flag is not set, a new entry is claimed from freelist.
 *				  NOTE: If an entry with the same key already exists, the entry given as argument is
 *					automatically retired.
 *
 * LF_LIST_BF_FIND_OR_INSERT	- If this flag is set and an entry for the same key already exists, the existing
 *				  key will be output. If the flag is not set and key exists, insert just gives up
 *				  and a NULL entry is output.
 *				  NOTE: insert will not give up when key exists, if edesc->f_update is provided.
 *					a new key is generated and insert is restarted.
 */
static int
lf_list_insert_internal (LF_TRAN_ENTRY * tran, void **list_p, void *key, int *behavior_flags,
			 LF_ENTRY_DESCRIPTOR * edesc, LF_FREELIST * freelist, void **entry, int *inserted)
{
  /* Macro's to avoid repeating the code (and making mistakes) */

  /* Assert used to make sure the current entry is protected by either transaction or mutex. */
#define LF_ASSERT_USE_MUTEX_OR_TRAN_STARTED() \
  assert (is_tran_started == !edesc->using_mutex); /* The transaction is started if and only if we don't use mutex */ \
  assert (!edesc->using_mutex || entry_mutex) /* If we use mutex, we have a mutex locked. */

  /* Start a transaction */
#define LF_START_TRAN() \
  if (!is_tran_started) lf_tran_start_with_mb (tran, false); is_tran_started = true
#define LF_START_TRAN_FORCE() \
  assert (!is_tran_started); lf_tran_start_with_mb (tran, false); is_tran_started = true

  /* End a transaction if started */
#define LF_END_TRAN() \
  if (is_tran_started) lf_tran_end_with_mb (tran)
  /* Force end transaction; a transaction is expected */
#define LF_END_TRAN_FORCE() \
  assert (is_tran_started); lf_tran_end_with_mb (tran); is_tran_started = false

#if defined (UNITTEST_LF)
  /* Lock current entry (using mutex is expected) */
#define LF_LOCK_ENTRY(tolock) \
  assert (tran->locked_mutex == NULL); \
  assert (edesc->using_mutex); \
  assert ((tolock) != NULL); \
  assert (entry_mutex == NULL); \
  /* entry has a mutex protecting it's members; lock it */ \
  entry_mutex = (pthread_mutex_t *) OF_GET_PTR (tolock, edesc->of_mutex); \
  tran->locked_mutex_line = __LINE__; \
  tran->locked_mutex = entry_mutex; \
  rv = pthread_mutex_lock (entry_mutex)

  /* Unlock current entry (if it was locked). */
#define LF_UNLOCK_ENTRY() \
  if (edesc->using_mutex && entry_mutex) \
    { \
      assert (tran->locked_mutex == entry_mutex); \
      tran->locked_mutex = NULL; \
      pthread_mutex_unlock (entry_mutex); \
      entry_mutex = NULL; \
    }
  /* Force unlocking current entry (it is expected to be locked). */
#define LF_UNLOCK_ENTRY_FORCE() \
  assert (edesc->using_mutex && entry_mutex != NULL); \
  assert (tran->locked_mutex == entry_mutex); \
  tran->locked_mutex = NULL; \
  pthread_mutex_unlock (entry_mutex); \
  entry_mutex = NULL
#else /* !UNITTEST_LF */
  /* Lock current entry (using mutex is expected) */
#define LF_LOCK_ENTRY(tolock) \
  assert (edesc->using_mutex); \
  assert ((tolock) != NULL); \
  assert (entry_mutex == NULL); \
  /* entry has a mutex protecting it's members; lock it */ \
  entry_mutex = (pthread_mutex_t *) OF_GET_PTR (tolock, edesc->of_mutex); \
  rv = pthread_mutex_lock (entry_mutex)

  /* Unlock current entry (if it was locked). */
#define LF_UNLOCK_ENTRY() \
  if (edesc->using_mutex && entry_mutex) \
    { \
      pthread_mutex_unlock (entry_mutex); \
      entry_mutex = NULL; \
    }
  /* Force unlocking current entry (it is expected to be locked). */
#define LF_UNLOCK_ENTRY_FORCE() \
  assert (edesc->using_mutex && entry_mutex != NULL); \
  pthread_mutex_unlock (entry_mutex); \
  entry_mutex = NULL
#endif /* !UNITTEST_LF */

  pthread_mutex_t *entry_mutex = NULL;	/* Locked entry mutex when not NULL */
  void **curr_p;
  void *curr;
  int rv;
  bool is_tran_started = false;

  assert (tran != NULL);
  assert (list_p != NULL && edesc != NULL);
  assert (key != NULL && entry != NULL);
  assert (freelist != NULL);
  assert (behavior_flags != NULL);

  assert ((*entry != NULL) == LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN));

  if (inserted != NULL)
    {
      *inserted = 0;
    }

restart_search:

  LF_UNITTEST_INC (&lf_list_inserts, 1);

  LF_START_TRAN_FORCE ();

  curr_p = list_p;
  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));

  /* search */
  while (curr_p != NULL)
    {
      assert (is_tran_started);
      assert (entry_mutex == NULL);

      if (curr != NULL)
	{
	  if (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0)
	    {
	      /* found an entry with the same key. */

	      LF_UNITTEST_INC (&lf_list_inserts_found, 1);

	      if (!LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN) && *entry != NULL)
		{
		  /* save this for further (local) use. */
		  assert (tran->temp_entry == NULL);
		  tran->temp_entry = *entry;

		  LF_UNITTEST_INC (&lf_list_inserts_save_temp_1, 1);
		  LF_UNITTEST_INC (&lf_temps, 1);

		  /* don't keep the entry around. */
		  *entry = NULL;
		}

	      if (edesc->using_mutex)
		{
		  /* entry has a mutex protecting it's members; lock it */
		  LF_LOCK_ENTRY (curr);

		  /* mutex has been locked, no need to keep transaction alive */
		  LF_END_TRAN_FORCE ();

		  if (ADDR_HAS_MARK (OF_GET_PTR_DEREF (curr, edesc->of_next)))
		    {
		      /* while waiting for lock, somebody else deleted the entry; restart the search */
		      LF_UNLOCK_ENTRY_FORCE ();

		      if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
			{
			  *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
			  return NO_ERROR;
			}
		      else
			{
			  goto restart_search;
			}
		    }
		}

	      LF_ASSERT_USE_MUTEX_OR_TRAN_STARTED ();
	      if (edesc->f_duplicate != NULL)
		{
		  /* we have a duplicate key callback. */
		  if (edesc->f_duplicate (key, curr) != NO_ERROR)
		    {
		      ASSERT_ERROR ();
		      LF_END_TRAN ();
		      LF_UNLOCK_ENTRY ();
		      return NO_ERROR;
		    }
#if 1
		  LF_LIST_BR_SET_FLAG (behavior_flags, LF_LIST_BR_RESTARTED);
		  LF_END_TRAN ();
		  LF_UNLOCK_ENTRY ();
		  return NO_ERROR;
#else /* !1 = 0 */
		  /* Could we have such cases that we just update existing entry without modifying anything else?
		   * And would it be usable with just a flag?
		   * Then this code may be used.
		   * So far we have only one usage for f_duplicate, which increment SESSION_ID and requires
		   * restarting hash search. This will be the usual approach if f_duplicate.
		   * If we just increment a counter in existing entry, we don't need to do anything else. This however
		   * most likely depends on f_duplicate implementation. Maybe it is more useful to give behavior_flags
		   * argument to f_duplicate to tell us if restart is or is not needed.
		   */
		  if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_RESTART_ON_DUPLICATE))
		    {
		      LF_LIST_BR_SET_FLAG (behavior_flags, LF_LIST_BR_RESTARTED);
		      LF_END_TRAN ();
		      LF_UNLOCK_ENTRY ();
		      return NO_ERROR;
		    }
		  else
		    {
		      /* duplicate does not require restarting search. */
		      if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN))
			{
			  /* Could not be inserted. Retire the entry. */
			  lf_freelist_retire (tran, freelist, *entry);
			  *entry = NULL;
			}

		      /* fall through to output current entry. */
		    }
#endif /* 0 */
		}
	      else
		{
		  if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN))
		    {
		      /* the given entry could not be inserted. retire it. */
		      lf_freelist_retire (tran, freelist, *entry);
		      *entry = NULL;
		    }

		  if (!LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_FIND_OR_INSERT))
		    {
		      /* found entry is not accepted */
		      LF_END_TRAN ();
		      LF_UNLOCK_ENTRY ();
		      return NO_ERROR;
		    }

		  /* fall through to output current entry. */
		}

	      assert (*entry == NULL);
	      LF_ASSERT_USE_MUTEX_OR_TRAN_STARTED ();
	      /* We don't end transaction or unlock mutex here. */
	      *entry = curr;
	      return NO_ERROR;
	    }

	  /* advance */
	  curr_p = (void **) OF_GET_REF (curr, edesc->of_next);
	  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));
	}
      else
	{
	  /* end of bucket, we must insert */
	  if (*entry == NULL)
	    {
	      assert (!LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN));

	      *entry = lf_freelist_claim (tran, freelist);
	      if (*entry == NULL)
		{
		  assert (false);
		  LF_END_TRAN_FORCE ();
		  return ER_FAILED;
		}

	      LF_UNITTEST_INC (&lf_list_inserts_claim, 1);

	      /* set it's key */
	      if (edesc->f_key_copy (key, OF_GET_PTR (*entry, edesc->of_key)) != NO_ERROR)
		{
		  assert (false);
		  LF_END_TRAN_FORCE ();
		  return ER_FAILED;
		}
	    }

	  if (edesc->using_mutex)
	    {
	      /* entry has a mutex protecting it's members; lock it */
	      LF_LOCK_ENTRY (*entry);
	    }

	  /* attempt an add */
	  if (!ATOMIC_CAS_ADDR (curr_p, NULL, (*entry)))
	    {
	      if (edesc->using_mutex)
		{
		  /* link failed, unlock mutex */
		  LF_UNLOCK_ENTRY_FORCE ();
		}

	      LF_UNITTEST_INC (&lf_list_inserts_fail_link, 1);

	      /* someone added before us, restart process */
	      if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_RETURN_ON_RESTART))
		{
		  if (!LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_INSERT_GIVEN))
		    {
		      assert (tran->temp_entry == NULL);
		      tran->temp_entry = *entry;
		      *entry = NULL;

		      LF_UNITTEST_INC (&lf_list_inserts_save_temp_2, 1);
		      LF_UNITTEST_INC (&lf_temps, 1);
		    }
		  LF_LIST_BR_SET_FLAG (behavior_flags, LF_LIST_BR_RESTARTED);
		  LF_END_TRAN_FORCE ();
		  return NO_ERROR;
		}
	      else
		{
		  LF_END_TRAN_FORCE ();
		  goto restart_search;
		}
	    }

	  LF_UNITTEST_INC (&lf_list_inserts_success_link, 1);

	  /* end transaction if mutex is acquired */
	  if (edesc->using_mutex)
	    {
	      LF_END_TRAN_FORCE ();
	    }
	  if (inserted)
	    {
	      *inserted = 1;
	    }
	  LF_UNITTEST_INC (&lf_hash_size, 1);

	  /* done! */
	  return NO_ERROR;
	}
    }

  /* impossible case */
  assert (false);
  return ER_FAILED;

#undef LF_ASSERT_USE_MUTEX_OR_TRAN_STARTED
#undef LF_START_TRAN
#undef LF_START_TRAN_FORCE
#undef LF_END_TRAN
#undef LF_END_TRAN_FORCE
#undef LF_LOCK_ENTRY
#undef LF_UNLOCK_ENTRY
#undef LF_UNLOCK_ENTRY_FORCE
}

/*
 * lf_list_delete () - delete an entry from a list
 *   returns: error code or NO_ERROR
 *   tran(in): lock free transaction system
 *   list_p(in): address of list head
 *   key(in): key to search for
 *   locked_entry(in): entry already locked.
 *   behavior_flags(in/out): flags that control restart behavior
 *   edesc(in): entry descriptor
 *   freelist(in): freelist to place deleted entries to
 *   success(out): 1 if entry was deleted, 0 otherwise
 */
int
lf_list_delete (LF_TRAN_ENTRY * tran, void **list_p, void *key, void *locked_entry, int *behavior_flags,
		LF_ENTRY_DESCRIPTOR * edesc, LF_FREELIST * freelist, int *success)
{
  /* Start a transaction */
#define LF_START_TRAN_FORCE() \
  assert (!is_tran_started); lf_tran_start_with_mb (tran, false); is_tran_started = true

  /* Promote from transaction without incremented transaction ID to transaction with incremented transaction ID. */
#define LF_PROMOTE_TRAN_FORCE() \
  assert (is_tran_started); MEMORY_BARRIER (); lf_tran_start_with_mb (tran, true)

  /* End a transaction */
  /* Force end transaction; a transaction is expected */
#define LF_END_TRAN_FORCE() \
  assert (is_tran_started); lf_tran_end_with_mb (tran); is_tran_started = false

#if defined (UNITTEST_LF)
  /* Lock current entry (using mutex is expected) */
#define LF_LOCK_ENTRY(tolock) \
  assert (edesc->using_mutex); \
  assert ((tolock) != NULL); \
  assert (entry_mutex == NULL); \
  /* entry has a mutex protecting it's members; lock it */ \
  entry_mutex = (pthread_mutex_t *) OF_GET_PTR (tolock, edesc->of_mutex); \
  assert (tran->locked_mutex == NULL); \
  tran->locked_mutex = entry_mutex; \
  tran->locked_mutex_line = __LINE__; \
  rv = pthread_mutex_lock (entry_mutex)

  /* Force unlocking current entry (it is expected to be locked). */
#define LF_UNLOCK_ENTRY_FORCE() \
  assert (edesc->using_mutex && entry_mutex != NULL); \
  assert (tran->locked_mutex == entry_mutex); \
  tran->locked_mutex = NULL; \
  pthread_mutex_unlock (entry_mutex); \
  entry_mutex = NULL
#else /* !UNITTEST_LF */
  /* Lock current entry (using mutex is expected) */
#define LF_LOCK_ENTRY(tolock) \
  assert (edesc->using_mutex); \
  assert ((tolock) != NULL); \
  assert (entry_mutex == NULL); \
  /* entry has a mutex protecting it's members; lock it */ \
  entry_mutex = (pthread_mutex_t *) OF_GET_PTR (tolock, edesc->of_mutex); \
  rv = pthread_mutex_lock (entry_mutex)

  /* Force unlocking current entry (it is expected to be locked). */
#define LF_UNLOCK_ENTRY_FORCE() \
  assert (edesc->using_mutex && entry_mutex != NULL); \
  pthread_mutex_unlock (entry_mutex); \
  entry_mutex = NULL
#endif /* !UNITTEST_LF */

  pthread_mutex_t *entry_mutex = NULL;
  void **curr_p, **next_p;
  void *curr, *next;
  int rv;
  bool is_tran_started = false;

  /* reset success flag */
  if (success != NULL)
    {
      *success = 0;
    }

  assert (list_p != NULL && edesc != NULL && key != NULL);
  assert (freelist != NULL);
  assert (tran != NULL && tran->tran_system != NULL);

restart_search:

  LF_UNITTEST_INC (&lf_list_deletes, 1);

  /* read transaction; we start a write transaction only after remove */
  LF_START_TRAN_FORCE ();

  curr_p = list_p;
  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));

  /* search */
  while (curr != NULL)
    {
      /* is this the droid we are looking for? */
      if (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0)
	{
	  if (locked_entry != NULL && locked_entry != curr)
	    {
	      assert (edesc->using_mutex && !LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_LOCK_ON_DELETE));

	      /* We are here because lf_hash_delete_already_locked was called. The entry found by matching key is
	       * different from the entry we were trying to delete.
	       * This is possible (please find the description of lf_hash_delete_already_locked). */
	      LF_UNITTEST_INC (&lf_list_deletes_not_match, 1);
	      LF_END_TRAN_FORCE ();
	      return NO_ERROR;
	    }

	  /* fetch next entry */
	  next_p = (void **) OF_GET_REF (curr, edesc->of_next);
	  next = ADDR_STRIP_MARK (*((void *volatile *) next_p));

	  LF_UNITTEST_INC (&lf_list_deletes_found, 1);

	  /* set mark on next pointer; this way, if anyone else is trying to delete the next entry, it will fail */
	  if (!ATOMIC_CAS_ADDR (next_p, next, ADDR_WITH_MARK (next)))
	    {
	      /* joke's on us, this time; somebody else marked it before */

	      LF_UNITTEST_INC (&lf_list_deletes_fail_mark_next, 1);

	      LF_END_TRAN_FORCE ();
	      if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		{
		  *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		  assert ((*behavior_flags) & LF_LIST_BR_RESTARTED);
		  return NO_ERROR;
		}
	      else
		{
		  goto restart_search;
		}
	    }

	  /* lock mutex if necessary */
	  if (edesc->using_mutex)
	    {
	      if (LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_LOCK_ON_DELETE))
		{
		  LF_LOCK_ENTRY (curr);
		}
	      else
		{
		  /* Must be already locked! */
#if defined (UNITTEST_LF)
		  assert (locked_entry != NULL && locked_entry == curr);
#endif /* UNITTEST_LF */
		  entry_mutex = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);

#if defined (UNITTEST_LF)
		  assert (tran->locked_mutex != NULL && tran->locked_mutex == entry_mutex);
#endif /* UNITTEST_LF */
		}

	      /* since we set the mark, nobody else can delete it, so we have nothing else to check */
	    }

	  /* unlink */
	  if (!ATOMIC_CAS_ADDR (curr_p, curr, next))
	    {
	      /* unlink failed; first step is to remove lock (if applicable) */
	      if (edesc->using_mutex && LF_LIST_BF_IS_FLAG_SET (behavior_flags, LF_LIST_BF_LOCK_ON_DELETE))
		{
		  LF_UNLOCK_ENTRY_FORCE ();
		}

	      LF_UNITTEST_INC (&lf_list_deletes_fail_unlink, 1);

	      /* remove mark and restart search */
	      if (!ATOMIC_CAS_ADDR (next_p, ADDR_WITH_MARK (next), next))
		{
		  /* impossible case */
		  assert (false);
		  LF_END_TRAN_FORCE ();
		  return ER_FAILED;
		}

	      LF_END_TRAN_FORCE ();
	      if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		{
		  *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		  assert ((*behavior_flags) & LF_LIST_BR_RESTARTED);
		  return NO_ERROR;
		}
	      else
		{
		  goto restart_search;
		}
	    }
	  /* unlink successful */

	  LF_UNITTEST_INC (&lf_list_deletes_success_unlink, 1);

	  /* unlock mutex */
	  if (edesc->using_mutex)
	    {
	      LF_UNLOCK_ENTRY_FORCE ();
	    }

	  LF_PROMOTE_TRAN_FORCE ();

	  /* now we can feed the entry to the freelist and forget about it */
	  if (lf_freelist_retire (tran, freelist, curr) != NO_ERROR)
	    {
	      assert (false);
	      LF_END_TRAN_FORCE ();
	      return ER_FAILED;
	    }

	  /* end the transaction */
	  LF_END_TRAN_FORCE ();

	  /* set success flag */
	  if (success != NULL)
	    {
	      *success = 1;
	    }
	  LF_UNITTEST_INC (&lf_hash_size, -1);

	  /* success! */
	  return NO_ERROR;
	}

      /* advance */
      curr_p = (void **) OF_GET_REF (curr, edesc->of_next);
      curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));
    }

  LF_UNITTEST_INC (&lf_list_deletes_not_found, 1);

  /* search yielded no result so no delete was performed */
  LF_END_TRAN_FORCE ();
  return NO_ERROR;

#undef LF_START_TRAN_FORCE
#undef LF_PROMOTE_TRAN_FORCE
#undef LF_END_TRAN_FORCE
#undef LF_LOCK_ENTRY
#undef LF_UNLOCK_ENTRY_FORCE
}

/*
 * lf_hash_init () - initialize a lock free hash table
 *   returns: error code or NO_ERROR
 *   table(in): hash table structure to initialize
 *   freelist(in): freelist to use for entries
 *   hash_size(in): size of hash table
 *   edesc(in): entry descriptor
 */
int
lf_hash_init (LF_HASH_TABLE * table, LF_FREELIST * freelist, unsigned int hash_size, LF_ENTRY_DESCRIPTOR * edesc)
{
  assert (table != NULL && freelist != NULL && edesc != NULL);
  assert (hash_size > 0);

  if (table->buckets != NULL)
    {
      /* already initialized */
      return NO_ERROR;
    }

  /* register values */
  table->freelist = freelist;
  table->hash_size = hash_size;
  table->entry_desc = edesc;

  /* allocate bucket space */
  table->buckets = (void **) malloc (sizeof (void *) * hash_size);
  if (table->buckets == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (void *) * hash_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  else
    {
      /* zero all */
      memset (table->buckets, 0, sizeof (void *) * hash_size);
    }

  /* allocate backbuffer */
  table->backbuffer = (void **) malloc (sizeof (void *) * hash_size);
  if (table->backbuffer == NULL)
    {
      free (table->buckets);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (void *) * hash_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  else
    {
      int i;

      /* put backbuffer in a "locked" state */
      for (i = 0; i < (int) hash_size; i++)
	{
	  table->backbuffer[i] = ADDR_WITH_MARK (NULL);
	}

      /* initialize mutex */
      pthread_mutex_init (&table->backbuffer_mutex, NULL);
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * lf_hash_destroy () - destroy a hash table
 *   table(in): hash table to destroy
 */
void
lf_hash_destroy (LF_HASH_TABLE * table)
{
  unsigned int i;
  void *entry, *next;

  if (table == NULL)
    {
      return;
    }

  if (table->buckets)
    {
      /* free entries */
      for (i = 0; i < table->hash_size; i++)
	{
	  entry = table->buckets[i];

	  while (entry != NULL)
	    {
	      next = OF_GET_PTR_DEREF (entry, table->entry_desc->of_next);

	      if (table->entry_desc->f_uninit != NULL)
		{
		  table->entry_desc->f_uninit (entry);
		}
	      table->entry_desc->f_free (entry);
	      entry = next;
	    }
	}

      /* free memory */
      free (table->buckets);
      table->buckets = NULL;
    }

  pthread_mutex_destroy (&table->backbuffer_mutex);
  if (table->backbuffer)
    {
      free (table->backbuffer);
      table->backbuffer = NULL;
    }

  table->hash_size = 0;
}

/*
 * lf_hash_find () - find an entry in the hash table given a key
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table
 *   key(in): key of entry that we seek
 *   entry(out): existing or NULL otherwise
 */
int
lf_hash_find (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  unsigned int hash_value;
  int rc, bflags;

  assert (table != NULL && key != NULL && entry != NULL);
  edesc = table->entry_desc;
  assert (edesc != NULL);

restart:
  *entry = NULL;
  hash_value = edesc->f_hash (key, table->hash_size);
  if (hash_value >= table->hash_size)
    {
      assert (false);
      return ER_FAILED;
    }

  bflags = LF_LIST_BF_RETURN_ON_RESTART;
  rc = lf_list_find (tran, &table->buckets[hash_value], key, &bflags, edesc, entry);
  if ((rc == NO_ERROR) && (bflags & LF_LIST_BR_RESTARTED))
    {
      goto restart;
    }
  else
    {
      return rc;
    }
}

/*
 * lf_hash_insert_internal () - hash insert function.
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table
 *   key(in): key of entry that we seek
 *   bflags(in): behavior flags
 *   entry(out): existing or new entry
 *   inserted(out): returns 1 if inserted, 0 if found or not inserted.
 *
 * Behavior flags:
 *
 * LF_LIST_BF_RETURN_ON_RESTART - When insert fails because last entry in bucket was deleted, if this flag is set,
 *				  then the operation is restarted from here, instead of looping inside
 *				  lf_list_insert_internal (as a consequence, hash key is recalculated).
 *				  NOTE: Currently, this flag is always used (I must find out why).
 *
 * LF_LIST_BF_INSERT_GIVEN	- If this flag is set, the caller means to force its own entry into hash table.
 *				  When the flag is not set, a new entry is claimed from freelist.
 *				  NOTE: If an entry with the same key already exists, the entry given as argument is
 *					automatically retired.
 *
 * LF_LIST_BF_FIND_OR_INSERT	- If this flag is set and an entry for the same key already exists, the existing
 *				  key will be output. If the flag is not set and key exists, insert just gives up
 *				  and a NULL entry is output.
 *				  NOTE: insert will not give up when key exists, if edesc->f_update is provided.
 *					a new key is generated and insert is restarted.
 */
static int
lf_hash_insert_internal (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, int bflags, void **entry,
			 int *inserted)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  unsigned int hash_value;
  int rc;

  assert (table != NULL && key != NULL && entry != NULL);
  edesc = table->entry_desc;
  assert (edesc != NULL);

  LF_UNITTEST_INC (&lf_inserts, 1);

restart:
  if (LF_LIST_BF_IS_FLAG_SET (&bflags, LF_LIST_BF_INSERT_GIVEN))
    {
      assert (*entry != NULL);
    }
  else
    {
      *entry = NULL;
    }
  hash_value = edesc->f_hash (key, table->hash_size);
  if (hash_value >= table->hash_size)
    {
      assert (false);
      return ER_FAILED;
    }

  rc =
    lf_list_insert_internal (tran, &table->buckets[hash_value], key, &bflags, edesc, table->freelist, entry, inserted);
  if ((rc == NO_ERROR) && (bflags & LF_LIST_BR_RESTARTED))
    {
      bflags &= ~LF_LIST_BR_RESTARTED;
      LF_UNITTEST_INC (&lf_inserts_restart, 1);
      goto restart;
    }
  else
    {
      return rc;
    }
}

/*
 * lf_hash_find_or_insert () - find or insert an entry in the hash table
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table
 *   key(in): key of entry that we seek
 *   entry(out): existing or new entry
 *   inserted(out): returns 1 if inserted, 0 if found or not inserted.
 *
 */
int
lf_hash_find_or_insert (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry, int *inserted)
{
  return lf_hash_insert_internal (tran, table, key, LF_LIST_BF_RETURN_ON_RESTART | LF_LIST_BF_FIND_OR_INSERT, entry,
				  inserted);
}

/*
 * lf_hash_insert () - insert a new entry with a specified key.
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table
 *   key(in): key of entry to insert
 *   entry(out): new entry
 *   inserted(out): returns 1 if inserted, 0 if found or not inserted.
 *
 */
int
lf_hash_insert (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry, int *inserted)
{
  return lf_hash_insert_internal (tran, table, key, LF_LIST_BF_RETURN_ON_RESTART, entry, inserted);
}

/*
 * lf_hash_insert_given () - insert entry given as argument. if same key exists however, replace it with existing key.
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table
 *   key(in): key of entry to insert
 *   entry(in/out): new entry
 *   inserted(out): returns 1 if inserted, 0 if found or not inserted.
 *
 */
int
lf_hash_insert_given (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry, int *inserted)
{
  assert (entry != NULL && *entry != NULL);
  return lf_hash_insert_internal (tran, table, key,
				  LF_LIST_BF_RETURN_ON_RESTART | LF_LIST_BF_INSERT_GIVEN | LF_LIST_BF_FIND_OR_INSERT,
				  entry, inserted);
}

/*
 * lf_hash_delete_already_locked () - Delete hash entry without locking mutex.
 *
 * return	     : error code or NO_ERROR
 * tran (in)	     : LF transaction entry
 * table (in)	     : hash table
 * key (in)	     : key to seek
 * locked_entry (in) : locked entry
 * success (out)     : 1 if entry is deleted, 0 otherwise
 *
 * NOTE: Careful when calling this function. The typical scenario to call this function is to first find entry using
 *	 lf_hash_find and then call lf_hash_delete on the found entry.
 * NOTE: lf_hash_delete_already_locks can be called only if entry has mutexes.
 * NOTE: The delete will be successful only if the entry found by key matches the given entry.
 *	 Usually, the entry will match. However, we do have a limited scenario when a different entry with the same
 *	 key may be found:
 *	 1. Entry was found or inserted by this transaction.
 *	 2. Another transaction cleared the hash. All current entries are moved to back buffer and will be soon retired.
 *	 3. A third transaction inserts a new entry with the same key.
 *	 4. This transaction tries to delete the entry but the entry inserted by the third transaction si found.
 */
int
lf_hash_delete_already_locked (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void *locked_entry, int *success)
{
  assert (locked_entry != NULL);
  assert (table->entry_desc->using_mutex);
  return lf_hash_delete_internal (tran, table, key, locked_entry, LF_LIST_BF_RETURN_ON_RESTART, success);
}

/*
 * lf_hash_delete () - Delete hash entry. If the entries have mutex, it will lock the mutex before deleting.
 *
 * return	 : error code or NO_ERROR
 * tran (in)	 : LF transaction entry
 * table (in)	 : hash table
 * key (in)	 : key to seek
 * success (out) : 1 if entry is deleted, 0 otherwise
 */
int
lf_hash_delete (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, int *success)
{
  return lf_hash_delete_internal (tran, table, key, NULL, LF_LIST_BF_RETURN_ON_RESTART | LF_LIST_BF_LOCK_ON_DELETE,
				  success);
}


/*
 * lf_hash_delete () - delete an entry from the hash table
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table
 *   locked_entry(in): locked entry
 *   key(in): key to seek
 *   success(out): 1 if entry is deleted, 0 otherwise.
 */
static int
lf_hash_delete_internal (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void *locked_entry, int bflags,
			 int *success)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  unsigned int hash_value;
  int rc;

  assert (table != NULL && key != NULL);
  edesc = table->entry_desc;
  assert (edesc != NULL);

  LF_UNITTEST_INC (&lf_deletes, 1);

restart:
  if (success != NULL)
    {
      *success = 0;
    }
  hash_value = edesc->f_hash (key, table->hash_size);
  if (hash_value >= table->hash_size)
    {
      assert (false);
      return ER_FAILED;
    }

  rc = lf_list_delete (tran, &table->buckets[hash_value], key, locked_entry, &bflags, edesc, table->freelist, success);
  if ((rc == NO_ERROR) && (bflags & LF_LIST_BR_RESTARTED))
    {
      /* Remove LF_LIST_BR_RESTARTED from behavior flags. */
      bflags &= ~LF_LIST_BR_RESTARTED;
      LF_UNITTEST_INC (&lf_deletes_restart, 1);
      goto restart;
    }
  else
    {
      return rc;
    }
}

/*
 * lf_hash_clear () - clear the hash table
 *   returns: Void
 *   tran(in): LF transaction entry
 *   table(in): hash table to clear
 *
 * NOTE: This function is NOT lock free.
 */
void
lf_hash_clear (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  void **old_buckets, *curr, **next_p, *next;
  void *ret_head = NULL, *ret_tail = NULL;
  pthread_mutex_t *mutex_p;
  int rv, i, ret_count = 0;

  assert (tran != NULL && table != NULL && table->freelist != NULL);
  edesc = table->entry_desc;
  assert (edesc != NULL);

#if defined (UNITTEST_LF)
  assert (tran->locked_mutex == NULL);
#endif /* UNITTEST_LF */

  /* lock mutex */
  rv = pthread_mutex_lock (&table->backbuffer_mutex);

  /* swap bucket pointer with current backbuffer */
  do
    {
      old_buckets = VOLATILE_ACCESS (table->buckets, void **);
    }
  while (!ATOMIC_CAS_ADDR (&table->buckets, old_buckets, table->backbuffer));

  /* clear bucket buffer, containing remains of old entries marked for delete */
  for (i = 0; i < (int) table->hash_size; i++)
    {
      assert (table->backbuffer[i] == ADDR_WITH_MARK (NULL));
      table->buckets[i] = NULL;
    }

  /* register new backbuffer */
  table->backbuffer = old_buckets;

  /* retire all entries from old buckets; note that threads currently operating on the entries will not be disturbed
   * since the actual deletion is performed when the entries are no longer handled by active transactions */
  for (i = 0; i < (int) table->hash_size; i++)
    {
      do
	{
	  curr = ADDR_STRIP_MARK (VOLATILE_ACCESS (old_buckets[i], void *));
	}
      while (!ATOMIC_CAS_ADDR (&old_buckets[i], curr, ADDR_WITH_MARK (NULL)));

      while (curr)
	{
	  next_p = (void **) OF_GET_REF (curr, edesc->of_next);

	  /* unlink from hash chain */
	  do
	    {
	      next = ADDR_STRIP_MARK (VOLATILE_ACCESS (*next_p, void *));
	    }
	  while (!ATOMIC_CAS_ADDR (next_p, next, ADDR_WITH_MARK (next)));

	  /* wait for mutex */
	  if (edesc->using_mutex)
	    {
	      mutex_p = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);

	      rv = pthread_mutex_lock (mutex_p);
	      pthread_mutex_unlock (mutex_p);

	      /* there should be only one mutex lock-unlock per entry per access via bucket array, so locking/unlocking 
	       * once while the entry is inaccessible should be enough to guarantee nobody will be using it afterwards */
	    }

	  /* save and advance */
	  if (ret_head == NULL)
	    {
	      ret_head = curr;
	      ret_tail = curr;
	      ret_count = 1;
	    }
	  else
	    {
	      OF_GET_PTR_DEREF (ret_tail, edesc->of_local_next) = curr;
	      ret_tail = curr;
	      ret_count++;
	    }

	  /* advance */
	  curr = next;
	}
    }

  if (ret_head != NULL)
    {
      /* reuse entries */
      lf_tran_start_with_mb (tran, true);

      for (curr = ret_head; curr != NULL; curr = OF_GET_PTR_DEREF (curr, edesc->of_local_next))
	{
	  UINT64 *del_id = (UINT64 *) OF_GET_PTR (curr, edesc->of_del_tran_id);
	  *del_id = tran->transaction_id;
	}

      OF_GET_PTR_DEREF (ret_tail, edesc->of_local_next) = tran->retired_list;
      tran->retired_list = ret_head;

      ATOMIC_INC_32 (&table->freelist->retired_cnt, ret_count);

      LF_UNITTEST_INC (&lf_clears, ret_count);
      LF_UNITTEST_INC (&lf_hash_size, -ret_count);

      lf_tran_end_with_mb (tran);
    }

  /* unlock mutex and return to caller */
  pthread_mutex_unlock (&table->backbuffer_mutex);
}

/*
 * lf_hash_create_iterator () - create an iterator for a hash table
 *   iterator(out): iterator to be initialized
 *   tran_entry(in): 
 *   table(in): hash table to iterate on
 *   returns: void
 */
void
lf_hash_create_iterator (LF_HASH_TABLE_ITERATOR * iterator, LF_TRAN_ENTRY * tran_entry, LF_HASH_TABLE * table)
{
  assert (iterator != NULL && table != NULL);

  iterator->hash_table = table;
  iterator->curr = NULL;
  iterator->bucket_index = -1;
  iterator->tran_entry = tran_entry;
}

/*
 * lf_hash_iterate () - iterate using a pre-created iterator
 *   it(in): iterator
 *   returns: next entry or NULL on end
 *
 * NOTE: Absolutely no order guaranteed.
 * NOTE: Caller must not change HP_LEADER until end of iteration.
 * NOTE: This function will change HP_TRAILER.
 */
void *
lf_hash_iterate (LF_HASH_TABLE_ITERATOR * it)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  LF_TRAN_ENTRY *tran_entry;
  void **next_p;

  if (it == NULL || it->hash_table == NULL)
    {
      assert (false);
      return NULL;
    }

  edesc = it->hash_table->entry_desc;
  tran_entry = it->tran_entry;
  assert (edesc != NULL && tran_entry != NULL);

  do
    {
      /* save current leader as trailer */
      if (it->curr != NULL)
	{
	  if (edesc->using_mutex)
	    {
	      /* follow house rules: lock mutex */
	      pthread_mutex_t *mx;
	      mx = (pthread_mutex_t *) OF_GET_PTR (it->curr, edesc->of_mutex);
	      pthread_mutex_unlock (mx);
	    }

	  /* load next entry */
	  next_p = (void **) OF_GET_REF (it->curr, edesc->of_next);
	  it->curr = *(void *volatile *) next_p;
	  it->curr = ADDR_STRIP_MARK (it->curr);
	}
      else
	{
	  /* reset transaction for each bucket */
	  if (it->bucket_index >= 0)
	    {
	      lf_tran_end_with_mb (tran_entry);
	    }
	  lf_tran_start_with_mb (tran_entry, false);

	  /* load next bucket */
	  it->bucket_index++;
	  if (it->bucket_index < (int) it->hash_table->hash_size)
	    {
	      it->curr = VOLATILE_ACCESS (it->hash_table->buckets[it->bucket_index], void *);
	      it->curr = ADDR_STRIP_MARK (it->curr);
	    }
	  else
	    {
	      /* end */
	      lf_tran_end_with_mb (tran_entry);
	      return NULL;
	    }
	}

      if (it->curr != NULL)
	{
	  if (edesc->using_mutex)
	    {
	      pthread_mutex_t *mx;
	      int rv;

	      mx = (pthread_mutex_t *) OF_GET_PTR (it->curr, edesc->of_mutex);
	      rv = pthread_mutex_lock (mx);

	      if (ADDR_HAS_MARK (OF_GET_PTR_DEREF (it->curr, edesc->of_next)))
		{
		  /* deleted in the meantime, skip it */
		  continue;
		}
	    }
	}
    }
  while (it->curr == NULL);

  /* we have a valid entry */
  return it->curr;
}

/*
 * Lock-Free Circular Queue section -
 * Lock free circular queue algorithm is based on two cursors, one for
 * consuming entries and one for producing new entries and a state for each
 * entry in queue.
 *
 * Requirements:
 * 1. Queue must have a fixed maximum size. After reaching that size,
 *    producing new entries will be refused.
 *
 * Characteristics:
 * 1. Queue should perform well under any level of contention.
 * 2. Queue is guaranteed to be consistent. No produced entries are skipped
 *    by consume and no produced entries can be consumed twice.
 * 3. Queue doesn't guarantee the order of consume or the order of produce.
 *    This means that if thread 1 calls consume before thread 2, it is not
 *    guaranteed that the entry consumed by thread 1 was produced before
 *    the entry consumed by thread 2.
 *    Same for two concurrent producers.
 * 4. Consume/produce never spins on the same entry (next loop is
 *    guaranteed to be on a different cursor).
 */

/* States for circular queue entries */
#define LFCQ_READY_FOR_PRODUCE		((UINT64) 0x0000000000000000)
#define LFCQ_RESERVED_FOR_PRODUCE	((UINT64) 0x8000000000000000)
#define LFCQ_READY_FOR_CONSUME		((UINT64) 0x4000000000000000)
#define LFCQ_RESERVED_FOR_CONSUME	((UINT64) 0xC000000000000000)
#define LFCQ_STATE_MASK			((UINT64) 0xC000000000000000)

/*
 * lf_circular_queue_is_full () - Quick estimate if lock-free circular queue is full.
 *
 * return     : True if full, false otherwise.
 * queue (in) : Lock-free circular queue.
 */
bool
lf_circular_queue_is_full (LOCK_FREE_CIRCULAR_QUEUE * queue)
{
  UINT64 cc = ATOMIC_LOAD_64 (&queue->consume_cursor);
  UINT64 pc = ATOMIC_LOAD_64 (&queue->produce_cursor);

  /* The queue is full is consume cursor is behind produce cursor with one generation (difference of capacity + 1). */
  return cc + queue->capacity <= pc + 1;
}

/*
 * lf_circular_queue_is_empty () - Quick estimate if lock-free circular queue is empty.
 *
 * return     : True if empty, false otherwise.
 * queue (in) : Lock-free circular queue.
 */
bool
lf_circular_queue_is_empty (LOCK_FREE_CIRCULAR_QUEUE * queue)
{
  UINT64 cc = ATOMIC_LOAD_64 (&queue->consume_cursor);
  UINT64 pc = ATOMIC_LOAD_64 (&queue->produce_cursor);

  /* The queue is empty if the consume cursor is equal to produce cursor. */
  return cc <= pc;
}

/*
 * lf_circular_queue_approx_size () - Estimate size of queue.
 *
 * return     : Estimated size of queue.
 * queue (in) : Lock-free circular queue.
 */
int
lf_circular_queue_approx_size (LOCK_FREE_CIRCULAR_QUEUE * queue)
{
  /* We need to read consume cursor first and then the produce cursor. We cannot afford to have a "newer" consume
   * cursor that is bigger than the produce cursor.
   */
  UINT64 cc = ATOMIC_LOAD_64 (&queue->consume_cursor);
  UINT64 pc = ATOMIC_LOAD_64 (&queue->produce_cursor);

  if (pc <= cc)
    {
      return 0;
    }
  return (int) (pc - cc);
}

/*
 * lf_circular_queue_produce () - Add new entry to queue.
 *
 * return     : True if the entry was added, false otherwise.
 * queue (in) : Lock-free circular queue.
 * data (in)  : New entry data.
 */
bool
lf_circular_queue_produce (LOCK_FREE_CIRCULAR_QUEUE * queue, void *data)
{
  int entry_index;
  UINT64 produce_cursor;
  UINT64 consume_cursor;
  UINT64 old_state;
  UINT64 new_state;
  volatile UINT64 *entry_state_p;
#if !defined (NDEBUG) || defined (UNITTEST_CQ)
  bool was_not_ready = false;
#endif /* !NDEBUG || UNITTEST_CQ */

  assert (data != NULL);

  /* Loop until a free entry for produce is found or queue is full. */
  /* Since this may be done under concurrency with no locks, a produce cursor and an entry state for the cursor are
   * used to synchronize producing data. After reading the produce cursor, since there is no lock to protect it, other
   * producer may race to use it for its own produced data. The producer can gain an entry only if it successfully
   * changes the state from READY_FOR_PRODUCE to RESERVED_FOR_PRODUCE (using compare & swap). */
  while (true)
    {
      /* Get current cursors */
      consume_cursor = ATOMIC_LOAD_64 (&queue->consume_cursor);
      produce_cursor = ATOMIC_LOAD_64 (&queue->produce_cursor);

      if (consume_cursor + queue->capacity <= produce_cursor + 1)
	{
	  /* The queue is full, cannot produce new entries */
	  return false;
	}

      /* Compute entry's index in circular queue */
      entry_index = (int) produce_cursor % queue->capacity;
      entry_state_p = &queue->entry_state[entry_index];

#if !defined (NDEBUG) || defined (UNITTEST_CQ)
      was_not_ready =
	ATOMIC_LOAD_64 (entry_state_p) == ((produce_cursor - queue->capacity) | LFCQ_RESERVED_FOR_CONSUME);
#endif /* !NDEBUG || UNITTEST_CQ */

      /* Change state to RESERVED_FOR_PRODUCE. The expected current state is produce_cursor | LFCQ_READY_FOR_PRODUCE.
       * The produce cursor is included in the state to avoid reusing same produce cursor in a scenario like:
       * thrd1: load queue->produce_cursor and get preempted.
       * thrd2: successfully produce at same queue->produce_cursor.
       * thrd3: successfully consume from same queue->produce_cursor.
       * thrd1: wake up and try to produce at the same queue->produce_cursor.
       * This actually happened when states did not include produce_cursor.
       * Now, producing at the same produce cursor will fail because CAS operation will fail. After entry was also
       * consumed, the expected produce cursor was incremented by one generation (queue->capacity).
       */
      old_state = produce_cursor | LFCQ_READY_FOR_PRODUCE;
      new_state = produce_cursor | LFCQ_RESERVED_FOR_PRODUCE;
      if (ATOMIC_CAS_64 (entry_state_p, old_state, new_state))
	{
	  /* Entry was successfully allocated for producing data, break the loop now. */
	  break;
	}
      /* Produce must be tried again with a different cursor */
      /* Did someone else reserve it? */
      if (ATOMIC_LOAD_64 (entry_state_p) == (produce_cursor | LFCQ_RESERVED_FOR_PRODUCE))
	{
	  /* The entry was already reserved by another producer, but the produce cursor may be the same. Try to
	   * increment the cursor to avoid being spin-locked on same cursor value. The increment will fail if the
	   * cursor was already incremented. */
	  (void) ATOMIC_CAS_64 (&queue->produce_cursor, produce_cursor, produce_cursor + 1);
	}
      else if (ATOMIC_LOAD_64 (entry_state_p) == ((produce_cursor - queue->capacity) | LFCQ_RESERVED_FOR_CONSUME))
	{
	  /* The entry at produce_cursor is being consumed still. The consume cursor is behind one generation and
	   * it was already incremented, but the consumer did not yet finish consuming. We can consider the queue
	   * is still full since we don't want to loop here for an indefinite time. */
	  return false;
	}
      else
	{
#if !defined (NDEBUG) || defined (UNITTEST_CQ)
	  /* The entry at current produce cursor was already "produced". The cursor should already be incremented or
	   * maybe it was not ready when ATOMIC_CAS was called, but now it is. */
	  assert ((queue->produce_cursor > produce_cursor) || was_not_ready);
#endif /* !NDEBUG || UNITTEST_CQ */
	}
      /* Loop again. */
    }

  /* Successfully allocated entry for new data */

  /* Copy produced data to allocated entry */
  memcpy (queue->data + (entry_index * queue->data_size), data, queue->data_size);
  /* Set entry as readable. Since other should no longer race for this entry after it was allocated, we don't need an
   * atomic CAS operation. */
  assert (ATOMIC_LOAD_64 (entry_state_p) == new_state);

  /* Try to increment produce cursor. If this thread was preempted after allocating entry and before increment, it may
   * have been already incremented. */
  (void) ATOMIC_CAS_64 (&queue->produce_cursor, produce_cursor, produce_cursor + 1);
  ATOMIC_STORE_64 (entry_state_p, produce_cursor | LFCQ_READY_FOR_CONSUME);

  /* Successfully produced a new entry */
  return true;
}

/*
 * lf_circular_queue_consume () - Pop one entry from queue.
 *
 * return     : First queue entry or NULL if the queue is empty.
 * queue (in) : Lock-free circular queue.
 * data (out) : Pointer where to save popped data.
 */
bool
lf_circular_queue_consume (LOCK_FREE_CIRCULAR_QUEUE * queue, void *data)
{
  int entry_index;
  UINT64 consume_cursor;
  UINT64 produce_cursor;
  UINT64 old_state;
  UINT64 new_state;
  volatile UINT64 *entry_state_p;
#if !defined (NDEBUG) || defined (UNITTEST_CQ)
  bool was_not_ready = false;
#endif /* !NDEBUG || UNITTEST_CQ */

  /* Loop until an entry can be consumed or until queue is empty */
  /* Since there may be more than one consumer and no locks is used, a consume cursor and entry states are used to
   * synchronize all consumers. If several threads race to consume same entry, only the one that successfully changes
   * state from READY_FOR_CONSUME to RESERVED_FOR_CONSUME can consume the entry. Others will have to retry with a
   * different entry. */
  while (true)
    {
      /* Get current cursors */
      consume_cursor = ATOMIC_LOAD_64 (&queue->consume_cursor);
      produce_cursor = ATOMIC_LOAD_64 (&queue->produce_cursor);

      if (consume_cursor >= produce_cursor)
	{
	  return false;
	}

      /* Compute entry's index in circular queue */
      entry_index = (int) consume_cursor % queue->capacity;
      entry_state_p = &queue->entry_state[entry_index];
#if !defined (NDEBUG) || defined (UNITTEST_CQ)
      was_not_ready = ATOMIC_LOAD_64 (entry_state_p) == (consume_cursor | LFCQ_RESERVED_FOR_PRODUCE);
#endif /* !NDEBUG || UNITTEST_CQ */

      /* Try to set entry state from READY_FOR_CONSUME to RESERVED_FOR_CONSUME. */
      old_state = consume_cursor | LFCQ_READY_FOR_CONSUME;
      new_state = consume_cursor | LFCQ_RESERVED_FOR_CONSUME;
      if (ATOMIC_CAS_64 (entry_state_p, old_state, new_state))
	{
	  /* Entry was successfully reserved for consume. Break loop. */
	  break;
	}

      /* Consume must be tried again with a different cursor */
      if (ATOMIC_LOAD_64 (entry_state_p) == (consume_cursor | LFCQ_RESERVED_FOR_CONSUME))
	{
	  /* The entry was already reserved by another consumer, but the consume cursor may be the same. Try to
	   * increment the cursor to avoid being spin-locked on same cursor value. The increment will fail if the
	   * cursor was already incremented. */
	  (void) ATOMIC_CAS_64 (&queue->consume_cursor, consume_cursor, consume_cursor + 1);
	}
      else if (ATOMIC_LOAD_64 (entry_state_p) == (consume_cursor | LFCQ_RESERVED_FOR_PRODUCE))
	{
	  /* We are here because produce_cursor was incremented but the entry at consume_cursor was not produced yet.
	   * We can consider the queue empty since we don't want to loop here for an indefinite time. */
	  return false;
	}
      else
	{
#if !defined (NDEBUG) || defined (UNITTEST_CQ)
	  /* The entry at current consume cursor was already "consumed". The cursor should already be incremented or it
	   * was not ready when ATOMIC_CAS was called but now it is. */
	  assert ((queue->consume_cursor > consume_cursor) || was_not_ready);
#endif /* !NDEBUG || UNITTEST_CQ */
	}
      /* Loop again. */
    }

  /* Successfully reserved entry to consume */

  /* Consume the data found in entry. If data argument is NULL, just remove the entry. */
  if (data != NULL)
    {
      memcpy (data, queue->data + (entry_index * queue->data_size), queue->data_size);
    }

  assert (ATOMIC_LOAD_64 (entry_state_p) == new_state);

  /* Try to increment consume cursor. If this thread was preempted after reserving the entry and before incrementing
   * the cursor, another consumer may have already incremented it. */
  ATOMIC_CAS_64 (&queue->consume_cursor, consume_cursor, consume_cursor + 1);

  /* Change state to READY_TO_PRODUCE */
  /* Nobody can race us on changing this value, so CAS is not necessary */
  /* We also need to set the next expected cursor, which is the next generation value of this consume cursor. */
  ATOMIC_STORE_64 (entry_state_p, (consume_cursor + queue->capacity) | LFCQ_READY_FOR_PRODUCE);

  return true;
}

/*
 * lf_circular_queue_async_peek () - Peek function that return pointer to
 *				     data found at current_consume cursor.
 *				     Returns NULL if queue is empty.
 *
 * return     : NULL or pointer to data at consume cursor.
 * queue (in) : Lock-free circular queue.
 *
 * NOTE: This function cannot work if there is concurrent access on queue.
 */
void *
lf_circular_queue_async_peek (LOCK_FREE_CIRCULAR_QUEUE * queue)
{
  if (lf_circular_queue_is_empty (queue))
    {
      return NULL;
    }
  /* Return pointer to first entry in queue. */
  return (queue->data + queue->data_size * (queue->consume_cursor % queue->capacity));
}

/*
 * lf_circular_queue_async_push_ahead () - Function that pushes data before
 *					   consume_cursor. consume_cursor is
 *					   also updated.
 *
 * return     : True if data was successfully pushed, false otherwise.
 * queue (in) : Lock-free circular queue.
 * data (in)  : Pushed data.
 *
 * NOTE: This function cannot work if there is concurrent access on queue.
 */
bool
lf_circular_queue_async_push_ahead (LOCK_FREE_CIRCULAR_QUEUE * queue, void *data)
{
  int index;

  assert (data != NULL);

  if (lf_circular_queue_is_full (queue))
    {
      /* Cannot push data */
      return false;
    }

  /* Push data before consume_cursor and decrement cursor. */
  if (queue->consume_cursor == 0)
    {
      /* Increase cursors with one generation to avoid negative values. It is safe to do it, since this is asynchronous 
       * access. */
      queue->consume_cursor += queue->capacity;
      queue->produce_cursor += queue->capacity;
    }

  index = (int) (--queue->consume_cursor % queue->capacity);
  memcpy (queue->data + index * queue->data_size, data, queue->data_size);

  /* Set pushed data READY_FOR_CONSUME. */
  queue->entry_state[index] = (LFCQ_READY_FOR_CONSUME | queue->consume_cursor);

  return true;
}

/*
 * lf_circular_queue_create () - Allocate, initialize and return
 *					a new lock-free circular queue.
 *
 * return	       : Lock-free circular queue.
 * capacity (in)       : The maximum queue capacity.
 * data_size (in)      : Size of queue entry data.
 */
LOCK_FREE_CIRCULAR_QUEUE *
lf_circular_queue_create (unsigned int capacity, int data_size)
{
  /* Allocate queue */
  LOCK_FREE_CIRCULAR_QUEUE *queue;
  UINT64 index;

  queue = (LOCK_FREE_CIRCULAR_QUEUE *) malloc (sizeof (LOCK_FREE_CIRCULAR_QUEUE));
  if (queue == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (LOCK_FREE_CIRCULAR_QUEUE));
      return NULL;
    }

  /* Allocate queue data buffer */
  queue->data = malloc (capacity * data_size);
  if (queue->data == NULL)
    {
      free (queue);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) capacity * data_size);
      return NULL;
    }

  /* Allocate the array of entry state */
  queue->entry_state = malloc (capacity * sizeof (UINT64));
  if (queue->entry_state == NULL)
    {
      free (queue->data);
      free (queue);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, capacity * sizeof (UINT64));
      return NULL;
    }
  /* Initialize all entries by expecting first generation of producers. */
  for (index = 0; index < capacity; index++)
    {
      queue->entry_state[index] = index | LFCQ_READY_FOR_PRODUCE;
    }

  /* Initialize data size and capacity */
  queue->data_size = data_size;
  queue->capacity = (UINT64) capacity;

  /* Initialize cursors */
  queue->consume_cursor = queue->produce_cursor = 0;

  /* Return initialized queue */
  return queue;
}

/*
 * lf_circular_queue_destroy () - Destroy a lock-free circular queue.
 *
 * return     : Void.
 * queue (in) : Lock-free circular queue.
 */
void
lf_circular_queue_destroy (LOCK_FREE_CIRCULAR_QUEUE * queue)
{
  if (queue == NULL)
    {
      /* Nothing to do */
      return;
    }

  /* Free data buffer */
  if (queue->data != NULL)
    {
      free (queue->data);
    }

  /* Free the array of entry state */
  if (queue->entry_state != NULL)
    {
      free ((void *) queue->entry_state);
    }

  /* Free queue */
  free (queue);
}

/*
 * lf_circular_queue_async_reset () - Reset lock-free circular queue.
 *				      NOTE: The function should not be called while concurrent threads produce or
 *					    consume entries.
 *
 * return	  : Void.
 * queue (in/out) : Lock-free circular queue.
 */
void
lf_circular_queue_async_reset (LOCK_FREE_CIRCULAR_QUEUE * queue)
{
  int es_idx;
  queue->produce_cursor = 0;
  queue->consume_cursor = 0;

  for (es_idx = 0; es_idx < (int) queue->capacity; es_idx++)
    {
      queue->entry_state[es_idx] = es_idx | LFCQ_READY_FOR_PRODUCE;
    }
}

/*
 * lf_bitmap_init () - initialize lock free bitmap
 *   returns: error code or NO_ERROR
 *   bitmap(out): bitmap to initialize
 *   style(in): bitmap style to be initialized
 *   entries_cnt(in): maximum number of entries
 *   usage_threshold(in): the usage threshold for this bitmap
 */
int
lf_bitmap_init (LF_BITMAP * bitmap, LF_BITMAP_STYLE style, int entries_cnt, float usage_threshold)
{
  size_t bitfield_size;
  int chunk_count;
  unsigned int mask, chunk;
  int i;

  assert (bitmap != NULL);
  /* We only allow full usage for LF_BITMAP_ONE_CHUNK. */
  assert (style == LF_BITMAP_LIST_OF_CHUNKS || usage_threshold == 1.0f);

  bitmap->style = style;
  bitmap->entry_count = entries_cnt;
  bitmap->entry_count_in_use = 0;
  bitmap->usage_threshold = usage_threshold;
  if (usage_threshold < 0.0f || usage_threshold > 1.0f)
    {
      bitmap->usage_threshold = 1.0f;
    }
  bitmap->start_idx = 0;

  /* initialize bitfield */
  chunk_count = CEIL_PTVDIV (entries_cnt, LF_BITFIELD_WORD_SIZE);
  bitfield_size = (chunk_count * sizeof (unsigned int));
  bitmap->bitfield = (unsigned int *) malloc (bitfield_size);
  if (bitmap->bitfield == NULL)
    {
      bitmap->entry_count = 0;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, bitfield_size);
      return ER_FAILED;
    }

  memset (bitmap->bitfield, 0, bitfield_size);

  /* pad out the rest bits with 1, It will simplify the code in lf_bitmap_get_entry() */
  if (entries_cnt % LF_BITFIELD_WORD_SIZE != 0)
    {
      chunk = 0;
      mask = 1;
      for (i = entries_cnt % LF_BITFIELD_WORD_SIZE, mask <<= i; i < LF_BITFIELD_WORD_SIZE; i++, mask <<= 1)
	{
	  chunk |= mask;
	}
      bitmap->bitfield[chunk_count - 1] = chunk;
    }

  return NO_ERROR;
}

/*
 * lf_bitmap_destroy () - destroy a lock free bitmap
 *   sys(in): tran system
 *
 */
void
lf_bitmap_destroy (LF_BITMAP * bitmap)
{
  assert (bitmap != NULL);

  if (bitmap->bitfield != NULL)
    {
      free_and_init (bitmap->bitfield);
    }
  bitmap->entry_count = 0;
  bitmap->entry_count_in_use = 0;
  bitmap->style = 0;
  bitmap->usage_threshold = 1.0f;
  bitmap->start_idx = 0;
}


/*
 * lf_bitmap_get_entry () - request an available bitmap slot
 *   returns: slot index or -1 if not found
 *   bitmap(in/out): bitmap object
 */
int
lf_bitmap_get_entry (LF_BITMAP * bitmap)
{
  int chunk_count;
  unsigned int mask, chunk, start_idx;
  int i, chunk_idx, slot_idx;

  assert (bitmap != NULL);
  assert (bitmap->entry_count > 0);
  assert (bitmap->bitfield != NULL);

  chunk_count = CEIL_PTVDIV (bitmap->entry_count, LF_BITFIELD_WORD_SIZE);

restart:			/* wait-free process */
  chunk_idx = -1;
  slot_idx = -1;

  /* when reaches the predefined threshold */
  if (LF_BITMAP_IS_FULL (bitmap))
    {
      return -1;
    }

#if defined (SERVER_MODE)
  /* round-robin to get start chunk index */
  start_idx = ATOMIC_INC_32 (&bitmap->start_idx, 1);
  start_idx = (start_idx - 1) % ((unsigned int) chunk_count);
#else
  /* iterate from the last allocated chunk */
  start_idx = bitmap->start_idx;
#endif

  /* find a chunk with an empty slot */
  i = start_idx;
  do
    {
      chunk = VOLATILE_ACCESS (bitmap->bitfield[i], unsigned int);
      if (~chunk)
	{
	  chunk_idx = i;
	  break;
	}

      i++;
      if (i >= chunk_count)
	{
	  i = 0;
	}
    }
  while (i != (int) start_idx);

  if (chunk_idx == -1)
    {
      /* full? */
      if (bitmap->style == LF_BITMAP_ONE_CHUNK)
	{
	  assert (false);
	}
      return -1;
    }

  /* find first empty slot in chunk */
  for (i = 0, mask = 1; i < LF_BITFIELD_WORD_SIZE; i++, mask <<= 1)
    {
      if ((~chunk) & mask)
	{
	  slot_idx = i;
	  break;
	}
    }

  if (slot_idx == -1)
    {
      /* chunk was filled in the meantime */
      goto restart;
    }

  assert ((chunk_idx * LF_BITFIELD_WORD_SIZE + slot_idx) < bitmap->entry_count);
  do
    {
      chunk = VOLATILE_ACCESS (bitmap->bitfield[chunk_idx], unsigned int);
      if (chunk & mask)
	{
	  /* slot was marked by someone else */
	  goto restart;
	}
    }
  while (!ATOMIC_CAS_32 (&bitmap->bitfield[chunk_idx], chunk, chunk | mask));

  if (bitmap->style == LF_BITMAP_LIST_OF_CHUNKS)
    {
      ATOMIC_INC_32 (&bitmap->entry_count_in_use, 1);
    }

#if !defined (SERVER_MODE)
  bitmap->start_idx = chunk_idx;
#endif

  return chunk_idx * LF_BITFIELD_WORD_SIZE + slot_idx;
}


/*
 * lf_bitmap_free_entry () - return a previously requested entry
 *   returns: error code or NO_ERROR
 *   bitmap(in/out): bitmap object
 *   entry_idx(in): entry index
 *
 * NOTE: Only entries requested from this system should be returned.
 */
int
lf_bitmap_free_entry (LF_BITMAP * bitmap, int entry_idx)
{
  unsigned int mask, inverse_mask, curr;
  int pos, bit;

  assert (bitmap != NULL);
  assert (entry_idx >= 0);
  assert (entry_idx < bitmap->entry_count);

  /* clear bitfield so slot may be reused */
  pos = entry_idx / LF_BITFIELD_WORD_SIZE;
  bit = entry_idx % LF_BITFIELD_WORD_SIZE;
  inverse_mask = (unsigned int) (1 << bit);
  mask = ~inverse_mask;

  do
    {
      /* clear slot */
      curr = VOLATILE_ACCESS (bitmap->bitfield[pos], unsigned int);

      if (!(curr & inverse_mask))
	{
	  /* free unused memory or double free */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LF_BITMAP_INVALID_FREE, 0);
	  assert (false);
	  return ER_LF_BITMAP_INVALID_FREE;
	}
    }
  while (!ATOMIC_CAS_32 (&bitmap->bitfield[pos], curr, curr & mask));

  if (bitmap->style == LF_BITMAP_LIST_OF_CHUNKS)
    {
      ATOMIC_INC_32 (&bitmap->entry_count_in_use, -1);
    }

#if !defined (SERVER_MODE)
  bitmap->start_idx = pos;	/* hint for a free slot */
#endif

  return NO_ERROR;
}

#if defined (UNITTEST_LF)
/*
 * lf_reset_counters () - Reset all counters.
 *
 * return :
 * void (in) :
 */
void
lf_reset_counters (void)
{
  lf_hash_size = 0;

  lf_inserts = 0;
  lf_inserts_restart = 0;
  lf_list_inserts = 0;
  lf_list_inserts_found = 0;
  lf_list_inserts_save_temp_1 = 0;
  lf_list_inserts_save_temp_2 = 0;
  lf_list_inserts_claim = 0;
  lf_list_inserts_fail_link = 0;
  lf_list_inserts_success_link = 0;

  lf_deletes = 0;
  lf_deletes_restart = 0;
  lf_list_deletes = 0;
  lf_list_deletes_found = 0;
  lf_list_deletes_fail_mark_next = 0;
  lf_list_deletes_fail_unlink = 0;
  lf_list_deletes_success_unlink = 0;
  lf_list_deletes_not_found = 0;
  lf_list_deletes_not_match = 0;

  lf_clears = 0;

  lf_retires = 0;
  lf_claims = 0;
  lf_claims_temp = 0;
  lf_transports = 0;
  lf_temps = 0;
}
#endif /* UNITTEST_LF */
