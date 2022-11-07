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

#include "config.h"
#include "porting.h"

#include "dbtype_def.h"
#include "thread_compat.hpp"
#include "memory_alignment.hpp"

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
#define db_ws_free_and_init(ptr) \
        do { \
          db_ws_free((ptr)); \
          (ptr) = NULL; \
        } while (0)

#if !defined (SERVER_MODE)

extern HL_HEAPID private_heap_id;
extern HL_HEAPID ws_heap_id;

#define os_malloc(size) (malloc (size))
#define os_free(ptr) (free (ptr))
#define os_realloc(ptr, size) (realloc ((ptr), (size)))

/* allocation APIs for workspace */
#define db_ws_alloc(size) \
        db_private_alloc(NULL, size)
#define db_ws_free(ptr) \
        db_private_free(NULL, ptr)
#define db_ws_realloc(ptr, size) \
        db_private_realloc(NULL, ptr, size)

#define db_create_workspace_heap() (ws_heap_id = db_create_private_heap())
#define db_destroy_workspace_heap() db_destroy_private_heap(NULL, ws_heap_id)

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
extern HL_HEAPID db_private_get_heapid_from_thread (REFPTR (THREAD_ENTRY, thread_p));
#endif // SERVER_MODE

extern HL_HEAPID db_create_fixed_heap (int req_size, int recs_per_chunk);
extern void db_destroy_fixed_heap (HL_HEAPID heap_id);
extern void *db_fixed_alloc (HL_HEAPID heap_id, size_t size);
extern void db_fixed_free (HL_HEAPID heap_id, void *ptr);

#endif /* _MEMORY_ALLOC_H_ */
