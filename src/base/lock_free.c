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
LF_TRAN_SYSTEM partition_link_Ts = LF_TRAN_SYSTEM_INITIALIZER;
LF_TRAN_SYSTEM hfid_table_Ts = LF_TRAN_SYSTEM_INITIALIZER;

static bool tran_systems_initialized = false;

/*
 * Macro definitions
 */
#define OF_GET_REF(p,o)		(void * volatile *) (((char *)(p)) + (o))
#define OF_GET_PTR(p,o)		(void *) (((char *)(p)) + (o))
#define OF_GET_PTR_DEREF(p,o)	(*OF_GET_REF (p,o))

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
 *   stc(in): source VPID
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
      sys->entries[i].last_cleanup_id = 0;
      sys->entries[i].retired_list = NULL;
      sys->entries[i].temp_entry = NULL;
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
int
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

  /* all ok */
  return NO_ERROR;
}

/*
 * lf_tran_start_op () - start operation in entry
 *   returns: error code or NO_ERROR
 *   entry(in): tran entry
 *   incr(in): increment global counter?
 */
int
lf_tran_start (LF_TRAN_ENTRY * entry, bool incr)
{
  LF_TRAN_SYSTEM *sys;

  assert (entry != NULL);
  assert (entry->tran_system != NULL);

  sys = entry->tran_system;

  if (incr)
    {
      entry->transaction_id = ATOMIC_INC_64 (&sys->global_transaction_id, 1);

      if (entry->transaction_id % entry->tran_system->mati_refresh_interval == 0)
	{
	  return lf_tran_compute_minimum_transaction_id (entry->tran_system);
	}
    }
  else
    {
      entry->transaction_id = VOLATILE_ACCESS (sys->global_transaction_id, UINT64);
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * lf_tran_end_op () - end operation in entry
 *   returns: error code or NO_ERROR
 *   sys(in): tran system
 *   entry(in): tran entry
 */
int
lf_tran_end (LF_TRAN_ENTRY * entry)
{
  /* maximum value of domain */
  entry->transaction_id = LF_NULL_TRANSACTION_ID;

  /* all ok */
  return NO_ERROR;
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

  if (lf_tran_system_init (&partition_link_Ts, max_threads) != NO_ERROR)
    {
      goto error;
    }

  if (lf_tran_system_init (&hfid_table_Ts, max_threads) != NO_ERROR)
    {
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
  lf_tran_system_destroy (&partition_link_Ts);
  lf_tran_system_destroy (&hfid_table_Ts);

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

  /* first check temporary entry */
  if (tran_entry->temp_entry != NULL)
    {
      entry = tran_entry->temp_entry;
      tran_entry->temp_entry = NULL;
      OF_GET_PTR_DEREF (entry, edesc->of_next) = NULL;
      return entry;
    }

  /* see if local transaction is required */
  if (tran_entry->transaction_id == LF_NULL_TRANSACTION_ID)
    {
      local_tran = true;
      if (lf_tran_start (tran_entry, true) != NO_ERROR)
	{
	  return NULL;
	}
      MEMORY_BARRIER ();
    }

  /* clean retired list, if possible */
  if (LF_TRAN_CLEANUP_NECESSARY (tran_entry))
    {
      if (lf_freelist_transport (tran_entry, freelist) != NO_ERROR)
	{
	  /* end local transaction */
	  if (local_tran)
	    {
	      MEMORY_BARRIER ();
	      (void) lf_tran_end (tran_entry);
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
	      /* end local transaction */
	      if (local_tran)
		{
		  MEMORY_BARRIER ();
		  (void) lf_tran_end (tran_entry);
		}
	      /* can't initialize it */
	      return NULL;
	    }

	  /* initialize next */
	  OF_GET_PTR_DEREF (entry, edesc->of_next) = NULL;

	  /* end local transaction */
	  if (local_tran)
	    {
	      MEMORY_BARRIER ();
	      (void) lf_tran_end (tran_entry);
	    }

	  /* done! */
	  return entry;
	}
      else
	{
	  /* NOTE: as you can see, more than one thread can start allocating a new freelist_entry block at the same
	   * time; this behavior is acceptable given that the freelist has a _low_ enough value of block_size; it sure
	   * beats synchronizing the operations */
	  if (lf_freelist_alloc_block (freelist) != NO_ERROR)
	    {
	      /* end local transaction */
	      if (local_tran)
		{
		  MEMORY_BARRIER ();
		  (void) lf_tran_end (tran_entry);
		}
	      return NULL;
	    }

	  /* retry a stack pop */
	  continue;
	}
    }

  /* impossible! */
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
      if (lf_tran_start (tran_entry, true) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      MEMORY_BARRIER ();
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
    }

  /* end local transaction */
  if (local_tran)
    {
      MEMORY_BARRIER ();
      if (lf_tran_end (tran_entry) != NO_ERROR)
	{
	  return ER_FAILED;
	}
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
	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
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
	      if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
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
	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
	    {
	      /* entry has a mutex protecting it's members; lock it */
	      entry_mutex = (pthread_mutex_t *) OF_GET_PTR ((*entry), edesc->of_mutex);
	      rv = pthread_mutex_lock (entry_mutex);
	    }

	  /* attempt an add */
	  if (!ATOMIC_CAS_ADDR (curr_p, NULL, (*entry)))
	    {
	      if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
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

restart_search:
  if (lf_tran_start (tran, false) != NO_ERROR)
    {
      return ER_FAILED;
    }
  MEMORY_BARRIER ();

  curr_p = list_p;
  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));

  /* search */
  while (curr != NULL)
    {
      if (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0)
	{
	  /* found! */
	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
	    {
	      /* entry has a mutex protecting it's members; lock it */
	      entry_mutex = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);
	      rv = pthread_mutex_lock (entry_mutex);

	      /* mutex has been locked, no need to keep transaction */
	      MEMORY_BARRIER ();
	      if (lf_tran_end (tran) != NO_ERROR)
		{
		  return ER_FAILED;
		}

	      if (ADDR_HAS_MARK (OF_GET_PTR_DEREF (curr, edesc->of_next)))
		{
		  /* while waiting for lock, somebody else deleted the entry; restart the search */
		  pthread_mutex_unlock (entry_mutex);

		  if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		    {
		      *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		      MEMORY_BARRIER ();
		      return lf_tran_end (tran);
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
  return lf_tran_end (tran);
}

/*
 * lf_list_find_or_insert () - find or insert an entry
 *   returns: error code or NO_ERROR
 *   tran(in): lock free transaction system
 *   list_p(in): address of list head
 *   key(in): key to search for or insert
 *   behavior_flags(in/out): flags that control restart behavior
 *   edesc(in): entry descriptor
 *   freelist(in): freelist to fetch new entries from
 *   entry(out): found entry or inserted entry
 *
 * NOTE: This function will search for an entry with the specified key; if none
 * is found, it will add the entry in the hash table and return it in "entry".
 */
int
lf_list_find_or_insert (LF_TRAN_ENTRY * tran, void **list_p, void *key, int *behavior_flags,
			LF_ENTRY_DESCRIPTOR * edesc, LF_FREELIST * freelist, void **entry)
{
  pthread_mutex_t *entry_mutex;
  void **curr_p;
  void *curr;
  int rv;

  assert (tran != NULL);
  assert (list_p != NULL && edesc != NULL);
  assert (key != NULL && entry != NULL);
  assert (freelist != NULL);

  /* by default, not found */
  (*entry) = NULL;

restart_search:
  if (lf_tran_start (tran, false) != NO_ERROR)
    {
      return ER_FAILED;
    }
  MEMORY_BARRIER ();

  curr_p = list_p;
  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));

  /* search */
  while (curr_p != NULL)
    {
      /* is this the droid we are looking for? */
      if (curr != NULL)
	{
	  if (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0)
	    {
	      if ((*entry) != NULL)
		{
		  /* save this for further (local) use */
		  tran->temp_entry = *entry;

		  /* this operation may fail as well, so don't keep the entry around */
		  (*entry) = NULL;
		}

	      /* found! */
	      if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
		{
		  /* entry has a mutex protecting it's members; lock it */
		  entry_mutex = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);
		  rv = pthread_mutex_lock (entry_mutex);

		  /* mutex has been locked, no need to keep transaction alive */
		  MEMORY_BARRIER ();
		  if (lf_tran_end (tran) != NO_ERROR)
		    {
		      return ER_FAILED;
		    }

		  if (ADDR_HAS_MARK (OF_GET_PTR_DEREF (curr, edesc->of_next)))
		    {
		      /* while waiting for lock, somebody else deleted the entry; restart the search */
		      pthread_mutex_unlock (entry_mutex);

		      if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
			{
			  *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
			  MEMORY_BARRIER ();
			  return lf_tran_end (tran);
			}
		      else
			{
			  goto restart_search;
			}
		    }
		}

	      assert (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0);
	      (*entry) = curr;
	      return NO_ERROR;
	    }

	  /* advance */
	  curr_p = (void **) OF_GET_REF (curr, edesc->of_next);
	  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));
	}
      else
	{
	  /* end of bucket, we must insert */
	  if ((*entry) == NULL)
	    {
	      if (tran->temp_entry != NULL)
		{
		  *entry = tran->temp_entry;
		  tran->temp_entry = NULL;
		}
	      else
		{
		  *entry = lf_freelist_claim (tran, freelist);
		  if (*entry == NULL)
		    {
		      return ER_FAILED;
		    }
		}
	      assert ((*entry) != NULL);

	      /* set it's key */
	      if (edesc->f_key_copy (key, OF_GET_PTR (*entry, edesc->of_key)) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }

	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
	    {
	      /* entry has a mutex protecting it's members; lock it */
	      entry_mutex = (pthread_mutex_t *) OF_GET_PTR ((*entry), edesc->of_mutex);
	      rv = pthread_mutex_lock (entry_mutex);
	    }

	  /* attempt an add */
	  if (!ATOMIC_CAS_ADDR (curr_p, NULL, (*entry)))
	    {
	      if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
		{
		  /* link failed, unlock mutex */
		  entry_mutex = (pthread_mutex_t *) OF_GET_PTR ((*entry), edesc->of_mutex);
		  pthread_mutex_unlock (entry_mutex);
		}

	      /* someone added before us, restart process */
	      if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		{
		  if (*entry != NULL)
		    {
		      tran->temp_entry = *entry;
		      *entry = NULL;
		    }
		  *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		  MEMORY_BARRIER ();
		  return lf_tran_end (tran);
		}
	      else
		{
		  goto restart_search;
		}
	    }

	  /* end transaction if mutex is acquired */
	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
	    {
	      MEMORY_BARRIER ();
	      if (lf_tran_end (tran) != NO_ERROR)
		{
		  return ER_FAILED;
		}
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
 * lf_list_insert () - insert an entry into a list
 *   returns: error code or NO_ERROR
 *   tran(in): lock free transaction system
 *   list_p(in): address of list head
 *   key(in): key to insert
 *   behavior_flags(in/out): flags that control restart behavior
 *   edesc(in): entry descriptor
 *   freelist(in): freelist to fetch new entries from
 *   entry(out): entry (if found) or NULL
 *   inserted_count(out): number of inserted entries (will return 0 on restart)
 *
 * NOTE: If key already exists, the function will call f_duplicate of the entry
 * descriptor and then retry the insert. If f_duplicate is NULL or does not
 * modify the key then it will consequently spin until the entry with the given
 * key is removed;
 * NOTE: The default use case would be for f_duplicate to increment the key.
 */
int
lf_list_insert (LF_TRAN_ENTRY * tran, void **list_p, void *key, int *behavior_flags, LF_ENTRY_DESCRIPTOR * edesc,
		LF_FREELIST * freelist, void **entry, int *inserted_count)
{
  pthread_mutex_t *entry_mutex;
  void **curr_p;
  void *curr;
  int rv;

  assert (tran != NULL);
  assert (list_p != NULL && edesc != NULL);
  assert (key != NULL && entry != NULL);
  assert (inserted_count != NULL);
  assert (freelist != NULL);

  *inserted_count = 0;

  if (tran->temp_entry != NULL)
    {
      *entry = tran->temp_entry;
      tran->temp_entry = NULL;
    }
  else
    {
      *entry = lf_freelist_claim (tran, freelist);
    }
  if ((*entry) == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

restart_search:
  if (lf_tran_start (tran, false) != NO_ERROR)
    {
      return ER_FAILED;
    }
  MEMORY_BARRIER ();

  curr_p = list_p;
  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));

  /* search */
  while (curr_p != NULL)
    {
      if (curr != NULL)
	{
	  if (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0)
	    {
	      /* found an entry with the same key */
	      if (edesc->f_duplicate != NULL)
		{
		  /* we have duplicate key callback */
		  if (edesc->f_duplicate (key, curr) != NO_ERROR)
		    {
		      return ER_FAILED;
		    }

		  if (*behavior_flags & LF_LIST_BF_RETURN_ON_DUPLICATE)
		    {
		      *behavior_flags = (*behavior_flags) | LF_LIST_BR_DUPLICATE;
		      MEMORY_BARRIER ();
		      return lf_tran_end (tran);
		    }
		}

	      /* retry insert */
	      if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		{
		  *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		  MEMORY_BARRIER ();
		  return lf_tran_end (tran);
		}
	      else
		{
		  goto restart_search;
		}
	    }

	  /* advance */
	  curr_p = (void **) OF_GET_REF (curr, edesc->of_next);
	  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));
	}
      else
	{
	  /* end of bucket, we must insert */
	  /* set entry's key */
	  if (edesc->f_key_copy (key, OF_GET_PTR (*entry, edesc->of_key)) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
	    {
	      /* entry has a mutex protecting it's members; lock it */
	      entry_mutex = (pthread_mutex_t *) OF_GET_PTR ((*entry), edesc->of_mutex);
	      rv = pthread_mutex_lock (entry_mutex);
	    }

	  /* attempt an add */
	  if (!ATOMIC_CAS_ADDR (curr_p, NULL, (*entry)))
	    {
	      if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
		{
		  /* link failed, unlock mutex */
		  entry_mutex = (pthread_mutex_t *) OF_GET_PTR ((*entry), edesc->of_mutex);
		  pthread_mutex_unlock (entry_mutex);
		}

	      /* someone added or deleted before us, restart process */
	      if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		{
		  *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		  MEMORY_BARRIER ();
		  return lf_tran_end (tran);
		}
	      else
		{
		  goto restart_search;
		}
	    }

	  /* end transaction if we have mutex acquired */
	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
	    {
	      MEMORY_BARRIER ();
	      if (lf_tran_end (tran) != NO_ERROR)
		{
		  return ER_FAILED;
		}
	    }

	  /* done! */
	  *inserted_count = 1;
	  return NO_ERROR;
	}
    }

  /* all not ok */
  assert (false);
  return ER_FAILED;
}

/*
 * lf_list_delete () - delete an entry from a list
 *   returns: error code or NO_ERROR
 *   tran(in): lock free transaction system
 *   list_p(in): address of list head
 *   key(in): key to search for
 *   behavior_flags(in/out): flags that control restart behavior
 *   edesc(in): entry descriptor
 *   freelist(in): freelist to place deleted entries to
 *   success(out): 1 if entry was deleted, 0 otherwise
 */
int
lf_list_delete (LF_TRAN_ENTRY * tran, void **list_p, void *key, int *behavior_flags, LF_ENTRY_DESCRIPTOR * edesc,
		LF_FREELIST * freelist, int *success)
{
  pthread_mutex_t *entry_mutex;
  void **curr_p, **next_p;
  void *curr, *next;
  int rv;

  /* reset success flag */
  if (success != NULL)
    {
      *success = 0;
    }

  assert (list_p != NULL && edesc != NULL && key != NULL);
  assert (freelist != NULL);
  assert (tran != NULL && tran->tran_system != NULL);

restart_search:
  if (lf_tran_start (tran, false) != NO_ERROR)
    {
/* read transaction; we start a write transaction only after remove */
      return ER_FAILED;
    }
  MEMORY_BARRIER ();

  curr_p = list_p;
  curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));

  /* search */
  while (curr != NULL)
    {
      /* is this the droid we are looking for? */
      if (edesc->f_key_cmp (key, OF_GET_PTR (curr, edesc->of_key)) == 0)
	{
	  /* fetch next entry */
	  next_p = (void **) OF_GET_REF (curr, edesc->of_next);
	  next = ADDR_STRIP_MARK (*((void *volatile *) next_p));

	  /* set mark on next pointer; this way, if anyone else is trying to delete the next entry, it will fail */
	  if (!ATOMIC_CAS_ADDR (next_p, next, ADDR_WITH_MARK (next)))
	    {
	      /* joke's on us, this time; somebody else marked it before */
	      if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		{
		  *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		  assert ((*behavior_flags) & LF_LIST_BR_RESTARTED);
		  MEMORY_BARRIER ();
		  return lf_tran_end (tran);
		}
	      else
		{
		  goto restart_search;
		}
	    }

	  /* lock mutex if necessary */
	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_DELETE)
	    {
	      entry_mutex = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);
	      rv = pthread_mutex_lock (entry_mutex);

	      /* since we set the mark, nobody else can delete it, so we have nothing else to check */
	    }

	  /* unlink */
	  if (!ATOMIC_CAS_ADDR (curr_p, curr, next))
	    {
	      /* unlink failed; first step is to remove lock (if applicable) */
	      if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_DELETE)
		{
		  entry_mutex = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);
		  pthread_mutex_unlock (entry_mutex);
		}

	      /* remove mark and restart search */
	      if (!ATOMIC_CAS_ADDR (next_p, ADDR_WITH_MARK (next), next))
		{
		  /* impossible case */
		  return ER_FAILED;
		}

	      if (behavior_flags && (*behavior_flags & LF_LIST_BF_RETURN_ON_RESTART))
		{
		  *behavior_flags = (*behavior_flags) | LF_LIST_BR_RESTARTED;
		  assert ((*behavior_flags) & LF_LIST_BR_RESTARTED);
		  MEMORY_BARRIER ();
		  return lf_tran_end (tran);
		}
	      else
		{
		  goto restart_search;
		}
	    }

	  /* unlock mutex if necessary */
	  if (edesc->mutex_flags & LF_EM_FLAG_UNLOCK_AFTER_DELETE)
	    {
	      entry_mutex = (pthread_mutex_t *) OF_GET_PTR (curr, edesc->of_mutex);
	      pthread_mutex_unlock (entry_mutex);
	    }

	  MEMORY_BARRIER ();
	  if (lf_tran_start (tran, true) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  MEMORY_BARRIER ();

	  /* now we can feed the entry to the freelist and forget about it */
	  if (lf_freelist_retire (tran, freelist, curr) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* end the transaction */
	  MEMORY_BARRIER ();
	  if (lf_tran_end (tran) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }

	  /* set success flag */
	  if (success != NULL)
	    {
	      *success = 1;
	    }

	  /* success! */
	  return NO_ERROR;
	}

      /* advance */
      curr_p = (void **) OF_GET_REF (curr, edesc->of_next);
      curr = ADDR_STRIP_MARK (*((void *volatile *) curr_p));
    }

  /* search yielded no result so no delete was performed */
  MEMORY_BARRIER ();
  if (lf_tran_end (tran) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
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
  assert (hash_size > 1);

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
 * lf_hash_find_or_insert () - find or insert an entry in the hash table
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table
 *   key(in): key of entry that we seek
 *   entry(out): existing or new entry
 *
 */
int
lf_hash_find_or_insert (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry)
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
  rc = lf_list_find_or_insert (tran, &table->buckets[hash_value], key, &bflags, edesc, table->freelist, entry);
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
 * lf_hash_insert () - insert a new entry with a specified key
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table
 *   key(in): key of entry to insert
 *   entry(out): new entry
 *
 */
int
lf_hash_insert (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, void **entry)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  unsigned int hash_value;
  int inserted_count = 0, err = NO_ERROR, bflags;

  assert (table != NULL && key != NULL && entry != NULL);
  edesc = table->entry_desc;
  assert (edesc != NULL);
  *entry = NULL;

  while (inserted_count == 0)
    {
      /* if duplicate is found then key may have been modified, so rehashing is necessary */
      hash_value = edesc->f_hash (key, table->hash_size);
      if (hash_value >= table->hash_size)
	{
	  assert (false);
	  return ER_FAILED;
	}

      bflags = LF_LIST_BF_RETURN_ON_DUPLICATE | LF_LIST_BF_RETURN_ON_RESTART;
      if (lf_list_insert
	  (tran, &table->buckets[hash_value], key, &bflags, edesc, table->freelist, entry, &inserted_count) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  /* all ok */
  return NO_ERROR;
}

/*
 * lf_hash_delete () - delete an entry from the hash table
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table
 *   key(in): key to seek
 */
int
lf_hash_delete (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table, void *key, int *success)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  unsigned int hash_value;
  int rc, bflags;

  assert (table != NULL && key != NULL);
  edesc = table->entry_desc;
  assert (edesc != NULL);

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

  bflags = LF_LIST_BF_RETURN_ON_RESTART;
  rc = lf_list_delete (tran, &table->buckets[hash_value], key, &bflags, edesc, table->freelist, success);
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
 * lf_hash_clear () - clear the hash table
 *   returns: error code or NO_ERROR
 *   tran(in): LF transaction entry
 *   table(in): hash table to clear
 *
 * NOTE: This function is NOT lock free.
 */
int
lf_hash_clear (LF_TRAN_ENTRY * tran, LF_HASH_TABLE * table)
{
  LF_ENTRY_DESCRIPTOR *edesc;
  void **old_buckets, *curr, **next_p, *next;
  void *ret_head = NULL, *ret_tail = NULL;
  pthread_mutex_t *mutex_p;
  int ret = NO_ERROR;
  int rv, i, ret_count = 0;

  assert (tran != NULL && table != NULL && table->freelist != NULL);
  edesc = table->entry_desc;
  assert (edesc != NULL);

  /* lock mutex */
  rv = pthread_mutex_lock (&table->backbuffer_mutex);

  /* swap bucket pointer with current backbuffer */
  do
    {
      old_buckets = VOLATILE_ACCESS (table->buckets, void **);
    }
  while (!ATOMIC_CAS_ADDR (&table->buckets, old_buckets, table->backbuffer));

  /* register new backbuffer */
  table->backbuffer = old_buckets;

  /* clear bucket buffer, containing remains of old entries marked for delete */
  for (i = 0; i < (int) table->hash_size; i++)
    {
      assert (table->buckets[i] == ADDR_WITH_MARK (NULL));
      table->buckets[i] = NULL;
    }

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
	  if ((edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND) || (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_DELETE))
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
      if (lf_tran_start (tran, true) != NO_ERROR)
	{
	  pthread_mutex_unlock (&table->backbuffer_mutex);
	  return ER_FAILED;
	}
      MEMORY_BARRIER ();

      for (curr = ret_head; curr != NULL; curr = OF_GET_PTR_DEREF (curr, edesc->of_local_next))
	{
	  UINT64 *del_id = (UINT64 *) OF_GET_PTR (curr, edesc->of_del_tran_id);
	  *del_id = tran->transaction_id;
	}

      OF_GET_PTR_DEREF (ret_tail, edesc->of_local_next) = tran->retired_list;
      tran->retired_list = ret_head;

      ATOMIC_INC_32 (&table->freelist->retired_cnt, ret_count);

      MEMORY_BARRIER ();
      if (lf_tran_end (tran) != NO_ERROR)
	{
	  pthread_mutex_unlock (&table->backbuffer_mutex);
	  return ER_FAILED;
	}
    }

  /* unlock mutex and return to caller */
  pthread_mutex_unlock (&table->backbuffer_mutex);
  return ret;
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
	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
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
	  MEMORY_BARRIER ();
	  if (lf_tran_end (tran_entry) != NO_ERROR)
	    {
	      /* should not happen */
	      assert (false);
	      return NULL;
	    }
	  if (lf_tran_start (tran_entry, false) != NO_ERROR)
	    {
	      /* should not happen */
	      assert (false);
	      return NULL;
	    }
	  MEMORY_BARRIER ();

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
	      MEMORY_BARRIER ();
	      if (lf_tran_end (tran_entry) != NO_ERROR)
		{
		  /* nothing we can report here, but shouldn't happen */
		  assert (false);
		}
	      return NULL;
	    }
	}

      if (it->curr != NULL)
	{
	  if (edesc->mutex_flags & LF_EM_FLAG_LOCK_ON_FIND)
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
#define READY_FOR_PRODUCE		  (INT32) 0
#define RESERVED_FOR_PRODUCE		  (INT32) 1
#define READY_FOR_CONSUME		  (INT32) 2
#define RESERVED_FOR_CONSUME		  (INT32) 3

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
  INT64 produce_cursor;

  assert (data != NULL);

  /* Loop until a free entry for produce is found or queue is full. */
  /* Since this may be done under concurrency with no locks, a produce cursor and an entry state for the cursor are
   * used to synchronize producing data. After reading the produce cursor, since there is no lock to protect it, other
   * producer may race to use it for its own produced data. The producer can gain an entry only if it successfully
   * changes the state from READY_FOR_PRODUCE to RESERVED_FOR_PRODUCE (using compare & swap). */
  while (true)
    {
      if (LOCK_FREE_CIRCULAR_QUEUE_IS_FULL (queue))
	{
	  /* The queue is full, cannot produce new entries */
	  return false;
	}

      /* Get current produce_cursor */
      produce_cursor = VOLATILE_ACCESS (queue->produce_cursor, INT64);

      /* Compute entry's index in circular queue */
      entry_index = (int) produce_cursor % queue->capacity;

      if (ATOMIC_CAS_32 (&queue->entry_state[entry_index], READY_FOR_PRODUCE, RESERVED_FOR_PRODUCE))
	{
	  /* Entry was successfully allocated for producing data, break the loop now. */
	  break;
	}
      /* Produce must be tried again with a different cursor */
      if (queue->entry_state[entry_index] == RESERVED_FOR_PRODUCE)
	{
	  /* The entry was already reserved by another producer, but the produce cursor may be the same. Try to
	   * increment the cursor to avoid being spin-locked on same cursor value. The increment will fail if the
	   * cursor was already incremented. */
	  (void) ATOMIC_CAS_64 (&queue->produce_cursor, produce_cursor, produce_cursor + 1);
	}
      else if (queue->entry_state[entry_index] == RESERVED_FOR_CONSUME)
	{
	  /* Consumer incremented the consumer cursor but didn't change the state to READY_FOR_PRODUCE. In this case,
	   * the list is considered full, and producer must fail. */
	  return false;
	}
      /* For all other states, the producer which used current cursor already incremented it. */
      /* Try again */
    }

  /* Successfully allocated entry for new data */

  /* Copy produced data to allocated entry */
  memcpy (queue->data + (entry_index * queue->data_size), data, queue->data_size);
  /* Set entry as readable. Since other should no longer race for this entry after it was allocated, we don't need an
   * atomic CAS operation. */
  assert (queue->entry_state[entry_index] == RESERVED_FOR_PRODUCE);

  /* Try to increment produce cursor. If this thread was preempted after allocating entry and before increment, it may
   * have been already incremented. */
  ATOMIC_CAS_64 (&queue->produce_cursor, produce_cursor, produce_cursor + 1);
  queue->entry_state[entry_index] = READY_FOR_CONSUME;

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
  INT64 consume_cursor;

  /* Loop until an entry can be consumed or until queue is empty */
  /* Since there may be more than one consumer and no locks is used, a consume cursor and entry states are used to
   * synchronize all consumers. If several threads race to consume same entry, only the one that successfully changes
   * state from READY_FOR_CONSUME to RESERVED_FOR_CONSUME can consume the entry. Others will have to retry with a
   * different entry. */
  while (true)
    {
      if (LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (queue))
	{
	  /* Queue is empty, nothing to consume */
	  return false;
	}

      /* Get current consume cursor */
      consume_cursor = VOLATILE_ACCESS (queue->consume_cursor, INT64);

      /* Compute entry's index in circular queue */
      entry_index = (int) consume_cursor % queue->capacity;

      /* Try to set entry state from READY_FOR_CONSUME to RESERVED_FOR_CONSUME. */
      if (ATOMIC_CAS_32 (&queue->entry_state[entry_index], READY_FOR_CONSUME, RESERVED_FOR_CONSUME))
	{
	  /* Entry was successfully reserved for consume. Break loop. */
	  break;
	}

      /* Consume must be tried again with a different cursor */
      if (queue->entry_state[entry_index] == RESERVED_FOR_CONSUME)
	{
	  /* The entry was already reserved by another consumer, but the consume cursor may be the same. Try to
	   * increment the cursor to avoid being spin-locked on same cursor value. The increment will fail if the
	   * cursor was already incremented. */
	  ATOMIC_CAS_64 (&queue->consume_cursor, consume_cursor, consume_cursor + 1);
	}
      else if (queue->entry_state[entry_index] == RESERVED_FOR_PRODUCE)
	{
	  /* Producer didn't finish yet, consider that list is empty and there is nothing to consume. */
	  return false;
	}
      /* For all other states, the producer which used current cursor already incremented it. */
      /* Try again */
    }

  /* Successfully reserved entry to consume */

  /* Consume the data found in entry. If data argument is NULL, just remove the entry. */
  if (data != NULL)
    {
      memcpy (data, queue->data + (entry_index * queue->data_size), queue->data_size);
    }

  /* Try to increment consume cursor. If this thread was preempted after reserving the entry and before incrementing
   * the cursor, another consumer may have already incremented it. */
  ATOMIC_CAS_64 (&queue->consume_cursor, consume_cursor, consume_cursor + 1);

  /* Change state to READY_TO_PRODUCE */
  /* Nobody can race us on changing this value, so CAS is not necessary */
  assert (queue->entry_state[entry_index] == RESERVED_FOR_CONSUME);
  queue->entry_state[entry_index] = READY_FOR_PRODUCE;

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
  if (LOCK_FREE_CIRCULAR_QUEUE_IS_EMPTY (queue))
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

  if (LOCK_FREE_CIRCULAR_QUEUE_IS_FULL (queue))
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
  queue->entry_state[index] = READY_FOR_CONSUME;

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
lf_circular_queue_create (INT32 capacity, int data_size)
{
  /* Allocate queue */
  LOCK_FREE_CIRCULAR_QUEUE *queue;

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
  queue->entry_state = malloc (capacity * sizeof (INT32));
  if (queue->entry_state == NULL)
    {
      free (queue->data);
      free (queue);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, capacity * sizeof (INT32));
      return NULL;
    }

  /* Initialize all entries as READY_TO_PRODUCE */
  memset (queue->entry_state, 0, capacity * sizeof (INT32));

  /* Initialize data size and capacity */
  queue->data_size = data_size;
  queue->capacity = capacity;

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
      free (queue->entry_state);
    }

  /* Free queue */
  free (queue);
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
