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

#include "error_manager.h"
#include "memory_alloc.h"
#include "area_alloc.h"
#include "work_space.h"
#include "object_domain.h"
#include "set_object.h"
#if defined (SERVER_MODE)
#include "thread.h"
#include "connection_error.h"
#endif


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

static void area_info (AREA * area, FILE * fp);
static AREA_BLOCK *area_alloc_block (AREA * area);

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

  Set_Ref_Area = Set_Obj_Area = NULL;
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (AREA));
      return NULL;
    }

  if (name == NULL)
    {
      area->name = NULL;
    }
  else
    {
      area->name = strdup (name);
      if (area->name == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, (size_t) (strlen (name) + 1));
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
  area->blocks = NULL;

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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      total);
      if (area->failure_function != NULL)
	{
	  (*(area->failure_function)) ();
	}

      return NULL;
    }

  if (lf_bitmap_init (&new_block->bitmap, LF_BITMAP_LIST_OF_CHUNKS,
		      area->alloc_count,
		      LF_AREA_BITMAP_USAGE_RATIO) != NO_ERROR)
    {
      goto error;
    }
  assert (area->alloc_count == new_block->bitmap.entry_count);

  new_block->data = ((char *) new_block) + sizeof (AREA_BLOCK);
  new_block->next = NULL;

  return new_block;

error:
  free_and_init (new_block);

  return NULL;
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
  AREA_BLOCK *curr, *block = NULL;
  AREA_BLOCK **curr_p;
  int free_entry_idx;
  char *entry_ptr;
#if !defined (NDEBUG)
  int *prefix;
#endif /* !NDEBUG */

  assert (area != NULL);

  curr_p = &area->blocks;
  curr = VOLATILE_ACCESS (*curr_p, AREA_BLOCK *);

  while (curr_p != NULL)
    {
      if (curr != NULL)
	{
	  free_entry_idx = lf_bitmap_get_entry (&curr->bitmap);
	  if (free_entry_idx != -1)
	    {
	      break;
	    }
	  curr_p = (AREA_BLOCK **) (&curr->next);
	  curr = VOLATILE_ACCESS (*curr_p, AREA_BLOCK *);
	}
      else
	{
	  block = area_alloc_block (area);
	  if (block == NULL)
	    {
	      /* error has been set */
	      return NULL;
	    }

	  if (!ATOMIC_CAS_ADDR (curr_p, NULL, block))
	    {
	      /* someone added block before us, free this block */
	      er_log_debug (ARG_FILE_LINE,
			    "The '%s' failed to append new area_block, it will try again.\n",
			    area->name);

	      lf_bitmap_destroy (&block->bitmap);
	      free_and_init (block);
	    }
	  curr = VOLATILE_ACCESS (*curr_p, AREA_BLOCK *);
	  assert_release (curr != NULL);
	}
    }

#if defined(SERVER_MODE)
  /* do not count in SERVER_MODE */
  /* ATOMIC_INC_32 (&area->n_allocs, 1); */
#else
  area->n_allocs++;
#endif

  entry_ptr = curr->data + area->element_size * free_entry_idx;

#if !defined (NDEBUG)
  prefix = (int *) entry_ptr;
  *prefix = AREA_PREFIX_INITED;

  entry_ptr += AREA_PREFIX_SIZE;
#endif /* !NDEBUG */

  assert (entry_ptr < (curr->data + area->block_size));

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
  int error = ER_AREA_ILLEGAL_POINTER;

  assert (area != NULL);

  for (p = area->blocks; p != NULL && error != NO_ERROR; p = p->next)
    {
      if ((p->data <= (char *) address)
	  && (p->data + area->block_size > (char *) address))
	{
	  error = NO_ERROR;
	  break;
	}
    }

  if (error != NO_ERROR)
    {
      /* need more specific error here */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_AREA_ILLEGAL_POINTER, 0);
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
  AREA_BLOCK *curr;
  char *entry_ptr;
  int error, entry_idx;
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

  curr = area->blocks;
  while (curr != NULL)
    {
      if ((curr->data <= entry_ptr)
	  && (entry_ptr < curr->data + area->block_size))
	{
	  offset = (int) (entry_ptr - curr->data);
	  break;
	}
      curr = curr->next;
    }

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

  entry_idx = offset / area->element_size;

  assert (entry_idx >= 0 && entry_idx < area->alloc_count);

  error = lf_bitmap_free_entry (&curr->bitmap, entry_idx);
  if (error != NO_ERROR)
    {
      return error;
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
  AREA_BLOCK *block, *next;

  assert (area != NULL);


  for (block = area->blocks, next = NULL; block != NULL; block = next)
    {
      next = block->next;
      lf_bitmap_destroy (&block->bitmap);
      free_and_init (block);
    }
  area->blocks = NULL;

  if (area->name != NULL)
    {
      free_and_init (area->name);
    }
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
  AREA_BLOCK *block;
  size_t nblocks, bytes, elements, used, unused;
  size_t nallocs = 0, nfrees = 0;
  unsigned int i;

  assert (area != NULL && fp != NULL);

  nblocks = bytes = elements = used = unused = 0;

  for (block = area->blocks; block != NULL; block = block->next)
    {
      nblocks++;
      used += block->bitmap.entry_count_in_use;
      bytes += area->block_size + sizeof (AREA_BLOCK);
    }
  elements = (nblocks * area->alloc_count);
  unused = elements - used;

  nallocs = area->n_allocs;
  nfrees = area->n_frees;

  fprintf (fp, "Area: %s\n", area->name);
#if !defined (NDEBUG)
  fprintf (fp, "  %lld bytes/element ",
	   (long long) area->element_size - AREA_PREFIX_SIZE);
  fprintf (fp, "(plus %d bytes overhead) ", (int) AREA_PREFIX_SIZE);
#else
  fprintf (fp, "  %lld bytes/element ", (long long) area->element_size);
#endif
  fprintf (fp, "%lld elements/block\n", (long long) area->alloc_count);

  fprintf (fp, "  %lld blocks, %lld bytes, %lld elements,"
	   " %lld unused, %lld in use\n",
	   (long long) nblocks, (long long) bytes, (long long) elements,
	   (long long) unused, (long long) used);

  fprintf (fp, "  %lld total allocs, %lld total frees\n",
	   (long long) nallocs, (long long) nfrees);
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
