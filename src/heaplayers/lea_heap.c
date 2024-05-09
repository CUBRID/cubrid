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
 * Following is Doug Lea's memory allocator with USE_MALLOC_INSTEAD 
 * feature amendment
 *
 * USE_MALLOC_INSTEAD
 * When this feature is enabled, uses system malloc/free instead of mmap/munmap.
 * 
 * ENABLE_SEPARATE_MMAP_EVENT_TRACE
 * Fix the problem that mmaped (malloced when USE_MALLOC_INSTEAD is 1)
 * memory region (which is returned by mmap_alloc function) is not
 * automatically freed when destroy_mspace is called.
 * 
 */

#include <stdlib.h>
#include <stddef.h>

#include "customheaps.h"
#include "error_manager.h"
#include "system_parameter.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* -------------------------------------------------------------------------- */
/* DL MALLOC ADAPTATION AND MODIFICATION LAYER */
/* -------------------------------------------------------------------------- */
/*
 * use malloc/free instead of mmap/munmap
 */

#define USE_MALLOC_INSTEAD 1
#define MOCK_LEA_HEAP_ID ((UINTPTR)(-1))
#define system_malloc my_malloc
#define system_free my_free

static void *my_malloc (size_t sz);
static int my_free (void *p);

/*
 * override  default DEFAULT_GRANULARITY
 */
#define DEFAULT_GRANULARITY (32U*1024U)

/*
 * prevent mremap use
 */
#if !defined(WINDOWS)
#if defined(linux)
#undef linux
#endif
#endif

/*
 * use mspace only
 */
#define ONLY_MSPACES 1

/*
 * Fix the problem that mmaped (malloced when USE_MALLOC_INSTEAD is 1)
 * memory region (which is returned by mmap_alloc function) is not
 * automatically freed when destroy_mspace is called.
 */
typedef struct mmap_trace_h_s MMAP_TRACE_H;
struct mmap_trace_h_s
{
  MMAP_TRACE_H *next;
  MMAP_TRACE_H *prev;
  void *ptr;
};

#define MMAP_TRACE_H_SIZE sizeof(MMAP_TRACE_H)

#define MMAP_TRACE_H_INIT(h, p) do {    \
  MMAP_TRACE_H *__h = (h);              \
  (__h)->next = (__h)->prev = (__h);    \
  (__h)->ptr = (p);                     \
} while (0)

#define MMAP_TRACE_H_REMOVE(h) do {     \
  MMAP_TRACE_H *__h = (h);              \
  (__h)->next->prev = (__h)->prev;      \
  (__h)->prev->next = (__h)->next;      \
} while (0)

#define MMAP_TRACE_H_ADD(p, h) do {     \
  MMAP_TRACE_H *__ih = (p);             \
  MMAP_TRACE_H *__bh = (h);             \
  (__ih)->prev = (__bh);                \
  (__ih)->next = (__bh)->next;          \
  (__bh)->next->prev = (__ih);          \
  (__bh)->next = (__ih);                \
} while (0)

static void mmap_called (void *m, void *ptr, MMAP_TRACE_H * h);
static void munmap_is_to_be_called (void *m, void *ptr, MMAP_TRACE_H * h);

#define ENABLE_SEPARATE_MMAP_EVENT_TRACE
#include "malloc_2_8_3.c"

/*
 * my_malloc - system_malloc hook function 
 *    return: memory allocated
 *    sz(in): request memory size
 */
static void *
my_malloc (size_t sz)
{
  void *ptr = malloc (sz);
  if (ptr != NULL)
    {
      return ptr;
    }
  else
    {
      return CMFAIL;
    }
}

/*
 * my_free -  system_free hook function
 *    return: memory allocated
 *    p(in):
 */
static int
my_free (void *p)
{
  free (p);
  return 0;
}

/* ------------------------------------------------------------------------- */
/* HL_LEA_HEAP IMPLEMENTATION */
/* ------------------------------------------------------------------------- */

typedef struct hl_mspace_s HL_MSPACE;
struct hl_mspace_s
{
  mstate ms;
  void *base;
  size_t base_size;
  MMAP_TRACE_H header;
};

/* Memory layout of HL_MSPACE
   +-----------+
   | HL_MSPACE |
   +-----------+ <-- 8 byte aligned. msp
   |           |     mstate (or mspace) m = chunk2mem(msp)
   +-----------+ <-- m
   |  mstate   |
   | ......... |

 */
#define mspace2hlmspace(m) \
  (HL_MSPACE *)((char *)mem2chunk(m) - ((sizeof (HL_MSPACE) + 7U) & ~7U))

/*
 * mmap_called - ENABLE_SEPARATE_MMAP_EVENT_TRACE hook function called after
 *               large chunk allocated
 *    return: void
 *    m(in): lea heap mspace pointer
 *    ptr(in): allocated chunk
 *    h(in): embedded trace header
 */
static void
mmap_called (void *m, void *ptr, MMAP_TRACE_H * h)
{
  HL_MSPACE *hms = mspace2hlmspace (m);
  MMAP_TRACE_H_INIT (h, ptr);
  MMAP_TRACE_H_ADD (h, &hms->header);
}

/*
 * munmap_is_to_be_called - ENABLE_SEPARATE_MMAP_EVENT_TRACE hook function 
 *                          called before large chunk is to be freed.
 *    return: void
 *    m(in): lea heap mspace pointer
 *    ptr(in): memory chunk
 *    h(in): embedded trace header
 */
static void
munmap_is_to_be_called (void *m, void *ptr, MMAP_TRACE_H * h)
{
  HL_MSPACE *hms = mspace2hlmspace (m);
  assert (h->ptr == ptr);
  MMAP_TRACE_H_REMOVE (h);
}

/* EXPORTED FUNCTIONS */
#define LEA_HEAP_BASE_SIZE (64U*1024U)

/*
 * hl_register_lea_heap - register new lea heap instance
 *    return: heap handle
 */
UINTPTR
hl_register_lea_heap (void)
{
#if !defined (NDEBUG)
  if (prm_get_bool_value (PRM_ID_USE_SYSTEM_MALLOC))
    {
      return MOCK_LEA_HEAP_ID;
    }
  else
#endif /* !NDEBUG */
    {
      HL_MSPACE *hms;

      hms = (HL_MSPACE *) malloc (LEA_HEAP_BASE_SIZE);
      if (hms == NULL)
	{
	  return 0;
	}

      hms->base = (char *) hms + ((sizeof (*hms) + 7U) & ~7U);
      hms->base_size = LEA_HEAP_BASE_SIZE - (size_t) ((char *) hms->base - (char *) hms);
      MMAP_TRACE_H_INIT (&hms->header, NULL);

      hms->ms = (mstate) create_mspace_with_base (hms->base, hms->base_size, 0);
      if (hms->ms == NULL)
	{
	  free (hms);
	  return 0;
	}

      return ((UINTPTR) hms);
    }
}

/*
 * destroy_mspace_internal - see unregister_leap_heap 
 *    return: void
 *    hms(in): leap heap mspace pointer
 */
static void
destroy_mspace_internal (HL_MSPACE * hms)
{
  destroy_mspace (hms->ms);
  hms->ms = NULL;
  /* remove unmapped segments */
  while (hms->header.next != &hms->header)
    {
      MMAP_TRACE_H *h = hms->header.next;
      MMAP_TRACE_H_REMOVE (h);	/* unlink from doubley linked list */
      my_free (h->ptr);		/* no need to free h. h is embedded */
    }
}


/*
 * hl_clear_lea_heap - clears lea heap
 *    return: void
 *    heap_id(in): lea heap handle
 */
void
hl_clear_lea_heap (UINTPTR heap_id)
{
#if !defined (NDEBUG)
  if (prm_get_bool_value (PRM_ID_USE_SYSTEM_MALLOC))
    {
      assert (heap_id == MOCK_LEA_HEAP_ID);
    }
  else
#endif /* !NDEBUG */
    {
      HL_MSPACE *hms = (HL_MSPACE *) heap_id;

      if (hms != NULL)
	{
	  destroy_mspace_internal (hms);
	  hms->ms = (mstate) create_mspace_with_base (hms->base, hms->base_size, 0);
	  assert (hms->ms != NULL);
	}
    }
}

/*
 * hl_unregister_lea_heap - destoyes lea heap
 *    return: void
 *    heap_id(in): lea heap handle
 */
void
hl_unregister_lea_heap (UINTPTR heap_id)
{
#if !defined (NDEBUG)
  if (prm_get_bool_value (PRM_ID_USE_SYSTEM_MALLOC))
    {
      assert (heap_id == MOCK_LEA_HEAP_ID);
    }
  else
#endif /* !NDEBUG */
    {
      HL_MSPACE *hms = (HL_MSPACE *) heap_id;

      if (hms != NULL)
	{
	  destroy_mspace_internal (hms);
	  free (hms);
	}
    }
}

/*
 * hl_lea_alloc - alloc
 *    return: pointer to allocated memory
 *    heap_id(in): lea heap handle
 *    sz(in): requested size
 */
void *
hl_lea_alloc (UINTPTR heap_id, size_t sz)
{
#if !defined (NDEBUG)
  if (prm_get_bool_value (PRM_ID_USE_SYSTEM_MALLOC))
    {
      return malloc (sz);
    }
  else
#endif /* !NDEBUG */
    {
      HL_MSPACE *hms = (HL_MSPACE *) heap_id;
      void *p;

      if (hms != NULL)
	{
	  p = mspace_malloc (hms->ms, sz);
	  if (p == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sz);
	    }

	  return p;
	}
      return NULL;
    }
}

/*
 * hl_lea_realloc - realloc
 *    return: pointer to re-allocated memory
 *    heap_id(in): lea heap handle
 *    ptr(in): pointer to memory block allocated before
 *    sz(in): requested size
 */
void *
hl_lea_realloc (UINTPTR heap_id, void *ptr, size_t sz)
{
#if !defined (NDEBUG)
  if (prm_get_bool_value (PRM_ID_USE_SYSTEM_MALLOC))
    {
      return realloc (ptr, sz);
    }
  else
#endif /* !NDEBUG */
    {
      HL_MSPACE *hms = (HL_MSPACE *) heap_id;
      void *p;

      if (hms != NULL)
	{
	  p = mspace_realloc (hms->ms, ptr, sz);
	  if (p == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sz);
	    }

	  return p;
	}
      return NULL;
    }
}

/*
 * hl_lea_free - free
 *    return: void
 *    heap_id(in): lea heap handle
 *    ptr(in): pointer to memory block allocated before
 */
void
hl_lea_free (UINTPTR heap_id, void *ptr)
{
#if !defined (NDEBUG)
  if (prm_get_bool_value (PRM_ID_USE_SYSTEM_MALLOC))
    {
      free (ptr);
    }
  else
#endif /* !NDEBUG */
    {
      HL_MSPACE *hms = (HL_MSPACE *) heap_id;

      if (hms != NULL)
	{
	  mspace_free (hms->ms, ptr);
	}
    }
}
