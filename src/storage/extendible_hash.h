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
