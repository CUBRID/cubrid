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
 * memory_hash.h - memory hash table
 */

#ifndef _MEMORY_HASH_H_
#define _MEMORY_HASH_H_

#ident "$Id$"

#include <stdio.h>

#include "dbtype.h"
#include "memory_alloc.h"
#include "thread.h"

/* Hash Table Entry - linked list */
typedef struct hentry HENTRY;
typedef struct hentry *HENTRY_PTR;
struct hentry
{
  HENTRY_PTR act_next;		/* Next active entry on hash table */
  HENTRY_PTR act_prev;		/* Previous active entry on hash table */
  HENTRY_PTR next;		/* Next hash table entry for colisions */
  const void *key;		/* Key associated with entry */
  void *data;			/* Data associated with key entry */
};

/* Memory Hash Table */
typedef struct mht_table MHT_TABLE;
struct mht_table
{
  unsigned int (*hash_func) (const void *key, unsigned int htsize);
  int (*cmp_func) (const void *key1, const void *key2);
  const char *name;
  HENTRY_PTR *table;		/* The hash table (entries) */
  HENTRY_PTR act_head;		/* Head of active double link list
				 * entries. Used to perform quick
				 * mappings of hash table.
				 */
  HENTRY_PTR act_tail;		/* Tail of active double link list
				 * entries. Used to perform quick
				 * mappings of hash table.
				 */
  HENTRY_PTR prealloc_entries;	/* Free entries allocated for
				 * locality reasons
				 */
  unsigned int size;		/* Better if prime number */
  unsigned int rehash_at;	/* Rehash at this num of entries */
  unsigned int nentries;	/* Actual number of entries */
  unsigned int nprealloc_entries;	/* Number of preallocated entries
					 * for future insertions
					 */
  unsigned int ncollisions;	/* Number of collisions in HT */
  HL_HEAPID heap_id;		/* Id of heap allocator */
};

extern unsigned int mht_1strlowerhash (const void *key,
				       const unsigned int ht_size);
extern unsigned int mht_1strhash (const void *key,
				  const unsigned int ht_size);
extern unsigned int mht_2strhash (const void *key,
				  const unsigned int ht_size);
extern unsigned int mht_3strhash (const void *key,
				  const unsigned int ht_size);
extern unsigned int mht_4strhash (const void *key,
				  const unsigned int ht_size);
extern unsigned int mht_5strhash (const void *key,
				  const unsigned int ht_size);
extern unsigned int mht_numhash (const void *key, const unsigned int ht_size);
extern unsigned int mht_logpageidhash (const void *key, unsigned int htsize);

extern unsigned int mht_get_hash_number (const int ht_size,
					 const DB_VALUE * val);
extern unsigned int mht_ptrhash (const void *ptr, const unsigned int ht_size);
extern unsigned int mht_valhash (const void *key, const unsigned int ht_size);
extern int mht_compare_strings_are_case_insensitively_equal (const void *key1,
							     const void
							     *key2);
extern int mht_compare_strings_are_equal (const void *key1, const void *key2);
extern int mht_compare_ints_are_equal (const void *key1, const void *key2);
extern int mht_compare_logpageids_are_equal (const void *key1,
					     const void *key2);
extern int mht_compare_ptrs_are_equal (const void *key1, const void *key2);
extern int mht_compare_dbvalues_are_equal (const void *key1,
					   const void *key2);

extern MHT_TABLE *mht_create (const char *name, int est_size,
			      unsigned int (*hash_func) (const void *key,
							 unsigned int
							 ht_size),
			      int (*cmp_func) (const void *key1,
					       const void *key2));
extern void mht_destroy (MHT_TABLE * ht);
extern int mht_clear (MHT_TABLE * ht);
extern void *mht_get (const MHT_TABLE * ht, const void *key);
extern void *mht_get2 (const MHT_TABLE * ht, const void *key, void **last);
extern const void *mht_put (MHT_TABLE * ht, const void *key, void *data);
extern const void *mht_put_data (MHT_TABLE * ht, const void *key, void *data);
extern const void *mht_put_new (MHT_TABLE * ht, const void *key, void *data);
#if defined (ENABLE_UNUSED_FUNCTION)
extern const void *mht_put2 (MHT_TABLE * ht, const void *key, void *data);
extern const void *mht_put2_data (MHT_TABLE * ht, const void *key,
				  void *data);
#endif
extern const void *mht_put2_new (MHT_TABLE * ht, const void *key, void *data);
extern int mht_rem (MHT_TABLE * ht, const void *key,
		    int (*rem_func) (const void *key, void *data, void *args),
		    void *func_args);
extern int mht_rem2 (MHT_TABLE * ht, const void *key, const void *data,
		     int (*rem_func) (const void *key, void *data,
				      void *args), void *func_args);
extern int mht_map (const MHT_TABLE * ht,
		    int (*map_func) (const void *key, void *data, void *args),
		    void *func_args);
extern int mht_map_no_key (THREAD_ENTRY * thread_p, const MHT_TABLE * ht,
			   int (*map_func) (THREAD_ENTRY * thread_p,
					    void *data, void *args),
			   void *func_args);
extern unsigned int mht_count (const MHT_TABLE * ht);
extern int mht_dump (FILE * out_fp, const MHT_TABLE * ht,
		     const int print_id_opt,
		     int (*print_func) (FILE * fp, const void *key,
					void *data, void *args),
		     void *func_args);

#endif /* _MEMORY_HASH_H_ */
