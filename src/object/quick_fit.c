/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */

/*
 * quick_fit.c: Implementation of the Quick Fit storage allocation algorithm.
 */

#ident "$Id$"

#include "config.h"
#include "customheaps.h"
#include "quick_fit.h"

unsigned int ws_heap_id = 0;

/*
 * db_create_workspace_heap () - create a kingsley heap
 *   return: memory heap identifier
 *   req_size(in): a paramter to get chunk size
 *   recs_per_chunk(in): a parameter to get chunk size
 */
unsigned int
db_create_workspace_heap (void)
{
  ws_heap_id = hl_register_kingsley_heap ();
  return ws_heap_id;
}

/*
 * db_destroy_workspace_heap () - destroy a kingsley heap
 *   return:
 *   heap_id(in): memory heap identifier to destroy
 */
void
db_destroy_workspace_heap (void)
{
  hl_unregister_kingsley_heap (ws_heap_id);
}

/*
 * db_ws_alloc () - call allocation function for the kingsley heap
 *   return: allocated memory pointer
 *   size(in): size to allocate
 */
void *
db_ws_alloc (size_t size)
{
  void *ptr = NULL;
  if (ws_heap_id && (size > 0))
    {
      ptr = hl_kingsley_alloc (ws_heap_id, size);
    }
  return ptr;
}

/*
 * db_ws_realloc () - call re-allocation function for the kingsley heap
 *   return: allocated memory pointer
 *   size(in): size to allocate
 */
void *
db_ws_realloc (void *ptr, size_t size)
{
  if (ws_heap_id && (size > 0))
    {
      ptr = hl_kingsley_realloc (ws_heap_id, ptr, size);
    }
  return ptr;
}

/*
 * db_ws_free () - call free function for the kingsley heap
 *   return:
 *   ptr(in): memory pointer to free
 */
void
db_ws_free (void *ptr)
{
  if (ws_heap_id && ptr)
    {
      hl_kingsley_free (ws_heap_id, ptr);
    }
}
