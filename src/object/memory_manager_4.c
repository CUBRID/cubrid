/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * mmgr.c - Utility routines used by all DB_MMGR implementations.
 * TODO: merge this file into base/memory_manager_2.c 
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory_manager_4.h"

#if !defined (SERVER_MODE)
unsigned int private_heap_id = 0;
#endif /* SERVER_MODE */

/*
 * db_alignment () -
 *   return:
 *   n(in):
 */
int
db_alignment (int n)
{
  /*
   * Assumes that anything bigger than sizeof(double) will require
   * double alignment, and so on.  This isn't always the case, but
   * because of the way we add extra space onto the start of a region
   * in debugging mode, it's the only safe way to play the game.  For
   * example, a user may ask for 3 bytes, but in debugging mode we'll
   * turn that into (3 + sizeof(DEBUG_HEADER) + sizeof(GUARD_BAND)),
   * *and* it will need to be at least pointer-aligned, because the
   * first thing in the DEBUG_HEADER will be a pointer.
   */
  return (n >= (int) sizeof (double)) ? (int) sizeof (double) :
    (n >= (int) sizeof (void *))? (int) sizeof (void *) :
    (n >= (int) sizeof (int)) ? (int) sizeof (int) :
    (n >= (int) sizeof (short)) ? (int) sizeof (short) : 1;
}

/*
 * db_align_to () - Return the least multiple of 'alignment' that is greater
 * than or equal to 'n'.
 *   return:
 *   n(in):
 *   alignment(in):
 */
uintptr_t
db_align_to (uintptr_t n, int alignment)
{
  /*
   * Return the least multiple of 'alignment' that is greater than or
   * equal to 'n'.  'alignment' must be a power of 2.
   */
  return (uintptr_t) (((uintptr_t) n +
		       ((uintptr_t) alignment -
			1)) & ~((uintptr_t) alignment - 1));
}
