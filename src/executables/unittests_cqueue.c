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
 * unittest_area.c : unit tests for area manager
 */

#include "porting.h"

#include <stdio.h>
#include <pthread.h>
#include <sys/time.h>
#include <assert.h>

#include "lock_free.h"
#include "vacuum.h"

LOCK_FREE_CIRCULAR_QUEUE *vacuum_Finished_job_queue = NULL;

void *test_circular_queue_consumer (void *param);
void *test_circular_queue_producer (void *param);

/* print function */
static struct timeval start_time;

static void
begin (char *test_name)
{
#define MSG_LEN 60
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

/* thread entry functions */
void *
test_circular_queue_consumer (void *param)
{
#define NOPS	  1000000	/* 1M */
  VACUUM_LOG_BLOCKID data;
  bool r;

  while (1)
    {
      r = lf_circular_queue_consume (vacuum_Finished_job_queue, &data);
    }

  pthread_exit ((void *) NO_ERROR);

#undef NOPS
}

void *
test_circular_queue_producer (void *param)
{
#define NOPS	  1000000	/* 1M */
  VACUUM_LOG_BLOCKID data;
  bool r;

  while (1)
    {
      data = 0;
      r = lf_circular_queue_produce (vacuum_Finished_job_queue, &data);
/*
	  if (r == false)
	    {
	      abort ();
	      pthread_exit ((void *) ER_FAILED);
	    }
*/

      /* need some delay */
      thread_sleep (50);
    }

  pthread_exit ((void *) NO_ERROR);

#undef NOPS
}

#define VACUUM_FINISHED_JOB_QUEUE_CAPACITY 2048

/* test functions */
static int
test_cqueue (int num_consumers, int num_producers)
{
#define MAX_THREADS 64
  pthread_t threads[MAX_THREADS];
  int i;

  /* initialization */
  if (MAX_THREADS < num_consumers + num_producers)
    {
      return fail ("too many threads");
    }

  vacuum_Finished_job_queue =
    lf_circular_queue_create (VACUUM_FINISHED_JOB_QUEUE_CAPACITY, sizeof (VACUUM_LOG_BLOCKID));
  if (vacuum_Finished_job_queue == NULL)
    {
      return fail ("circular queue create fail");
    }

  for (i = 0; i < num_consumers; i++)
    {
      /* fork consumers first */
      if (pthread_create (&threads[i], NULL, test_circular_queue_consumer, NULL) != NO_ERROR)
	{
	  return fail ("thread create");
	}
    }

  for (i = 0; i < num_producers; i++)
    {
      if (pthread_create (&threads[num_consumers + i], NULL, test_circular_queue_producer, NULL) != NO_ERROR)
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

  /* destory */
  lf_circular_queue_destroy (vacuum_Finished_job_queue);

  return success ();

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
