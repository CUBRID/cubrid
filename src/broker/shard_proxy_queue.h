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
 * shard_proxy_queue.h - 
 *
 */

#ifndef _SHARD_PROXY_QUEUE_H_
#define _SHARD_PROXY_QUEUE_H_

#ident "$Id$"

#if !defined(WINDOWS)
#include <pthread.h>
#endif /* !WINDOWS */

typedef struct t_shard_queue_ent T_SHARD_QUEUE_ENT;
struct t_shard_queue_ent
{
  T_SHARD_QUEUE_ENT *next;

  void *v;
};

typedef struct t_shard_queue T_SHARD_QUEUE;
struct t_shard_queue
{
  pthread_mutex_t lock;		/* further use */
  pthread_cond_t cond;		/* further use */

  T_SHARD_QUEUE_ENT *head;
  T_SHARD_QUEUE_ENT *tail;
};

typedef struct t_shard_cqueue T_SHARD_CQUEUE;
struct t_shard_cqueue
{
  int size;
  int count;
  int front;
  int rear;

  void **ent;
};

extern int shard_queue_enqueue (T_SHARD_QUEUE * q, void *v);
extern void *shard_queue_dequeue (T_SHARD_QUEUE * q);
extern void *shard_queue_dequeue_nonblocking (T_SHARD_QUEUE * q);
extern void *shard_queue_dequeue_blocking (T_SHARD_QUEUE * q);
extern int shard_queue_initialize (T_SHARD_QUEUE * q);
extern void shard_queue_destroy (T_SHARD_QUEUE * q);

extern int shard_cqueue_enqueue (T_SHARD_CQUEUE * cq, void *e);
extern void *shard_cqueue_dequeue (T_SHARD_CQUEUE * cq);
extern int shard_cqueue_initialize (T_SHARD_CQUEUE * cq, int size);
extern void shard_cqueue_destroy (T_SHARD_CQUEUE * cq);

#endif /* _SHARD_PROXY_QUEUE_H_ */
