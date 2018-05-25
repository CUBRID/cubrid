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
 * area_alloc.h - Area memory manager manager
 *
 * Note:
 */

#ifndef _AREA_ALLOC_H_
#define _AREA_ALLOC_H_

#ident "$Id$"

#if !defined (SERVER_MODE)
#else /* SERVER_MODE */
#include "connection_defs.h"
#endif /* SERVER_MODE */
#include "lock_free.h"

#define AREA_BLOCKSET_SIZE 256

/*
 * AREA_BLOCK - Structure used in the implementation of workspace areas.
 *   Maintains information about a block of allocation in an area.
 *   This structure should not be used by external modules but is needed
 *   for the definition of ws_area.
 */
typedef struct area_block AREA_BLOCK;
struct area_block
{
  LF_BITMAP bitmap;
  char *data;
};

/*
 * AREA_BLOCKSET_LIST - Structure used in the implementation of workspace
 *   areas. It includes a group of blocks pointers. These pointers are
 *   sorted by their address.
 */
typedef struct area_blockset_list AREA_BLOCKSET_LIST;
struct area_blockset_list
{
  AREA_BLOCKSET_LIST *next;
  AREA_BLOCK *items[AREA_BLOCKSET_SIZE];
  int used_count;
};

/*
 * AREA - Primary structure for an allocation area.
 */
typedef struct area AREA;
struct area
{
  AREA *next;

  char *name;
  size_t element_size;		/* the element size, including prefix */
  size_t alloc_count;
  size_t block_size;

  AREA_BLOCKSET_LIST *blockset_list;	/* the blockset list */
  AREA_BLOCK *hint_block;	/* the hint block which may include free slot */
  pthread_mutex_t area_mutex;	/* only used for insert new block */

  /* for dumping */
  size_t n_allocs;		/* total alloc element count */
  size_t n_frees;		/* total free element count */

  void (*failure_function) (void);
};

#ifdef __cplusplus
extern "C"
{
#endif

/* system startup, shutdown */
  extern void area_init (void);
  extern void area_final (void);

/* area definition */
  extern AREA *area_create (const char *name, size_t element_size, size_t alloc_count);
  extern void area_destroy (AREA * area);

/* allocation functions */
  extern void *area_alloc (AREA * area);
  extern int area_validate (AREA * area, const void *address);
  extern int area_free (AREA * area, void *ptr);
  extern void area_flush (AREA * area);

/* debug functions */
  extern void area_dump (FILE * fpp);

#ifdef __cplusplus
}
#endif


#endif				/* _AREA_ALLOC_H_ */
