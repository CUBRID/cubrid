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
 * memory_hash.h - memory hash table
 */

#ifndef _MEMORY_HASH_H_
#define _MEMORY_HASH_H_

#ident "$Id$"

#include "dbtype_def.h"
#include "memory_alloc.h"
#include "thread_compat.hpp"

#include <stdio.h>


#define MHT2STR_COLL(id, str, size) \
  (lang_get_collation (id))->mht2str ((lang_get_collation (id)), (str), (size))

/* Hash Table Entry - linked list */
typedef struct hentry HENTRY;
typedef struct hentry *HENTRY_PTR;
struct hentry
{
  HENTRY_PTR act_next;		/* Next active entry on hash table */
  HENTRY_PTR act_prev;		/* Previous active entry on hash table */
  HENTRY_PTR lru_next;		/* least recentry used list next item */
  HENTRY_PTR lru_prev;		/* least recentry used list prev item */
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
  HENTRY_PTR act_head;		/* Head of active double link list entries. Used to perform quick mappings of hash
				 * table. */
  HENTRY_PTR act_tail;		/* Tail of active double link list entries. Used to perform quick mappings of hash
				 * table. */
  HENTRY_PTR lru_head;		/* least recently used head */
  HENTRY_PTR lru_tail;		/* least recently used tail */
  HENTRY_PTR prealloc_entries;	/* Free entries allocated for locality reasons */
  unsigned int size;		/* Better if prime number */
  unsigned int rehash_at;	/* Rehash at this num of entries */
  unsigned int nentries;	/* Actual number of entries */
  unsigned int nprealloc_entries;	/* Number of preallocated entries for future insertions */
  unsigned int ncollisions;	/* Number of collisions in HT */
  HL_HEAPID heap_id;		/* Id of heap allocator */
  bool build_lru_list;		/* true if LRU list must be built */
};

extern unsigned int mht_2str_pseudo_key (const void *key, int key_size);
extern unsigned int mht_1strlowerhash (const void *key, const unsigned int ht_size);
extern unsigned int mht_1strhash (const void *key, const unsigned int ht_size);
extern unsigned int mht_2strhash (const void *key, const unsigned int ht_size);
extern unsigned int mht_3strhash (const void *key, const unsigned int ht_size);
extern unsigned int mht_4strhash (const void *key, const unsigned int ht_size);
extern unsigned int mht_5strhash (const void *key, const unsigned int ht_size);
extern unsigned int mht_numhash (const void *key, const unsigned int ht_size);

extern unsigned int mht_get_hash_number (const int unsigned ht_size, const DB_VALUE * val);
extern unsigned int mht_ptrhash (const void *ptr, const unsigned int ht_size);
extern unsigned int mht_valhash (const void *key, const unsigned int ht_size);
extern int mht_compare_identifiers_equal (const void *key1, const void *key2);
extern int mht_compare_strings_are_equal (const void *key1, const void *key2);
extern int mht_compare_ints_are_equal (const void *key1, const void *key2);
extern int mht_compare_logpageids_are_equal (const void *key1, const void *key2);
extern int mht_compare_ptrs_are_equal (const void *key1, const void *key2);
extern int mht_compare_dbvalues_are_equal (const void *key1, const void *key2);

extern MHT_TABLE *mht_create (const char *name, int est_size,
			      unsigned int (*hash_func) (const void *key, unsigned int ht_size),
			      int (*cmp_func) (const void *key1, const void *key2));
extern void mht_destroy (MHT_TABLE * ht);
extern int mht_clear (MHT_TABLE * ht, int (*rem_func) (const void *key, void *data, void *args), void *func_args);
extern void *mht_get (MHT_TABLE * ht, const void *key);
extern void *mht_get2 (const MHT_TABLE * ht, const void *key, void **last);
extern const void *mht_put (MHT_TABLE * ht, const void *key, void *data);
extern const void *mht_put_data (MHT_TABLE * ht, const void *key, void *data);
extern const void *mht_put_new (MHT_TABLE * ht, const void *key, void *data);
extern const void *mht_put_if_not_exists (MHT_TABLE * ht, const void *key, void *data);
#if defined (ENABLE_UNUSED_FUNCTION)
extern const void *mht_put2 (MHT_TABLE * ht, const void *key, void *data);
extern const void *mht_put2_data (MHT_TABLE * ht, const void *key, void *data);
#endif
extern const void *mht_put2_new (MHT_TABLE * ht, const void *key, void *data);
extern int mht_rem (MHT_TABLE * ht, const void *key, int (*rem_func) (const void *key, void *data, void *args),
		    void *func_args);
extern int mht_rem2 (MHT_TABLE * ht, const void *key, const void *data,
		     int (*rem_func) (const void *key, void *data, void *args), void *func_args);
extern int mht_map (const MHT_TABLE * ht, int (*map_func) (const void *key, void *data, void *args), void *func_args);
extern int mht_map_no_key (THREAD_ENTRY * thread_p, const MHT_TABLE * ht,
			   int (*map_func) (THREAD_ENTRY * thread_p, void *data, void *args), void *func_args);
extern int mht_adjust_lru_list (MHT_TABLE * ht, HENTRY_PTR hentry);
extern unsigned int mht_count (const MHT_TABLE * ht);
extern int mht_dump (THREAD_ENTRY * thread_p, FILE * out_fp, const MHT_TABLE * ht, const int print_id_opt,
		     int (*print_func) (THREAD_ENTRY * thread_p, FILE * fp, const void *key, void *data, void *args),
		     void *func_args);

/*
 * Hash table for HASH LIST SCAN
 * In order to minimize the size of the hash entry, a hash table for HASH LIST SCAN is created separately.
 * It has the following features.
 * 1. lru, act is not used. (remove variable related to lru, act)
 * 2. key comparison is performed in executor. (remove key of hash entry)
 * 3. put data orderly. (add tail pointer for it)
 * 4. Since hash size is fixed, rehashing the hash table is not necessary.
 */

/* Hash Table Entry for HASH LIST SCAN - linked list, keyless hash entry */
typedef struct hentry_hls HENTRY_HLS;
typedef struct hentry_hls *HENTRY_HLS_PTR;
struct hentry_hls
{
  HENTRY_HLS_PTR tail;		/* tail node on hash table entry */
  HENTRY_HLS_PTR next;		/* Next hash table entry for colisions */
  void *data;			/* Data associated with key entry */
  unsigned int key;		/* hash key */
};

/* Memory Hash Table for HASH LIST SCAN*/
typedef struct mht_hls_table MHT_HLS_TABLE;
struct mht_hls_table
{
  unsigned int (*hash_func) (const void *key, unsigned int htsize);
  int (*cmp_func) (const void *key1, const void *key2);
  const char *name;
  HENTRY_HLS_PTR *table;	/* The hash table (entries) */
  HENTRY_HLS_PTR prealloc_entries;	/* Free entries allocated for locality reasons */
  unsigned int size;		/* Better if prime number */
  unsigned int nentries;	/* Actual number of entries */
  unsigned int nprealloc_entries;	/* Number of preallocated entries for future insertions */
  unsigned int ncollisions;	/* Number of collisions in HT */
  HL_HEAPID heap_id;		/* Id of heap allocator */
  bool build_lru_list;		/* true if LRU list must be built */
};

extern const void *mht_put_hls (MHT_HLS_TABLE * ht, const void *key, void *data);
extern void *mht_get_hls (const MHT_HLS_TABLE * ht, const void *key, void **last);
extern void *mht_get_next_hls (const MHT_HLS_TABLE * ht, const void *key, void **last);
extern MHT_HLS_TABLE *mht_create_hls (const char *name, int est_size,
				      unsigned int (*hash_func) (const void *key, unsigned int ht_size),
				      int (*cmp_func) (const void *key1, const void *key2));
extern int mht_clear_hls (MHT_HLS_TABLE * ht, int (*rem_func) (const void *key, void *data, void *args),
			  void *func_args);
extern void mht_destroy_hls (MHT_HLS_TABLE * ht);
extern int mht_dump_hls (THREAD_ENTRY * thread_p, FILE * out_fp, const MHT_HLS_TABLE * ht, const int print_id_opt,
			 int (*print_func) (THREAD_ENTRY * thread_p, FILE * fp, const void *data, void *args),
			 void *func_args);
extern unsigned int mht_calculate_htsize (unsigned int ht_size);
/* for HASH LIST SCAN (end) */

#endif /* _MEMORY_HASH_H_ */
