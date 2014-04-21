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
 * fixed_alloc.c - implementation of fixed-size record allocator
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "memory_alloc.h"
#include "customheaps.h"
#if defined (SERVER_MODE)
#include "connection_defs.h"
#include "thread.h"
#include "connection_error.h"
#endif /* SERVER_MODE */

typedef struct rec_link REC_LINK;
struct rec_link
{
  REC_LINK *next;
};

/*
 * compute_rec_size () - compute the actual memory size for requested
 * memory size
 *   return:
 *   nominal_size(in):
 */
static int
compute_rec_size (int nominal_size)
{
  return db_align_to (MAX ((int) sizeof (REC_LINK), nominal_size),
		      MAX (db_alignment (nominal_size),
			   db_alignment ((int) sizeof (REC_LINK *))));
}

/*
 * db_create_fixed_heap () - create a fixed heap
 *   return: memory heap identifier
 *   req_size(in): a paramter to get chunk size
 *   recs_per_chunk(in): a parameter to get chunk size
 */
HL_HEAPID
db_create_fixed_heap (int req_size, int recs_per_chunk)
{
  int chunk_size, rec_size;

  rec_size = compute_rec_size (req_size);
  chunk_size = rec_size * recs_per_chunk;

  return (HL_HEAPID) hl_register_fixed_heap (chunk_size);
}

/*
 * db_destroy_fixed_heap () - destroy a fixed heap
 *   return:
 *   heap_id(in): memory heap identifier to destroy
 */
void
db_destroy_fixed_heap (HL_HEAPID heap_id)
{
  hl_unregister_fixed_heap (heap_id);
}

/*
 * db_fixed_alloc () - call allocation function for the fixed heap
 *   return: allocated memory pointer
 *   heap_id(in): memory heap identifier
 *   size(in): size to allocate
 */
void *
db_fixed_alloc (HL_HEAPID heap_id, size_t size)
{
  void *ptr = NULL;
  if (heap_id && (size > 0))
    {
      ptr = hl_fixed_alloc (heap_id, size);
    }
  return ptr;
}

/*
 * db_fixed_free () - call free function for the fixed heap
 *   return:
 *   heap_id(in): memory heap identifier
 *   ptr(in): memory pointer to free
 */
void
db_fixed_free (HL_HEAPID heap_id, void *ptr)
{
  if (heap_id && ptr)
    {
      hl_fixed_free (heap_id, ptr);
    }
}
