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
 * unittest_lf.c : unit tests for latch free primitives
 */

#include "porting.h"
#include "lock_free.h"
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>

#define strlen(s1) ((int) strlen(s1))

/* wait-free random number array */
#define RAND_BLOCKS	64
#define RAND_BLOCK_SIZE	1000000
#define RAND_SIZE	RAND_BLOCKS * RAND_BLOCK_SIZE
static int random_numbers[RAND_SIZE];

#define PTHREAD_ABORT_AND_EXIT(code) \
  do \
    { \
      int rc = (code); \
      abort (); \
      pthread_exit (&rc); \
    } \
  while (0)

static void
generate_random ()
{
  int i = 0;

  srand (time (NULL));

  for (i = 0; i < RAND_SIZE; i++)
    {
      random_numbers[i] = rand ();
    }
}

/* hash entry type definition */
typedef struct xentry XENTRY;
struct xentry
{
  XENTRY *next;			/* used by hash and freelist */
  XENTRY *stack;		/* used by freelist */
  UINT64 del_tran_id;		/* used by freelist */

  pthread_mutex_t mutex;	/* entry mutex (where applicable) */

  int key;
  unsigned long long int data;
};

/* entry manipulation functions */
static void *
xentry_alloc ()
{
  XENTRY *ptr = (XENTRY *) malloc (sizeof (XENTRY));
  if (ptr != NULL)
    {
      pthread_mutex_init (&ptr->mutex, NULL);
      ptr->data = 0;
    }
  return ptr;
}

static int
xentry_free (void *entry)
{
  pthread_mutex_destroy (&((XENTRY *) entry)->mutex);
  free (entry);
  return NO_ERROR;
}

static int
xentry_init (void *entry)
{
  if (entry != NULL)
    {
      ((XENTRY *) entry)->data = 0;
      return NO_ERROR;
    }
  else
    {
      return ER_FAILED;
    }
}

static int
xentry_uninit (void *entry)
{
  if (entry != NULL)
    {
      ((XENTRY *) entry)->data = -1;
      return NO_ERROR;
    }
  else
    {
      return ER_FAILED;
    }

}

static unsigned int
xentry_hash (void *key, int htsize)
{
  int *ikey = (int *) key;
  return (*ikey) % htsize;
}

static int
xentry_key_compare (void *k1, void *k2)
{
  int *ik1 = (int *) k1, *ik2 = (int *) k2;
  return !(*ik1 == *ik2);
}

static int
xentry_key_copy (void *src, void *dest)
{
  int *isrc = (int *) src, *idest = (int *) dest;

  *idest = *isrc;

  return NO_ERROR;
}

/* hash entry descriptors */
static LF_ENTRY_DESCRIPTOR xentry_desc = {
  /* signature */
  offsetof (XENTRY, stack),
  offsetof (XENTRY, next),
  offsetof (XENTRY, del_tran_id),
  offsetof (XENTRY, key),
  offsetof (XENTRY, mutex),

  /* mutex flags */
  LF_EM_NOT_USING_MUTEX,

  /* functions */
  xentry_alloc,
  xentry_free,
  xentry_init,
  xentry_uninit,
  xentry_key_copy,
  xentry_key_compare,
  xentry_hash,
  NULL
};

/* print function */
static struct timeval start_time;

static void
begin (const char *test_name)
{
#define MSG_LEN	  40
  int i;

  printf ("Testing %s", test_name);
  for (i = 0; i < MSG_LEN - strlen (test_name); i++)
    {
      putchar (' ');
    }
  printf ("...");

  gettimeofday (&start_time, NULL);

#undef MSG_LEN
}

static int
fail (const char *message)
{
  printf (" %s: %s\n", "FAILED", message);
  abort ();
  return ER_FAILED;
}

static int
success ()
{
  struct timeval end_time;
  long long int elapsed_msec = 0;

  gettimeofday (&end_time, NULL);

  elapsed_msec = (end_time.tv_usec - start_time.tv_usec) / 1000;
  elapsed_msec += (end_time.tv_sec - start_time.tv_sec) * 1000;

  printf (" %s [%9.3f sec]\n", "OK", (float) elapsed_msec / 1000.0f);
  return NO_ERROR;
}

/* thread entry functions */
void *test_freelist_proc (void *param);

void *
test_freelist_proc (void *param)
{
#define NOPS	  1000000	/* 1M */

  LF_FREELIST *freelist = (LF_FREELIST *) param;
  LF_TRAN_SYSTEM *ts = freelist->tran_system;
  LF_TRAN_ENTRY *te;
  XENTRY *entry = NULL;
  int i;

  te = lf_tran_request_entry (ts);
  if (te == NULL)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }

  for (i = 0; i < NOPS; i++)
    {
      lf_tran_start_with_mb (te, true);

      if (i % 2 == 0)
	{
	  entry = (XENTRY *) lf_freelist_claim (te, freelist);
	  if (entry == NULL)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	}
      else
	{
	  if (lf_freelist_retire (te, freelist, (void *) entry) != NO_ERROR)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	}

      lf_tran_end_with_mb (te);
    }

  lf_tran_return_entry (te);

  pthread_exit (NO_ERROR);

#undef NOPS
}

void *test_freelist_proc_local_tran (void *param);

void *
test_freelist_proc_local_tran (void *param)
{
#define NOPS	  1000000	/* 1M */

  LF_FREELIST *freelist = (LF_FREELIST *) param;
  LF_TRAN_SYSTEM *ts = freelist->tran_system;
  LF_TRAN_ENTRY *te;
  XENTRY *entry = NULL;
  int i;

  te = lf_tran_request_entry (ts);
  if (te == NULL)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }

  for (i = 0; i < NOPS; i++)
    {
      /* Test freelist without transaction */
      if (i % 2 == 0)
	{
	  entry = (XENTRY *) lf_freelist_claim (te, freelist);
	  if (entry == NULL)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	}
      else
	{
	  if (lf_freelist_retire (te, freelist, (void *) entry) != NO_ERROR)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	}
    }

  lf_tran_return_entry (te);

  pthread_exit (NO_ERROR);

#undef NOPS
}

void *test_hash_proc_1 (void *param);

void *
test_hash_proc_1 (void *param)
{
#define NOPS  1000000

  LF_HASH_TABLE *hash = (LF_HASH_TABLE *) param;
  LF_TRAN_SYSTEM *ts = hash->freelist->tran_system;
  LF_TRAN_ENTRY *te;
  XENTRY *entry;
  int i, rand_base, key;

  te = lf_tran_request_entry (ts);
  if (te == NULL)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }

  if (te->entry_idx >= RAND_BLOCKS || te->entry_idx < 0)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }
  else
    {
      rand_base = te->entry_idx * RAND_BLOCK_SIZE;
    }

  for (i = 0; i < NOPS; i++)
    {
      key = random_numbers[rand_base + i] % 1000;

      if (i % 10 < 5)
	{
	  entry = NULL;
	  if (lf_hash_find_or_insert (te, hash, &key, (void **) &entry, NULL) != NO_ERROR)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	  lf_tran_end_with_mb (te);
	}
      else
	{
	  if (lf_hash_delete (te, hash, &key, NULL) != NO_ERROR)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	}
    }

  lf_tran_return_entry (te);

  pthread_exit (NO_ERROR);

#undef NOPS
}

void *test_hash_proc_2 (void *param);

void *
test_hash_proc_2 (void *param)
{
#define NOPS  1000000

  LF_HASH_TABLE *hash = (LF_HASH_TABLE *) param;
  LF_TRAN_SYSTEM *ts = hash->freelist->tran_system;
  LF_TRAN_ENTRY *te;
  XENTRY *entry;
  int i, rand_base, key;

  te = lf_tran_request_entry (ts);
  if (te == NULL)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }

  if (te->entry_idx >= RAND_BLOCKS || te->entry_idx < 0)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }
  else
    {
      rand_base = te->entry_idx * RAND_BLOCK_SIZE;
    }

  for (i = 0; i < NOPS; i++)
    {
      key = random_numbers[rand_base + i] % 1000;

      if (i % 10 < 5)
	{
	  if (lf_hash_find_or_insert (te, hash, &key, (void **) &entry, NULL) != NO_ERROR)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	  if (entry == NULL)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }

	  if (te->locked_mutex != &entry->mutex)
	    {
	      abort ();
	    }
	  te->locked_mutex = NULL;
	  pthread_mutex_unlock (&entry->mutex);
	}
      else
	{
	  if (lf_hash_delete (te, hash, &key, NULL) != NO_ERROR)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	}

      assert (te->locked_mutex == NULL);
    }

  lf_tran_return_entry (te);

  pthread_exit (NO_ERROR);

#undef NOPS
}

static int del_op_count = -1;

void *test_hash_proc_3 (void *param);

void *
test_hash_proc_3 (void *param)
{
#define NOPS  1000000

  LF_HASH_TABLE *hash = (LF_HASH_TABLE *) param;
  LF_TRAN_SYSTEM *ts = hash->freelist->tran_system;
  LF_TRAN_ENTRY *te;
  XENTRY *entry;
  int i, rand_base, key, local_del_op_count = 0;

  te = lf_tran_request_entry (ts);
  if (te == NULL)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }

  if (te->entry_idx >= RAND_BLOCKS || te->entry_idx < 0)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }
  else
    {
      rand_base = te->entry_idx * RAND_BLOCK_SIZE;
    }

  for (i = 0; i < NOPS; i++)
    {
      key = random_numbers[rand_base + i] % 1000;

      if (lf_hash_find_or_insert (te, hash, &key, (void **) &entry, NULL) != NO_ERROR)
	{
	  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	}
      if (entry == NULL)
	{
	  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	}
      if (entry->key != key)
	{
	  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	}

      entry->data++;

      if (entry->data >= 10)
	{
	  int success = 0;

	  local_del_op_count += entry->data;
	  if (lf_hash_delete_already_locked (te, hash, &key, entry, &success) != NO_ERROR)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	  else if (!success)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	}
      else
	{
	  if (te->locked_mutex != &entry->mutex)
	    {
	      abort ();
	    }
	  te->locked_mutex = NULL;
	  pthread_mutex_unlock (&entry->mutex);
	}

      assert (te->locked_mutex == NULL);
    }

  lf_tran_return_entry (te);

  ATOMIC_INC_32 (&del_op_count, local_del_op_count);
  pthread_exit (NO_ERROR);

#undef NOPS
}

void *test_clear_proc_1 (void *param);

void *
test_clear_proc_1 (void *param)
{
#define NOPS  1000000

  LF_HASH_TABLE *hash = (LF_HASH_TABLE *) param;
  LF_TRAN_SYSTEM *ts = hash->freelist->tran_system;
  LF_TRAN_ENTRY *te;
  XENTRY *entry;
  int i, rand_base, key;

  te = lf_tran_request_entry (ts);
  if (te == NULL)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }

  if (te->entry_idx >= RAND_BLOCKS || te->entry_idx < 0)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }
  else
    {
      rand_base = te->entry_idx * RAND_BLOCK_SIZE;
    }

  for (i = 0; i < NOPS; i++)
    {
      key = random_numbers[rand_base + i] % 1000;
      key = i % 100;

      if (i % 1000 != 999)
	{
	  if (i % 10 < 8)
	    {
	      entry = NULL;
	      if (lf_hash_find_or_insert (te, hash, &key, (void **) &entry, NULL) != NO_ERROR)
		{
		  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
		}
	      lf_tran_end_with_mb (te);
	    }
	  else if (i % 1000 < 999)
	    {
	      if (lf_hash_delete (te, hash, &key, NULL) != NO_ERROR)
		{
		  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
		}
	    }
	}
      else
	{
	  lf_hash_clear (te, hash);
	}
    }

  lf_tran_return_entry (te);

  pthread_exit (NO_ERROR);

#undef NOPS
}

void *test_clear_proc_2 (void *param);

void *
test_clear_proc_2 (void *param)
{
#define NOPS  1000000

  LF_HASH_TABLE *hash = (LF_HASH_TABLE *) param;
  LF_TRAN_SYSTEM *ts = hash->freelist->tran_system;
  LF_TRAN_ENTRY *te;
  XENTRY *entry;
  int i, rand_base, key;

  te = lf_tran_request_entry (ts);
  if (te == NULL)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }

  if (te->entry_idx >= RAND_BLOCKS || te->entry_idx < 0)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }
  else
    {
      rand_base = te->entry_idx * RAND_BLOCK_SIZE;
    }

  for (i = 0; i < NOPS; i++)
    {
      key = random_numbers[rand_base + i] % 1000;

      if (i % 1000 < 999)
	{
	  if (i % 10 < 5)
	    {
	      if (lf_hash_find_or_insert (te, hash, &key, (void **) &entry, NULL) != NO_ERROR)
		{
		  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
		}
	      if (entry == NULL)
		{
		  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
		}


	      if (te->locked_mutex != &entry->mutex)
		{
		  abort ();
		}
	      te->locked_mutex = NULL;
	      pthread_mutex_unlock (&entry->mutex);
	    }
	  else
	    {
	      if (lf_hash_delete (te, hash, &key, NULL) != NO_ERROR)
		{
		  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
		}
	    }
	}
      else
	{
	  lf_hash_clear (te, hash);
	}
      assert (te->locked_mutex == NULL);
    }

  lf_tran_return_entry (te);

  pthread_exit (NO_ERROR);

#undef NOPS
}

void *test_clear_proc_3 (void *param);

void *
test_clear_proc_3 (void *param)
{
#define NOPS  1000000

  LF_HASH_TABLE *hash = (LF_HASH_TABLE *) param;
  LF_TRAN_SYSTEM *ts = hash->freelist->tran_system;
  LF_TRAN_ENTRY *te;
  XENTRY *entry;
  int i, rand_base, key;

  te = lf_tran_request_entry (ts);
  if (te == NULL)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }

  if (te->entry_idx >= RAND_BLOCKS || te->entry_idx < 0)
    {
      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
    }
  else
    {
      rand_base = te->entry_idx * RAND_BLOCK_SIZE;
    }

  for (i = 0; i < NOPS; i++)
    {
      key = random_numbers[rand_base + i] % 1000;

      if (i % 1000 == 999)
	{
	  lf_hash_clear (te, hash);
	  continue;
	}

      if (lf_hash_find_or_insert (te, hash, &key, (void **) &entry, NULL) != NO_ERROR)
	{
	  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	}
      if (entry == NULL)
	{
	  PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	}

      entry->data++;

      if (entry->data >= 10)
	{
	  int success = 0;

	  if (lf_hash_delete_already_locked (te, hash, &key, entry, &success) != NO_ERROR)
	    {
	      PTHREAD_ABORT_AND_EXIT (ER_FAILED);
	    }
	  else if (!success)
	    {
	      /* cleared in the meantime */
	      if (te->locked_mutex != &entry->mutex)
		{
		  abort ();
		}
	      te->locked_mutex = NULL;
	      pthread_mutex_unlock (&entry->mutex);
	    }
	}
      else
	{
	  if (te->locked_mutex != &entry->mutex)
	    {
	      abort ();
	    }
	  te->locked_mutex = NULL;
	  pthread_mutex_unlock (&entry->mutex);
	}

      assert (te->locked_mutex == NULL);
    }

  lf_tran_return_entry (te);

  pthread_exit (NO_ERROR);

#undef NOPS
}

/* test functions */
static int
test_freelist (LF_ENTRY_DESCRIPTOR * edesc, int nthreads, bool test_local_tran)
{
#define MAX_THREADS 64

  static LF_FREELIST freelist;
  static LF_TRAN_SYSTEM ts;
  pthread_t threads[MAX_THREADS];
  char msg[256];
  int i;

  sprintf (msg, "freelist (transaction=%s, %d threads)", test_local_tran ? "n" : "y", nthreads);
  begin (msg);

  /* initialization */
  if (nthreads > MAX_THREADS)
    {
      return fail ("too many threads");
    }

  if (lf_tran_system_init (&ts, nthreads) != NO_ERROR)
    {
      return fail ("transaction system init");
    }

  if (lf_freelist_init (&freelist, 100, 100, edesc, &ts) != NO_ERROR)
    {
      return fail ("freelist init");
    }

  /* multithreaded test */
  for (i = 0; i < nthreads; i++)
    {
      if (pthread_create (&threads[i], NULL, (test_local_tran ? test_freelist_proc_local_tran : test_freelist_proc),
			  (void *) &freelist) != NO_ERROR)
	{
	  return fail ("thread create");
	}
    }

  for (i = 0; i < nthreads; i++)
    {
      void *retval;

      pthread_join (threads[i], &retval);
      if (retval != NO_ERROR)
	{
	  return fail ("thread proc error");
	}
    }

  /* results */
  {
    volatile XENTRY *e, *a;
    volatile int active, retired, _a, _r, _t;

    a = (XENTRY *) VOLATILE_ACCESS (freelist.available, void *);

    _a = VOLATILE_ACCESS (freelist.available_cnt, int);
    _r = VOLATILE_ACCESS (freelist.retired_cnt, int);
    _t = VOLATILE_ACCESS (freelist.alloc_cnt, int);

    active = 0;
    retired = 0;
    for (e = (XENTRY *) a; e != NULL; e = e->stack)
      {
	active++;
      }
    for (i = 0; i < ts.entry_count; i++)
      {
	for (e = (XENTRY *) ts.entries[i].retired_list; e != NULL; e = e->stack)
	  {
	    retired++;
	  }
      }

    if ((_t - active - retired) != 0)
      {
	sprintf (msg, "leak problem (lost %d entries)", _t - active + retired);
	return fail (msg);
      }

    if ((active != _a) || (retired != _r))
      {
	sprintf (msg, "counting problem (%d != %d) or (%d != %d)", active, _a, retired, _r);
	return fail (msg);
      }
  }

  /* uninit */
  lf_freelist_destroy (&freelist);
  lf_tran_system_destroy (&ts);

  return success ();

#undef MAX_THREADS
}

static int
test_hash_table (LF_ENTRY_DESCRIPTOR * edesc, int nthreads, void *(*proc) (void *))
{
#define MAX_THREADS	  1024
#define HASH_SIZE	  113

  static LF_FREELIST freelist;
  static LF_TRAN_SYSTEM ts;
  static LF_HASH_TABLE hash;
  pthread_t threads[MAX_THREADS];
  char msg[256];
  int i;
  XENTRY *e = NULL;

  sprintf (msg, "hash (mutex=%s, %d threads)", edesc->using_mutex ? "y" : "n", nthreads);
  begin (msg);

  lf_reset_counters ();

  /* initialization */
  if (nthreads > MAX_THREADS)
    {
      return fail ("too many threads");
    }

  if (lf_tran_system_init (&ts, nthreads) != NO_ERROR)
    {
      return fail ("transaction system init");
    }

  if (lf_freelist_init (&freelist, 100, 100, edesc, &ts) != NO_ERROR)
    {
      return fail ("freelist init");
    }

  if (lf_hash_init (&hash, &freelist, HASH_SIZE, edesc) != NO_ERROR)
    {
      return fail ("hashtable init");
    }

  /* multithreaded test */
  for (i = 0; i < nthreads; i++)
    {
      if (pthread_create (&threads[i], NULL, proc, (void *) &hash) != NO_ERROR)
	{
	  return fail ("thread create");
	}
    }

  for (i = 0; i < nthreads; i++)
    {
      void *retval;

      pthread_join (threads[i], &retval);
      if (retval != NO_ERROR)
	{
	  return fail ("thread proc error");
	}
    }

  for (i = 0; i < HASH_SIZE; i++)
    {
      for (e = (XENTRY *) hash.buckets[i]; e != NULL; e = e->next)
	{
	  if (edesc->f_hash (&e->key, HASH_SIZE) != (unsigned int) i)
	    {
	      sprintf (msg, "hash (%d) = %d != %d", e->key, edesc->f_hash (&e->key, HASH_SIZE), i);
	      return fail (msg);
	    }
	}
    }

  /* count operations */
  if (edesc->using_mutex)
    {
      int nondel_op_count = 0;

      for (i = 0; i < HASH_SIZE; i++)
	{
	  for (e = (XENTRY *) hash.buckets[i]; e != NULL; e = e->next)
	    {
	      nondel_op_count += e->data;
	    }
	}

      if (del_op_count != -1)
	{
	  /* we're counting delete ops */
	  if (nondel_op_count + del_op_count != nthreads * 1000000)
	    {
	      sprintf (msg, "op count fail (%d + %d != %d)", nondel_op_count, del_op_count, nthreads * 1000000);
	      return fail (msg);
	    }
	}
    }

  /* count entries */
  {
    XENTRY *e;
    int ecount = 0, acount = 0, rcount = 0;

    for (i = 0; i < HASH_SIZE; i++)
      {
	for (e = (XENTRY *) hash.buckets[i]; e != NULL; e = e->next)
	  {
	    ecount++;
	  }
      }

    for (e = (XENTRY *) freelist.available; e != NULL; e = e->stack)
      {
	acount++;
      }
    for (i = 0; i < ts.entry_count; i++)
      {
	for (e = (XENTRY *) ts.entries[i].retired_list; e != NULL; e = e->stack)
	  {
	    rcount++;
	  }
	if (ts.entries[i].temp_entry != NULL)
	  {
	    ecount++;
	  }
      }

    if (freelist.available_cnt != acount)
      {
	sprintf (msg, "counting fail (available %d != %d)", freelist.available_cnt, acount);
	return fail (msg);
      }
    if (freelist.retired_cnt != rcount)
      {
	sprintf (msg, "counting fail (retired %d != %d)", freelist.retired_cnt, rcount);
	return fail (msg);
      }

    if (ecount + freelist.available_cnt + freelist.retired_cnt != freelist.alloc_cnt)
      {
	sprintf (msg, "leak check fail (%d + %d + %d = %d != %d)", ecount, freelist.available_cnt, freelist.retired_cnt,
		 ecount + freelist.available_cnt + freelist.retired_cnt, freelist.alloc_cnt);
	return fail (msg);
      }
  }

  /* uninit */
  lf_hash_destroy (&hash);
  lf_freelist_destroy (&freelist);
  lf_tran_system_destroy (&ts);

  return success ();
#undef HASH_SIZE
#undef MAX_THREADS
}

static int
test_hash_iterator ()
{
#define HASH_SIZE 200
#define HASH_POPULATION HASH_SIZE * 5
#define NUM_THREADS 16

  static LF_FREELIST freelist;
  static LF_TRAN_SYSTEM ts;
  static LF_HASH_TABLE hash;
  static LF_TRAN_ENTRY *te;
  int i;

  begin ("hash table iterator");

  /* initialization */
  if (lf_tran_system_init (&ts, NUM_THREADS) != NO_ERROR)
    {
      return fail ("transaction system init");
    }

  te = lf_tran_request_entry (&ts);
  if (te == NULL)
    {
      return fail ("failed to fetch tran entry");
    }

  if (lf_freelist_init (&freelist, 100, 100, &xentry_desc, &ts) != NO_ERROR)
    {
      return fail ("freelist init");
    }

  if (lf_hash_init (&hash, &freelist, HASH_SIZE, &xentry_desc) != NO_ERROR)
    {
      return fail ("hashtable init");
    }

  /* static (single threaded) test */
  for (i = 0; i < HASH_POPULATION; i++)
    {
      XENTRY *entry;

      if (lf_hash_find_or_insert (te, &hash, &i, (void **) &entry, NULL) != NO_ERROR)
	{
	  return fail ("insert error");
	}
      else if (entry == NULL)
	{
	  return fail ("null insert error");
	}
      else
	{
	  entry->data = i;
	  /* end transaction */
	  lf_tran_end_with_mb (te);
	}
    }

  {
    LF_HASH_TABLE_ITERATOR it;
    XENTRY *curr = NULL;
    char msg[256];
    int sum = 0;

    lf_hash_create_iterator (&it, te, &hash);
    for (curr = (XENTRY *) lf_hash_iterate (&it); curr != NULL; curr = (XENTRY *) lf_hash_iterate (&it))
      {
	sum += ((XENTRY *) curr)->data;
      }

    if (sum != ((HASH_POPULATION - 1) * HASH_POPULATION) / 2)
      {
	sprintf (msg, "counting error (%d != %d)", sum, (HASH_POPULATION - 1) * HASH_POPULATION / 2);
	return fail (msg);
      }
  }

  /* reset */
  lf_hash_clear (te, &hash);

  /* multi-threaded test */
  /* TODO TODO TODO */

  /* uninit */
  lf_tran_return_entry (te);
  lf_hash_destroy (&hash);
  lf_freelist_destroy (&freelist);
  lf_tran_system_destroy (&ts);
  return success ();
#undef HASH_SIZE
#undef HASH_POPULATION
#undef NUM_THREADS
}

/* program entry */
int
main (int argc, char **argv)
{
  int i;
  bool test_local_tran;

  /* generate random number array for non-blocking access */
  generate_random ();

  /* circular queue */
  /* temporarily disabled */
  /* for (i = 1; i <= 64; i *= 2) { if (test_circular_queue (i) != NO_ERROR) { goto fail; } } */

  /* freelist */
  test_local_tran = false;
  for (i = 1; i <= 64; i *= 2)
    {
      if (test_freelist (&xentry_desc, i, test_local_tran) != NO_ERROR)
	{
	  goto fail;
	}
    }

  test_local_tran = true;
  for (i = 1; i <= 64; i *= 2)
    {
      if (test_freelist (&xentry_desc, i, test_local_tran) != NO_ERROR)
	{
	  goto fail;
	}
    }

  /* test hash table iterator */
  if (test_hash_iterator () != NO_ERROR)
    {
      goto fail;
    }

  /* hash table - no entry mutex */
  for (i = 1; i <= 64; i *= 2)
    {
      if (test_hash_table (&xentry_desc, i, test_hash_proc_1) != NO_ERROR)
	{
	  goto fail;
	}
      if (test_hash_table (&xentry_desc, i, test_clear_proc_1) != NO_ERROR)
	{
	  goto fail;
	}
    }

  /* hash table - entry mutex, no lock between find and delete */
  xentry_desc.using_mutex = LF_EM_USING_MUTEX;
  for (i = 1; i <= 64; i *= 2)
    {
      if (test_hash_table (&xentry_desc, i, test_hash_proc_2) != NO_ERROR)
	{
	  goto fail;
	}
      if (test_hash_table (&xentry_desc, i, test_clear_proc_2) != NO_ERROR)
	{
	  goto fail;
	}

    }

  /* hash table - entry mutex, hold lock between find and delete */
  xentry_desc.using_mutex = LF_EM_USING_MUTEX;
  for (i = 1; i <= 64; i *= 2)
    {
      /* test_hash_proc_3 uses global del_op_count */
      del_op_count = 0;
      if (test_hash_table (&xentry_desc, i, test_hash_proc_3) != NO_ERROR)
	{
	  goto fail;
	}
      del_op_count = -1;
      if (test_hash_table (&xentry_desc, i, test_clear_proc_3) != NO_ERROR)
	{
	  goto fail;
	}

    }

  /* all ok */
  return 0;

fail:
  printf ("Unit tests failed!\n");
  return ER_FAILED;
}
