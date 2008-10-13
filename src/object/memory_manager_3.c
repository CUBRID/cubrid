/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * fixed_alloc.c - implementation of fixed-size record allocator 
 * TODO: move this file to base/ and rename to fiexd_alloc.c
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "memory_manager_4.h"
#include "customheaps.h"
#if defined (SERVER_MODE)
#include "defs.h"
#include "thread_impl.h"
#include "csserror.h"
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
unsigned int
db_create_fixed_heap (int req_size, int recs_per_chunk)
{
  int chunk_size, rec_size;

  rec_size = compute_rec_size (req_size);
  chunk_size = rec_size * recs_per_chunk;

  return hl_register_fixed_heap (chunk_size);
}

/*
 * db_destroy_fixed_heap () - destroy a fixed heap
 *   return:
 *   heap_id(in): memory heap identifier to destroy
 */
void
db_destroy_fixed_heap (unsigned int heap_id)
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
db_fixed_alloc (unsigned int heap_id, size_t size)
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
db_fixed_free (unsigned int heap_id, void *ptr)
{
  if (heap_id && ptr)
    {
      hl_fixed_free (heap_id, ptr);
    }
}
