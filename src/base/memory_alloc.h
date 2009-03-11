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
 * memory_alloc.h - Memory allocation module
 */

#ifndef _MEMORY_ALLOC_H_
#define _MEMORY_ALLOC_H_

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <stddef.h>
#include <string.h>
#if !defined(WINDOWS)
#include <stdint.h>
#endif

#include "thread_impl.h"
#include "dbtype.h"

#define UP      TRUE
#define DOWN    FALSE

/* Ceiling of positive division */
#define CEIL_PTVDIV(dividend, divisor) \
        (((dividend) == 0) ? 0 : ((dividend) - 1) / (divisor) + 1)

/* Make sure that sizeof returns and integer, so I can use in the operations */
#define DB_SIZEOF(val)          (sizeof(val))

/*
 * Mascros related to alignmentss
 * TODO: Check LP64
 */
#define CHAR_ALIGNMENT          (size_t)sizeof(char)
#define SHORT_ALIGNMENT         (size_t)sizeof(short)
#define INT_ALIGNMENT           (size_t)sizeof(int)
#define LONG_ALIGNMENT          (size_t)sizeof(long)
#define FLOAT_ALIGNMENT         (size_t)sizeof(float)
#define DOUBLE_ALIGNMENT        (size_t)sizeof(double)
#define MAX_ALIGNMENT           DOUBLE_ALIGNMENT

#if defined (AIX) && defined (ALIGNMENT)
#undef ALIGNMENT
#endif /* AIX && ALIGNMENT */

#define ALIGNMENT       INT_ALIGNMENT
#define ALIGNMENT_MASK  ((unsigned long)(ALIGNMENT-1))

#define ALIGN_BELOW(p) \
        ((char*)((unsigned long)(p) & ~ALIGNMENT_MASK))
#define ALIGN_ABOVE(p) \
        ((char*)(((unsigned long)(p) + ALIGNMENT_MASK) & ~ALIGNMENT_MASK))
#define PTR_ALIGN(addr, boundary) \
        ((char *)((((unsigned long)(addr) + ((unsigned long)((boundary)-1)))) \
                  & ~((unsigned long)((boundary)-1))))

#define DB_ALIGN(offset, align) \
        (offset) = (((offset) + (align) - 1) & ~((align) - 1))

#define DB_ALIGN_BELOW(offset, align) \
        (offset) = ((offset) & ~((align) - 1))

#define DB_WASTED_ALIGN(offset, align, wasted) \
        { \
          (wasted) = (offset); \
          DB_ALIGN(wasted, align); \
          (wasted) -= (offset); \
        }

#define DB_ATT_ALIGN(offset) \
        (offset) = (((offset) + (LONG_ALIGNMENT) - 1) & ~((LONG_ALIGNMENT) - 1))

/*
 * Macros related to memory allocation
 */

#define MEM_REGION_INIT_MARK       '\0'	/* Set this to allocated areas */
#define MEM_REGION_SCRAMBLE_MARK         '\01'	/* Set this to allocated areas */
#define MEM_REGION_GUARD_MARK            '\02'	/* Set this as a memory guard to detect
						 * over/under runs  */

#if defined (CUBRID_DEBUG)
extern void db_scramble (void *region, int size);
#define MEM_REGION_INIT(region, size) \
        memset((region), MEM_REGION_SCRAMBLE_MARK, (size))
#define MEM_REGION_SCRAMBLE(region, size) \
        memset (region, MEM_REGION_SCRAMBLE_MARK, size)
#else /* CUBRID_DEBUG */
#define MEM_REGION_INIT(region, size) \
        memset((region), MEM_REGION_INIT_MARK, (size))
#define MEM_REGION_SCRAMBLE(region, size)
#endif /* CUBRID_DEBUG */

#if defined(NDEBUG)
#define db_private_free_and_init(thrd, ptr) \
        do { \
          if ((ptr)) { \
            db_private_free ((thrd), (ptr)); \
            (ptr) = NULL; \
          } \
        } while (0)

#define free_and_init(ptr) \
        do { \
          if ((ptr)) { \
            free ((ptr)); \
            (ptr) = NULL; \
          } \
        } while (0)
#else /* NDEBUG */
#define db_private_free_and_init(thrd, ptr) \
        do { \
          db_private_free ((thrd), (ptr)); \
          (ptr) = NULL; \
	} while (0)

#define free_and_init(ptr) \
        do { \
          free ((ptr)); \
          (ptr) = NULL; \
	} while (0)
#endif /* NDEBUG */

extern int ansisql_strcmp (const char *s, const char *t);
extern int ansisql_strcasecmp (const char *s, const char *t);

#if !defined (SERVER_MODE)
extern unsigned int private_heap_id;
#endif /* SERVER_MODE */

/*
 * Return the assumed minimum alignment requirement for the requested
 * size.  Multiples of sizeof(double) are assumed to need double
 * alignment, etc.
 */
extern int db_alignment (int);

/*
 * Return the value of "n" to the next "alignment" boundary.  "alignment"
 * must be a power of 2.
 */
extern uintptr_t db_align_to (uintptr_t n, int alignment);

extern unsigned int db_create_ostk_heap (int chunk_size);
extern void db_destroy_ostk_heap (unsigned int heap_id);

extern void *db_ostk_alloc (unsigned int heap_id, size_t size);
extern void db_ostk_free (unsigned int heap_id, void *ptr);

extern unsigned int db_create_private_heap (void);
extern void db_clear_private_heap (THREAD_ENTRY * thread_p,
				   unsigned int heap_id);
extern unsigned int db_change_private_heap (THREAD_ENTRY * thread_p,
					    unsigned int heap_id);
extern unsigned int db_replace_private_heap (THREAD_ENTRY * thread_p);
extern void db_destroy_private_heap (THREAD_ENTRY * thread_p,
				     unsigned int heap_id);
extern void *db_private_alloc (void *thrd, size_t size);
extern void *db_private_realloc (void *thrd, void *ptr, size_t size);
extern void db_private_free (void *thrd, void *ptr);

extern unsigned int db_create_fixed_heap (int req_size, int recs_per_chunk);
extern void db_destroy_fixed_heap (unsigned int heap_id);
extern void *db_fixed_alloc (unsigned int heap_id, size_t size);
extern void db_fixed_free (unsigned int heap_id, void *ptr);

#if defined(SA_MODE)
typedef struct private_malloc_header_s PRIVATE_MALLOC_HEADER;
struct private_malloc_header_s
{
  int magic;
  int alloc_type;
};

#define PRIVATE_MALLOC_HEADER_MAGIC 0xafdaafda

enum
{
  PRIVATE_ALLOC_TYPE_LEA = 1,
  PRIVATE_ALLOC_TYPE_WS = 2
};

#define PRIVATE_MALLOC_HEADER_ALIGNED_SIZE \
  ((sizeof(PRIVATE_MALLOC_HEADER) + 7) & ~7)

#define private_request_size(s) \
  (PRIVATE_MALLOC_HEADER_ALIGNED_SIZE + (s))

#define private_hl2user_ptr(ptr) \
  (void *)((char *)(ptr) + PRIVATE_MALLOC_HEADER_ALIGNED_SIZE)

#define private_user2hl_ptr(ptr) \
  (PRIVATE_MALLOC_HEADER *)((char *)(ptr) - PRIVATE_MALLOC_HEADER_ALIGNED_SIZE)
#endif /* SA_MODE */

#endif /* _MEMORY_ALLOC_H_ */
