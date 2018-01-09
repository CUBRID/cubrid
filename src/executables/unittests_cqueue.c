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
 * unittest_cqueue.c : unit tests for latch free circular queue 
 */

#include "porting.h"

#include <stdio.h>
#include <pthread.h>
#include <sys/time.h>
#include <assert.h>

#include "lock_free.h"
#include "thread.h"
#include "vacuum.h"

#define strlen(s1) ((int) strlen(s1))

LOCK_FREE_CIRCULAR_QUEUE *vacuum_Finished_job_queue = NULL;

void *test_circular_queue_consumer (void *param);
void *test_circular_queue_producer (void *param);

/* print function */
static struct timeval start_time;

static void
begin (const char *test_name)
{
#define MSG_LEN 60
  int i;

  printf ("Testing %s", test_name);
  for (i = 0; i < MSG_LEN - strlen (test_name); i++)
    {
      putchar (' ');
    }
  printf ("... \n");

  gettimeofday (&start_time, NULL);

#undef MSG_LEN
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


static int
fail (const char *message)
{
  printf (" %s: %s\n", "FAILED", message);
  assert (false);
  return ER_FAILED;
}

static volatile INT64 global_nconsumed;
static volatile INT64 global_nproduced;

/* thread entry functions */
void *
test_circular_queue_consumer (void *param)
{
  VACUUM_LOG_BLOCKID data;
  bool r;
  INT64 local_nconsumed;
  int n_tocons = *((int *) param);

  while ((int) global_nconsumed < n_tocons)
    {
      r = lf_circular_queue_consume (vacuum_Finished_job_queue, &data);
      if (r)
	{
	  local_nconsumed = ATOMIC_INC_64 (&global_nconsumed, 1);
	  if (local_nconsumed % 100000 == 0)
	    {
	      printf (" Consumed %ld entries \n", local_nconsumed);
	    }
	}
    }

  pthread_exit ((void *) NO_ERROR);
}

void *
test_circular_queue_producer (void *param)
{
  VACUUM_LOG_BLOCKID data;
  bool r;
  INT64 local_nproduced;
  int ntoprod = *((int *) param);

  while ((int) global_nproduced < ntoprod)
    {
      data = 0;
      r = lf_circular_queue_produce (vacuum_Finished_job_queue, &data);
      if (r)
	{
	  local_nproduced = ATOMIC_INC_64 (&global_nproduced, 1);
	  if (local_nproduced % 100000 == 0)
	    {
	      printf (" Produced %ld entries \n", local_nproduced);
	    }
	}

      /* need some delay */
      thread_sleep (10);
    }

  pthread_exit ((void *) NO_ERROR);
}

#define VACUUM_FINISHED_JOB_QUEUE_CAPACITY 2048

/* test functions */
static int
test_cqueue (int num_consumers, int num_producers)
{
#define MAX_THREADS 64
#define NOPS 1000000		/* 1M */
  pthread_t threads[MAX_THREADS];
  int i;
  int n_toprod;
  int n_tocons;

  /* initialization */
  if (MAX_THREADS < num_consumers + num_producers)
    {
      return fail ("too many threads");
    }

  begin ("one consumer / 50 producers");

  global_nconsumed = 0;
  global_nproduced = 0;

  n_toprod = NOPS;
  n_tocons = NOPS;

  vacuum_Finished_job_queue =
    lf_circular_queue_create (VACUUM_FINISHED_JOB_QUEUE_CAPACITY, sizeof (VACUUM_LOG_BLOCKID));
  if (vacuum_Finished_job_queue == NULL)
    {
      return fail ("circular queue create fail");
    }

  for (i = 0; i < num_producers; i++)
    {
      /* fork producers first */
      if (pthread_create (&threads[i], NULL, test_circular_queue_producer, &n_toprod) != NO_ERROR)
	{
	  return fail ("thread create");
	}
    }

  for (i = 0; i < num_consumers; i++)
    {
      /* fork consumers later */
      if (pthread_create (&threads[num_producers + i], NULL, test_circular_queue_consumer, &n_tocons) != NO_ERROR)
	{
	  return fail ("thread create");
	}
    }

  for (i = 0; i < num_consumers + num_producers; i++)
    {
      void *retval;

      pthread_join (threads[i], &retval);
      if (retval != NO_ERROR)
	{
	  return fail ("thread proc error");
	}
    }

  /* results */

  /* destroy */
  lf_circular_queue_destroy (vacuum_Finished_job_queue);

  return success ();

#undef NOPS
#undef MAX_THREADS
}

/* program entry */
int
main (int argc, char **argv)
{
  int num_producers, num_consumers;

  /* test_cubrid_area */

  printf ("============================================================\n");
  printf ("Test one consumer / 50 producers entry:\n");

  num_producers = 50;
  num_consumers = 1;

  if (test_cqueue (num_consumers, num_producers) != NO_ERROR)
    {
      goto fail;
    }

  /* all ok */
  return 0;

fail:
  printf ("Unit tests failed!\n");
  return ER_FAILED;
}
