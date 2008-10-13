/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * area_alloc.c - Area memory manager
 * TODO: rename this file to area_alloc.c
 *
 * Note:
 * Allocation areas provide a way to block allocate structures and maintain
 * a free list.  Usefull for small structures that are used frequently.
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
#include "memory_manager_2.h"
#include "memory_manager_1.h"
#include "work_space.h"
#include "object_domain.h"
#include "set_object_1.h"
#if defined (SERVER_MODE)
#include "thread_impl.h"
#include "csserror.h"
#endif


#if !defined (SERVER_MODE)
#undef MUTEX_INIT
#undef MUTEX_DESTROY
#undef MUTEX_LOCK
#undef MUTEX_UNLOCK

#define MUTEX_INIT(a)
#define MUTEX_DESTROY(a)
#define MUTEX_LOCK(a, b)
#define MUTEX_UNLOCK(a)
#endif


/* There must be at least this much room in each element */
#define AREA_MIN_SIZE sizeof(AREA_FREE_LIST)

/* The size of the prefix containing allocation status, if we're
   on a machine that requires double allignment of structures, we
   may have to make this sizeof(double) */
/* TODO: LP64? by iamyaw (Mar 25, 2008) */
#define AREA_PREFIX_SIZE sizeof(double)

/*
 * Area_list - Global list of areas
 */
static AREA *area_List = NULL;
#if defined (SERVER_MODE)
MUTEX_T area_List_lock = MUTEX_INITIALIZER;
#endif

/*
 * Area_check_free -
 * Area_check_pointers -
 *    Flags to enable checking for elements that have been freed twice and
 *    to check freed pointers to make sure they are actually within the area.    *
 */
static bool area_Check_free = false;
static bool area_Check_pointers = false;

static void area_info (AREA * area, FILE * fp);
#if defined(SERVER_MODE)
static int area_grow (AREA * area, int thrd_index);
#else /* SERVER_MODE */
static int area_grow (AREA * area);
#endif /* SERVER_MODE */

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
	free_and_init (area->name);
      free_and_init (area);
    }
  area_List = NULL;

  tp_Domain_area = Set_Ref_Area = Set_Obj_Area = NULL;
#if defined(WINDOWS) && defined(SERVER_MODE)
  if (area_List_lock)
#endif
    MUTEX_DESTROY (area_List_lock);
}


/*
 * area_create - Build a new area and add it to the global list
 *   return: created AREA or NULL if fail
 *   name(in):
 *   element_size(in):
 *   alloc_count(in):
 *   flags(in):
 *
 * Note:
 */
AREA *
area_create (const char *name, size_t element_size, size_t alloc_count,
	     bool need_gc)
{
  AREA *area;
  size_t adjust;
#if defined (SERVER_MODE)
  int n_work_threads, dummy1, dummy2;
  unsigned int i;
  int rv;
#endif /* SERVER_MODE */

  if ((area = (AREA *) malloc (sizeof (AREA))) == NULL)
    return NULL;

  if (name == NULL)
    area->name = NULL;
  else
    area->name = strdup (name);

  /* always make sure element size is a double word multiple */
  adjust = element_size % 8;
  if (adjust)
    element_size += 8 - adjust;

  area->element_size = element_size;
  area->alloc_count = alloc_count;
  area->need_gc = need_gc;

#if defined (SERVER_MODE)
  thread_get_info_threads (&n_work_threads, &dummy1, &dummy2);
  area->n_threads = n_work_threads + NUM_PRE_DEFINED_THREADS;
  if (area->n_threads < 100)
    {
#if defined(CUBRID_DEBUG)
      fprintf (stderr,
	       "area(%s)->nthreads is smaller than 100. set to 100.\n",
	       area->name);
#endif
      area->n_threads = 100;
    }
  area->blocks = NULL;
  area->free = NULL;
  area->n_allocs = NULL;
  area->n_frees = NULL;
  area->b_cnt = NULL;
  area->a_cnt = NULL;
  area->f_cnt = NULL;
  area->failure_function = NULL;

  if ((area->blocks =
       (AREA_BLOCK **) malloc (sizeof (void *) * area->n_threads)) == NULL)
    goto error;
  if ((area->free =
       (AREA_FREE_LIST **) malloc (sizeof (void *) * area->n_threads)) ==
      NULL)
    goto error;
  if ((area->n_allocs =
       (size_t *) malloc (sizeof (int) * area->n_threads)) == NULL)
    goto error;
  if ((area->n_frees =
       (size_t *) malloc (sizeof (int) * area->n_threads)) == NULL)
    goto error;
  if ((area->b_cnt =
       (size_t *) malloc (sizeof (int) * area->n_threads)) == NULL)
    goto error;
  if ((area->a_cnt =
       (size_t *) malloc (sizeof (int) * area->n_threads)) == NULL)
    goto error;
  if ((area->f_cnt =
       (size_t *) malloc (sizeof (int) * area->n_threads)) == NULL)
    goto error;

  for (i = 0; i < area->n_threads; i++)
    {
      area->blocks[i] = NULL;
      area->free[i] = NULL;
      area->n_allocs[i] = 0;
      area->n_frees[i] = 0;
      area->b_cnt[i] = 0;
      area->a_cnt[i] = 0;
      area->f_cnt[i] = 0;
    }
#else /* SERVER_MODE */
  area->blocks = NULL;
  area->free = NULL;
  area->n_allocs = 0;
  area->n_frees = 0;
  area->b_cnt = 0;
  area->a_cnt = 0;
  area->f_cnt = 0;
  /* new, a function to call when out of virtual memory.  Should be
     an argument to this function */
  area->failure_function = ws_abort_transaction;
#endif

  MUTEX_LOCK (rv, area_List_lock);
  area->next = area_List;
  area_List = area;
  MUTEX_UNLOCK (area_List_lock);

  return (area);

#if defined (SERVER_MODE)
error:
  if (area->blocks)
    free_and_init (area->blocks);
  if (area->free)
    free_and_init (area->free);
  if (area->n_allocs)
    free_and_init (area->n_allocs);
  if (area->n_frees)
    free_and_init (area->n_frees);
  if (area->b_cnt)
    free_and_init (area->b_cnt);
  if (area->a_cnt)
    free_and_init (area->a_cnt);
  if (area->f_cnt)
    free_and_init (area->f_cnt);
  if (area->name)
    free_and_init (area->name);
  free_and_init (area);
  return NULL;
#endif /* SERVER_MODE */
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

  MUTEX_LOCK (rv, area_List_lock);

  for (prev = NULL, a = area_List; a != NULL && a != area; a = a->next)
    prev = a;

  if (a != NULL)
    {
      if (prev == NULL)
	area_List = a->next;
      else
	prev->next = a->next;
    }
  MUTEX_UNLOCK (area_List_lock);

  area_flush (area);
  if (area->name != NULL)
    free_and_init (area->name);
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
#if defined(SERVER_MODE)
static int
area_grow (AREA * area, int thrd_index)
#else /* SERVER_MODE */
static int
area_grow (AREA * area)
#endif				/* SERVER_MODE */
{
  AREA_BLOCK *new_;
  size_t size, total;

  assert (area != NULL);

  /* since this will be expensive, take this oportunity to check some
     fundamental limits of the area, these errors indicate design
     errors, by the area callers, not normal run time errors */
  if (area->element_size < AREA_MIN_SIZE)
    {
      /* should make an error for this */
      fprintf (stdout, "Area \"%s\" element size %d too small, aborting.\n",
	       area->name, area->element_size);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      if (area->failure_function != NULL)
	(*(area->failure_function)) ();

      return ER_GENERIC_ERROR;	/* formerly called exit */
    }

  size = area->element_size * area->alloc_count;

  if (area_Check_free)
    size += AREA_PREFIX_SIZE * area->alloc_count;

  total = size + sizeof (AREA_BLOCK);

  new_ = (AREA_BLOCK *) malloc (total);
  if (new_ == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      total);
      if (area->failure_function != NULL)
	(*(area->failure_function)) ();
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  new_->data = (char *) new_ + sizeof (AREA_BLOCK);
  new_->pointer = new_->data;
  new_->max = (char *) (new_->data) + size;
#if defined (SERVER_MODE)
  new_->next = area->blocks[thrd_index];
  area->blocks[thrd_index] = new_;
  area->b_cnt[thrd_index] += 1;
#else /* SERVER_MODE */
  new_->next = area->blocks;
  area->blocks = new_;
  area->b_cnt += 1;
#endif /* SERVER_MODE */

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
#if defined (SERVER_MODE)
  THREAD_ENTRY *_thrd = thread_get_thread_entry_info ();
#endif

  assert (area != NULL);

#if defined (SERVER_MODE)
  if (area->free[_thrd->index] != NULL)
    {
      link = area->free[_thrd->index];
      area->free[_thrd->index] = link->next;
      area->f_cnt[_thrd->index] -= 1;
    }
  else
    {
      if (area->blocks[_thrd->index] == NULL)
	{
	  if (area_grow (area, _thrd->index) != NO_ERROR)
	    {
	      /* out of memory ! */
	      return (NULL);
	    }
	}

      if (area_Check_free)
	{
	  link = (AREA_FREE_LIST *) (area->blocks[_thrd->index]->pointer
				     + AREA_PREFIX_SIZE);
	  area->blocks[_thrd->index]->pointer += (area->element_size
						  + AREA_PREFIX_SIZE);
	}
      else
	{
	  link = (AREA_FREE_LIST *) area->blocks[_thrd->index]->pointer;
	  area->blocks[_thrd->index]->pointer += area->element_size;
	}

      if (area->blocks[_thrd->index]->pointer >=
	  area->blocks[_thrd->index]->max)
	{
	  area->blocks[_thrd->index]->pointer =
	    area->blocks[_thrd->index]->max;
	  (void) area_grow (area, _thrd->index);
	}
    }
#else /* SERVER_MODE */
  if (area->free != NULL)
    {
      link = area->free;
      area->free = link->next;
      area->f_cnt -= 1;
    }
  else
    {
      if (area->blocks == NULL)
	{
	  if (area_grow (area) != NO_ERROR)
	    {
	      /* out of memory ! */
	      return (NULL);
	    }
	}

      if (area_Check_free)
	{
	  link =
	    (AREA_FREE_LIST *) (area->blocks->pointer + AREA_PREFIX_SIZE);
	  area->blocks->pointer += (area->element_size + AREA_PREFIX_SIZE);
	}
      else
	{
	  link = (AREA_FREE_LIST *) area->blocks->pointer;
	  area->blocks->pointer += area->element_size;
	}

      if (area->blocks->pointer >= area->blocks->max)
	{
	  area->blocks->pointer = area->blocks->max;
	  (void) area_grow (area);
	}
    }
#endif /* SERVER_MODE */

  if (area_Check_free)
    {
      int *prefix;
      prefix = (int *) (((char *) link) - AREA_PREFIX_SIZE);
      *prefix = 0;
    }

#if defined (SERVER_MODE)
  area->n_allocs[_thrd->index]++;
  area->a_cnt[_thrd->index] += 1;
#else /* SERVER_MODE */
  area->n_allocs++;
  area->a_cnt += 1;
#endif /* SERVER_MODE */

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
  AREA_BLOCK *b;
  int error = ER_QF_ILLEGAL_POINTER;

  assert (area != NULL);

#if defined (SERVER_MODE)
  b = area->blocks[thrd_index];
#else /* SERVER_MODE */
  b = area->blocks;
#endif /* SERVER_MODE */
  for (; b != NULL && error != NO_ERROR; b = b->next)
    {
      if ((b->data <= (char *) address) && (b->max > (char *) address))
	error = NO_ERROR;
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
  AREA_FREE_LIST *link;
  int *prefix;
#if defined (SERVER_MODE)
  THREAD_ENTRY *_thrd = thread_get_thread_entry_info ();
#endif

  assert (area != NULL);

#if defined (SERVER_MODE)
  if (area_Check_pointers
      && area_validate (area, _thrd->index, ptr) != NO_ERROR)
    return;
#else
  if (area_Check_pointers && area_validate (area, 0, ptr) != NO_ERROR)
    return;
#endif

#if defined (SERVER_MODE)
  if (area_Check_free)
    {
      prefix = (int *) (((char *) ptr) - AREA_PREFIX_SIZE);
      if (*prefix)
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QF_FREE_TWICE, 0);
      else
	{
	  *prefix = 0x01010101;
	  link = (AREA_FREE_LIST *) ptr;
	  link->next = area->free[_thrd->index];
	  area->free[_thrd->index] = link;
	  area->n_frees[_thrd->index]++;
	  area->f_cnt[_thrd->index] += 1;
	  area->a_cnt[_thrd->index] -= 1;
	}
    }
  else
    {
      link = (AREA_FREE_LIST *) ptr;
      link->next = area->free[_thrd->index];
      area->free[_thrd->index] = link;
      area->n_frees[_thrd->index]++;
      area->f_cnt[_thrd->index] += 1;
      area->a_cnt[_thrd->index] -= 1;
    }
#else /* SERVER_MODE */
  if (area_Check_free)
    {
      prefix = (int *) (((char *) ptr) - AREA_PREFIX_SIZE);
      if (*prefix)
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QF_FREE_TWICE, 0);
      else
	{
	  *prefix = 0x01010101;
	  link = (AREA_FREE_LIST *) ptr;
	  link->next = area->free;
	  area->free = link;
	  area->free = link;
	  area->n_frees++;
	  area->f_cnt += 1;
	  area->a_cnt -= 1;
	}
    }
  else
    {
      link = (AREA_FREE_LIST *) ptr;
      link->next = area->free;
      area->free = link;
      area->n_frees++;
      area->f_cnt += 1;
      area->a_cnt -= 1;
    }
#endif /* SERVER_MODE */
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
#if defined (SERVER_MODE)
  unsigned int i;
#endif

  assert (area != NULL);

#if defined (SERVER_MODE)
  for (i = 0; i < area->n_threads; i++)
    {
      block = area->blocks[i];
      for (next = NULL; block != NULL; block = next)
	{
	  next = block->next;
	  free_and_init (block);
	}
    }
  free_and_init (area->blocks);
  area->blocks = NULL;
  free_and_init (area->free);
  area->free = NULL;
  free_and_init (area->n_allocs);
  area->n_allocs = NULL;
  free_and_init (area->n_frees);
  area->n_frees = NULL;
  free_and_init (area->b_cnt);
  area->b_cnt = NULL;
  free_and_init (area->a_cnt);
  area->a_cnt = NULL;
  free_and_init (area->f_cnt);
  area->f_cnt = NULL;
#else /* SERVER_MODE */
  for (block = area->blocks, next = NULL; block != NULL; block = next)
    {
      next = block->next;
      free_and_init (block);
    }
  area->free = NULL;
  area->blocks = NULL;
#endif /* SERVER_MODE */
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
  AREA_FREE_LIST *free;
  size_t blocks, bytes, elements, unallocated, used, freed;
  size_t overhead, element_size;
#if defined (SERVER_MODE)
  size_t nallocs = 0, nfrees = 0;
#endif

  assert (area != NULL && fp != NULL);

  blocks = bytes = elements = unallocated = used = freed = 0;

  overhead = 0;
  if (area_Check_free)
    overhead = AREA_PREFIX_SIZE;
  element_size = area->element_size + overhead;

  if (area->blocks != NULL)
    {
#if defined (SERVER_MODE)
      unsigned int i;

      for (i = 0; i < area->n_threads; i++)
	{
	  for (block = area->blocks[i]; block != NULL; block = block->next)
	    {
	      blocks++;
	      bytes +=
		(element_size * area->alloc_count) + sizeof (AREA_BLOCK);
	    }

	  elements += (blocks * area->alloc_count);

	  for (free = area->free[i]; free != NULL;
	       free = free->next, freed++);

	  unallocated += (int) ((area->blocks[i]->max -
				 area->blocks[i]->pointer) / element_size);

	  used += (elements - unallocated - freed);

	  nallocs += area->n_allocs[i];
	  nfrees += area->n_frees[i];
	}
#else /* SERVER_MODE */
      for (block = area->blocks; block != NULL; block = block->next)
	{
	  blocks++;
	  bytes += (element_size * area->alloc_count) + sizeof (AREA_BLOCK);
	}

      elements = blocks * area->alloc_count;

      for (free = area->free, freed = 0; free != NULL;
	   free = free->next, freed++);

      unallocated = (int) (area->blocks->max - area->blocks->pointer)
	/ element_size;

      used = elements - unallocated - freed;
#endif /* SERVER_MODE */
    }

  fprintf (fp, "Area: %s\n", area->name);
  fprintf (fp, "  %d bytes/element ", area->element_size);
  if (area_Check_free)
    fprintf (fp, "(plus %d bytes overhead) ", AREA_PREFIX_SIZE);
  fprintf (fp, "%d elements/block\n", area->alloc_count);
  fprintf (fp, "  %d blocks, %d bytes, %d elements,"
	   " %d unallocated, %d free, %d in use\n",
	   blocks, bytes, elements, unallocated, freed, used);
#if defined (SERVER_MODE)
  fprintf (fp, "  %d total allocs, %d total frees\n", nallocs, nfrees);
#else /* SERVER_MODE */
  fprintf (fp, "  %d total allocs, %d total frees\n", area->n_allocs,
	   area->n_frees);
#endif /* SERVER_MODE */
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
    fp = stdout;

  MUTEX_LOCK (rv, area_List_lock);

  for (area = area_List; area != NULL; area = area->next)
    area_info (area, fp);

  MUTEX_UNLOCK (area_List_lock);
}
