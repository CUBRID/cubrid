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
 * extendible_hash.h: Extendible hashing module (interface)
 */

#ifndef _EXTENDIBLE_HASH_H_
#define _EXTENDIBLE_HASH_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "storage_common.h"
#include "file_manager.h"
#include "oid.h"

extern EH_SEARCH ehash_search (THREAD_ENTRY * thread_p, EHID * ehid, void *key, OID * value_ptr);
extern void *ehash_insert (THREAD_ENTRY * thread_p, EHID * ehid, void *key, OID * value_ptr);
extern void *ehash_delete (THREAD_ENTRY * thread_p, EHID * ehid, void *key);

/* TODO: check not use */
#if 0
/* Utility functions */
extern int eh_size (EHID * ehid, int *num_bucket_pages, int *num_dir_pages);
extern int eh_count (EHID * ehid);
extern int eh_depth (EHID * ehid);
extern int eh_capacity (EHID * ehid, int *num_recs, int *avg_reclength, int *num_bucket_pages, int *num_dir_pages,
			int *dir_depth, int *avg_freespace_per_page, int *avg_overhead_per_page);
#endif

extern int ehash_map (THREAD_ENTRY * thread_p, EHID * ehid,
		      int (*fun) (THREAD_ENTRY * thread_p, void *, void *, void *args), void *args);

/* For debugging purposes only */
extern void ehash_dump (THREAD_ENTRY * thread_p, EHID * ehid);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void ehash_print_bucket (THREAD_ENTRY * thread_p, EHID * ehid, int offset);
#endif

/* Recovery functions */
int ehash_rv_init_bucket_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_init_dir_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_insert_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_insert_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_delete_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_delete_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_increment (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_connect_bucket_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_init_dir_new_page_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);

#endif /* _EXTENDIBLE_HASH_H_ */
