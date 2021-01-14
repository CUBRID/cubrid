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
 * shard_proxy_queue.h -
 *
 */

#ifndef _SHARD_PROXY_QUEUE_H_
#define _SHARD_PROXY_QUEUE_H_

#ident "$Id$"

#if !defined(WINDOWS)
#include <pthread.h>
#endif /* !WINDOWS */

typedef int (*SHARD_COMP_FN) (const void *arg1, const void *arg2);

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
extern int shard_queue_ordered_enqueue (T_SHARD_QUEUE * q, void *v, SHARD_COMP_FN comp_fn);
extern void *shard_queue_dequeue (T_SHARD_QUEUE * q);
extern void *shard_queue_peek_value (T_SHARD_QUEUE * q);
#if 0				/* not implemented yet */
extern void *shard_queue_dequeue_nonblocking (T_SHARD_QUEUE * q);
extern void *shard_queue_dequeue_blocking (T_SHARD_QUEUE * q);
#endif
extern int shard_queue_initialize (T_SHARD_QUEUE * q);
extern void shard_queue_destroy (T_SHARD_QUEUE * q);

extern int shard_cqueue_enqueue (T_SHARD_CQUEUE * cq, void *e);
extern void *shard_cqueue_dequeue (T_SHARD_CQUEUE * cq);

extern int shard_cqueue_initialize (T_SHARD_CQUEUE * cq, int size);
extern void shard_cqueue_destroy (T_SHARD_CQUEUE * cq);

#endif /* _SHARD_PROXY_QUEUE_H_ */
