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
#include "thread.h"
#endif /* SERVER_MODE */


/*
 * AREA_FREE_LIST - Structure used in the implementation of workspace areas.
 *   This structure should not be used by external modules but is needed
 *   for the definition of ws_area.
 */
typedef struct area_free_list AREA_FREE_LIST;
struct area_free_list
{
  struct area_free_list *next;
};

/*
 * AREA_BLOCK - Structure used in the implementation of workspace areas.
 *   Maintains information about a block of allocation in an area.
 *   This structure should not be used by external modules but is needed
 *   for the definition of ws_area.
 */
typedef struct area_block AREA_BLOCK;
struct area_block
{
  struct area_block *next;
  char *data;
  char *pointer;
  char *max;
};

/*
 * AREA - Primary structure for an allocation area.
 *   These are usually initialized in static space by a higher module
 *   and passed to the ws_area functions.  The "blocks" and "free" fields
 *   must be initialized to NULL by the higher module as these will
 *   be allocated by the area functions.
 */
typedef struct area AREA;
struct area
{
  struct area *next;

  char *name;
  size_t element_size;
  size_t alloc_count;

#if defined (SERVER_MODE)
  size_t n_threads;

  AREA_BLOCK **blocks;
  AREA_FREE_LIST **free;

  size_t *n_allocs;
  size_t *n_frees;
  size_t *b_cnt;
  size_t *a_cnt;
  size_t *f_cnt;
#else				/* SERVER_MODE */
  AREA_BLOCK *blocks;
  AREA_FREE_LIST *free;

  size_t n_allocs;
  size_t n_frees;
  size_t b_cnt;
  size_t a_cnt;
  size_t f_cnt;
#endif				/* SERVER_MODE */

  void (*failure_function) (void);
  bool need_gc;
};


/* system startup, shutdown */
extern void area_init (bool enable_check);
extern void area_final (void);

/* area definition */
extern AREA *area_create (const char *name, size_t element_size,
			  size_t alloc_count, bool need_gc);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void area_destroy (AREA * area);
#endif

/* allocation functions */
extern void *area_alloc (AREA * area);
extern int area_validate (AREA * area, int thrd_index, const void *address);
extern void area_free (AREA * area, void *ptr);
extern void area_flush (AREA * area);

/* debug functions */
extern void area_dump (FILE * fpp);

#endif /* _AREA_ALLOC_H_ */
