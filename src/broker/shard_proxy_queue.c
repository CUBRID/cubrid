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
 * shard_proxy_queue.c -
 *               
 */

#ident "$Id$"


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "cas_common.h"
#include "shard_proxy_common.h"
#include "shard_proxy_queue.h"


int
shard_queue_enqueue (T_SHARD_QUEUE * q, void *v)
{
  T_SHARD_QUEUE_ENT *q_ent;

  assert (q);
  assert (v);

  q_ent = (T_SHARD_QUEUE_ENT *) malloc (sizeof (T_SHARD_QUEUE_ENT));
  if (q_ent)
    {
      q_ent->v = v;
      if (q->head == NULL)
	{
	  q->head = q->tail = q_ent;
	}
      else
	{
	  q->tail->next = q_ent;
	  q->tail = q_ent;
	}

      q_ent->next = NULL;

      return 0;			/* SUCCESS */
    }

  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Not enough virtual memory. "
	     "Failed to alloc shard queue entry. " "(errno:%d).", errno);

  return -1;			/* FAILED */
}

void *
shard_queue_dequeue (T_SHARD_QUEUE * q)
{
  T_SHARD_QUEUE_ENT *q_ent;
  void *ret;

  if (q->head == NULL)
    {
      return NULL;
    }

  q_ent = q->head;
  ret = q_ent->v;

  if (q->head == q->tail)
    {
      q->head = q->tail = NULL;
    }
  else
    {
      q->head = q->head->next;
    }

  FREE_MEM (q_ent);

  return ret;
}

int
shard_queue_initialize (T_SHARD_QUEUE * q)
{
  assert (q);

  q->head = NULL;
  q->tail = NULL;

  return 0;
}

void
shard_queue_destroy (T_SHARD_QUEUE * q)
{
  void *v;

  while ((v = shard_queue_dequeue (q)) != NULL)
    {
      FREE_MEM (v);
    }

  q->head = NULL;
  q->tail = NULL;

  return;
}

static bool
shard_cqueue_is_full (T_SHARD_CQUEUE * q)
{
  assert (q);

  return (q->count == q->size) ? true : false;
}

static bool
shard_cqueue_is_empty (T_SHARD_CQUEUE * q)
{
  assert (q);

  return (q->count == 0) ? true : false;
}


int
shard_cqueue_enqueue (T_SHARD_CQUEUE * q, void *e)
{
  assert (q);

  if (shard_cqueue_is_full (q))
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Queue is full. (q_size:%d, q_count:%d).", q->size,
		 q->count);
      /* FAILED */
      return -1;
    }

  q->ent[q->front] = e;

  q->count++;
  q->front = (q->front + 1) % q->size;

  /* SUCCESS */
  return 0;
}

void *
shard_cqueue_dequeue (T_SHARD_CQUEUE * q)
{
  void *e;

  assert (q);

  if (shard_cqueue_is_empty (q))
    {
      /* FAIELD */
      return NULL;
    }

  e = q->ent[q->rear];

  q->count--;
  q->rear = (q->rear + 1) % q->size;

  return e;
}

int
shard_cqueue_initialize (T_SHARD_CQUEUE * q, int size)
{
  assert (q);
  assert (q->ent == NULL);

  q->size = size;
  q->count = 0;
  q->front = 0;
  q->rear = 0;

  q->ent = (void **) malloc (sizeof (void *) * size);
  if (q->ent == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Not enough virtual memory. "
		 "Failed to alloc shard cqueue entry. "
		 "(errno:%d, size:%d).", errno, (size * sizeof (void *)));

      /* FAILED */
      return -1;
    }

  /* SUCCESS */
  return 0;
}

void
shard_cqueue_destroy (T_SHARD_CQUEUE * q)
{
  q->size = 0;
  q->count = 0;
  q->front = 0;
  q->rear = 0;
  FREE_MEM (q->ent);
}
