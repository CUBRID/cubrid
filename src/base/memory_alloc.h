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
 * memory_alloc.h - Memory allocation module
 */

#ifndef _MEMORY_ALLOC_H_
#define _MEMORY_ALLOC_H_

#ident "$Id$"

#include "config.h"

#include "dbtype_def.h"
#include "thread_compat.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <stddef.h>
#include <string.h>
#if !defined(WINDOWS)
#include <stdint.h>
#endif

#if defined (__cplusplus)
#include <memory>
#include <functional>
#endif
/* ***IMPORTANT!!***
 * memory_wrapper.hpp has a restriction that it must locate at the end of including section
 * because the user-defined new for overloaded format can make build error in glibc
 * when glibc header use "placement new" or another overloaded format of new.
 * So memory_wrapper.hpp cannot be included in header file, but memory_cwrapper.h can be included.
 * You can include memory_cwrapper.h in a header file when the header file use allocation function. */
#include "memory_cwrapper.h"

/* Ceiling of positive division */
#define CEIL_PTVDIV(dividend, divisor) \
        (((dividend) == 0) ? 0 : (((dividend) - 1) / (divisor)) + 1)

/* Make sure that sizeof returns and integer, so I can use in the operations */
#define DB_SIZEOF(val)          (sizeof(val))

/*
 * Macros related to alignments
 */
#define CHAR_ALIGNMENT          sizeof(char)
#define SHORT_ALIGNMENT         sizeof(short)
#define INT_ALIGNMENT           sizeof(int)
#define LONG_ALIGNMENT          sizeof(long)
#define FLOAT_ALIGNMENT         sizeof(float)
#define DOUBLE_ALIGNMENT        sizeof(double)
#if __WORDSIZE == 32
#define PTR_ALIGNMENT		4
#else
#define PTR_ALIGNMENT		8
#endif
#define MAX_ALIGNMENT           DOUBLE_ALIGNMENT

#if defined(NDEBUG)
#define PTR_ALIGN(addr, boundary) \
        ((char *)((((UINTPTR)(addr) + ((UINTPTR)((boundary)-1)))) \
                  & ~((UINTPTR)((boundary)-1))))
#else
#define PTR_ALIGN(addr, boundary) \
        (memset((void*)(addr), 0,\
	   DB_WASTED_ALIGN((UINTPTR)(addr), (UINTPTR)(boundary))),\
        (char *)((((UINTPTR)(addr) + ((UINTPTR)((boundary)-1)))) \
                  & ~((UINTPTR)((boundary)-1))))
#endif

#define DB_ALIGN(offset, align) \
        (((offset) + (align) - 1) & ~((align) - 1))

#define DB_ALIGN_BELOW(offset, align) \
        ((offset) & ~((align) - 1))

#define DB_WASTED_ALIGN(offset, align) \
        (DB_ALIGN((offset), (align)) - (offset))

#define DB_ATT_ALIGN(offset) \
        (((offset) + (INT_ALIGNMENT) - 1) & ~((INT_ALIGNMENT) - 1))

/*
 * Macros related to memory allocation
 */

#define MEM_REGION_INIT_MARK       '\0'	/* Set this to allocated areas */
#define MEM_REGION_SCRAMBLE_MARK         '\01'	/* Set this to allocated areas */
#define MEM_REGION_GUARD_MARK            '\02'	/* Set this as a memory guard to detect over/under runs */

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
            free ((void*) (ptr)); \
            (ptr) = NULL; \
          } \
        } while (0)

#define os_free_and_init(ptr) \
        do { \
          if ((ptr)) { \
            os_free((ptr)); \
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
          free ((void*) (ptr)); \
          (ptr) = NULL; \
	} while (0)

#define os_free_and_init(ptr) \
        do { \
          os_free((ptr)); \
          (ptr) = NULL; \
        } while (0)
#endif /* NDEBUG */

extern int ansisql_strcmp (const char *s, const char *t);
extern int ansisql_strcasecmp (const char *s, const char *t);

#if !defined (SERVER_MODE)

extern HL_HEAPID private_heap_id;

#define os_malloc(size) (malloc (size))
#define os_free(ptr) (free (ptr))
#define os_realloc(ptr, size) (realloc ((ptr), (size)))

#else /* SERVER_MODE */

#if !defined(NDEBUG)
#define os_malloc(size) \
        os_malloc_debug(size, true, __FILE__, __LINE__)
extern void *os_malloc_debug (size_t size, bool rc_track, const char *caller_file, int caller_line);
#define os_calloc(n, size) \
        os_calloc_debug(n, size, true, __FILE__, __LINE__)
extern void *os_calloc_debug (size_t n, size_t size, bool rc_track, const char *caller_file, int caller_line);
#define os_free(ptr) \
        os_free_debug(ptr, true, __FILE__, __LINE__)
extern void os_free_debug (void *ptr, bool rc_track, const char *caller_file, int caller_line);
#define os_realloc(ptr, size) (realloc ((ptr), (size)))
#else /* NDEBUG */
#define os_malloc(size) \
        os_malloc_release(size, false)
extern void *os_malloc_release (size_t size, bool rc_track);
#define os_calloc(n, size) \
        os_calloc_release(n, size, false)
extern void *os_calloc_release (size_t n, size_t size, bool rc_track);
#define os_free(ptr) \
        os_free_release(ptr, false)
extern void os_free_release (void *ptr, bool rc_track);
#define os_realloc(ptr, size) (realloc ((ptr), (size)))
#endif /* NDEBUG */

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
extern int db_align_to (int n, int alignment);

extern HL_HEAPID db_create_ostk_heap (int chunk_size);
extern void db_destroy_ostk_heap (HL_HEAPID heap_id);

extern void *db_ostk_alloc (HL_HEAPID heap_id, size_t size);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void db_ostk_free (HL_HEAPID heap_id, void *ptr);
#endif

extern HL_HEAPID db_create_private_heap (void);
extern void db_clear_private_heap (THREAD_ENTRY * thread_p, HL_HEAPID heap_id);
extern HL_HEAPID db_change_private_heap (THREAD_ENTRY * thread_p, HL_HEAPID heap_id);
extern HL_HEAPID db_replace_private_heap (THREAD_ENTRY * thread_p);
extern void db_destroy_private_heap (THREAD_ENTRY * thread_p, HL_HEAPID heap_id);

#if !defined(NDEBUG)
#define db_private_alloc(thrd, size) \
        db_private_alloc_debug(thrd, size, true, __FILE__, __LINE__)
#define db_private_free(thrd, ptr) \
        db_private_free_debug(thrd, ptr, true, __FILE__, __LINE__)
#define db_private_realloc(thrd, ptr, size) \
        db_private_realloc_debug(thrd, ptr, size, true, __FILE__, __LINE__)

#ifdef __cplusplus
extern "C"
{
#endif
  extern void *db_private_alloc_debug (THREAD_ENTRY * thrd, size_t size, bool rc_track, const char *caller_file,
				       int caller_line);
  extern void db_private_free_debug (THREAD_ENTRY * thrd, void *ptr, bool rc_track, const char *caller_file,
				     int caller_line);
  extern void *db_private_realloc_debug (THREAD_ENTRY * thrd, void *ptr, size_t size, bool rc_track,
					 const char *caller_file, int caller_line);
#ifdef __cplusplus
}
#endif

#else /* NDEBUG */
#define db_private_alloc(thrd, size) \
        db_private_alloc_release(thrd, size, false)
#define db_private_free(thrd, ptr) \
        db_private_free_release(thrd, ptr, false)
#define db_private_realloc(thrd, ptr, size) \
        db_private_realloc_release(thrd, ptr, size, false)


#ifdef __cplusplus
extern "C"
{
#endif
  extern void *db_private_alloc_release (THREAD_ENTRY * thrd, size_t size, bool rc_track);
  extern void db_private_free_release (THREAD_ENTRY * thrd, void *ptr, bool rc_track);
  extern void *db_private_realloc_release (THREAD_ENTRY * thrd, void *ptr, size_t size, bool rc_track);
#ifdef __cplusplus
}
#endif
#endif				/* NDEBUG */

#ifdef __cplusplus
extern "C"
{
#endif
  extern char *db_private_strdup (THREAD_ENTRY * thrd, const char *s);
#ifdef __cplusplus
}
#endif

/* for external package */
extern void *db_private_alloc_external (THREAD_ENTRY * thrd, size_t size);
extern void db_private_free_external (THREAD_ENTRY * thrd, void *ptr);
extern void *db_private_realloc_external (THREAD_ENTRY * thrd, void *ptr, size_t size);

#if defined (SERVER_MODE)
extern HL_HEAPID db_private_set_heapid_to_thread (THREAD_ENTRY * thread_p, HL_HEAPID heap_id);
#endif // SERVER_MODE

extern HL_HEAPID db_create_fixed_heap (int req_size, int recs_per_chunk);
extern void db_destroy_fixed_heap (HL_HEAPID heap_id);
extern void *db_fixed_alloc (HL_HEAPID heap_id, size_t size);
extern void db_fixed_free (HL_HEAPID heap_id, void *ptr);

#if defined(SA_MODE)
typedef struct private_malloc_header_s PRIVATE_MALLOC_HEADER;
struct private_malloc_header_s
{
  unsigned int magic;
  int alloc_type;
};

#define PRIVATE_MALLOC_HEADER_MAGIC 0xafdaafdaU

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
