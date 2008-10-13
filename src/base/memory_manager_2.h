/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * memory_alloc.h - Memory allocation module
 * TODO: rename this file to memory_alloc.h
 */

#ifndef _MEMORY_ALLOC_H_
#define _MEMORY_ALLOC_H_

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>

#include "dbtype.h"
#include "memory_manager_4.h"

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

#endif /* _MEMORY_ALLOC_H_ */
