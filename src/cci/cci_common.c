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

#include "cci_common.h"
#include "cas_cci.h"

#if defined (WINDOWS)
int
cci_mutex_init (cci_mutex_t * mutex, cci_mutexattr_t * attr)
{
  if (mutex->csp == &mutex->cs)
    {
      /* already inited */
      assert (0);
      return 0;
    }

  mutex->csp = &mutex->cs;
  InitializeCriticalSection (mutex->csp);

  return 0;
}

int
cci_mutex_destroy (cci_mutex_t * mutex)
{
  if (mutex->csp != &mutex->cs)
    {
      if (mutex->csp == NULL)	/* inited by PTHREAD_MUTEX_INITIALIZER */
	{
	  return 0;
	}

      /* invalid destroy */
      assert (0);
      mutex->csp = NULL;
      return 0;
    }

  DeleteCriticalSection (mutex->csp);
  mutex->csp = NULL;
  return 0;
}

cci_mutex_t cci_Internal_mutex_for_mutex_initialize =
  PTHREAD_MUTEX_INITIALIZER;

void
port_cci_mutex_init_and_lock (cci_mutex_t * mutex)
{
  if (cci_Internal_mutex_for_mutex_initialize.csp !=
      &cci_Internal_mutex_for_mutex_initialize.cs)
    {
      cci_mutex_init (&cci_Internal_mutex_for_mutex_initialize, NULL);
    }

  EnterCriticalSection (cci_Internal_mutex_for_mutex_initialize.csp);
  if (mutex->csp != &mutex->cs)
    {
      /*
       * below assert means that lock without pthread_mutex_init
       * or PTHREAD_MUTEX_INITIALIZER
       */
      assert (mutex->csp == NULL);
      cci_mutex_init (mutex, NULL);
    }
  LeaveCriticalSection (cci_Internal_mutex_for_mutex_initialize.csp);

  EnterCriticalSection (mutex->csp);
}

typedef void (WINAPI * InitializeConditionVariable_t) (CONDITION_VARIABLE *);
typedef bool (WINAPI * SleepConditionVariableCS_t) (CONDITION_VARIABLE *,
						    CRITICAL_SECTION *,
						    DWORD dwMilliseconds);

typedef void (WINAPI * WakeAllConditionVariable_t) (CONDITION_VARIABLE *);
typedef void (WINAPI * WakeConditionVariable_t) (CONDITION_VARIABLE *);

InitializeConditionVariable_t fp_InitializeConditionVariable;
SleepConditionVariableCS_t fp_SleepConditionVariableCS;
WakeAllConditionVariable_t fp_WakeAllConditionVariable;
WakeConditionVariable_t fp_WakeConditionVariable;

static bool have_CONDITION_VARIABLE = false;

static void
check_CONDITION_VARIABLE (void)
{
  HMODULE kernel32 = GetModuleHandle ("kernel32");

  have_CONDITION_VARIABLE = true;
  fp_InitializeConditionVariable = (InitializeConditionVariable_t)
    GetProcAddress (kernel32, "InitializeConditionVariable");
  if (fp_InitializeConditionVariable == NULL)
    {
      have_CONDITION_VARIABLE = false;
      return;
    }

  fp_SleepConditionVariableCS = (SleepConditionVariableCS_t)
    GetProcAddress (kernel32, "SleepConditionVariableCS");
  fp_WakeAllConditionVariable = (WakeAllConditionVariable_t)
    GetProcAddress (kernel32, "WakeAllConditionVariable");
  fp_WakeConditionVariable = (WakeConditionVariable_t)
    GetProcAddress (kernel32, "WakeConditionVariable");
}

static int
timespec_to_msec (const struct timespec *abstime)
{
  int msec = 0;
  struct timeval tv;

  if (abstime == NULL)
    {
      return INFINITE;
    }

  cci_gettimeofday (&tv, NULL);
  msec = (abstime->tv_sec - tv.tv_sec) * 1000;
  msec += (abstime->tv_nsec / 1000 - tv.tv_usec) / 1000;

  if (msec < 0)
    {
      msec = 0;
    }

  return msec;
}

/*
 * old (pre-vista) windows does not support CONDITION_VARIABLES
 * so, we need below custom pthread_cond modules for them
 */
static int
win_custom_cond_init (cci_cond_t * cond, const cci_condattr_t * attr)
{
  cond->waiting = 0;
  InitializeCriticalSection (&cond->lock_waiting);

  cond->events[COND_SIGNAL] = CreateEvent (NULL, FALSE, FALSE, NULL);
  cond->events[COND_BROADCAST] = CreateEvent (NULL, TRUE, FALSE, NULL);
  cond->broadcast_block_event = CreateEvent (NULL, TRUE, TRUE, NULL);

  if (cond->events[COND_SIGNAL] == NULL ||
      cond->events[COND_BROADCAST] == NULL ||
      cond->broadcast_block_event == NULL)
    {
      return ENOMEM;
    }

  return 0;
}

static int
win_custom_cond_destroy (cci_cond_t * cond)
{
  DeleteCriticalSection (&cond->lock_waiting);

  if (CloseHandle (cond->events[COND_SIGNAL]) == 0 ||
      CloseHandle (cond->events[COND_BROADCAST]) == 0 ||
      CloseHandle (cond->broadcast_block_event) == 0)
    {
      return EINVAL;
    }

  return 0;
}

static int
win_custom_cond_timedwait (cci_cond_t * cond, cci_mutex_t * mutex,
			   struct timespec *abstime)
{
  int result;
  int msec;

  msec = timespec_to_msec (abstime);
  WaitForSingleObject (cond->broadcast_block_event, INFINITE);

  EnterCriticalSection (&cond->lock_waiting);
  cond->waiting++;
  LeaveCriticalSection (&cond->lock_waiting);

  LeaveCriticalSection (mutex->csp);
  result = WaitForMultipleObjects (2, cond->events, FALSE, msec);
  assert (result == WAIT_TIMEOUT || result <= 2);

  EnterCriticalSection (&cond->lock_waiting);
  cond->waiting--;

  if (cond->waiting == 0)
    {
      ResetEvent (cond->events[COND_BROADCAST]);
      SetEvent (cond->broadcast_block_event);
    }

  LeaveCriticalSection (&cond->lock_waiting);
  EnterCriticalSection (mutex->csp);

  return result == WAIT_TIMEOUT ? ETIMEDOUT : 0;
}

static int
win_custom_cond_signal (cci_cond_t * cond)
{
  EnterCriticalSection (&cond->lock_waiting);

  if (cond->waiting > 0)
    {
      SetEvent (cond->events[COND_SIGNAL]);
    }

  LeaveCriticalSection (&cond->lock_waiting);

  return 0;
}

static int
win_custom_cond_broadcast (cci_cond_t * cond)
{
  EnterCriticalSection (&cond->lock_waiting);

  if (cond->waiting > 0)
    {
      ResetEvent (cond->broadcast_block_event);
      SetEvent (cond->events[COND_BROADCAST]);
    }

  LeaveCriticalSection (&cond->lock_waiting);

  return 0;
}

int
cci_cond_init (cci_cond_t * cond, const cci_condattr_t * attr)
{
  static bool checked = false;
  if (checked == false)
    {
      check_CONDITION_VARIABLE ();
      checked = true;
    }

  if (have_CONDITION_VARIABLE)
    {
      fp_InitializeConditionVariable (&cond->native_cond);
      return 0;
    }

  return win_custom_cond_init (cond, attr);
}

int
cci_cond_destroy (cci_cond_t * cond)
{
  if (have_CONDITION_VARIABLE)
    {
      return 0;
    }

  return win_custom_cond_destroy (cond);
}

int
cci_cond_broadcast (cci_cond_t * cond)
{
  if (have_CONDITION_VARIABLE)
    {
      fp_WakeAllConditionVariable (&cond->native_cond);
      return 0;
    }

  return win_custom_cond_broadcast (cond);
}

int
cci_cond_signal (cci_cond_t * cond)
{
  if (have_CONDITION_VARIABLE)
    {
      fp_WakeConditionVariable (&cond->native_cond);
      return 0;
    }

  return win_custom_cond_signal (cond);
}

int
cci_cond_timedwait (cci_cond_t * cond, cci_mutex_t * mutex,
		    struct timespec *abstime)
{
  if (have_CONDITION_VARIABLE)
    {
      int msec = timespec_to_msec (abstime);
      if (fp_SleepConditionVariableCS (&cond->native_cond, mutex->csp, msec)
	  == false)
	{
	  return ETIMEDOUT;
	}

      return 0;
    }

  return win_custom_cond_timedwait (cond, mutex, abstime);
}

int
cci_cond_wait (cci_cond_t * cond, cci_mutex_t * mutex)
{
  return cci_cond_timedwait (cond, mutex, NULL);
}

/*
 * gettimeofday - Windows port of Unix gettimeofday()
 *   return: none
 *   tp(out): where time is stored
 *   tzp(in): unused
 */
int
cci_gettimeofday (struct timeval *tp, void *tzp)
{
#if 1				/* _ftime() version */
  struct _timeb tm;
  _ftime (&tm);
  tp->tv_sec = (long) tm.time;
  tp->tv_usec = (long) tm.millitm * 1000;
  return 0;
#else /* GetSystemTimeAsFileTime version */
  FILETIME ft;
  unsigned __int64 tmpres = 0;
  static int tzflag;

  GetSystemTimeAsFileTime (&ft);

  tmpres |= ft.dwHighDateTime;
  tmpres <<= 32;
  tmpres |= ft.dwLowDateTime;

  tmpres -= DELTA_EPOCH_IN_MICROSECS;

  tmpres /= 10;

  tv->tv_sec = (tmpres / 1000000UL);
  tv->tv_usec = (tmpres % 1000000UL);

  return 0;
#endif
}

#endif

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

#if __WORDSIZE == 32
#define GET_PTR_FOR_HASH(key) ((unsigned int)(key))
#else
#define GET_PTR_FOR_HASH(key) ((unsigned int)((key) && 0xFFFFFFFF))
#endif

/* constants for rehash */
static const float MHT_REHASH_TRESHOLD = 0.7f;
static const float MHT_REHASH_FACTOR = 1.3f;

/* options for mht_put() */
typedef enum mht_put_opt MHT_PUT_OPT;
enum mht_put_opt
{
  MHT_OPT_DEFAULT,
  MHT_OPT_KEEP_KEY,
  MHT_OPT_INSERT_ONLY
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

static unsigned int mht_5str_pseudo_key (void *key, int key_size);

static unsigned int mht_calculate_htsize (unsigned int ht_size);
static int mht_rehash (MHT_TABLE * ht);

static void *mht_put_internal (MHT_TABLE * ht, void *key, void *data,
			       MHT_PUT_OPT opt);

/*
 * mht_5str_pseudo_key() - hash string key into pseudo integer key
 * return: pseudo integer key
 * key(in): string key to hash
 * key_size(in): size of key or -1 when unknown
 *
 * Note: Based on hash method reported by Diniel J. Bernstein.
 */
static unsigned int
mht_5str_pseudo_key (void *key, int key_size)
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
 * mht_5strhash - hash a string key
 *   return: hash value
 *   key(in): key to hash
 *   ht_size(in): size of hash table
 *
 * Note: Based on hash method reported by Diniel J. Bernstein.
 */
unsigned int
mht_5strhash (void *key, unsigned int ht_size)
{
  return mht_5str_pseudo_key (key, -1) % ht_size;
}

/*
 * mht_strcasecmpeq - compare two string keys (ignoring case)
 *   return: 0 or 1 (key1 == key2)
 *   key1(in): pointer to string key1
 *   key2(in): pointer to string key2
 */
int
mht_strcasecmpeq (void *key1, void *key2)
{
  if ((strcasecmp ((char *) key1, (char *) key2)) == 0)
    {
      return TRUE;
    }
  return FALSE;
}

/*
 * mht_calculate_htsize - find a good hash table size
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
static const unsigned int mht_Primes[NPRIMES] = {
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
        (((dividend) == 0) ? 0 : ((dividend) - 1) / (divisor) + 1)

static unsigned int
mht_calculate_htsize (unsigned int ht_size)
{
  int left, right, middle;	/* indices for binary search */

  if (ht_size > mht_Primes[NPRIMES - 1])
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
	  if (ht_size == mht_Primes[middle])
	    {
	      break;
	    }
	  else if (ht_size > mht_Primes[middle])
	    {
	      left = middle + 1;
	    }
	  else
	    {
	      right = middle - 1;
	    }
	}
      /* If we didn't find the size, get the larger size and not the small one */
      if (ht_size > mht_Primes[middle] && middle < (NPRIMES - 1))
	{
	  middle++;
	}
      ht_size = mht_Primes[middle];
    }

  return ht_size;
}

/*
 * mht_create - create a hash table
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
MHT_TABLE *
mht_create (char *name, int est_size, HASH_FUNC hash_func, CMP_FUNC cmp_func)
{
  MHT_TABLE *ht;
  HENTRY_PTR *hvector;		/* Entries of hash table         */
  unsigned int ht_estsize;
  unsigned int size;

  assert (hash_func != NULL && cmp_func != NULL);

  /* Get a good number of entries for hash table */
  if (est_size <= 0)
    {
      est_size = 2;
    }

  ht_estsize = mht_calculate_htsize ((unsigned int) est_size);

  /* Allocate the header information for hash table */
  ht = (MHT_TABLE *) malloc (sizeof (MHT_TABLE));
  if (ht == NULL)
    {
      return NULL;
    }

  /* Allocate the hash table entry pointers */
  size = ht_estsize * sizeof (HENTRY_PTR);
  hvector = (HENTRY_PTR *) malloc (size);
  if (hvector == NULL)
    {
      free (ht);
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
  ht->rehash_at = (unsigned int) (ht_estsize * MHT_REHASH_TRESHOLD);
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
 * mht_rehash - rehash all entires of a hash table
 *   return: error code
 *   ht(in/out): hash table to rehash
 *
 * Note: Expand a hash table. All entries are rehashed onto a larger
 *       hash table of entries.
 */
static int
mht_rehash (MHT_TABLE * ht)
{
  HENTRY_PTR *new_hvector;	/* New entries of hash table       */
  HENTRY_PTR *hvector;		/* Entries of hash table           */
  HENTRY_PTR hentry;		/* A hash table entry. linked list */
  HENTRY_PTR next_hentry = NULL;	/* Next element in linked list     */
  float rehash_factor;
  unsigned int hash;
  unsigned int est_size;
  unsigned int size;
  unsigned int i;

  /* Find an estimated size for hash table entries */
  rehash_factor = 1.0f + ((float) ht->ncollisions / (float) ht->nentries);
  if (MHT_REHASH_FACTOR > rehash_factor)
    {
      est_size = (unsigned int) (ht->size * MHT_REHASH_FACTOR);
    }
  else
    {
      est_size = (unsigned int) (ht->size * rehash_factor);
    }

  est_size = mht_calculate_htsize (est_size);

  /* Allocate a new vector to keep the estimated hash entries */
  size = est_size * sizeof (HENTRY_PTR);
  new_hvector = (HENTRY_PTR *) malloc (size);
  if (new_hvector == NULL)
    {
      return CCI_ER_NO_MORE_MEMORY;
    }

  /* Initialize all entries */
  memset (new_hvector, 0x00, size);

  /* Now rehash the current entries onto the vector of hash entries table */
  for (ht->ncollisions = 0, hvector = ht->table, i = 0; i < ht->size;
       hvector++, i++)
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
  free (ht->table);

  ht->table = new_hvector;
  ht->size = est_size;
  ht->rehash_at = (int) (est_size * MHT_REHASH_TRESHOLD);

  return CCI_ER_NO_ERROR;
}

/*
 * mht_destroy - destroy a hash table
 *   return: void
 *   ht(in/out): hash table
 *
 * Note: ht is set as a side effect
 */
void
mht_destroy (MHT_TABLE * ht, bool free_key, bool free_data)
{
  HENTRY_PTR *hvector;
  HENTRY_PTR hentry;
  HENTRY_PTR next_hentry = NULL;
  unsigned int i;

  assert (ht != NULL);

  for (hvector = ht->table, i = 0; i < ht->size; hvector++, i++)
    {
      /* Go over each linked list */
      for (hentry = *hvector; hentry != NULL; hentry = next_hentry)
	{
	  next_hentry = hentry->next;
	  if (free_key)
	    free (hentry->key);
	  if (free_data)
	    free (hentry->data);
	  free (hentry);
	}
    }

  free (ht->table);
  free (ht);
}

/*
 * mht_get - find data associated with key
 *   return: the data associated with the key, or NULL if not found
 *   ht(in): hash table
 *   key(in): hashing key
 *
 * Note: Find the entry in hash table whose key is the value of the given key
 */
void *
mht_get (MHT_TABLE * ht, void *key)
{
  unsigned int hash;
  HENTRY_PTR hentry;

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
 * mht_put_internal - internal function for mht_put(), mht_put_new(), and
 *                    mht_put_data();
 *                    insert an entry associating key with data
 *   return: Returns key if insertion was OK, otherwise, it returns NULL
 *   ht(in/out): hash table (set as a side effect)
 *   key(in): hashing key
 *   data(in): data associated with hashing key
 *   opt(in): options;
 *            MHT_OPT_DEFAULT - change data and the key as given of the hash
 *                              entry; replace the old entry with the new one
 *                              if there is an entry with the same key
 *            MHT_OPT_KEEP_KEY - change data but the key of the hash entry
 *            MHT_OPT_INSERT_ONLY - do not replace the existing hash entry
 *                                  even if there is an etnry with the same key
 */
static void *
mht_put_internal (MHT_TABLE * ht, void *key, void *data, MHT_PUT_OPT opt)
{
  unsigned int hash;
  HENTRY_PTR hentry;

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

  if (!(opt & MHT_OPT_INSERT_ONLY))
    {
      /* Now search the linked list.. Is there any entry with the given key ? */
      for (hentry = ht->table[hash]; hentry != NULL; hentry = hentry->next)
	{
	  if (hentry->key == key || (*ht->cmp_func) (hentry->key, key))
	    {
	      /* Replace the old data with the new one */
	      if (!(opt & MHT_OPT_KEEP_KEY))
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
      hentry = (HENTRY_PTR) malloc (sizeof (HENTRY));
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
      if (mht_rehash (ht) < 0)
	{
	  return NULL;
	}
    }

  return key;
}

/*
 * mht_put - insert an entry associating key with data
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
mht_put (MHT_TABLE * ht, void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put_internal (ht, key, data, MHT_OPT_DEFAULT);
}

void *
mht_put_data (MHT_TABLE * ht, void *key, void *data)
{
  assert (ht != NULL && key != NULL);
  return mht_put_internal (ht, key, data, MHT_OPT_KEEP_KEY);
}
