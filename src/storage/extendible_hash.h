/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * eh.h: Extendible hashing module (interface)			      
 */

#ifndef _EH_H_
#define _EH_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "common.h"
#include "file_manager.h"
#include "oid.h"

/* The definition of the EHID is in dtsr.h */

#if defined(SERVER_MODE)
/* in xserver.h */
extern EHID *xehash_create (THREAD_ENTRY * thread_p, EHID * ehid,
			    DB_TYPE key_type, int exp_num_entries,
			    OID * class_oid, int attr_id);
extern int xehash_destroy (THREAD_ENTRY * thread_p, EHID * ehid);
#if 0
extern EH_SEARCH xeh_find (EHID * ehid, void *value, OID * oid);
#endif
#endif /* SERVER_MODE */

extern EH_SEARCH ehash_search (THREAD_ENTRY * thread_p, EHID * ehid,
			       void *key, OID * value_ptr);
extern void *ehash_insert (THREAD_ENTRY * thread_p, EHID * ehid, void *key,
			   OID * value_ptr);
extern void *ehash_delete (THREAD_ENTRY * thread_p, EHID * ehid, void *key);

/* TODO: check not use */
#if 0
/* Utility functions */
extern int eh_size (EHID * ehid, int *num_bucket_pages, int *num_dir_pages);
extern int eh_count (EHID * ehid);
extern int eh_depth (EHID * ehid);
extern int eh_capacity (EHID * ehid, int *num_recs,
			int *avg_reclength, int *num_bucket_pages,
			int *num_dir_pages, int *dir_depth,
			int *avg_freespace_per_page,
			int *avg_overhead_per_page);
#endif

extern int ehash_estimate_npages_needed (THREAD_ENTRY * thread_p,
					 int total_nkeys, int avg_key_size);
extern int ehash_map (THREAD_ENTRY * thread_p, EHID * ehid,
		      int (*fun) (THREAD_ENTRY * thread_p, void *, void *,
				  void *args), void *args);

/* For debugging purposes only */
extern void ehash_dump (THREAD_ENTRY * thread_p, EHID * ehid);
extern void ehash_print_bucket (THREAD_ENTRY * thread_p, EHID * ehid,
				int offset);

/* Recovery functions */
int ehash_rv_init_bucket_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_insert_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_insert_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_delete_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_delete_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_increment (THREAD_ENTRY * thread_p, LOG_RCV * recv);
int ehash_rv_connect_bucket_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);

#endif /* _EH_H_ */
