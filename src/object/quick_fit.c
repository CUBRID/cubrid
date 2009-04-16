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
 * quick_fit.c: Implementation of the workspace heap
 */

#ident "$Id$"

#include "config.h"
#include "customheaps.h"
#include "quick_fit.h"
#include "memory_alloc.h"

unsigned int ws_heap_id = 0;

/*
 * db_create_workspace_heap () - create a lea heap
 *   return: memory heap identifier
 *   req_size(in): a paramter to get chunk size
 *   recs_per_chunk(in): a parameter to get chunk size
 */
unsigned int
db_create_workspace_heap (void)
{
  ws_heap_id = hl_register_lea_heap ();
  return ws_heap_id;
}

/*
 * db_destroy_workspace_heap () - destroy a lea heap
 *   return:
 *   heap_id(in): memory heap identifier to destroy
 */
void
db_destroy_workspace_heap (void)
{
  hl_unregister_lea_heap (ws_heap_id);
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

  if (ws_heap_id && size > 0)
    {
      PRIVATE_MALLOC_HEADER *h;
      size_t req_sz;

      req_sz = private_request_size (size);
      h = hl_lea_alloc (ws_heap_id, req_sz);

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
  if (ws_heap_id && (size > 0))
    {
      ptr = hl_lea_alloc (ws_heap_id, size);
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

  if (ws_heap_id && size > 0)
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
	  new_h = hl_lea_realloc (ws_heap_id, h, req_sz);
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
  if (ws_heap_id && (size > 0))
    {
      ptr = hl_lea_realloc (ws_heap_id, ptr, size);
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
  if (ws_heap_id && ptr)
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
	  hl_lea_free (ws_heap_id, h);
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
  if (ws_heap_id && ptr)
    {
      hl_lea_free (ws_heap_id, ptr);
    }
#endif
}
