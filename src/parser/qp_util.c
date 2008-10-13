/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * qp_util.c - ostk style memory buffer routines
 */

#ident "$Id$"

#include "config.h"

#include <string.h>
#include <limits.h>
#include <ctype.h>

#include "error_manager.h"
#include "qp_util.h"
#include "memory_manager_4.h"

int qp_Packing_er_code = NO_ERROR;

/* constants */
static const int PACKING_MMGR_CHUNK_SIZE = 1024;
static const int PACKING_MMGR_BLOCK_SIZE = 10;

static int packing_heap_num_slot = 0;
static unsigned int *packing_heap = NULL;
static int packing_level = 0;

static void pt_free_packing_buf (int slot);

/* pt_enter_packing_buf() - mark the beginning of another level of packing
 *   return: none									
 */
void
pt_enter_packing_buf (void)
{
  ++packing_level;
}

/* pt_alloc_packing_buf() - allocate space for packing
 *   return: pointer to the allocated space if all OK, NULL otherwise
 *   size(in): the amount of space to be allocated
 */
char *
pt_alloc_packing_buf (int size)
{
  char *res;
  unsigned int heap_id;
  int i;

  if (size <= 0)
    {
      return NULL;
    }

  if (packing_heap == NULL)
    {				/* first time */
      packing_heap_num_slot = PACKING_MMGR_BLOCK_SIZE;
      packing_heap = (unsigned int *) calloc (packing_heap_num_slot,
					      sizeof (unsigned int));
      if (packing_heap == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  PACKING_MMGR_BLOCK_SIZE * sizeof (unsigned int));
	  return NULL;
	}
    }
  else if (packing_heap_num_slot == packing_level - 1)
    {
      packing_heap_num_slot += PACKING_MMGR_BLOCK_SIZE;

      packing_heap = (unsigned int *) realloc (packing_heap,
					       packing_heap_num_slot
					       * sizeof (unsigned int));
      if (packing_heap == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
		  ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  PACKING_MMGR_BLOCK_SIZE * sizeof (unsigned int));
	  return NULL;
	}

      for (i = 0; i < PACKING_MMGR_BLOCK_SIZE; i++)
	{
	  packing_heap[packing_heap_num_slot - i - 1] = 0;
	}
    }

  heap_id = packing_heap[packing_level - 1];
  if (heap_id <= 0)
    {
      heap_id = db_create_ostk_heap (PACKING_MMGR_CHUNK_SIZE);
      packing_heap[packing_level - 1] = heap_id;
    }
  if (heap_id <= 0)
    {
      /* make sure an error is set, one way or another */
      er_set (ER_ERROR_SEVERITY, __FILE__, __LINE__,
	      ER_OUT_OF_VIRTUAL_MEMORY, 1, PACKING_MMGR_CHUNK_SIZE);
      res = NULL;
    }
  else
    {
      res = db_ostk_alloc (heap_id, size);
    }

  if (res == NULL)
    {
      qp_Packing_er_code = -1;
    }

  return res;
}

/* pt_free_packing_buf() - free packing space
 *   return: none
 *   slot(in): index of the packing space
 */
static void
pt_free_packing_buf (int slot)
{
  if (packing_heap && slot >= 0 && packing_heap[slot])
    {
      db_destroy_ostk_heap (packing_heap[slot]);
      packing_heap[slot] = 0;
    }
}

/* pt_exit_packing_buf() - mark the end of another level of packing
 *   return: none									
 */
void
pt_exit_packing_buf (void)
{
  --packing_level;
  pt_free_packing_buf (packing_level);
}

/* pt_final_packing_buf() - free all resources for packing
 *   return: none                                                                       
 */
void
pt_final_packing_buf (void)
{
  int i;

  for (i = 0; i < packing_level; i++)
    {
      pt_free_packing_buf (i);
    }

  free_and_init (packing_heap);
  packing_level = packing_heap_num_slot = 0;
}
