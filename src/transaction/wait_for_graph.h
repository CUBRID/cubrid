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
 * wait_for_graph.h - Header file for WFG
 *
 */

#ifndef _WAIT_FOR_GRAPH_H_
#define _WAIT_FOR_GRAPH_H_

#ident "$Id$"

#if defined(ENABLE_UNUSED_FUNCTION)
#include "thread_compat.hpp"

typedef enum
{
  WFG_CYCLE_YES_PRUNE,		/* Cycles were found. There may be more not listed */
  WFG_CYCLE_YES,		/* Cycles were found. All the cycles are listed */
  WFG_CYCLE_NO,			/* There are not cycles */
  WFG_CYCLE_ERROR		/* An error was found during the cycle detection */
} WFG_CYCLE_CASE;

/* structure of cycle which is obtained by wfg_detect_cycle() below */
typedef struct wfg_waiter WFG_WAITER;
struct wfg_waiter
{
  int tran_index;
  int (*cycle_fun) (int tran_index, void *args);
  void *args;			/* Arguments to be passed to cycle_fun */
};

typedef struct wfg_listcycle WFG_CYCLE;
struct wfg_listcycle
{
  int num_trans;		/* # of transactions in the cycle */
  WFG_WAITER *waiters;		/* Waiters in the cycle */
  struct wfg_listcycle *next;	/* pointer to next cycle */
};

extern int wfg_alloc_nodes (THREAD_ENTRY * thread_p, const int num_trans);
extern int wfg_free_nodes (THREAD_ENTRY * thread_p);
extern int wfg_insert_out_edges (THREAD_ENTRY * thread_p, const int wtran_index, int num_holders,
				 const int *htran_indices, int (*cycle_resolution_fn) (int tran_index, void *args),
				 void *args);
extern int wfg_remove_out_edges (THREAD_ENTRY * thread_p, const int waiter_tran_index, const int num_holders,
				 const int *htran_indices_p);
extern int wfg_get_status (int *num_edges_p, int *num_waiters_p);
extern int wfg_detect_cycle (THREAD_ENTRY * thread_p, WFG_CYCLE_CASE * cycle_case, WFG_CYCLE ** list_cycles_p);
extern int wfg_free_cycle (WFG_CYCLE * list_cycles_p);
extern int wfg_is_waiting (THREAD_ENTRY * thread_p, const int tran_index);
extern int wfg_get_tran_entries (THREAD_ENTRY * thread_p, const int tran_index);

/* Transaction group interfaces */
extern int wfg_alloc_tran_group (THREAD_ENTRY * thread_p);
extern int wfg_insert_holder_tran_group (THREAD_ENTRY * thread_p, const int tran_group_index,
					 const int holder_tran_index);
extern int wfg_remove_holder_tran_group (THREAD_ENTRY * thread_p, const int tran_group_index,
					 const int holder_tran_index);
extern int wfg_insert_waiter_tran_group (THREAD_ENTRY * thread_p, const int tran_group_index,
					 const int waiter_tran_index, int (*cycle_resolution_fn) (int tran_index,
												  void *args),
					 void *args);
extern int wfg_remove_waiter_tran_group (THREAD_ENTRY * thread_p, const int tran_group_index,
					 const int waiter_tran_index);
extern int wfg_is_tran_group_waiting (THREAD_ENTRY * thread_p, const int tran_index);

extern int wfg_dump (THREAD_ENTRY * thread_p);
#endif
#endif /* _WAIT_FOR_GRAPH_H_ */
