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
 * quick_fit.c: Implementation of the workspace heap
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "customheaps.h"
#include "memory_alloc.h"
#include "quick_fit.h"
#include "memory_alloc.h"

static HL_HEAPID ws_Heap_id = 0;

/*
 * db_create_workspace_heap () - create a lea heap
 *   return: memory heap identifier
 *   req_size(in): a paramter to get chunk size
 *   recs_per_chunk(in): a parameter to get chunk size
 */
HL_HEAPID
db_create_workspace_heap (void)
{
  if (ws_Heap_id == 0)
    {
      ws_Heap_id = hl_register_lea_heap ();
    }
  return ws_Heap_id;
}

/*
 * db_destroy_workspace_heap () - destroy a lea heap
 *   return:
 *   heap_id(in): memory heap identifier to destroy
 */
void
db_destroy_workspace_heap (void)
{
  if (ws_Heap_id != 0)
    {
      hl_unregister_lea_heap (ws_Heap_id);
      ws_Heap_id = 0;
    }
}

/*
 * db_ws_alloc () - call allocation function for the lea heap
 *   return: allocated memory pointer
 *   size(in): size to allocate
 */
void *
db_ws_alloc (size_t size)
{
#if defined(SA_MODE)
  void *ptr = NULL;

  if (ws_Heap_id == 0)
    {
      /* not initialized yet */
      db_create_workspace_heap ();
    }

  if (ws_Heap_id && size > 0)
    {
      PRIVATE_MALLOC_HEADER *h;
      size_t req_sz;

      req_sz = private_request_size (size);
      h = (PRIVATE_MALLOC_HEADER *) hl_lea_alloc (ws_Heap_id, req_sz);

      if (h != NULL)
	{
	  h->magic = PRIVATE_MALLOC_HEADER_MAGIC;
	  h->alloc_type = PRIVATE_ALLOC_TYPE_WS;
	  ptr = private_hl2user_ptr (h);
	}
    }
  return ptr;
#else
  void *ptr = NULL;

  if (ws_Heap_id == 0)
    {
      /* not initialized yet */
      db_create_workspace_heap ();
    }

  if (ws_Heap_id && (size > 0))
    {
      ptr = hl_lea_alloc (ws_Heap_id, size);
    }
  return ptr;
#endif
}

/*
 * db_ws_realloc () - call re-allocation function for the lea heap
 *   return: allocated memory pointer
 *   size(in): size to allocate
 */
void *
db_ws_realloc (void *ptr, size_t size)
{
#if defined(SA_MODE)
  if (ptr == NULL)
    {
      return db_ws_alloc (size);
    }

  if (ws_Heap_id == 0)
    {
      /* not initialized yet */
      db_create_workspace_heap ();
    }

  if (ws_Heap_id && size > 0)
    {
      PRIVATE_MALLOC_HEADER *h;

      h = private_user2hl_ptr (ptr);
      if (h->magic != PRIVATE_MALLOC_HEADER_MAGIC)
	{
	  return NULL;
	}

      if (h->alloc_type == PRIVATE_ALLOC_TYPE_WS)
	{
	  PRIVATE_MALLOC_HEADER *new_h;
	  size_t req_sz;

	  req_sz = private_request_size (size);
	  new_h = (PRIVATE_MALLOC_HEADER *) hl_lea_realloc (ws_Heap_id, h, req_sz);
	  if (new_h == NULL)
	    {
	      return NULL;
	    }
	  return private_hl2user_ptr (new_h);
	}
      else if (h->alloc_type == PRIVATE_ALLOC_TYPE_LEA)
	{
	  return db_private_realloc (NULL, ptr, size);
	}
      else
	{
	  return NULL;
	}
    }
  else
    {
      return NULL;
    }
#else
  if (ws_Heap_id == 0)
    {
      /* not initialized yet */
      db_create_workspace_heap ();
    }

  if (ws_Heap_id && (size > 0))
    {
      ptr = hl_lea_realloc (ws_Heap_id, ptr, size);
    }
  return ptr;
#endif
}

/*
 * db_ws_free () - call free function for the lea heap
 *   return:
 *   ptr(in): memory pointer to free
 */
void
db_ws_free (void *ptr)
{
#if defined(SA_MODE)
  assert (ws_Heap_id != 0);

  if (ws_Heap_id && ptr)
    {
      PRIVATE_MALLOC_HEADER *h;

      h = private_user2hl_ptr (ptr);
      if (h->magic != PRIVATE_MALLOC_HEADER_MAGIC)
	{
	  /* assertion point */
	  return;
	}

      if (h->alloc_type == PRIVATE_ALLOC_TYPE_WS)
	{
	  hl_lea_free (ws_Heap_id, h);
	}
      else if (h->alloc_type == PRIVATE_ALLOC_TYPE_LEA)
	{
	  db_private_free (NULL, ptr);	/* not 'h' */
	}
      else
	{
	  return;
	}
    }
#else
  assert (ws_Heap_id != 0);

  if (ws_Heap_id && ptr)
    {
      hl_lea_free (ws_Heap_id, ptr);
    }
#endif
}
