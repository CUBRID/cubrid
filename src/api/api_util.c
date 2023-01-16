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
 * api_util.h -
 */

#include "config.h"


#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include "api_util.h"
#include "error_code.h"

typedef struct ht_elem_s ht_elem;
typedef struct ht_bucket_s ht_bucket;
typedef struct debug_malloc_h_s debug_malloc_h;

/* static hash table entry. linked via doubly linked list header  */
struct ht_elem_s
{
  dlisth head;			/* must be the first member */
  void *elem;
};

struct ht_bucket_s
{
  dlisth head;			/* must be the first member */
};

struct hash_table_s
{
  int bucket_sz;
  ht_hashf hashf;
  ht_keyf keyf;
  ht_comparef comparef;
  ht_bucket buckets[1];
};

/* malloc debug header */
struct debug_malloc_h_s
{
  dlisth head;
  int line;
  char file[12];
};
#define MALLOC_HEADER_SZ 32

static void once_function ();
static void make_debug_h (debug_malloc_h * mh, const char *file, int line);
static void dump_debug_h (debug_malloc_h * mh, FILE * fp);

/* lock */
static API_MUTEX mutex;
static int malloc_count;
static int free_count;
static dlisth malloc_list;
static API_ONCE_TYPE once = API_ONCE_INIT;

/* global variable */
int api_malloc_dhook_flag_set = 0;

/*
 * once_function - initialize api_malloc/free/calloc debug information
 *    return: void
 */
static void
once_function ()
{
  API_MUTEX_INIT (&mutex);
  dlisth_init (&malloc_list);
  malloc_count = 0;
  free_count = 0;
}

/*
 * make_debug_h - initialize debug malloc header with given file and line
 *    return: void
 *    mh(in): malloc debug header
 *    file(in): file name
 *    line(in): line number
 */
static void
make_debug_h (debug_malloc_h * mh, const char *file, int line)
{
  int file_len;
  char *bp;

  dlisth_init (&mh->head);
  mh->line = line;
  file_len = strlen (file);
  bp = file_len < 11 ? (char *) file : (char *) file + file_len - 11;
  strncpy (mh->file, bp, 11);
  mh->file[11] = 0;
}

/*
 * dump_debug_h - print debug malloc header information to given out fp
 *    return:
 *    mh(in): malloc debug header
 *    fp(in): FILE pointer
 */
static void
dump_debug_h (debug_malloc_h * mh, FILE * fp)
{
  fprintf (fp, "file: %s, line %d\n", mh->file, mh->line);
}


/*
 * dlisth_map - map over dlist and call map function
 *    return: NO_ERROR if successful, error code otherwise
 *    h(in): doubly linked list header
 *    func(in): map function
 *    arg(in): argument for the map function
 *
 * NOTE: traverse stops when map function set continue out parameter to 0
 */
int
dlisth_map (dlisth * h, dlist_map_func func, void *arg)
{
  dlisth *tmp;
  int cont;
  assert (h != NULL);
  assert (func != NULL);
  cont = 0;
  for (tmp = h->next; tmp != h; tmp = tmp->next)
    {
      int r = func (tmp, arg, &cont);
      if (r != NO_ERROR)
	return r;
      else if (cont == 0)
	break;
    }
  return NO_ERROR;
}

/*
 * hash_new - create new static hash table
 *    return: NO_ERROR if successful, error code otherwise
 *    bucket_sz(in): hash table bucket size
 *    hashf(in): hash function
 *    keyf(in): key function
 *    comparef(in): compare function
 *    rht(out): hash table
 */
int
hash_new (int bucket_sz, ht_hashf hashf, ht_keyf keyf, ht_comparef comparef, hash_table ** rht)
{
  int sz, i;
  hash_table *ht;

  assert (bucket_sz > 0);
  assert (hashf != NULL);
  assert (comparef != NULL);

  sz = sizeof (*ht) + (bucket_sz - 1) * sizeof (ht_bucket);
  ht = (hash_table *) API_MALLOC (sz);
  if (ht == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;

  ht->bucket_sz = bucket_sz;
  ht->hashf = hashf;
  ht->keyf = keyf;
  ht->comparef = comparef;
  for (i = 0; i < bucket_sz; i++)
    dlisth_init ((dlisth *) (&ht->buckets[i].head));

  *rht = ht;
  return NO_ERROR;
}

/*
 * hash_destroy - destroy static hash table
 *    return: void
 *    ht(in): hash table
 *    dtor(in): element destructor function
 */
void
hash_destroy (hash_table * ht, ht_destroyf dtor)
{
  int i;

  assert (ht != NULL);
  if (ht == NULL)
    return;

  for (i = 0; i < ht->bucket_sz; i++)
    {
      dlisth *h, *header;
      h = header = &ht->buckets[i].head;
      h = h->next;
      while (h != header)
	{
	  ht_elem *hte = (ht_elem *) h;
	  h = h->next;

	  dlisth_delete ((dlisth *) hte);
	  if (dtor)
	    dtor (hte->elem);
	  API_FREE (hte);
	}
    }
  API_FREE (ht);
}

/*
 * hash_lookup - lookup element
 *    return: NO_ERROR if successful, error code otherwise
 *    ht(in): hash table
 *    key(in): pointer to the key
 *    relem(out): element found
 */
int
hash_lookup (hash_table * ht, void *key, void **relem)
{
  int rc;
  unsigned int hcode;
  dlisth *h, *header;

  assert (ht != NULL);
  assert (relem != NULL);

  rc = ht->hashf (key, &hcode);
  if (rc != NO_ERROR)
    return rc;
  hcode = hcode % ht->bucket_sz;

  header = &ht->buckets[(size_t) hcode].head;
  for (h = header->next; h != header; h = h->next)
    {
      ht_elem *hte = (ht_elem *) h;
      void *ekey;
      int r;

      if ((rc = ht->keyf (hte->elem, &ekey)) != NO_ERROR)
	return rc;
      if ((rc = ht->comparef (key, ekey, &r)) == NO_ERROR && r == 0)
	{
	  *relem = hte->elem;
	  return NO_ERROR;
	}
      else if (rc != NO_ERROR)
	return rc;
    }
  *relem = NULL;
  return NO_ERROR;
}

/*
 * hash_insert - insert element to the hash table
 *    return: NO_ERROR if successful, error code otherwise
 *    ht(in): hash table
 *    elem(in): element to insert
 */
int
hash_insert (hash_table * ht, void *elem)
{
  int rc;
  void *key;
  dlisth *header;
  ht_elem *hte;
  unsigned int hcode;

  assert (ht != NULL && elem != NULL);

  rc = ht->keyf (elem, &key);
  if (rc != NO_ERROR)
    return rc;

  rc = ht->hashf (key, &hcode);
  if (rc != NO_ERROR)
    return rc;

  hcode = hcode % ht->bucket_sz;
  header = &ht->buckets[(size_t) hcode].head;

  hte = API_MALLOC (sizeof (ht_elem));
  if (hte == NULL)
    return ER_INTERFACE_NO_MORE_MEMORY;

  hte->elem = elem;

  dlisth_insert_after ((dlisth *) hte, header);
  return NO_ERROR;
}


/*
 * hash_delete - delete hash entry from hash table and return element found
 *    return: NO_ERROR if successful, error_code otherwise
 *    ht(in): hash table
 *    key(in): pointer to the key
 *    relem(out): element found, or set to NULL
 *
 * NOTE
 *   When element is not found then NO_ERROR returned with NULL relem value
 */
int
hash_delete (hash_table * ht, void *key, void **relem)
{
  int rc;
  unsigned int hcode;
  dlisth *h, *header;

  assert (ht != NULL);
  assert (relem != NULL);
  *relem = NULL;

  rc = ht->hashf (key, &hcode);
  if (rc != NO_ERROR)
    return rc;

  hcode = hcode % ht->bucket_sz;
  header = &ht->buckets[(size_t) hcode].head;
  for (h = header->next; h != header; h = h->next)
    {
      int r;
      ht_elem *hte = (ht_elem *) h;
      void *ekey;

      if ((rc = ht->keyf (hte->elem, &ekey)) != NO_ERROR)
	return rc;

      if ((rc = ht->comparef (key, ekey, &r)) == NO_ERROR && r == 0)
	{
	  *relem = hte->elem;
	  dlisth_delete (h);
	  API_FREE (hte);
	  return NO_ERROR;
	}
      else if (rc != NO_ERROR)
	return rc;
    }
  return NO_ERROR;
}


/*
 * api_calloc - calloc() debug version
 *    return: pointer to the allocated memory
 *    nmemb(in): number of elem
 *    size(in): size of elem
 *    file(in): file name
 *    line(in): line number
 */
void *
api_calloc (size_t nmemb, size_t size, const char *file, int line)
{
  void *ptr;
  size_t sz;

  if (api_malloc_dhook_flag_set == 0)
    return calloc (nmemb, size);

  API_ONCE_FUNC (&once, once_function);

  sz = MALLOC_HEADER_SZ + nmemb * size;
  assert (sizeof (debug_malloc_h) <= MALLOC_HEADER_SZ);
  ptr = malloc (sz);
  if (ptr)
    {
      make_debug_h ((debug_malloc_h *) ptr, file, line);
      memset ((char *) ptr + MALLOC_HEADER_SZ, 0, nmemb * size);
      API_LOCK (&mutex);
      dlisth_insert_after ((dlisth *) ptr, &malloc_list);
      malloc_count++;
      API_UNLOCK (&mutex);
    }
  return (char *) ptr + MALLOC_HEADER_SZ;
}

/*
 * api_malloc - malloc() debug version
 *    return: pointer to allcated memory
 *    size(in): allcation size
 *    file(in): file name
 *    line(in): line number
 */
void *
api_malloc (size_t size, const char *file, int line)
{
  void *ptr;
  size_t sz;

  if (api_malloc_dhook_flag_set == 0)
    return malloc (size);

  API_ONCE_FUNC (&once, once_function);

  sz = MALLOC_HEADER_SZ + size;
  assert (sizeof (debug_malloc_h) <= MALLOC_HEADER_SZ);
  ptr = malloc (sz);
  if (ptr)
    {
      make_debug_h ((debug_malloc_h *) ptr, file, line);
      API_LOCK (&mutex);
      dlisth_insert_after ((dlisth *) ptr, &malloc_list);
      malloc_count++;
      API_UNLOCK (&mutex);
    }
  return (char *) ptr + MALLOC_HEADER_SZ;
}

/*
 * api_free - free() debug version
 *    return: void
 *    ptr(in): pointer to allcated memory via api_malloc(), api_calloc()
 *    file(in): file name
 *    line(in): line number
 */
void
api_free (void *ptr, const char *file, int line)
{
  char *p = NULL;

  if (api_malloc_dhook_flag_set == 0)
    return free (ptr);

  API_ONCE_FUNC (&once, once_function);
  if (ptr)
    {
      p = (char *) ptr - MALLOC_HEADER_SZ;
      API_LOCK (&mutex);
      dlisth_delete ((dlisth *) p);
      free_count++;
      API_UNLOCK (&mutex);
    }
  free (p);
}

/*
 * api_check_memory - check memory
 *    return: 0 if successful, count of failure otherwise
 *    fp(in): FILE * to print check message
 * NOTE : this function checks malloc/free pair match and
 *        dump debug debug information of malloc/calloced but not freeed.
 */
int
api_check_memory (FILE * fp)
{
  int res = 0;

  if (api_malloc_dhook_flag_set == 0)
    return 0;

  API_ONCE_FUNC (&once, once_function);
  API_LOCK (&mutex);
  if (malloc_count != free_count)
    {
      fprintf (fp, "malloc/free count mismatch (%d/%d)\n", malloc_count, free_count);
      res++;
    }
  if (!dlisth_is_empty (&malloc_list))
    {
      dlisth *h;
      fprintf (fp, "malloc list not empty (memory leak)\n");
      for (h = malloc_list.next; h != &malloc_list; h = h->next)
	{
	  fprintf (fp, "\t");
	  dump_debug_h ((debug_malloc_h *) h, fp);
	  fprintf (fp, "\n");
	}
      res++;
    }
  API_UNLOCK (&mutex);
  return res;
}
