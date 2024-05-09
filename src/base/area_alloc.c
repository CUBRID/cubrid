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
 * area_alloc.c - Area memory manager
 *
 * Note:
 * Allocation areas provide a way to block allocate structures and maintain
 * a free list.  Useful for small structures that are used frequently.
 * Used for allocation and freeing of many small objects of the same size.
 *
 * These areas are NOT allocated within the "workspace" memory so that
 * they can be used for structures that need to serve as roots to the
 * garbage collector.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "area_alloc.h"
#include "set_object.h"

#if !defined (SERVER_MODE)
#include "work_space.h"
#endif /* !defined (SERVER_MODE) */
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if !defined (SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif

#if !defined (NDEBUG)
/* The size of the prefix containing allocation status, if we're
   on a machine that requires double allignment of structures, we
   may have to make this sizeof(double) */
#define AREA_PREFIX_SIZE sizeof(double)

enum
{
  AREA_PREFIX_INITED = 0,
  AREA_PREFIX_FREED = 0x01010101
};
#endif /* !NDEBUG */

/*
 * Area_list - Global list of areas
 */
static AREA *area_List = NULL;
#if defined (SERVER_MODE)
pthread_mutex_t area_List_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

#if defined (SERVER_MODE)
#define LF_AREA_BITMAP_USAGE_RATIO LF_BITMAP_95PERCENTILE_USAGE_RATIO
#else
#define LF_AREA_BITMAP_USAGE_RATIO LF_BITMAP_FULL_USAGE_RATIO
#endif

/*
 * Volatile access to a variable
 */
#define VOLATILE_ACCESS(v,t)		(*((t volatile *) &(v)))

static void area_info (AREA * area, FILE * fp);
static AREA_BLOCK *area_alloc_block (AREA * area);
static AREA_BLOCKSET_LIST *area_alloc_blockset (AREA * area);
static int area_insert_block (AREA * area, AREA_BLOCK * new_block);
static AREA_BLOCK *area_find_block (AREA * area, const void *ptr);

/*
 * area_init - Initialize the area manager
 *   return: none
 *
 * Note: Will be called during system startup
 */
void
area_init (void)
{
#define ER_AREA_ALREADY_STARTED ER_GENERIC_ERROR
  if (area_List != NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AREA_ALREADY_STARTED, 0);
      return;
    }

  area_List = NULL;
}

/*
 * area_final - Shut down the area manager
 *   return: none
 *
 * Note: Will be called during system shutdown
 */
void
area_final (void)
{
  AREA *area, *next;

  for (area = area_List, next = NULL; area != NULL; area = next)
    {
      next = area->next;
      area_flush (area);
      free_and_init (area);
    }
  area_List = NULL;

  set_area_reset ();

  pthread_mutex_destroy (&area_List_lock);
}

/*
 * area_create - Build a new area and add it to the global list
 *   return: created AREA or NULL if fail
 *   name(in):
 *   element_size(in):
 *   alloc_count(in):
 *
 * Note:
 */
AREA *
area_create (const char *name, size_t element_size, size_t alloc_count)
{
  AREA *area;
  size_t adjust;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  area = (AREA *) malloc (sizeof (AREA));
  if (area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (AREA));
      return NULL;
    }
  area->blockset_list = NULL;

  if (name == NULL)
    {
      area->name = NULL;
    }
  else
    {
      area->name = strdup (name);
      if (area->name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (strlen (name) + 1));
	  goto error;
	}
    }

#if !defined (NDEBUG)
  /* Reserve space for memory checking */
  element_size += AREA_PREFIX_SIZE;
#endif /* !NDEBUG */

  /* always make sure element size is a double word multiple */
  adjust = element_size % 8;
  if (adjust)
    {
      element_size += 8 - adjust;
    }
  area->element_size = element_size;

  /* adjust alloc count for lf_bitmap */
  area->alloc_count = LF_BITMAP_COUNT_ALIGN (alloc_count);
  area->block_size = area->element_size * area->alloc_count;

  area->n_allocs = 0;
  area->n_frees = 0;
#if defined (SERVER_MODE)
  area->failure_function = NULL;
#else
  area->failure_function = ws_abort_transaction;
#endif

  area->blockset_list = area_alloc_blockset (area);
  if (area->blockset_list == NULL)
    {
      goto error;
    }

  area->hint_block = area_alloc_block (area);
  if (area->hint_block == NULL)
    {
      goto error;
    }
  area->blockset_list->items[0] = area->hint_block;
  area->blockset_list->used_count++;

  pthread_mutex_init (&area->area_mutex, NULL);

  rv = pthread_mutex_lock (&area_List_lock);
  area->next = area_List;
  area_List = area;
  pthread_mutex_unlock (&area_List_lock);

  return area;

error:

  if (area->name)
    {
      free_and_init (area->name);
    }

  if (area->blockset_list != NULL)
    {
      free_and_init (area->blockset_list);
    }

  free_and_init (area);

  return NULL;
}

/*
 * area_destroy - Removes an area
 *   return: none
 *   area(in): AREA tp destroy
 */
void
area_destroy (AREA * area)
{
  AREA *a, *prev;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  assert (area != NULL);

  rv = pthread_mutex_lock (&area_List_lock);

  for (prev = NULL, a = area_List; a != NULL && a != area; a = a->next)
    {
      prev = a;
    }

  if (a != NULL)
    {
      if (prev == NULL)
	{
	  area_List = a->next;
	}
      else
	{
	  prev->next = a->next;
	}
    }

  pthread_mutex_unlock (&area_List_lock);

  area_flush (area);

  free_and_init (area);
}

/*
 * area_alloc_block - Allocate a new block for an area
 *   return: the address of area block, if error, return NULL.
 *   area(in): AREA
 *   thrd_index(in): thread index
 *
 * Note: this is called by area_alloc, and lock is also held by area_alloc
 */
static AREA_BLOCK *
area_alloc_block (AREA * area)
{
  AREA_BLOCK *new_block;
  size_t total;

  assert (area != NULL);

  total = area->block_size + sizeof (AREA_BLOCK);

  new_block = (AREA_BLOCK *) malloc (total);
  if (new_block == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, total);
      if (area->failure_function != NULL)
	{
	  (*(area->failure_function)) ();
	}

      return NULL;
    }

  new_block->bitmap.init (LF_BITMAP_LIST_OF_CHUNKS, (int) area->alloc_count, LF_AREA_BITMAP_USAGE_RATIO);
  assert ((int) area->alloc_count == new_block->bitmap.entry_count);

  new_block->data = ((char *) new_block) + sizeof (AREA_BLOCK);

  return new_block;
}

/*
 * area_alloc_blockset - Allocate a new blockset node
 *   return: the address of area blockset, if error, return NULL.
 *   area(in): AREA
 *
 */
static AREA_BLOCKSET_LIST *
area_alloc_blockset (AREA * area)
{
  AREA_BLOCKSET_LIST *new_blockset;

  new_blockset = (AREA_BLOCKSET_LIST *) malloc (sizeof (AREA_BLOCKSET_LIST));
  if (new_blockset == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (AREA_BLOCKSET_LIST));
      if (area->failure_function != NULL)
	{
	  (*(area->failure_function)) ();
	}

      return NULL;
    }

  new_blockset->next = NULL;
  new_blockset->used_count = 0;
  memset ((void *) new_blockset->items, 0, sizeof (sizeof (AREA_BLOCK *) * AREA_BLOCKSET_SIZE));

  return new_blockset;
}

/*
 * area_alloc - Allocate a new element from an area
 *   return: pointer to the element allocated
 *   area(in):
 *
 * Note: The element will be taken from the area's free list,
 *       otherwise a new block will be allocated and the element
 *       taken from there
 */
void *
area_alloc (AREA * area)
{
  AREA_BLOCKSET_LIST *blockset;
  AREA_BLOCK *block, *hint_block;
  int used_count, i, entry_idx;
  char *entry_ptr;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */
#if !defined (NDEBUG)
  int *prefix;
#endif /* !NDEBUG */

  assert (area != NULL);

  /* Step 1: find a free entry from the hint block */
  hint_block = VOLATILE_ACCESS (area->hint_block, AREA_BLOCK *);
  entry_idx = hint_block->bitmap.get_entry ();
  if (entry_idx != -1)
    {
      block = hint_block;
      goto found;
    }

  /* Step 2: if not found, find a free entry from the blockset lists */
  for (blockset = area->blockset_list; blockset != NULL;
       blockset = VOLATILE_ACCESS (blockset->next, AREA_BLOCKSET_LIST *))
    {
      used_count = VOLATILE_ACCESS (blockset->used_count, int);
      for (i = 0; i < used_count; i++)
	{
	  block = VOLATILE_ACCESS (blockset->items[i], AREA_BLOCK *);

	  entry_idx = block->bitmap.get_entry ();
	  if (entry_idx != -1)
	    {
	      /* change the hint block */
	      hint_block = VOLATILE_ACCESS (area->hint_block, AREA_BLOCK *);
	      if (LF_BITMAP_IS_FULL (&hint_block->bitmap) && !LF_BITMAP_IS_FULL (&block->bitmap))
		{
		  ATOMIC_CAS_ADDR (&area->hint_block, hint_block, block);
		}

	      goto found;
	    }
	}
    }

  /* Step 3: if not found, add a new block. Then find free entry in this new block.
   * Only one thread is allowed to add a new block at a moment.
   */
  rv = pthread_mutex_lock (&area->area_mutex);

  if (area->hint_block != hint_block)
    {
      /* someone may change the hint block */
      block = area->hint_block;
      entry_idx = block->bitmap.get_entry ();
      if (entry_idx != -1)
	{
	  pthread_mutex_unlock (&area->area_mutex);
	  goto found;
	}
    }

  block = area_alloc_block (area);
  if (block == NULL)
    {
      pthread_mutex_unlock (&area->area_mutex);
      /* error has been set */
      return NULL;
    }

  /* alloc free entry from this new block */
  entry_idx = block->bitmap.get_entry ();
  assert (entry_idx != -1);

  if (area_insert_block (area, block) != NO_ERROR)
    {
      block->bitmap.destroy ();
      free_and_init (block);

      pthread_mutex_unlock (&area->area_mutex);
      /* error has been set */
      return NULL;
    }

  /* always set new block as hint_block */
  area->hint_block = block;

  pthread_mutex_unlock (&area->area_mutex);

found:

#if defined(SERVER_MODE)
  /* do not count in SERVER_MODE */
  /* ATOMIC_INC_32 (&area->n_allocs, 1); */
#else
  area->n_allocs++;
#endif

  entry_ptr = block->data + area->element_size * entry_idx;

#if !defined (NDEBUG)
  prefix = (int *) entry_ptr;
  *prefix = AREA_PREFIX_INITED;

  entry_ptr += AREA_PREFIX_SIZE;
#endif /* !NDEBUG */

  assert (entry_ptr < (block->data + area->block_size));

  return ((void *) entry_ptr);
}

/*
 * area_validate - validate that a pointer is within range in an area
 *   return: NO_ERROR if ok (address in range) or ER_AREA_ILLEGAL_POINTER
 *   area(in): AREA
 *   address(in): pointer to check
 *
 * Note: ER_AREA_ILLEGAL_POINTER will be set if fails.
 *       This does not guarentee that the pointer is alligned
 *       correctly to the start of an element, only that it points into one
 *       of the area blocks.
 */
int
area_validate (AREA * area, const void *address)
{
  AREA_BLOCK *p;
  int error = NO_ERROR;

  assert (area != NULL);

  p = area_find_block (area, address);
  if (p == NULL)
    {
      /* need more specific error here */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AREA_ILLEGAL_POINTER, 0);
      error = ER_AREA_ILLEGAL_POINTER;
    }

  return error;
}

/*
 * area_free - Free an element in an area
 *   return: error code
 *   area(in): AREA
 *   ptr(in): pointer to the element
 *
 * Note: Validation is performed; the element is simply pushed on the free list
 */
int
area_free (AREA * area, void *ptr)
{
  AREA_BLOCK *block, *hint_block;
  char *entry_ptr;
  int entry_idx;
  int offset = -1;
#if !defined (NDEBUG)
  int *prefix;
#endif /* !NDEBUG */

  assert (area != NULL);

  if (ptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AREA_ILLEGAL_POINTER, 0);
      assert (ptr != NULL);
      return ER_AREA_ILLEGAL_POINTER;
    }

#if !defined (NDEBUG)
  entry_ptr = ((char *) ptr) - AREA_PREFIX_SIZE;
#else
  entry_ptr = (char *) ptr;
#endif /* !NDEBUG */

  block = area_find_block (area, (const void *) entry_ptr);
  if (block == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AREA_ILLEGAL_POINTER, 0);
      assert (false);
      return ER_AREA_ILLEGAL_POINTER;
    }

  offset = (int) (entry_ptr - block->data);
  if (offset < 0 || offset % area->element_size != 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AREA_ILLEGAL_POINTER, 0);
      assert (false);
      return ER_AREA_ILLEGAL_POINTER;
    }

#if !defined (NDEBUG)
  prefix = (int *) entry_ptr;
  if ((*prefix) != AREA_PREFIX_INITED)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AREA_FREE_TWICE, 0);
      assert ((*prefix) == AREA_PREFIX_INITED);
      return ER_AREA_FREE_TWICE;
    }
  *prefix = AREA_PREFIX_FREED;
#endif /* !NDEBUG */

  entry_idx = offset / (int) area->element_size;

  assert (entry_idx >= 0 && entry_idx < (int) area->alloc_count);

  block->bitmap.free_entry (entry_idx);

  /* change hint block if needed */
  hint_block = VOLATILE_ACCESS (area->hint_block, AREA_BLOCK *);
  if (LF_BITMAP_IS_FULL (&hint_block->bitmap) && !LF_BITMAP_IS_FULL (&block->bitmap))
    {
      ATOMIC_CAS_ADDR (&area->hint_block, hint_block, block);
    }

#if defined(SERVER_MODE)
  /* do not count in SERVER_MODE */
  /* ATOMIC_INC_32 (&area->n_frees, 1); */
#else
  area->n_frees++;
#endif

  return NO_ERROR;
}

/*
 * area_flush - Free all storage allocated for an area
 *   return: none
 *   area(in): AREA to free
 *
 * Note: Normally called as part of final
 */
void
area_flush (AREA * area)
{
  AREA_BLOCKSET_LIST *blockset, *next_blockset;
  AREA_BLOCK *block;
  int i;

  assert (area != NULL);

  for (blockset = area->blockset_list; blockset != NULL; blockset = next_blockset)
    {
      next_blockset = blockset->next;

      for (i = 0; i < blockset->used_count; i++)
	{
	  block = blockset->items[i];

	  block->bitmap.destroy ();
	  free_and_init (block);
	  blockset->items[i] = NULL;
	}

      free_and_init (blockset);
    }
  area->blockset_list = NULL;

  pthread_mutex_destroy (&area->area_mutex);

  if (area->name != NULL)
    {
      free_and_init (area->name);
    }

}

/*
 * area_add_block --- insert block into the blockset list
 *   return: none
 *   area(in): area descriptor
 *   new_block(in): the new area_block pointer
 *
 *   Note: This function is protected by area_mutex, which mean only
 *   1 thread can insert block in the same time.
 */
static int
area_insert_block (AREA * area, AREA_BLOCK * new_block)
{
  AREA_BLOCKSET_LIST **last_blockset_p;
  AREA_BLOCKSET_LIST *blockset, *new_blockset;
  int used_count;

  assert (area != NULL && new_block != NULL);

  last_blockset_p = &area->blockset_list;
  /* find an available blockset and insert new_block into it */
  for (blockset = area->blockset_list; blockset != NULL; blockset = blockset->next)
    {
      last_blockset_p = &blockset->next;

      used_count = blockset->used_count;
      if (used_count == AREA_BLOCKSET_SIZE)
	{
	  /* no room */
	  continue;
	}
      /* each blockset owns one block at least */
      assert (used_count >= 1);

      /* If it fits, insert new_block to the last slot of this blockset. We don't shift/re-sort the blockset to manage
       * sorted order. Our policy may require more space but greatly reduces the complexity of logic. */
      if (blockset->items[used_count - 1] < new_block)
	{
	  blockset->items[used_count] = new_block;

	  /* Use full barrier to ensure that above assignment done before increase used_count */
	  ATOMIC_INC_32 (&blockset->used_count, 1);

	  return NO_ERROR;
	}
    }

  /* If there's no available blockset, we need to allocate a new blockset */
  new_blockset = area_alloc_blockset (area);
  if (new_blockset == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* insert new_block to the first slot */
  new_blockset->items[0] = new_block;

  /* Use full barrier to ensure that above assignment done before increase used_count */
  ATOMIC_INC_32 (&new_blockset->used_count, 1);

  /* append the new blockset to the end of blockset list */
  assert ((*last_blockset_p) == NULL);
  *last_blockset_p = new_blockset;

  return NO_ERROR;
}

/*
 * area_find_block -- find related block in blockset list.
 *
 *   return: the related block pointer, if not found, return NULL
 *   area(in): area descriptor
 *   ptr(in): the entry pointer
 *
 * Note: This function does not require area_mutex.
 */
static AREA_BLOCK *
area_find_block (AREA * area, const void *ptr)
{
  AREA_BLOCKSET_LIST *blockset;
  AREA_BLOCK *first_block, *last_block, *block;
  int middle, left, right, pos;
  int used_count;

  /* find the related block in blockset list */
  for (blockset = area->blockset_list; blockset != NULL;
       blockset = VOLATILE_ACCESS (blockset->next, AREA_BLOCKSET_LIST *))
    {
      used_count = VOLATILE_ACCESS (blockset->used_count, int);
      /* each blocskset owns one block at least */
      assert (used_count >= 1);

      first_block = blockset->items[0];
      if ((char *) ptr < first_block->data)
	{
	  continue;		/* less than min address */
	}

      last_block = VOLATILE_ACCESS (blockset->items[used_count - 1], AREA_BLOCK *);
      if (last_block->data + area->block_size <= (char *) ptr)
	{
	  continue;		/* large than max address */
	}

      assert ((first_block->data <= (char *) ptr) && ((char *) ptr < last_block->data + area->block_size));

      left = 0;
      right = used_count - 1;

      /* binary search in this blockset */
      while (left <= right)
	{
	  middle = (left + right) / 2;

	  block = VOLATILE_ACCESS (blockset->items[middle], AREA_BLOCK *);
	  if (block->data > (char *) ptr)
	    {
	      right = middle - 1;
	    }
	  else
	    {
	      left = middle + 1;
	    }
	}
      pos = right;

      if (pos < 0 || pos >= AREA_BLOCKSET_SIZE)
	{
	  /* impossible to here */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AREA_ILLEGAL_POINTER, 0);
	  assert (false);
	  return NULL;
	}

      block = VOLATILE_ACCESS (blockset->items[pos], AREA_BLOCK *);
      if ((block->data <= (char *) ptr) && ((char *) ptr < block->data + area->block_size))
	{
	  return block;
	}
    }

  /* not found */
  return NULL;
}

/*
 * area_info - Display information about an area
 *   return: none
 *   area(in): area descriptor
 *   fp(in):
 */
static void
area_info (AREA * area, FILE * fp)
{
  AREA_BLOCKSET_LIST *blockset;
  AREA_BLOCK *block;
  size_t nblocksets, nblocks, bytes, elements, used, unused;
  size_t min_blocks_in_set, avg_blocks_in_set, max_blocks_in_set;
  size_t nallocs = 0, nfrees = 0;
  int i;

  assert (area != NULL && fp != NULL);

  nblocksets = nblocks = bytes = elements = used = unused = 0;
  min_blocks_in_set = AREA_BLOCKSET_SIZE;
  max_blocks_in_set = 0;

  for (blockset = area->blockset_list; blockset != NULL; blockset = blockset->next)
    {
      nblocksets++;

      for (i = 0; i < blockset->used_count; i++)
	{
	  block = blockset->items[i];

	  nblocks++;
	  used += block->bitmap.entry_count_in_use;
	  bytes += area->block_size + sizeof (AREA_BLOCK);
	}

      if ((size_t) blockset->used_count < min_blocks_in_set)
	{
	  min_blocks_in_set = blockset->used_count;
	}
      if ((size_t) blockset->used_count > max_blocks_in_set)
	{
	  max_blocks_in_set = blockset->used_count;
	}
    }
  avg_blocks_in_set = nblocks / nblocksets;

  elements = (nblocks * area->alloc_count);
  unused = elements - used;

  nallocs = area->n_allocs;
  nfrees = area->n_frees;

  fprintf (fp, "Area: %s\n", area->name);
#if !defined (NDEBUG)
  fprintf (fp, "  %lld bytes/element ", (long long) area->element_size - AREA_PREFIX_SIZE);
  fprintf (fp, "(plus %d bytes overhead), ", (int) AREA_PREFIX_SIZE);
#else
  fprintf (fp, "  %lld bytes/element, ", (long long) area->element_size);
#endif
  fprintf (fp, "%lld elements/block, %lld blocks/blockset\n", (long long) area->alloc_count,
	   (long long) AREA_BLOCKSET_SIZE);

  fprintf (fp, "  %lld blocksets, usage stats:" " MIN %lld, AVG %lld, MAX %lld\n", (long long) nblocksets,
	   (long long) min_blocks_in_set, (long long) avg_blocks_in_set, (long long) max_blocks_in_set);

  fprintf (fp, "  %lld blocks, %lld bytes, %lld elements," " %lld unused, %lld in use\n", (long long) nblocks,
	   (long long) bytes, (long long) elements, (long long) unused, (long long) used);

  fprintf (fp, "  %lld total allocs, %lld total frees\n", (long long) nallocs, (long long) nfrees);
}

/*
 * area_dump - Print descriptions of all areas.
 *   return: none
 *   fp(in):
 */
void
area_dump (FILE * fp)
{
  AREA *area;
#if defined(SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (fp == NULL)
    {
      fp = stdout;
    }

  rv = pthread_mutex_lock (&area_List_lock);

  for (area = area_List; area != NULL; area = area->next)
    {
      area_info (area, fp);
    }

  pthread_mutex_unlock (&area_List_lock);
}
