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
 * area_alloc.h - Area memory manager manager
 *
 * Note:
 */

#ifndef _AREA_ALLOC_H_
#define _AREA_ALLOC_H_

#ident "$Id$"

#ifndef __cplusplus
#error C++ is required
#endif

#if !defined (SERVER_MODE)
#else /* SERVER_MODE */
#include "connection_defs.h"
#endif /* SERVER_MODE */
#include "lockfree_bitmap.hpp"
#include "porting.h"

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
