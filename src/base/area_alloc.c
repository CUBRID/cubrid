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


/* There must be at least this much room in each element */
#define AREA_MIN_SIZE sizeof(AREA_FREE_LIST)

/* The size of the prefix containing allocation status, if we're
   on a machine that requires double allignment of structures, we
   may have to make this sizeof(double) */
#define AREA_PREFIX_SIZE sizeof(double)

/*
 * Area_list - Global list of areas
 */
static AREA *area_List = NULL;
#if defined (SERVER_MODE)
pthread_mutex_t area_List_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/*
 * Area_check_free -
 * Area_check_pointers -
 *    Flags to enable checking for elements that have been freed twice and
 *    to check freed pointers to make sure they are actually within the area.
 */
static bool area_Check_free = false;
static bool area_Check_pointers = false;

static void area_info (AREA * area, FILE * fp);
static int area_grow (AREA * area, int thrd_index);

/*
 * area_init - Initialize the area manager
 *   return: none
 *   enable_check: Turn on Area_check_free and Area_check_pointers
 *
 * Note: Will be called during system startup
 */
void
area_init (bool enable_check)
{
#define ER_AREA_ALREADY_STARTED ER_GENERIC_ERROR
  if (area_List != NULL)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_AREA_ALREADY_STARTED, 0);
      return;
    }

  area_List = NULL;

  if (enable_check)
    {
      /* check free twice & valid pointers */
      area_Check_free = true;
      area_Check_pointers = true;
    }
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
      if (area->name != NULL)
	{
	  free_and_init (area->name);
	}
      if (area->area_entries != NULL)
	{
	  free_and_init (area->area_entries);
	}
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
  AREA_ENTRY *area_entry_p;
  size_t adjust;
  size_t size;
  unsigned int i;
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

  /* always make sure element size is a double word multiple */
  adjust = element_size % 8;
  if (adjust)
    {
      element_size += 8 - adjust;
    }

  area->element_size = element_size;
  area->alloc_count = alloc_count;

#if defined (SERVER_MODE)
  area->n_threads = thread_num_total_threads ();
  area->failure_function = NULL;
#else
  area->n_threads = 1;
  area->failure_function = ws_abort_transaction;
#endif

  size = sizeof (AREA_ENTRY) * area->n_threads;
  area->area_entries = (AREA_ENTRY *) malloc (size);
  if (area->area_entries == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, size);
      goto error;
    }

  for (i = 0; i < area->n_threads; i++)
    {
      area_entry_p = &(area->area_entries[i]);

      area_entry_p->blocks = NULL;
      area_entry_p->free = NULL;
      area_entry_p->n_allocs = 0;
      area_entry_p->n_frees = 0;
      area_entry_p->b_cnt = 0;
      area_entry_p->a_cnt = 0;
      area_entry_p->f_cnt = 0;
    }

  rv = pthread_mutex_lock (&area_List_lock);
  area->next = area_List;
  area_List = area;
  pthread_mutex_unlock (&area_List_lock);

  return area;

error:
  if (area->area_entries)
    {
      free_and_init (area->area_entries);
    }

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

  if (area->area_entries != NULL)
    {
      free_and_init (area->area_entries);
    }

  if (area->name != NULL)
    {
      free_and_init (area->name);
    }

  free_and_init (area);
}

/*
 * area_grow - Allocate a new block for an area and add it to the list
 *   return: NO_ERROR if success;
 *           ER_GENERIC_ERROR or ER_OUT_OF_VIRTUAL_MEMORY
 *   area(in): AREA
 *   thrd_index(in): thread index
 *
 * Note: this is called by area_alloc, and lock is also held by area_alloc
 */
static int
area_grow (AREA * area, int thrd_index)
{
  AREA_BLOCK *new_area;
  AREA_ENTRY *area_entry_p;
  size_t size, total;

  assert (area != NULL);

  /* since this will be expensive, take this oportunity to check some
     fundamental limits of the area, these errors indicate design
     errors, by the area callers, not normal run time errors */
  if (area->element_size < AREA_MIN_SIZE)
    {
      /* should make an error for this */
      fprintf (stdout, "Area \"%s\" element size %lld too small, aborting.\n",
	       area->name, (long long) area->element_size);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      if (area->failure_function != NULL)
	{
	  (*(area->failure_function)) ();
	}

      return ER_GENERIC_ERROR;	/* formerly called exit */
    }

  size = area->element_size * area->alloc_count;

  if (area_Check_free)
    {
      size += AREA_PREFIX_SIZE * area->alloc_count;
    }

  total = size + sizeof (AREA_BLOCK);

  new_area = (AREA_BLOCK *) malloc (total);
  if (new_area == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      total);
      if (area->failure_function != NULL)
	{
	  (*(area->failure_function)) ();
	}

      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  new_area->data = (char *) new_area + sizeof (AREA_BLOCK);
  new_area->pointer = new_area->data;
  new_area->max = (char *) (new_area->data) + size;

  area_entry_p = &area->area_entries[thrd_index];

  new_area->next = area_entry_p->blocks;
  area_entry_p->blocks = new_area;

  area_entry_p->b_cnt++;

  return NO_ERROR;
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
  AREA_FREE_LIST *link = NULL;
  AREA_ENTRY *area_entry_p;
  int index;

  assert (area != NULL);

  index = thread_get_current_entry_index ();

  area_entry_p = &(area->area_entries[index]);

  if (area_entry_p->free != NULL)
    {
      link = area_entry_p->free;
      area_entry_p->free = link->next;
      area_entry_p->f_cnt--;
    }
  else
    {
      if (area_entry_p->blocks == NULL)
	{
	  if (area_grow (area, index) != NO_ERROR)
	    {
	      /* out of memory ! */
	      return NULL;
	    }
	}

      if (area_Check_free)
	{
	  link =
	    (AREA_FREE_LIST *) (area_entry_p->blocks->pointer +
				AREA_PREFIX_SIZE);
	  area_entry_p->blocks->pointer +=
	    (area->element_size + AREA_PREFIX_SIZE);
	}
      else
	{
	  link = (AREA_FREE_LIST *) area_entry_p->blocks->pointer;
	  area_entry_p->blocks->pointer += area->element_size;
	}

      if (area_entry_p->blocks->pointer >= area_entry_p->blocks->max)
	{
	  area_entry_p->blocks->pointer = area_entry_p->blocks->max;
	  (void) area_grow (area, index);
	}
    }

  if (area_Check_free)
    {
      int *prefix;

      prefix = (int *) (((char *) link) - AREA_PREFIX_SIZE);
      *prefix = 0;
    }

  area_entry_p->n_allocs++;
  area_entry_p->a_cnt++;

  return ((void *) link);
}

/*
 * area_validate - validate that a pointer is within range in an area
 *   return: NO_ERROR if ok (address in range) or ER_QF_ILLEGAL_POINTER
 *   area(in): AREA
 *   thrd_index(in): thread index
 *   address(in): pointer to check
 *
 * Note: ER_QF_ILLEGAL_POINTER will be set if fails.
 *       This does not guarentee that the pointer is alligned
 *       correctly to the start of an element, only that it points into one
 *       of the area blocks.
 */
int
area_validate (AREA * area, int thrd_index, const void *address)
{
  AREA_ENTRY *area_entry_p;
  AREA_BLOCK *p;
  int error = ER_QF_ILLEGAL_POINTER;

  assert (area != NULL);

  area_entry_p = &(area->area_entries[thrd_index]);
  for (p = area_entry_p->blocks; p != NULL && error != NO_ERROR; p = p->next)
    {
      if ((p->data <= (char *) address) && (p->max > (char *) address))
	{
	  error = NO_ERROR;
	}
    }

  if (error != NO_ERROR)
    {
      /* need more specific error here */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QF_ILLEGAL_POINTER, 0);
    }

  return error;
}

/*
 * area_free - Free an element in an area
 *   return: none
 *   area(in): AREA
 *   ptr(in): pointer to the element
 *
 * Note: Validation is performed; the element is simply pushed on the free list
 */
void
area_free (AREA * area, void *ptr)
{
  AREA_ENTRY *area_entry_p;
  AREA_FREE_LIST *link;
  int *prefix;
  int index;

  assert (area != NULL);

  index = thread_get_current_entry_index ();

  if (area_Check_pointers && area_validate (area, index, ptr) != NO_ERROR)
    {
      return;
    }

  if (area_Check_free)
    {
      prefix = (int *) (((char *) ptr) - AREA_PREFIX_SIZE);
      if (*prefix)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QF_FREE_TWICE, 0);
	  return;
	}
      else
	{
	  *prefix = 0x01010101;
	}
    }

  area_entry_p = &(area->area_entries[index]);

  link = (AREA_FREE_LIST *) ptr;
  link->next = area_entry_p->free;
  area_entry_p->n_frees++;
  area_entry_p->f_cnt++;
  area_entry_p->a_cnt--;
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
  AREA_ENTRY *area_entry_p;
  AREA_BLOCK *block, *next;
  unsigned int i;

  assert (area != NULL);

  for (i = 0; i < area->n_threads; i++)
    {
      area_entry_p = &(area->area_entries[i]);

      for (block = area_entry_p->blocks, next = NULL; block != NULL;
	   block = next)
	{
	  next = block->next;
	  free_and_init (block);
	}

      area_entry_p->free = NULL;
      area_entry_p->blocks = NULL;
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
  AREA_ENTRY *area_entry_p;
  AREA_BLOCK *block;
  AREA_FREE_LIST *free;
  size_t nblocks, bytes, elements, unallocated, used, freed;
  size_t overhead, element_size;
  size_t nallocs = 0, nfrees = 0;
  unsigned int i;

  assert (area != NULL && fp != NULL);

  nblocks = bytes = elements = unallocated = used = freed = 0;

  overhead = 0;
  if (area_Check_free)
    {
      overhead = AREA_PREFIX_SIZE;
    }
  element_size = area->element_size + overhead;

  for (i = 0; i < area->n_threads; i++)
    {
      area_entry_p = &(area->area_entries[i]);
      for (block = area_entry_p->blocks; block != NULL; block = block->next)
	{
	  nblocks++;
	  bytes += ((element_size * area->alloc_count) + sizeof (AREA_BLOCK));
	  unallocated += (int) ((block->max - block->pointer) / element_size);
	}

      for (free = area_entry_p->free; free != NULL; free = free->next)
	{
	  freed++;
	}

      nallocs += area_entry_p->n_allocs;
      nfrees += area_entry_p->n_frees;
    }

  elements = (nblocks * area->alloc_count);
  used = (elements - unallocated - freed);

  fprintf (fp, "Area: %s\n", area->name);
  fprintf (fp, "  %lld bytes/element ", (long long) area->element_size);
  if (area_Check_free)
    {
      fprintf (fp, "(plus %d bytes overhead) ", (int) AREA_PREFIX_SIZE);
    }
  fprintf (fp, "%lld elements/block\n", (long long) area->alloc_count);
  fprintf (fp, "  %lld blocks, %lld bytes, %lld elements,"
	   " %lld unallocated, %lld free, %lld in use\n",
	   (long long) nblocks, (long long) bytes, (long long) elements,
	   (long long) unallocated, (long long) freed, (long long) used);
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
