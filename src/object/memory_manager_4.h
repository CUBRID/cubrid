/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * mmgr.h - DB_MMGR memory mananger
 * TODO: rename ??? merge to ???
 */
#ifndef _MMGR_H_
#define _MMGR_H_

#ident "$Id$"

#include "config.h"

#include <stddef.h>
#include <string.h>
#if !defined(WINDOWS)
#include <stdint.h>
#endif

#include "thread_impl.h"


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
extern void * db_fixed_alloc (unsigned int heap_id, size_t size);
extern void db_fixed_free (unsigned int heap_id, void *ptr);

#endif /* _MMGR_H_ */
