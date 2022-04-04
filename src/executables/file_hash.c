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
 * file_hash.c: file hashing implementation
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stddef.h>
#if defined (WINDOWS)
#include <io.h>
#endif

#include "utility.h"
#include "error_manager.h"
#include "file_hash.h"
#include "filesys_temp.hpp"
#include "memory_alloc.h"
#include "object_representation.h"

#include "message_catalog.h"

#if defined (WINDOWS)
#include "porting.h"
#endif

#define ISPOWER2(x) (((x) & (x-1)) == 0)

static OID null_oid;
static int null_int;


/*
 * A Table of Prime Numbers
 *
 * Some prime numbers which were picked randomly. This table contains more
 * smaller prime numbers. For example, between 1000 and 2000, we include
 * only prime numbers that are farther than 50 units. And above 2000, we
 * include prime numbers that are farther in 100 units.
 *
 * NOTE: if x is a prime number, the n is prime if
 *       X**(n-1) mod n == 1
 */

#define NPRIMES 100

static int fh_Primes[NPRIMES] = {
  11, 23, 37, 53, 67, 79, 97, 109, 127, 149,
  167, 191, 211, 227, 251, 269, 293, 311, 331, 349,
  367, 389, 409, 431, 449, 467, 487, 509, 541, 563,
  587, 607, 631, 653, 673, 521, 541, 557, 569, 587,
  599, 613, 641, 673, 701, 727, 751, 787, 821, 853,
  881, 907, 941, 977, 1039, 1087, 1129, 1171, 1212, 1259,
  1301, 1361, 1409, 1471, 1523, 1579, 1637, 1693, 1747, 1777,
  1823, 1867, 1913, 1973, 2017, 2129, 2237, 2339, 2441, 2543,
  2647, 2749, 2851, 2953, 3061, 3163, 3271, 3373, 3491, 3593,
  3697, 3803, 3907, 4013, 4177, 4337, 4493, 4649, 4801, 4957
};


static int fh_calculate_htsize (int htsize);
static FH_PAGE_HDR *fh_fetch_page (FH_TABLE * ht, int page);
static FH_PAGE_HDR *fh_read_page (FH_TABLE * ht, int page);
static FH_PAGE_HDR *fh_write_page (FH_TABLE * ht, FH_PAGE_HDR * pg_hdr);
static void fh_bitset (FH_TABLE * ht, int page);
static int fh_bittest (FH_TABLE * ht, int page);

/*
 * fh_calculate_htsize - find a good size for a hash table
 *    return: htsize..
 *    htsize(in): Desired hash table size
 * Note:
 *    A prime number is the best size for a hash table, unfortunately prime
 *    numbers are difficult to find. If the given htsize, falls among the
 *    prime numbers known by this module, a close prime number to
 *    the given htsize value is returned, otherwise, a power of two
 *    is returned.
 */
static int
fh_calculate_htsize (int htsize)
{
  int left, right, middle;	/* Indices for binary search */

  /* Can we get a prime number ? */
  if (htsize > fh_Primes[NPRIMES - 1])
    {
      /* Use a prower of two */
      if (!ISPOWER2 (htsize))
	{
	  /* Turn off some bits but the left most one */
	  while (!ISPOWER2 (htsize))
	    {
	      htsize = htsize & (htsize - 1);
	    }
	  htsize = htsize << 1;
	}
    }
  else
    {
      /* We can assign a primary number, ... Binary search */
      for (left = 0, right = NPRIMES - 1; left <= right;)
	{
	  /* get the middle record */
	  middle = CEIL_PTVDIV ((left + right), 2);
	  if (htsize == fh_Primes[middle])
	    {
	      break;
	    }
	  else if (htsize > fh_Primes[middle])
	    {
	      left = middle + 1;
	    }
	  else
	    {
	      right = middle - 1;
	    }
	}
      htsize = fh_Primes[middle];
    }
  return htsize;
}


/*
 * fh_create - create a new file hash table
 *    return: hash table handle
 *    name(in): Name of hash table
 *    est_size(in): Estimated number of entries
 *    page_size(in): Number of bytes per cached hash entry page
 *    cached_pages(in): Number of entry pages to cache
 *    hash_filename(in): pathname of the hash file
 *    key_type(in): Type of key
 *    data_size(in): Number of bytes of data per entry
 *    hfun(in): Hash function
 *    cmpfun(): Key comparison function
 * Note:
 *    The estimated number of entries for the hash table is adjusted in order
 *    to obtain certain favorable circumstances when the table is created.
 *    hfun must return an integer between 0 and table_size - 1. cmpfun must
 *    return true if key1 == key2, otherwise, false.
 */
FH_TABLE *
fh_create (const char *name, int est_size, int page_size, int cached_pages, const char *hash_filename,
	   FH_KEY_TYPE key_type, int data_size, HASH_FUNC hfun, CMP_FUNC cmpfun)
{
  FH_TABLE *ht;			/* Hash table information */
  FH_PAGE_HDR *pg_hdr;		/* Entries of hash table */
  int size;
  int n;
  int entry_size;

  OR_PUT_NULL_OID (&null_oid);
  null_int = -1;

  /* Validate arguments */
  switch (key_type)
    {
    case FH_OID_KEY:
      entry_size = data_size + offsetof (FH_INFO, fh_oidk_data) + offsetof (FH_ENTRY, info);
      break;
    case FH_INT_KEY:
      entry_size = data_size + offsetof (FH_INFO, fh_intk_data) + offsetof (FH_ENTRY, info);
      break;
    default:
      /*
       * this should be calling er_set
       * fprintf(stderr, "Invalid key type\n");
       */
      return NULL;
    }

  if (page_size < entry_size)
    {
      /*
       * should be calling er_set
       * fprintf(stderr, "Invalid page_size\n");
       */
      return NULL;
    }

  /* Allocate the header information for hash table */
  if ((ht = (FH_TABLE *) malloc (DB_SIZEOF (FH_TABLE))) == NULL)
    {
      return NULL;
    }
  memset (ht, 0, DB_SIZEOF (FH_TABLE));

  /* Get a good number of entries for hash table */
  if (est_size <= 0)
    {
      est_size = 2;
    }
  est_size = fh_calculate_htsize (est_size);

  /* Open the hash file */
  if (!hash_filename || hash_filename[0] == '\0')
    {
      auto[filename, filedes] = filesys::open_temp_filedes ("fhash_");
      close (filedes);
      ht->hash_filename = strdup (filename.c_str ());
    }
  else
    {
      ht->hash_filename = strdup (hash_filename);
    }
  if (ht->hash_filename == NULL)
    {
      fh_destroy (ht);
      return NULL;
    }
#if defined(SOLARIS) || defined(AIX) || (defined(I386) && defined(LINUX)) || (defined(HPUX) && (_LFS64_LARGEFILE == 1))
  ht->fd = open64 (ht->hash_filename, O_RDWR | O_CREAT | O_TRUNC, 0600);
#else
  ht->fd = open (ht->hash_filename, O_RDWR | O_CREAT | O_TRUNC, 0600);
#endif
  if (ht->fd < 0)
    {
      perror (msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_HASH_FILENAME));
      fh_destroy (ht);
      return NULL;
    }

  /* Allocate the page bitmap */
  ht->page_size = page_size;
  ht->data_size = data_size;
  ht->entry_size = entry_size;
  ht->entries_per_page = page_size / entry_size;
  ht->overflow = est_size + 1;
  ht->bitmap_size = (ht->overflow / ht->entries_per_page + 7) / 8;
  ht->bitmap = (char *) malloc (ht->bitmap_size);
  if (ht->bitmap == NULL)
    {
      fh_destroy (ht);
      return NULL;
    }
  memset (ht->bitmap, 0, ht->bitmap_size);

  /* Allocate the cached page headers */
  size = cached_pages * DB_SIZEOF (FH_PAGE_HDR);
  if ((pg_hdr = (FH_PAGE_HDR *) malloc (size)) == NULL)
    {
      fh_destroy (ht);
      return NULL;
    }
  ht->pg_hdr = ht->pg_hdr_alloc = pg_hdr;
  memset (ht->pg_hdr, 0, size);

  /*
   * Allocate the cached pages and fill in the page headers and
   * initialize each of the hash entries
   */
  for (n = 0; n < cached_pages; n++, pg_hdr++)
    {
      pg_hdr->next = pg_hdr + 1;
      pg_hdr->prev = pg_hdr - 1;
      if ((pg_hdr->fh_entries = (FH_ENTRY *) malloc (page_size)) == NULL)
	{
	  fh_destroy (ht);
	  return NULL;
	}
      memset ((char *) pg_hdr->fh_entries, 0, page_size);
      pg_hdr->page = INVALID_FILE_POS;
    }
  pg_hdr--;
  pg_hdr->next = NULL;
  ht->pg_hdr->prev = NULL;

  ht->pg_hdr_last = pg_hdr;
  ht->pg_hdr_free = ht->pg_hdr;
  ht->hfun = hfun;
  ht->cmpfun = cmpfun;
  ht->name = name;
  ht->size = est_size;
  ht->cached_pages = cached_pages;
  ht->nentries = 0;
  ht->ncollisions = 0;
  ht->key_type = key_type;

  return ht;
}


/*
 * fh_destroy - destroy the given hash table
 *    return: void
 *    ht(out): Hash table (set as a side effect)
 */
void
fh_destroy (FH_TABLE * ht)
{
  FH_PAGE_HDR *pg_hdr;		/* Entries of hash table */

  for (pg_hdr = ht->pg_hdr; pg_hdr; pg_hdr = pg_hdr->next)
    {
      if (pg_hdr->fh_entries)
	{
	  free_and_init (pg_hdr->fh_entries);
	}
    }
  if (ht->pg_hdr_alloc)
    {
      free_and_init (ht->pg_hdr_alloc);
    }

  if (ht->fd > 0)
    {
      close (ht->fd);
      unlink (ht->hash_filename);
    }
  if (ht->hash_filename)
    {
      free_and_init (ht->hash_filename);
    }
  if (ht->bitmap)
    {
      free_and_init (ht->bitmap);
    }
  free_and_init (ht);
  return;
}


/*
 * fh_get - Find the entry in hash table whose key is the value of the
 * given key and return the data associated with the key.
 *    return: NO_ERROR or error code
 *    ht(in): Hash table
 *    key(in): Hashing key
 *    data(out): pointer to data or NULL if not found
 */
int
fh_get (FH_TABLE * ht, FH_KEY key, FH_DATA * data)
{
  int hash;
  FH_FILE_POS pos;
  int page_no;
  int entry_no;
  FH_ENTRY *entry;
  FH_PAGE_HDR *pg_hdr;
  char *ptr;

  /*
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hfun) (key, ht->size);
  if (hash < 0)
    {
      hash = -hash;
    }
  if (hash >= ht->size)
    {
      hash = hash % ht->size;
    }

  /* Determine the page & entry in the hash file */
  pos = hash;

  while (pos != INVALID_FILE_POS)
    {
      page_no = pos / ht->entries_per_page;
      entry_no = pos % ht->entries_per_page;

      /* fetch the page */
      if ((pg_hdr = fh_fetch_page (ht, page_no)) == NULL)
	{
	  *data = NULL;
	  return ER_GENERIC_ERROR;
	}

      /* Determine if the entry has the key */
      ptr = (char *) pg_hdr->fh_entries;
      ptr += entry_no * ht->entry_size;
      entry = (FH_ENTRY *) ptr;
      switch (ht->key_type)
	{
	case FH_OID_KEY:
	  /* If the entry is null, the key doesn't exist */
	  if ((*ht->cmpfun) (&entry->info.fh_oidk_key, &null_oid))
	    {
	      *data = NULL;
	      return ER_GENERIC_ERROR;
	    }
	  if ((*ht->cmpfun) (&entry->info.fh_oidk_key, key))
	    {
	      *data = &entry->info.fh_oidk_data;
	      return NO_ERROR;
	    }
	  break;
	case FH_INT_KEY:
	  /* If the entry is null, the key doesn't exist */
	  if ((*ht->cmpfun) (&entry->info.fh_intk_key, &null_int))
	    {
	      *data = NULL;
	      return ER_GENERIC_ERROR;
	    }
	  if ((*ht->cmpfun) (&entry->info.fh_intk_key, key))
	    {
	      *data = &entry->info.fh_intk_data;
	      return NO_ERROR;
	    }
	  break;
	default:
	  *data = NULL;
	  return ER_GENERIC_ERROR;
	}
      pos = entry->next;
    }
  *data = NULL;
  return ER_GENERIC_ERROR;
}


/*
 * fh_put - insert an entry into a hash table
 *    return: NO_ERROR or ER_GENERIC_ERROR
 *    ht(in/out): Hash table
 *    key(in): Hashing key
 *    data(in): Data associated with hashing key
 * Note:
 *    If the key already exists, the new entry replaces the old one.
 *
 *    The key and data are copied.
 *    The user must be aware, that changes to the key and value are
 *    not reflected in the hash table.
 */
int
fh_put (FH_TABLE * ht, FH_KEY key, FH_DATA data)
{
  int hash;
  FH_FILE_POS pos;
  int page_no;
  int entry_no;
  FH_ENTRY *entry = NULL;
  FH_PAGE_HDR *pg_hdr;
  char *ptr;

  /*
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hfun) (key, ht->size);
  if (hash < 0)
    {
      hash = -hash;
    }
  if (hash >= ht->size)
    {
      hash = hash % ht->size;
    }

  /* Determine the page & entry in the hash file */
  pos = hash;

  do
    {
      page_no = pos / ht->entries_per_page;
      entry_no = pos % ht->entries_per_page;

      /* fetch the page */
      if ((pg_hdr = fh_fetch_page (ht, page_no)) == NULL)
	{
	  return ER_GENERIC_ERROR;
	}

      /* Determine if the entry has the key */
      ptr = (char *) pg_hdr->fh_entries;
      ptr += entry_no * ht->entry_size;
      entry = (FH_ENTRY *) ptr;
      switch (ht->key_type)
	{
	case FH_OID_KEY:
	  /* If the entry is null, the key doesn't exist */
	  if ((*ht->cmpfun) (&entry->info.fh_oidk_key, &null_oid))
	    {
	      goto use_entry;
	    }
	  if ((*ht->cmpfun) (&entry->info.fh_oidk_key, key))
	    {
	      memcpy (&entry->info.fh_oidk_data, (char *) data, ht->data_size);
	      return NO_ERROR;
	    }
	  break;
	case FH_INT_KEY:
	  if ((*ht->cmpfun) (&entry->info.fh_intk_key, &null_int))
	    {
	      goto use_entry;
	    }
	  if ((*ht->cmpfun) (&entry->info.fh_intk_key, key))
	    {
	      memcpy (&entry->info.fh_intk_data, (char *) data, ht->data_size);
	      return NO_ERROR;
	    }
	  break;
	default:
	  return ER_GENERIC_ERROR;
	}
      pos = entry->next;
    }
  while (pos != INVALID_FILE_POS);

  /* This is a new entry, and the last entry examined is not available */
  entry->next = ht->overflow;
  /* fetch the overflow page */
  page_no = ht->overflow / ht->entries_per_page;
  if ((pg_hdr = fh_fetch_page (ht, page_no)) == NULL)
    {
      return ER_GENERIC_ERROR;
    }
  entry_no = ht->overflow % ht->entries_per_page;
  ptr = (char *) pg_hdr->fh_entries;
  ptr += entry_no * ht->entry_size;
  entry = (FH_ENTRY *) ptr;
  ++ht->overflow;
  ++ht->ncollisions;

  /* This is a new entry, and the last entry examined is available */
use_entry:
  ++ht->nentries;
  switch (ht->key_type)
    {
    case FH_OID_KEY:
      entry->info.fh_oidk_key.volid = ((OID *) key)->volid;
      entry->info.fh_oidk_key.pageid = ((OID *) key)->pageid;
      entry->info.fh_oidk_key.slotid = ((OID *) key)->slotid;
      memcpy (&entry->info.fh_oidk_data, (char *) data, ht->data_size);
      break;
    case FH_INT_KEY:
      entry->info.fh_intk_key = *(int *) key;
      memcpy (&entry->info.fh_intk_data, (char *) data, ht->data_size);
      break;
    default:
      return ER_GENERIC_ERROR;
    }
  entry->next = INVALID_FILE_POS;
  return NO_ERROR;
}


/*
 * fh_fetch_page - fetch a page from the hash file to the cache
 *    return: page header pointer if the page exists, or if there is enough
 *            disk space to create a new page. otherwise, it returns NULL.
 *    ht(in/out): Hash table
 *    page(in): Page desired
 *
 * Note:
 *    Determine if the page is already in the cache.  If so,  then
 *    return its page header.  Otherwise, select the least recently
 *    used page, write it the the hash file and fetch the desired
 *    page.  The page found or fetched becomes the most recently
 *    used page.
 */
static FH_PAGE_HDR *
fh_fetch_page (FH_TABLE * ht, int page)
{
  FH_PAGE_HDR *pg_hdr;

  /* Search for the page in the cache */
  for (pg_hdr = ht->pg_hdr; pg_hdr; pg_hdr = pg_hdr->next)
    {
      /* The page is in the cache. */
      if (pg_hdr->page == (unsigned int) page)
	{
	  break;
	}
    }

  /* If the page is not in the cache, get it from the hash file */
  if (pg_hdr == NULL)
    {
      if ((pg_hdr = fh_read_page (ht, page)) == NULL)
	{
	  return NULL;
	}
    }

  /*
   * If the page is the least recently used, make the previous page
   * the least recently used.
   */
  if ((ht->pg_hdr_last == pg_hdr) && (ht->cached_pages > 1))
    {
      ht->pg_hdr_last = pg_hdr->prev;
    }
  /*
   * If the page is the most recently used, do nothing.
   * Otherwise, make the page the most recently used.
   */
  if (ht->pg_hdr != pg_hdr)
    {
      pg_hdr->prev->next = pg_hdr->next;
      if (pg_hdr->next)
	{
	  pg_hdr->next->prev = pg_hdr->prev;
	}
      pg_hdr->next = ht->pg_hdr;
      ht->pg_hdr->prev = pg_hdr;
      pg_hdr->prev = NULL;
      ht->pg_hdr = pg_hdr;
    }
  return pg_hdr;
}


/*
 * fh_read_page - read a page from the hash file to the cache
 *    return: page header pointer if the page exists, or if there is enough
 *            disk space to create a new page. otherwise, it returns NULL.
 *    ht(in/out): Hash table
 *    page(in): Page desired
 *
 * Note:
 *    Determine if a free page is in the cache.  If so, use it.
 *    Otherwise, write the least recently used page to the hash
 *    file and use its page slot to read in the desired page.
 *    If the desired page has never been written, initialize it to
 *    contain all unused entries.
 */
static FH_PAGE_HDR *
fh_read_page (FH_TABLE * ht, int page)
{
  FH_PAGE_HDR *pg_hdr;
#if defined(SOLARIS) || defined(AIX) || (defined(HPUX) && (_LFS64_LARGEFILE == 1))
  off64_t offset;
#elif defined(I386) && defined(LINUX)
  __off64_t offset;
#elif defined(WINDOWS)
  __int64 offset;
#else
  int offset;
#endif
  int n;

  /*
   * If a free page exists, use it.  Since pages are not freed, free
   * pages only exist after initialization until they are used.  Thus
   * the next page after the first free page is the next free page.
   */
  if (ht->pg_hdr_free)
    {
      pg_hdr = ht->pg_hdr_free;
      ht->pg_hdr_free = ht->pg_hdr_free->next;
    }
  /* Use the least recently page slot.  Write out the page first. */
  else
    {
      if (fh_write_page (ht, ht->pg_hdr_last) == NULL)
	{
	  return NULL;
	}
      pg_hdr = ht->pg_hdr_last;
    }

  /* If the page has never been written, initialize a new page */
  if (fh_bittest (ht, page) == 0)
    {
      FH_ENTRY *entry;
      int i;
      char *ptr;

      for (i = 0, entry = pg_hdr->fh_entries; i < ht->entries_per_page; ++i)
	{
	  switch (ht->key_type)
	    {
	    case FH_OID_KEY:
	      COPY_OID (&entry->info.fh_oidk_key, &null_oid);
	      break;
	    case FH_INT_KEY:
	      entry->info.fh_intk_key = null_int;
	      break;
	    default:
	      return NULL;
	    }
	  entry->next = INVALID_FILE_POS;
	  ptr = (char *) entry;
	  ptr += ht->entry_size;
	  entry = (FH_ENTRY *) ptr;
	}
      pg_hdr->page = page;
      return pg_hdr;
    }

  /* Seek to the page location and read it.  */
#if defined(SOLARIS) || defined(AIX) || (defined(HPUX) && (_LFS64_LARGEFILE == 1))
  offset = lseek64 (ht->fd, ((off64_t) page) * ht->page_size, SEEK_SET);
#elif defined(I386) && defined(LINUX)
  offset = lseek64 (ht->fd, ((__off64_t) page) * ht->page_size, SEEK_SET);
#elif defined(WINDOWS)
  offset = _lseeki64 (ht->fd, ((__int64) page) * ht->page_size, SEEK_SET);
#else
  offset = lseek (ht->fd, page * ht->page_size, SEEK_SET);
#endif
  if (offset < 0)
    {
      return NULL;
    }
  n = read (ht->fd, pg_hdr->fh_entries, ht->page_size);
  if (n != ht->page_size)
    {
      return NULL;
    }

  pg_hdr->page = page;
  return pg_hdr;
}


/*
 * fh_write_page - write a page from the cache to the hash file
 *    return: page header pointer if the write is successful otherwise, it
 *                 returns NULL.
 *    ht(in/out): Hash table
 *    pg_hdr(in): Page header of page to write
 */
static FH_PAGE_HDR *
fh_write_page (FH_TABLE * ht, FH_PAGE_HDR * pg_hdr)
{
#if defined(SOLARIS) || defined(AIX) || (defined(HPUX) && (_LFS64_LARGEFILE == 1))
  off64_t offset;
#elif defined(I386) && defined(LINUX)
  __off64_t offset;
#elif defined(WINDOWS)
  __int64 offset;
#else
  int offset;
#endif
  int n;

  /* Seek to the page location and write it.  */
#if defined(SOLARIS) || defined(AIX) || (defined(HPUX) && (_LFS64_LARGEFILE == 1))
  offset = lseek64 (ht->fd, ((off64_t) pg_hdr->page) * ht->page_size, SEEK_SET);
#elif defined(I386) && defined(LINUX)
  offset = lseek64 (ht->fd, ((__off64_t) pg_hdr->page) * ht->page_size, SEEK_SET);
#elif defined(WINDOWS)
  offset = _lseeki64 (ht->fd, ((__int64) pg_hdr->page) * ht->page_size, SEEK_SET);
#else
  offset = lseek (ht->fd, pg_hdr->page * ht->page_size, SEEK_SET);
#endif
  if (offset < 0)
    {
      return NULL;
    }
  n = write (ht->fd, pg_hdr->fh_entries, ht->page_size);
  if (n != ht->page_size)
    {
      return NULL;
    }
  fh_bitset (ht, pg_hdr->page);
  return pg_hdr;
}


/*
 * fh_bitset - set a bit corresponding to a page in the hash file page bitmap
 *    return: void
 *    ht(in/out): Hash table
 *    page(in): Page to set the bit for
 *
 * Note:
 *    The bit is assumed to exist since bits are tested before being set and
 *    fh_bittest allocates a location for bits that don't exist.
 */
static void
fh_bitset (FH_TABLE * ht, int page)
{
  int byte;
  int bit;

  byte = page / 8;
  bit = page % 8;
  ht->bitmap[byte] |= 1 << bit;
}


/*
 * fh_bittest - test a bit corresponding to a page in the hash file page bitmap
 *    return: 1/0
 *    ht(in): Hash table
 *    page(in): Page to test for
 */
static int
fh_bittest (FH_TABLE * ht, int page)
{
  int byte;
  int bit;

  byte = page / 8;
  if (byte + 1 > ht->bitmap_size)
    {
      ht->bitmap = (char *) realloc (ht->bitmap, byte + 1);
      if (ht->bitmap == NULL)
	{
	  perror ("SYSTEM ERROR");
	  exit (1);
	}
      memset (&ht->bitmap[ht->bitmap_size], 0, byte + 1 - ht->bitmap_size);
      ht->bitmap_size = byte + 1;
      return 0;
    }
  bit = page % 8;
  return ht->bitmap[byte] & 1 << bit;
}


/*
 * fh_dump - dump the hash table header structure
 *    return: void
 *    ht(in): Hash table
 */
void
fh_dump (FH_TABLE * ht)
{
  static char oid_string[] = "OID";
  static char int_string[] = "INT";

  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_NAME), ht->name);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_SIZE), ht->name);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_PAGE_SIZE), ht->page_size);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_DATA_SIZE), ht->data_size);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_ENTRY_SIZE),
	   ht->entry_size);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_ENTRIES_PER_PAGE),
	   ht->entries_per_page);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_CACHED_PAGES),
	   ht->cached_pages);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_NUM_ENTRIES),
	   ht->nentries);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_NUM_COLLISIONS),
	   ht->ncollisions);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_HASH_FILENAME2),
	   ht->hash_filename);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_NEXT_OVERFLOW_ENTRY),
	   ht->overflow);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_KEY_TYPE),
	   ht->key_type == FH_OID_KEY ? oid_string : int_string);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_PAGE_HEADERS),
	   (unsigned long) (unsigned long long) ht->pg_hdr);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_LAST_PAGE_HEADER),
	   (unsigned long) (unsigned long long) ht->pg_hdr_last);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_FREE_PAGE_HEADER),
	   (unsigned long) (unsigned long long) ht->pg_hdr_free);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_PAGE_BITMAP),
	   (unsigned long) (unsigned long long) ht->bitmap);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS, MSGCAT_UTIL_SET_MIGDB, MIGDB_MSG_FH_PAGE_BITMAP_SIZE),
	   ht->bitmap_size);
}
