/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 *      qf.c: Implementation of the Quick Fit storage allocation
 *                algorithm.
 *      NOTE:
 *      There is a lot of pointer arithmetic in this file.  I've tried to
 *      be careful but they may be some machine/compiler assumptions in a
 *      few places.  Most of this is done in the macros at the top of the
 *      file so check there if something is breaking.  The code itself
 *      should be machine independent.
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
