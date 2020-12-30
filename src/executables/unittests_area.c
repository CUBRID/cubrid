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
 * unittest_area.c : unit tests for area manager
 */

#include "dbtype_def.h"
#include "db_set.h"
#include "porting.h"
#include "lock_free.h"
#include "object_domain.h"
#include "set_object.h"

#include <stdio.h>
#include <pthread.h>
#include <sys/time.h>
#include <assert.h>

#define strlen(s1) ((int) strlen(s1))

#undef SERVER_MODE
/* suppress SERVER_MODE while including client module headers */
#include "class_object.h"
#include "object_template.h"
#define SERVER_MODE

/* areate_create info */
typedef struct area_create_info AREA_CREATE_INFO;
struct area_create_info
{
  const char *name;		/* area name */
  int entry_size;		/* element size */
  int alloc_cnt;		/* alloc count */
};

void *test_area_proc (void *param);
void *test_area_proc_1 (void *param);
void *test_area_proc_2 (void *param);

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
test_area_proc (void *param)
{
#define NOPS	  1000000	/* 1M */

  AREA *area_p = (AREA *) param;
  void *entry = NULL;
  int i, error;

  for (i = 0; i < NOPS; i++)
    {
      if (i % 2 == 0)
	{
	  entry = area_alloc (area_p);
	  if (entry == NULL)
	    {
	      pthread_exit ((void *) ER_FAILED);
	    }
	}
      else
	{
	  error = area_free (area_p, (void *) entry);
	  if (error != NO_ERROR)
	    {
	      pthread_exit ((void *) ER_FAILED);
	    }
	}
    }

  pthread_exit ((void *) NO_ERROR);

#undef NOPS
}

void *
test_area_proc_1 (void *param)
{
#define NOPS	  1000000	/* 1M */
#define NCACHES   32

  AREA *area_p = (AREA *) param;
  void *entry[NCACHES];
  int idx, i, error;


  for (idx = 0; idx < NCACHES; idx++)
    {
      entry[idx] = NULL;
    }

  idx = 0;
  for (i = 0; i < NOPS; i++)
    {
      if (entry[idx] != NULL)
	{
	  error = area_free (area_p, (void *) entry[idx]);
	  if (error != NO_ERROR)
	    {
	      pthread_exit ((void *) ER_FAILED);
	    }
	  entry[idx] = NULL;
	}

      entry[idx] = area_alloc (area_p);
      if (entry[idx] == NULL)
	{
	  pthread_exit ((void *) ER_FAILED);
	}
      idx++;
      if (idx >= NCACHES)
	{
	  idx = 0;
	}
    }

  for (i = 0; i < NCACHES; i++)
    {
      if (entry[idx] != NULL)
	{
	  error = area_free (area_p, (void *) entry[idx]);
	  if (error != NO_ERROR)
	    {
	      pthread_exit ((void *) ER_FAILED);
	    }
	  entry[idx] = NULL;
	}
      idx++;
      if (idx >= NCACHES)
	{
	  idx = 0;
	}
    }

  pthread_exit ((void *) NO_ERROR);

#undef NCACHES
#undef NOPS
}

void *
test_area_proc_2 (void *param)
{
#define NOPS	  1000000	/* 1M */
#define NCACHES   500

  AREA *area_p = (AREA *) param;
  void *entry[NCACHES];
  int idx, i, error;


  for (idx = 0; idx < NCACHES; idx++)
    {
      entry[idx] = NULL;
    }

  for (i = 0; i < NOPS; i++)
    {
      idx = rand () % NCACHES;
      if (entry[idx] != NULL)
	{
	  error = area_free (area_p, (void *) entry[idx]);
	  if (error != NO_ERROR)
	    {
	      pthread_exit ((void *) ER_FAILED);
	    }
	  entry[idx] = NULL;
	}

      entry[idx] = area_alloc (area_p);
      if (entry[idx] == NULL)
	{
	  pthread_exit ((void *) ER_FAILED);
	}
    }

  for (idx = 0; idx < NCACHES; idx++)
    {
      if (entry[idx] != NULL)
	{
	  error = area_free (area_p, (void *) entry[idx]);
	  if (error != NO_ERROR)
	    {
	      pthread_exit ((void *) ER_FAILED);
	    }
	  entry[idx] = NULL;
	}
    }

  pthread_exit ((void *) NO_ERROR);

#undef NCACHES
#undef NOPS
}


/* test functions */
static int
test_area (AREA_CREATE_INFO * info, int nthreads, void *(*proc) (void *))
{
#define MAX_THREADS 64
  AREA *area = NULL;
  pthread_t threads[MAX_THREADS];
  char msg[256];
  int i;

  assert (info != NULL);
  sprintf (msg, "%s(size:%d, count:%d), %d threads", info->name, info->entry_size, info->alloc_cnt, nthreads);
  begin (msg);

  /* initialization */
  if (nthreads > MAX_THREADS)
    {
      return fail ("too many threads");
    }

  /* initialization */
  area_init ();

  area = area_create (info->name, info->entry_size, info->alloc_cnt);
  if (area == NULL)
    {
      return fail ("area create fail");
    }

  /* multithreaded test */
  for (i = 0; i < nthreads; i++)
    {
      if (pthread_create (&threads[i], NULL, proc, (void *) area) != NO_ERROR)
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
    AREA_BLOCKSET_LIST *blockset;
    AREA_BLOCK *block;
    int i, j, blockset_cnt = 0, block_cnt = 0, chunk_count;
    for (blockset = area->blockset_list; blockset != NULL; blockset = blockset->next)
      {
	for (i = 0; i < blockset->used_count; i++)
	  {
	    block = blockset->items[i];
	    assert (block != NULL);

	    chunk_count = CEIL_PTVDIV (block->bitmap.entry_count, LF_BITFIELD_WORD_SIZE);

	    for (j = 0; j < chunk_count; j++)
	      {
		if (block->bitmap.bitfield[j])
		  {
		    return fail ("check bitmap status");
		  }
	      }

	    block_cnt++;
	  }
	blockset_cnt++;
      }
    printf (" Used %3d blocks(%2d blocksets). ", block_cnt, blockset_cnt);
  }

  /* destory */
  area_destroy (area);
  area_final ();

  return success ();

#undef MAX_THREADS
}

/* program entry */
int
main (int argc, char **argv)
{
  int i, j;

  /* test_cubrid_area */
  {
    AREA_CREATE_INFO cubrid_infos[] = {
      {"Schema templates", sizeof (SM_TEMPLATE), 4}
      ,
      {"Domains", sizeof (TP_DOMAIN), 1024}
      ,
      {"Value containers", sizeof (DB_VALUE), 1024}
      ,
      {"Object templates", sizeof (OBJ_TEMPLATE), 32}
      ,
      {"Assignment templates", sizeof (OBJ_TEMPASSIGN), 64}
      ,
      {"Set references", sizeof (DB_COLLECTION), 1024}
      ,
      {"Set objects", sizeof (COL), 1024}
      ,
      {"Object list links", sizeof (DB_OBJLIST), 4096}
    };

    printf ("============================================================\n");
    printf ("Test simple get/free entry:\n");
    for (j = 0; j < (int) DIM (cubrid_infos); j++)
      {
	for (i = 1; i <= 64; i *= 2)
	  {
	    if (test_area (&cubrid_infos[j], i, test_area_proc) != NO_ERROR)
	      {
		goto fail;
	      }
	  }
      }

    printf ("============================================================\n");
    printf ("Test get/free entry with cache(32):\n");
    for (j = 0; j < (int) DIM (cubrid_infos); j++)
      {
	for (i = 1; i <= 64; i *= 2)
	  {
	    if (test_area (&cubrid_infos[j], i, test_area_proc_1) != NO_ERROR)
	      {
		goto fail;
	      }
	  }
      }

    printf ("============================================================\n");
    printf ("Test get/free entry with cache(500), random access:\n");
    for (j = 0; j < (int) DIM (cubrid_infos); j++)
      {
	for (i = 1; i <= 64; i *= 2)
	  {
	    if (test_area (&cubrid_infos[j], i, test_area_proc_2) != NO_ERROR)
	      {
		goto fail;
	      }
	  }
      }
  }

  /* test different alloc count */
  {
    AREA_CREATE_INFO diff_count_infos[] = {
      {"size 1", 50, 32}
      ,
      {"size 2", 50, 32 * 2}
      ,
      {"size 4", 50, 32 * 4}
      ,
      {"size 8", 50, 32 * 8}
      ,
      {"size 16", 50, 32 * 16}
      ,
      {"size 32", 50, 32 * 32}
      ,
      {"size 64", 50, 32 * 64}
      ,
      {"size 128", 50, 32 * 128}
    };

    printf ("============================================================\n");
    printf ("Test simple get/free entry with different alloc count:\n");
    for (i = 16; i <= 64; i *= 2)
      {
	for (j = 0; j < (int) DIM (diff_count_infos); j++)
	  {
	    if (test_area (&diff_count_infos[j], i, test_area_proc) != NO_ERROR)
	      {
		goto fail;
	      }
	  }
      }
  }


  /* all ok */
  return 0;

fail:
  printf ("Unit tests failed!\n");
  return ER_FAILED;
}
