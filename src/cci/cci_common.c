/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

#include <ctype.h>

#if defined (WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#endif

#if defined(AIX)
#include <netinet/if_ether.h>
#include <net/if_dl.h>
#endif /* AIX */
#if defined(SOLARIS)
#include <sys/filio.h>
#endif /* SOLARIS */
#if defined(sun)
#include <sys/sockio.h>
#endif /* sun */

#include "cci_common.h"
#include "cas_cci.h"
/*
 *  The structure of the hash table is approximately as follows;
 *
 *   Hash Table header
 *   -----------------
 *   |hash_fun       |
 *   |cmp_fun        |
 *   |table_name     |
 *   |Ptr to entries----->     The entries
 *   |size           |     -------------------
 *   |rehash_at      |     | key, data, next |--> ... -> key, data, next
 *   |num_entries    |     |       .         |
 *   |num_collisions |     |   .             |
 *   -----------------     |       .         |
 *                         -------------------
 */


/* constants for rehash */
static const float CCI_MHT_REHASH_TRESHOLD = 0.7f;
static const float CCI_MHT_REHASH_FACTOR = 1.3f;

/* options for cci_mht_put() */
typedef enum cci_mht_put_opt CCI_MHT_PUT_OPT;
enum cci_mht_put_opt
{
  CCI_MHT_OPT_DEFAULT,
  CCI_MHT_OPT_KEEP_KEY,
  CCI_MHT_OPT_INSERT_ONLY
};

/*
 * A table of prime numbers.
 *
 * Some prime numbers which were picked randomly.
 * This table contains more smaller prime numbers.
 * Some of the prime numbers included are:
 * between  1000 and  2000, prime numbers that are farther than  50 units.
 * between  2000 and  7000, prime numbers that are farther than 100 units.
 * between  7000 and 12000, prime numbers that are farther than 200 units.
 * between 12000 and 17000, prime numbers that are farther than 400 units.
 * between 17000 and 22000, prime numbers that are farther than 800 units.
 * above 22000, prime numbers that are farther than 1000 units.
 *
 * Note: if x is a prime number, the n is prime if X**(n-1) mod n == 1
 */

static unsigned int cci_mht_5str_pseudo_key (void *key, int key_size);

static unsigned int cci_mht_calculate_htsize (unsigned int ht_size);
static int cci_mht_rehash (CCI_MHT_TABLE * ht);

static void *cci_mht_put_internal (CCI_MHT_TABLE * ht, void *key, void *data, CCI_MHT_PUT_OPT opt);

/*
 * cci_mht_5str_pseudo_key() - hash string key into pseudo integer key
 * return: pseudo integer key
 * key(in): string key to hash
 * key_size(in): size of key or -1 when unknown
 *
 * Note: Based on hash method reported by Diniel J. Bernstein.
 */
static unsigned int
cci_mht_5str_pseudo_key (void *key, int key_size)
{
  unsigned int hash = 5381;
  int i = 0;
  char *k;

  assert (key != NULL);
  k = (char *) key;

  if (key_size == -1)
    {
      int c;
      while ((c = *(k + i++)) != 0)
	{
	  hash = ((hash << 5) + hash) + c;	/* hash * 33 + c */
	}
    }
  else
    {
      for (; i < key_size; i++)
	{
	  hash = ((hash << 5) + hash) + *(k + i);
	}
    }

  hash += ~(hash << 9);
  hash ^= ((hash >> 14) | (i << 18));	/* >>> */
  hash += (hash << 4);
  hash ^= ((hash >> 10) | (i << 22));	/* >>> */

  return hash;
}

/*
 * cci_mht_5strhash - hash a string key
 *   return: hash value
 *   key(in): key to hash
 *   ht_size(in): size of hash table
 *
 * Note: Based on hash method reported by Diniel J. Bernstein.
 */
unsigned int
cci_mht_5strhash (void *key, unsigned int ht_size)
{
  return cci_mht_5str_pseudo_key (key, -1) % ht_size;
}

/*
 * cci_mht_strcasecmpeq - compare two string keys (ignoring case)
 *   return: 0 or 1 (key1 == key2)
 *   key1(in): pointer to string key1
 *   key2(in): pointer to string key2
 */
int
cci_mht_strcasecmpeq (void *key1, void *key2)
{
  if ((strcasecmp ((char *) key1, (char *) key2)) == 0)
    {
      return TRUE;
    }
  return FALSE;
}

/*
 * cci_mht_calculate_htsize - find a good hash table size
 *   return: adjusted hash table size
 *   ht_size(in): given hash table size
 *
 * Note: Find a good size for a hash table. A prime number is the best
 *       size for a hash table, unfortunately prime numbers are
 *       difficult to found. If the given htsize, falls among the
 *       prime numbers known by this module, a close prime number to
 *       the given htsize value is returned, otherwise, a power of two
 *       is returned.
 */
#define NPRIMES 170
static const unsigned int cci_mht_Primes[NPRIMES] = {
  11, 23, 37, 53, 67, 79, 97, 109, 127, 149,
  167, 191, 211, 227, 251, 269, 293, 311, 331, 349,
  367, 389, 409, 431, 449, 467, 487, 509, 541, 563,
  587, 607, 631, 653, 673, 521, 541, 557, 569, 587,
  599, 613, 641, 673, 701, 727, 751, 787, 821, 853,
  881, 907, 941, 977, 1039, 1087, 1129, 1171, 1213, 1259,
  1301, 1361, 1409, 1471, 1523, 1579, 1637, 1693, 1747, 1777,
  1823, 1867, 1913, 1973, 2017, 2129, 2237, 2339, 2441, 2543,
  2647, 2749, 2851, 2953, 3061, 3163, 3271, 3373, 3491, 3593,
  3697, 3803, 3907, 4013, 4177, 4337, 4493, 4649, 4801, 4957,
  5059, 5167, 5273, 5381, 5483, 5591, 5693, 5801, 5903, 6007,
  6113, 6217, 6317, 6421, 6521, 6637, 6737, 6841, 6847, 7057,
  7283, 7487, 7687, 7901, 8101, 8311, 8513, 8713, 8923, 9127,
  9337, 9539, 9739, 9941, 10141, 10343, 10559, 10771, 10973, 11177,
  11383, 11587, 11789, 12007, 12409, 12809, 13217, 13619, 14029, 14431,
  14831, 15233, 15641, 16057, 16477, 16879, 17291, 18097, 18899, 19699,
  20507, 21313, 22123, 23131, 24133, 25147, 26153, 27179, 28181, 29123
};

/* Ceiling of positive division */
#define CEIL_PTVDIV(dividend, divisor) \
        (((dividend) == 0) ? 0 : (((dividend) - 1) / (divisor)) + 1)

static unsigned int
cci_mht_calculate_htsize (unsigned int ht_size)
{
  int left, right, middle;	/* indices for binary search */

  if (ht_size > cci_mht_Primes[NPRIMES - 1])
    {
      /* get a power of two */
      if (!((ht_size & (ht_size - 1)) == 0))
	{
	  /* Turn off some bits but the left most one */
	  while (!(ht_size & (ht_size - 1)))
	    {
	      ht_size &= (ht_size - 1);
	    }
	  ht_size <<= 1;
	}
    }
  else
    {
      /* we can assign a primary number; binary search */
      for (middle = 0, left = 0, right = NPRIMES - 1; left <= right;)
	{
	  middle = CEIL_PTVDIV ((left + right), 2);
	  if (ht_size == cci_mht_Primes[middle])
	    {
	      break;
	    }
	  else if (ht_size > cci_mht_Primes[middle])
	    {
	      left = middle + 1;
	    }
	  else
	    {
	      right = middle - 1;
	    }
	}
      /* If we didn't find the size, get the larger size and not the small one */
      if (ht_size > cci_mht_Primes[middle] && middle < (NPRIMES - 1))
	{
	  middle++;
	}
      ht_size = cci_mht_Primes[middle];
    }

  return ht_size;
}

/*
 * cci_mht_create - create a hash table
 *   return: hash table
 *   name(in): name of hash table
 *   est_size(in): estimated number of entries
 *   hash_func(in): hash function
 *   cmp_func(in): key compare function
 *
 * Note: Create a new hash table. The estimated number of entries for
 *       the hash table may be adjusted (to a prime number) in order to
 *       obtain certain favorable circumstances when the table is
 *       created, when the table is almost full and there are at least
 *       5% of collisions. 'hash_func' must return an integer between 0 and
 *       table_size - 1. 'cmp_func' must return TRUE if key1 = key2,
 *       otherwise, FALSE.
 */
CCI_MHT_TABLE *
cci_mht_create (char *name, int est_size, HASH_FUNC hash_func, CMP_FUNC cmp_func)
{
  CCI_MHT_TABLE *ht;
  CCI_HENTRY_PTR *hvector;	/* Entries of hash table */
  unsigned int ht_estsize;
  unsigned int size;

  assert (hash_func != NULL && cmp_func != NULL);

  /* Get a good number of entries for hash table */
  if (est_size <= 0)
    {
      est_size = 2;
    }

  ht_estsize = cci_mht_calculate_htsize ((unsigned int) est_size);

  /* Allocate the header information for hash table */
  ht = (CCI_MHT_TABLE *) MALLOC (sizeof (CCI_MHT_TABLE));
  if (ht == NULL)
    {
      return NULL;
    }

  /* Allocate the hash table entry pointers */
  size = ht_estsize * sizeof (CCI_HENTRY_PTR);
  hvector = (CCI_HENTRY_PTR *) MALLOC (size);
  if (hvector == NULL)
    {
      FREE (ht);
      return NULL;
    }

  ht->hash_func = hash_func;
  ht->cmp_func = cmp_func;
  ht->name = name;
  ht->table = hvector;
  ht->act_head = NULL;
  ht->act_tail = NULL;
  ht->prealloc_entries = NULL;
  ht->size = ht_estsize;
  ht->rehash_at = (unsigned int) (ht_estsize * CCI_MHT_REHASH_TRESHOLD);
  ht->nentries = 0;
  ht->nprealloc_entries = 0;
  ht->ncollisions = 0;

  /* Initialize each of the hash entries */
  for (; ht_estsize > 0; ht_estsize--)
    {
      *hvector++ = NULL;
    }

  return ht;
}

/*
 * cci_mht_rehash - rehash all entires of a hash table
 *   return: error code
 *   ht(in/out): hash table to rehash
 *
 * Note: Expand a hash table. All entries are rehashed onto a larger
 *       hash table of entries.
 */
static int
cci_mht_rehash (CCI_MHT_TABLE * ht)
{
  CCI_HENTRY_PTR *new_hvector;	/* New entries of hash table */
  CCI_HENTRY_PTR *hvector;	/* Entries of hash table */
  CCI_HENTRY_PTR hentry;	/* A hash table entry. linked list */
  CCI_HENTRY_PTR next_hentry = NULL;	/* Next element in linked list */
  float rehash_factor;
  unsigned int hash;
  unsigned int est_size;
  unsigned int size;
  unsigned int i;

  /* Find an estimated size for hash table entries */
  rehash_factor = 1.0f + ((float) ht->ncollisions / (float) ht->nentries);
  if (CCI_MHT_REHASH_FACTOR > rehash_factor)
    {
      est_size = (unsigned int) (ht->size * CCI_MHT_REHASH_FACTOR);
    }
  else
    {
      est_size = (unsigned int) (ht->size * rehash_factor);
    }

  est_size = cci_mht_calculate_htsize (est_size);

  /* Allocate a new vector to keep the estimated hash entries */
  size = est_size * sizeof (CCI_HENTRY_PTR);
  new_hvector = (CCI_HENTRY_PTR *) MALLOC (size);
  if (new_hvector == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  /* Initialize all entries */
  memset (new_hvector, 0x00, size);

  /* Now rehash the current entries onto the vector of hash entries table */
  for (ht->ncollisions = 0, hvector = ht->table, i = 0; i < ht->size; hvector++, i++)
    {
      /* Go over each linked list */
      for (hentry = *hvector; hentry != NULL; hentry = next_hentry)
	{
	  next_hentry = hentry->next;

	  hash = (*ht->hash_func) (hentry->key, est_size);
	  if (hash >= est_size)
	    {
	      hash %= est_size;
	    }

	  /* Link the new entry with any previous elements */
	  hentry->next = new_hvector[hash];
	  if (hentry->next != NULL)
	    {
	      ht->ncollisions++;
	    }
	  new_hvector[hash] = hentry;
	}
    }

  /* Now move to new vector of entries */
  FREE_MEM (ht->table);

  ht->table = new_hvector;
  ht->size = est_size;
  ht->rehash_at = (int) (est_size * CCI_MHT_REHASH_TRESHOLD);

  return CCI_ER_NO_ERROR;
}

/*
 * cci_mht_destroy - destroy a hash table
 *   return: void
 *   ht(in/out): hash table
 *
 * Note: ht is set as a side effect
 */
void
cci_mht_destroy (CCI_MHT_TABLE * ht, bool free_key, bool free_data)
{
  CCI_HENTRY_PTR *hvector;
  CCI_HENTRY_PTR hentry;
  CCI_HENTRY_PTR next_hentry = NULL;
  unsigned int i;

  assert (ht != NULL);

  for (hvector = ht->table, i = 0; i < ht->size; hvector++, i++)
    {
      /* Go over each linked list */
      for (hentry = *hvector; hentry != NULL; hentry = next_hentry)
	{
	  next_hentry = hentry->next;
	  if (free_key)
	    {
	      FREE (hentry->key);
	    }
	  if (free_data)
	    {
	      FREE (hentry->data);
	    }
	  FREE (hentry);
	}
    }

  while (ht->nprealloc_entries > 0)
    {
      hentry = ht->prealloc_entries;
      ht->prealloc_entries = ht->prealloc_entries->next;
      FREE_MEM (hentry);
      ht->nprealloc_entries--;
    }

  FREE_MEM (ht->table);
  FREE_MEM (ht);
}

/*
 * cci_mht_rem - remove a hash entry
 *   return: removed data
 *   ht(in): hash table
 *   key(in): hashing key
 *   free_key(in): flag to free key memory
 *   free_data(in): flag to free data memory
 *
 * Note: For each entry in hash table
 */
void *
cci_mht_rem (CCI_MHT_TABLE * ht, void *key, bool free_key, bool free_data)
{
  unsigned int hash;
  CCI_HENTRY_PTR prev_hentry;
  CCI_HENTRY_PTR hentry;
  int error_code = CCI_ER_NO_ERROR;
  void *data = NULL;

  assert (ht != NULL && key != NULL);

  /* 
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hash_func) (key, ht->size);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  /* Now search the linked list.. Is there any entry with the given key ? */
  for (hentry = ht->table[hash], prev_hentry = NULL; hentry != NULL; prev_hentry = hentry, hentry = hentry->next)
    {
      if (hentry->key == key || (*ht->cmp_func) (hentry->key, key))
	{
	  data = hentry->data;

	  /* Remove from double link list of active entries */
	  if (ht->act_head == ht->act_tail)
	    {
	      /* Single active element */
	      ht->act_head = ht->act_tail = NULL;
	    }
	  else if (ht->act_head == hentry)
	    {
	      /* Deleting from the head */
	      ht->act_head = hentry->act_next;
	      hentry->act_next->act_prev = NULL;
	    }
	  else if (ht->act_tail == hentry)
	    {
	      /* Deleting from the tail */
	      ht->act_tail = hentry->act_prev;
	      hentry->act_prev->act_next = NULL;
	    }
	  else
	    {
	      /* Deleting from the middle */
	      hentry->act_prev->act_next = hentry->act_next;
	      hentry->act_next->act_prev = hentry->act_prev;
	    }

	  /* Remove from the hash */
	  if (prev_hentry != NULL)
	    {
	      prev_hentry->next = hentry->next;
	      ht->ncollisions--;
	    }
	  else if ((ht->table[hash] = hentry->next) != NULL)
	    {
	      ht->ncollisions--;
	    }
	  ht->nentries--;
	  /* Save the entry for future insertions */
	  ht->nprealloc_entries++;
	  hentry->next = ht->prealloc_entries;
	  ht->prealloc_entries = hentry;

	  if (free_key)
	    {
	      FREE_MEM (hentry->key);
	    }

	  if (free_data)
	    {
	      FREE_MEM (hentry->data);
	      return NULL;
	    }

	  return data;
	}
    }

  return NULL;
}

/*
 * cci_mht_get - find data associated with key
 *   return: the data associated with the key, or NULL if not found
 *   ht(in): hash table
 *   key(in): hashing key
 *
 * Note: Find the entry in hash table whose key is the value of the given key
 */
void *
cci_mht_get (CCI_MHT_TABLE * ht, void *key)
{
  unsigned int hash;
  CCI_HENTRY_PTR hentry;

  assert (ht != NULL && key != NULL);

  /* 
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hash_func) (key, ht->size);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  /* now search the linked list */
  for (hentry = ht->table[hash]; hentry != NULL; hentry = hentry->next)
    {
      if (hentry->key == key || (*ht->cmp_func) (hentry->key, key))
	{
	  return hentry->data;
	}
    }
  return NULL;
}

/*
 * cci_mht_put_internal - internal function for cci_mht_put(), 
 *                        cci_mht_put_new(), and cci_mht_put_data();
 *                        insert an entry associating key with data
 *   return: Returns key if insertion was OK, otherwise, it returns NULL
 *   ht(in/out): hash table (set as a side effect)
 *   key(in): hashing key
 *   data(in): data associated with hashing key
 *   opt(in): options;
 *            CCI_MHT_OPT_DEFAULT - change data and the key as given of the hash
 *                                  entry; replace the old entry with the new
 *                                  one if there is an entry with the same key
 *            CCI_MHT_OPT_KEEP_KEY - change data but the key of the hash entry
 *            CCI_MHT_OPT_INSERT_ONLY - do not replace the existing hash entry
 *                                      even if there is an etnry with the same
 *                                      key
 */
static void *
cci_mht_put_internal (CCI_MHT_TABLE * ht, void *key, void *data, CCI_MHT_PUT_OPT opt)
{
  unsigned int hash;
  CCI_HENTRY_PTR hentry;

  assert (ht != NULL && key != NULL);

  /* 
   * Hash the key and make sure that the return value is between 0 and size
   * of hash table
   */
  hash = (*ht->hash_func) (key, ht->size);
  if (hash >= ht->size)
    {
      hash %= ht->size;
    }

  if (!(opt & CCI_MHT_OPT_INSERT_ONLY))
    {
      /* Now search the linked list.. Is there any entry with the given key ? */
      for (hentry = ht->table[hash]; hentry != NULL; hentry = hentry->next)
	{
	  if (hentry->key == key || (*ht->cmp_func) (hentry->key, key))
	    {
	      /* Replace the old data with the new one */
	      if (!(opt & CCI_MHT_OPT_KEEP_KEY))
		{
		  hentry->key = key;
		}
	      hentry->data = data;
	      return key;
	    }
	}
    }

  /* This is a new entry */
  if (ht->nprealloc_entries > 0)
    {
      ht->nprealloc_entries--;
      hentry = ht->prealloc_entries;
      ht->prealloc_entries = ht->prealloc_entries->next;
    }
  else
    {
      hentry = (CCI_HENTRY_PTR) MALLOC (sizeof (CCI_HENTRY));
      if (hentry == NULL)
	{
	  return NULL;
	}
    }

  /* 
   * Link the new entry to the double link list of active entries and the
   * hash itself. The previous entry should point to new one.
   */

  hentry->key = key;
  hentry->data = data;
  hentry->act_next = NULL;
  hentry->act_prev = ht->act_tail;
  /* Double link tail entry should point to newly created entry */
  if (ht->act_tail != NULL)
    {
      ht->act_tail->act_next = hentry;
    }

  ht->act_tail = hentry;
  if (ht->act_head == NULL)
    {
      ht->act_head = hentry;
    }

  hentry->next = ht->table[hash];
  if (hentry->next != NULL)
    {
      ht->ncollisions++;
    }

  ht->table[hash] = hentry;
  ht->nentries++;

  /* 
   * Rehash if almost all entries of hash table are used and there are at least
   * 5% of collisions
   */
  if (ht->nentries > ht->rehash_at && ht->ncollisions > (ht->nentries * 0.05))
    {
      if (cci_mht_rehash (ht) < 0)
	{
	  return NULL;
	}
    }

  return key;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * cci_mht_put - insert an entry associating key with data
 *   return: Returns key if insertion was OK, otherwise, it returns NULL
 *   ht(in/out): hash table (set as a side effect)
 *   key(in): hashing key
 *   data(in): data associated with hashing key
 *
 * Note: Insert an entry into a hash table, associating the given key
 *       with the given data. If the entry is duplicated, that is, if
 *       the key already exist the new entry replaces the old one.
 *
 *       The key and data are NOT COPIED, only its pointers are copied. The
 *       user must be aware that changes to the key and value are reflected
 *       into the hash table
 */
void *
cci_mht_put (CCI_MHT_TABLE * ht, void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return cci_mht_put_internal (ht, key, data, CCI_MHT_OPT_DEFAULT);
}
#endif

void *
cci_mht_put_data (CCI_MHT_TABLE * ht, void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return cci_mht_put_internal (ht, key, data, CCI_MHT_OPT_KEEP_KEY);
}

#ifndef HAVE_GETHOSTBYNAME_R
static pthread_mutex_t gethostbyname_lock = PTHREAD_MUTEX_INITIALIZER;
#endif /* HAVE_GETHOSTBYNAME_R */

int
hostname2uchar (char *host, unsigned char *ip_addr)
{
  in_addr_t in_addr;

  /* 
   * First try to convert to the host name as a dotten-decimal number.
   * Only if that fails do we call gethostbyname.
   */
  in_addr = inet_addr (host);
  if (in_addr != INADDR_NONE)
    {
      memcpy ((void *) ip_addr, (void *) &in_addr, sizeof (in_addr));
      return CCI_ER_NO_ERROR;
    }
  else
    {
#ifdef HAVE_GETHOSTBYNAME_R
# if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
      struct hostent *hp, hent;
      int herr;
      char buf[1024];

      if (gethostbyname_r (host, &hent, buf, sizeof (buf), &hp, &herr) != 0 || hp == NULL)
	{
	  return INVALID_SOCKET;
	}
      memcpy ((void *) ip_addr, (void *) hent.h_addr, hent.h_length);
# elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
      struct hostent hent;
      int herr;
      char buf[1024];

      if (gethostbyname_r (host, &hent, buf, sizeof (buf), &herr) == NULL)
	{
	  return INVALID_SOCKET;
	}
      memcpy ((void *) ip_addr, (void *) hent.h_addr, hent.h_length);
# elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
      struct hostent hent;
      struct hostent_data ht_data;

      if (gethostbyname_r (host, &hent, &ht_data) == -1)
	{
	  return INVALID_SOCKET;
	}
      memcpy ((void *) ip_addr, (void *) hent.h_addr, hent.h_length);
# else
#   error "HAVE_GETHOSTBYNAME_R"
# endif
#else /* HAVE_GETHOSTBYNAME_R */
      struct hostent *hp;

      pthread_mutex_lock (&gethostbyname_lock);
      hp = gethostbyname (host);
      if (hp == NULL)
	{
	  pthread_mutex_unlock (&gethostbyname_lock);
	  return INVALID_SOCKET;
	}
      memcpy ((void *) ip_addr, (void *) hp->h_addr, hp->h_length);
      pthread_mutex_unlock (&gethostbyname_lock);
#endif /* !HAVE_GETHOSTBYNAME_R */
    }

  return CCI_ER_NO_ERROR;
}
