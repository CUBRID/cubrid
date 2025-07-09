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
 * wait_for_graph.c - management of Wait-For-Graph
 */

#ident "$Id$"

#if defined(ENABLE_UNUSED_FUNCTION)
#include "config.h"

#include <stddef.h>
#include <assert.h>

#include "error_manager.h"
#include "memory_alloc.h"
#include "wait_for_graph.h"
#include "critical_section.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Prune the number of found cycles in a cycle group */
static const int WFG_PRUNE_CYCLES_IN_CYCLE_GROUP = 10;
static const int WFG_MAX_CYCLES_TO_REPORT = 100;

/* status of a node in WFG */
typedef enum
{
  WFG_NOT_VISITED,
  WFG_ON_STACK,
  WFG_OFF_STACK,
  WFG_RE_ON_STACK,
  WFG_ON_TG_CYCLE
} WFG_STACK_STATUS;

/* structure of an edge in WFG */
typedef struct wfg_edge WFG_EDGE;
struct wfg_edge
{
  int waiter_tran_index;	/* index of waiter transaction */
  int holder_tran_index;	/* index of holder transaction */
  struct wfg_edge *next_holder_edge_p;	/* pointer to next holder */
  struct wfg_edge *next_waiter_edge_p;	/* pointer to next waiter */
};

/* structure of a node in WFG */
typedef struct wfg_node WFG_NODE;
struct wfg_node
{
  WFG_STACK_STATUS status;
  int cycle_group_no;		/* Group no in a cycle */
  /*
   * Fun to call to solve a cycle. If NULL, the transaction will be aborted.
   * Assumption a transaction can be waiting for many transaction,
   * but it can only be waiting in one place.
   */
  int (*cycle_fun) (int tran_index, void *args);
  void *args;			/* Arguments to be passed to cycle_fun */
  WFG_EDGE *first_holder_edge_p;	/* pointer to first holder */
  WFG_EDGE *last_holder_edge_p;	/* pointer to last holder */
  WFG_EDGE *first_waiter_edge_p;	/* pointer to first waiter */
  WFG_EDGE *last_waiter_edge_p;	/* pointer to last waiter */
};

/* WFG stack to implement non-recursive DFS */
typedef struct wfg_stack WFG_STACK;
struct wfg_stack
{
  int wait_tran_index;		/* current wait node */
  WFG_EDGE *current_holder_edge_p;	/* current holder edge */
};

/* structure to maintain transaction index list for TG table */
typedef struct wfg_trans_list WFG_TRANS_LIST;
struct wfg_trans_list
{
  int tran_index;		/* transaction index */
  struct wfg_trans_list *next;	/* next transaction */
};

/* structure of transaction group table entry */
typedef struct wfg_tran_group WFG_TRAN_GROUP;
struct wfg_tran_group
{
  int num_holders;
  int num_waiters;
  WFG_TRANS_LIST *holder_tran_list_p;	/* transaction group */
  WFG_TRANS_LIST *waiter_tran_list_p;	/* trans waiting for the TG */
};

static WFG_NODE *wfg_Nodes = NULL;	/* ptr to WFG nodes */
static int wfg_Total_nodes = 0;	/* # of nodes */
static int wfg_Total_edges = 0;	/* # of edges */
static int wfg_Total_waiters = 0;	/* # of waiters */

/* Transaction group table */
static WFG_TRAN_GROUP *wfg_Tran_group = NULL;
static int wfg_Total_tran_groups = 0;

static int wfg_initialize_node (WFG_NODE * node_p);
static int wfg_free_group_list (void);

static int wfg_push_stack (WFG_STACK ** top_p, int node);
static int wfg_pop_stack (WFG_STACK ** top_p, WFG_STACK ** bottom_p);
static int wfg_internal_detect_cycle (THREAD_ENTRY * thread_p, WFG_CYCLE_CASE * cycle_case_p,
				      WFG_CYCLE ** list_cycles_p, const int max_cycles_in_cycle_group,
				      const int max_cycles);
static int wfg_detect_ordinary_cycle (THREAD_ENTRY * thread_p, WFG_CYCLE_CASE * cycle_case_p,
				      WFG_CYCLE ** list_cycles_p, const int max_cycles_in_group, const int max_cycles);
static int wfg_add_waiters_of_tg (int *smallest_onstack, int holder_node, int tg_index);
static int wfg_add_waiters_normal_wfg (int *smallest_onstack, int node_index);
static int wfg_get_all_waiting_and_add_waiter (bool * all_waiting, bool * add_waiter, int tg_index);
static WFG_CYCLE *wfg_detect_tran_group_cycle_internal (WFG_CYCLE_CASE * cycle_case_p, WFG_TRANS_LIST * w_tran_list_p,
							bool add_waiter, int tg_index, int num_tran_groups_holders);
static int wfg_detect_tran_group_cycle (THREAD_ENTRY * thread_p, WFG_CYCLE_CASE * cycle_case_p,
					WFG_CYCLE ** list_cycles);

static int wfg_dump_given_cycles (FILE * out_fp, WFG_CYCLE * list_cycles_p);
static void wfg_dump_holder_waiter (FILE * out_fp, int node_index);
static void wfg_dump_holder_waiter_of_tran_group (FILE * out_fp, int group_index);

#if defined(WFG_DEBUG)
static int wfg_valid_tran_index (const int holder_index, const int holder_tran_index, const int waiter_tran_index);
static int check_duplication_holders (const int holder_index, const int *holder_tran_indices, const int num_holders,
				      const int waiter_tran_index);
static int wfg_check_insert_out_edges (const int waiter_tran_index, int num_holders, const int *holder_tran_indeces);
static int wfg_check_remove_out_edges (const int waiter_tran_index, int num_holders, const int *holder_tran_indeces);
#endif /* WFG_DEBUG */

static int wfg_allocate_edges (WFG_EDGE ** first_edge_p, WFG_EDGE ** last_edge_p, const int *holder_tran_indices,
			       const int num_holders, const int waiter_tran_index);
static int wfg_link_edge_holders_waiter_list (WFG_EDGE * first_edge_p, WFG_EDGE * last_edge_p,
					      const int waiter_tran_index);
static int wfg_remove_waiter_list_of_holder_edge (WFG_NODE * node_p, WFG_EDGE ** holder_p);

/*
 * TODO : M2, error check
 * wfg_push_stack : push operation on WFG stack
 *
 * return : NO_ERROR
 *
 *   top_p(IN/OUT) :
 *   node(IN)      :
 */
static int
wfg_push_stack (WFG_STACK ** top_p, int node)
{
  (*top_p)++;
  (*top_p)->wait_tran_index = node;
  (*top_p)->current_holder_edge_p = wfg_Nodes[node].first_holder_edge_p;

  return NO_ERROR;
}

/*
 * wfg_pop_stack : pop operation on WFG stack
 *
 * return : NO_ERROR
 *
 *   top_p(IN/OUT) :
 *   bottom(IN)    :
 */
static int
wfg_pop_stack (WFG_STACK ** top_p, WFG_STACK ** bottom_p)
{
  (*top_p)--;
  if ((*top_p) - (*bottom_p) < 0)
    {
      return ER_FAILED;
    }
  (*top_p)->current_holder_edge_p = (*top_p)->current_holder_edge_p->next_holder_edge_p;

  return NO_ERROR;
}

/*
 * wfg_initialize_node : Initialize WFG_NODE
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   node_p(out) :
 *
 * Note:
 */
static int
wfg_initialize_node (WFG_NODE * node_p)
{
  if (node_p == NULL)
    {
      return ER_FAILED;
    }

  node_p->cycle_group_no = -1;
  node_p->cycle_fun = NULL;
  node_p->args = NULL;
  node_p->first_holder_edge_p = NULL;
  node_p->last_holder_edge_p = NULL;
  node_p->first_waiter_edge_p = NULL;
  node_p->last_waiter_edge_p = NULL;

  return NO_ERROR;
}

/*
 * wfg_alloc_nodes : Initialize or expand WFG to have num_trans.
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   num_trans(IN) : number of transactions
 *
 * NOTE: num_trans should not be decreased in succeeding calls, otherwise,
 *       behavior undefined.
 */
int
wfg_alloc_nodes (THREAD_ENTRY * thread_p, const int num_trans)
{
  WFG_NODE *temp_node_p;	/* a temporary pointer to WFG nodes */
  int i;			/* loop counter */
  int error_code = NO_ERROR;

#if defined(WFG_DEBUG)
  if (num_trans < 0)
    {
      er_log_debug (ARG_FILE_LINE, "wfg_alloc: num_trans = %d should NOT be" " negative. Will assume 10\n" num_trans);
      num_trans = 10;
    }
#endif /* WFG_DEBUG */

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /*
   * allocate new nodes
   */
  if (wfg_Nodes == NULL)
    {
      temp_node_p = (WFG_NODE *) malloc (sizeof (WFG_NODE) * num_trans);
      if (temp_node_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WFG_NODE) * num_trans);
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto end;
	}
    }
  else
    {
      if (num_trans < wfg_Total_nodes)
	{
	  error_code = NO_ERROR;
	  goto end;
	}
      temp_node_p = (WFG_NODE *) realloc (wfg_Nodes, sizeof (WFG_NODE) * num_trans);
      if (temp_node_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WFG_NODE) * num_trans);
	  error_code = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto end;
	}
    }

  /* initialize newly allocated nodes */
  for (i = wfg_Total_nodes; i < num_trans; i++)
    {
      error_code = wfg_initialize_node (temp_node_p + i);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

  wfg_Total_nodes = num_trans;
  wfg_Nodes = temp_node_p;

end:
  csect_exit (thread_p, CSECT_WFG);
  return error_code;
}

/*
 * wfg_free_group_list :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 * Note:
 */
static int
wfg_free_group_list (void)
{
  WFG_TRANS_LIST *tran_list_p, *temp_p;	/* trans list pointer */
  int i;

  /* free transaction group list */
  for (i = 0; i < wfg_Total_tran_groups; i++)
    {
      for (tran_list_p = wfg_Tran_group[i].holder_tran_list_p; tran_list_p != NULL;)
	{			/* free holders */
	  temp_p = tran_list_p;
	  tran_list_p = tran_list_p->next;
	  free_and_init (temp_p);
	}
      for (tran_list_p = wfg_Tran_group[i].waiter_tran_list_p; tran_list_p != NULL;)
	{			/* free waiters */
	  temp_p = tran_list_p;
	  tran_list_p = tran_list_p->next;
	  free_and_init (temp_p);
	}
    }

  return NO_ERROR;
}

/*
 * wfg_free_nodes : Finish WFG module. The WFG area is freed.
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 */
int
wfg_free_nodes (THREAD_ENTRY * thread_p)
{
  WFG_EDGE *edge_p;		/* pointer to a WFG edge */
  void *temp_p;			/* temporary pointer */
  int i;			/* loop counter */
  int error_code = NO_ERROR;

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (wfg_Total_nodes > 0)
    {
      /* for each node */
      for (i = 0; i < wfg_Total_nodes; i++)
	{
	  /* for each edge */
	  for (edge_p = wfg_Nodes[i].first_holder_edge_p; edge_p != NULL;)
	    {
	      temp_p = edge_p;
	      edge_p = edge_p->next_holder_edge_p;
	      free_and_init (temp_p);
	    }
	}
      free_and_init (wfg_Nodes);
      wfg_Total_nodes = wfg_Total_edges = wfg_Total_waiters = 0;
    }

  /* free transaction group list */
  error_code = wfg_free_group_list ();

  csect_exit (thread_p, CSECT_WFG);
  return error_code;
}

#if defined(WFG_DEBUG)
static int
wfg_check_insert_out_edges (const int waiter_tran_index, int num_holders, const int *holder_tran_indeces)
{
  int i;			/* loop counter */
  int error_code = NO_ERROR;

  /*
   * Check for valid arguments
   */
  if (waiter_tran_index < 0 || waiter_tran_index > wfg_Total_nodes - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_insert_out_edges: value" " waiter_tran_index = %d should be between 0 and %d\n"
		    " ** OPERATION HAS BEEN IGNORED **\n", waiter_tran_index, wfg_Total_nodes - 1);
      error_code = ER_FAILED;
      goto end;
    }

  for (i = 0; i < num_holders; i++)
    {
      /* Verify for incorrect input */
      error_code = wfg_valid_tran_index (i, holder_tran_indices[i], waiter_tran_index);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}

      /* check duplication of holders */
      error_code = check_duplication_holders (i, holder_tran_indeces);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

end:
  return error_code;
}
#endif /* WFG_DEBUG */

/*
 * wfg_insert_out_edges : add edges from the node specified by waiter
 *                        to each node in holders.
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   waiter_tran_index(IN)   : index of transaction which is waiting
 *   num_holders(IN)         : # of transactions(holders) being waited for
 *   holder_tran_indices(IN) : array of holders
 *   cycle_resolution_fn(IN) : ptr to cycle resoultion function
 *   args(IN)                : arguments of cycle resolution function
 *
 * NOTE: indexes in waiter, holders should fall into the interval
 *       [0, num_trans of the most recent wfg_alloc_nodes()]
 *       Otherwise, behavior is undefined
 *
 */
int
wfg_insert_out_edges (THREAD_ENTRY * thread_p, const int waiter_tran_index, int num_holders,
		      const int *holder_tran_indeces, int (*cycle_resolution_fn) (int tran_index, void *args),
		      void *args)
{
  WFG_EDGE *first_edge_p;	/* pointer to the first of allocated edges */
  WFG_EDGE *last_edge_p;	/* pointer to the last of allocated edges */
  int error_code = NO_ERROR;

  if (num_holders < 0)
    {
      num_holders = 0;
    }

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

#if defined(WFG_DEBUG)
  error_code = wfg_check_insert_out_edges (waiter_tran_index, num_holders, holder_tran_indeces);
  if (error_code != NO_ERROR)
    {
      goto end;
    }
#endif /* WFG_DEBUG */


  /* allocate the edges */
  error_code = wfg_allocate_edges (&first_edge_p, &last_edge_p, holder_tran_indeces, num_holders, waiter_tran_index);
  if (error_code != NO_ERROR)
    {
      goto end;
    }
  /*
   * Save the function to call in the case of a cycle.
   */
  wfg_Nodes[waiter_tran_index].cycle_fun = cycle_resolution_fn;
  wfg_Nodes[waiter_tran_index].args = args;

  error_code = wfg_link_edge_holders_waiter_list (first_edge_p, last_edge_p, waiter_tran_index);
  if (error_code != NO_ERROR)
    {
      goto end;
    }

  wfg_Total_edges += num_holders;

end:
  csect_exit (thread_p, CSECT_WFG);

  return error_code;
}

#if defined(WFG_DEBUG)
/*
 * wfg_valid_tran_index :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   holder_index(IN)            :
 *   holder_transaction_index(IN) :
 *   waiter_transaction_index(IN) :
 */
static int
wfg_valid_tran_index (const int holder_index, const int holder_tran_index, const int waiter_tran_index)
{
  if (holder_tran_index < 0 || holder_tran_index > wfg_Total_nodes - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_insert_out_edges: value" " holder_tran_indices[%d] = %d should be between 0 and %d\n"
		    " ** OPERATION HAS BEEN IGNORED **\n", holder_index, holder_tran_index, wfg_Total_nodes - 1);

      return ER_FAILED;
    }
  if (holder_tran_index == waiter_tran_index)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_insert_out_edges: value" " holder_tran_indices[%d] = %d is equal to waiter_tran_index = %d\n"
		    " ** OPERATION HAS BEEN IGNORED **\n", holder_index, holder_tran_index, waiter_tran_index);

      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * check_duplication_holders :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   holder_index(IN)        :
 *   holder_tran_indices(IN) :
 *   num_holders(IN)         :
 *   waiter_tran_index(IN)   :
 *
 */
static int
check_duplication_holders (const int holder_index, const int *holder_tran_indices, const int num_holders,
			   const int waiter_tran_index)
{
  WFG_EDGE *edge_p;
  int i;

  for (i = holder_index + 1; i < num_holders; i++)
    {
      if (holder_tran_indices[holder_index] == holder_tran_indices[i])
	{
	  er_log_debug (ARG_FILE_LINE,
			"wfg_insert_outedges: value holder_tran_indices[%d] = %d is"
			" duplcated with holder_tran_indices[%d] = %d\n" "** OPERATION HAS BEEN IGNORED **\n",
			holder_index, holder_tran_indices[holder_index], i, holder_tran_indices[i]);
	  return ER_FAILED;
	}
    }

  for (edge_p = wfg_Nodes[waiter_tran_index].first_holder_edge_p; edge_p != NULL; edge_p = edge_p->next_holder_edge_p)
    {
      if (holder_tran_indices[holder_index] == edge_p->holder_tran_index)
	{
	  er_log_debug (ARG_FILE_LINE,
			"wfg_insert_outedges: value holder_tran_indices[%d] = %d" " is already a holders\n"
			" ** OPERATION HAS BEEN IGNORED **\n", holder_index, holder_tran_indices[holder_index]);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}
#endif /* WFG_DEBUG */

/*
 * wfg_allocate_edges  : Allocate and initialize edges to have num_holders
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   first_edge_p(OUT)      : pointer to the first of allocated edges
 *   last_edge_p(OUT)       : pointer to the last of allocated edges
 *   holder_tran_indices(IN): array of holders
 *   num_holders(IN)        : # of transactions(holders)
 *   waiter_tran_index(IN)  : index of transaction which is waiting
 *
 */
static int
wfg_allocate_edges (WFG_EDGE ** first_edge_p, WFG_EDGE ** last_edge_p, const int *holder_tran_indices,
		    const int num_holders, const int waiter_tran_index)
{
  WFG_EDGE *edge_p;
  WFG_EDGE *temp_p;
  int i;

  *first_edge_p = *last_edge_p = NULL;

  for (i = num_holders - 1; i >= 0; i--)
    {
      edge_p = (WFG_EDGE *) malloc (sizeof (WFG_EDGE));
      if (edge_p == NULL)
	{
	  /* Deallocate all edges and return a failure */
	  while (*first_edge_p != NULL)
	    {
	      temp_p = *first_edge_p;
	      *first_edge_p = (*first_edge_p)->next_holder_edge_p;
	      free_and_init (temp_p);
	    }

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WFG_EDGE));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      /*
       * Note that we are adding the edges in the reverse order to avoid
       * manupulating several pointers.
       */
      edge_p->waiter_tran_index = waiter_tran_index;
      edge_p->holder_tran_index = holder_tran_indices[i];
      edge_p->next_holder_edge_p = *first_edge_p;
      edge_p->next_waiter_edge_p = NULL;

      *first_edge_p = edge_p;
      if (*last_edge_p == NULL)
	{
	  *last_edge_p = *first_edge_p;
	}
    }

  return NO_ERROR;
}

/*
 * wfg_link_edge_holders_waiter_list : Link the list to the waiter as
 *                           its holders and link each edge to holders'
 *                           waiter list
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   first_edge_p(IN)      : pointer to the first of allocated edges
 *   last_edge_p(IN)       : pointer to the last of allocated edges
 *   waiter(IN)            : index of transaction which is waiting
 *
 */
static int
wfg_link_edge_holders_waiter_list (WFG_EDGE * first_edge_p, WFG_EDGE * last_edge_p, const int waiter)
{
  WFG_EDGE *edge_p;
  int holder;

  /*
   * Link the list to the waiter as its holders
   */
  if (first_edge_p != NULL)
    {
      if (wfg_Nodes[waiter].last_holder_edge_p == NULL)
	{
	  /* First holder */
	  wfg_Nodes[waiter].first_holder_edge_p = first_edge_p;
	  wfg_Total_waiters++;
	}
      else
	{
	  wfg_Nodes[waiter].last_holder_edge_p->next_holder_edge_p = first_edge_p;
	}
      wfg_Nodes[waiter].last_holder_edge_p = last_edge_p;
    }

  /*
   * Link each edge to holders' waiter list
   */
  for (edge_p = first_edge_p; edge_p; edge_p = edge_p->next_holder_edge_p)
    {
      holder = edge_p->holder_tran_index;
      if (wfg_Nodes[holder].last_waiter_edge_p == NULL)
	{
	  /* First waiter */
	  wfg_Nodes[holder].first_waiter_edge_p = edge_p;
	}
      else
	{
	  wfg_Nodes[holder].last_waiter_edge_p->next_waiter_edge_p = edge_p;
	}
      wfg_Nodes[holder].last_waiter_edge_p = edge_p;
    }

  return NO_ERROR;
}

#if defined(WFG_DEBUG)
static int
wfg_check_remove_out_edges (const int waiter_tran_index, const int num_holders, const int *holder_tran_indices_p)
{
  int error_code = NO_ERROR;
  int i;
  WFG_EDGE *edge_p;		/* An edge */

  /*
   * Check for valid arguments
   */
  if (waiter_tran_index < 0 || waiter_tran_index > wfg_Total_nodes - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_remove_outedges: value waiter = %d should"
		    " be between 0 and %d\n ** OPERATION HAS BEEN IGNORED **\n", waiter_tran_index,
		    wfg_Total_nodes - 1);

      error_code = ER_FAILED;
      goto end;
    }

  for (i = 0; i < num_holders; i++)
    {
      if (holder_tran_indices_p[i] < 0 || holder_tran_indices_p[i] > wfg_Total_nodes - 1)
	{
	  er_log_debug (ARG_FILE_LINE,
			"wfg_remove_outedges: value holder[%d] = %d"
			" should be between 0 and %d\n ** OPERATION HAS BEEN" " IGNORED **", i, htran_indices_p[i],
			wfg_Total_nodes - 1);
	  error_code = ER_FAILED;
	  goto end;
	}
      for (edge_p = wfg_Nodes[waiter_tran_index].first_holder_edge_p; edge_p != NULL;
	   edge_p = edge_p->next_holder_edge_p)
	{
	  if (holder_tran_indices_p[i] == edge_p->holder_tran_index)
	    {
	      er_log_debug (ARG_FILE_LINE,
			    "wfg_remove_outedges:" " value holder[%d] = %d is NOT holder of waiter = %d\n"
			    " ** THE HOLDER SKIPPED **", i, htran_indices_p[i], waiter_tran_index);
	    }
	}
    }

  return error_code;
}
#endif /* WFG_DEBUG */

/*
 * wfg_remove_waiter_list_of_holder_edge :
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   node_p(in/out):
 *   holder_p(in/out): pointer to prev. holder edge
 *
 */
static int
wfg_remove_waiter_list_of_holder_edge (WFG_NODE * node_p, WFG_EDGE ** holder_p)
{
  WFG_EDGE **waiter_p;		/* pointer to prev. waiter edge */

  for (waiter_p = &(node_p->first_waiter_edge_p); *waiter_p != NULL; waiter_p = &((*waiter_p)->next_waiter_edge_p))
    {
      if (*waiter_p == *holder_p)
	{
	  /* It has been found. Now remove it from the waiter list */
	  *waiter_p = (*waiter_p)->next_waiter_edge_p;
	  if (*waiter_p == NULL)
	    {
	      /* the last waiter of this holder edge */
	      if (node_p->first_waiter_edge_p == NULL)
		{
		  /* No more waiters */
		  node_p->last_waiter_edge_p = NULL;
		}
	      else
		{
		  /* There are still waiters */
		  node_p->last_waiter_edge_p =
		    (WFG_EDGE *) ((char *) waiter_p - offsetof (WFG_EDGE, next_waiter_edge_p));
		}
	    }
	  break;
	}
    }

  return NO_ERROR;
}

/*
 * wfg_remove_out_edges : remove edges from the node waiter_tran_index
 *              to each node in holders.
 *              If num_holders <= 0 or holders is NULL, it removes all
 *              outgoing edges of the node waiter_tran_index.
 *
 * returns : NO_ERROR if all OK, ER_ status otherwise
 *
 *   waiter_tran_index(IN)    : index of transaction which is waiting
 *   num_holders(IN)          : # of transactions(holders)
 *   holder_tran_indices_p(IN): array of holders
 *
 * NOTE: indexes in waiter, holders should fall into the interval
 *       [0, num_trans of the most recent wfg_alloc_nodes()]
 *       Otherwise, behavior is undefined
 *
 */
int
wfg_remove_out_edges (THREAD_ENTRY * thread_p, const int waiter_tran_index, const int num_holders,
		      const int *holder_tran_indices_p)
{
  int i;			/* loop counter */
  WFG_EDGE *edge_p;		/* An edge */
  WFG_EDGE **prev_holder_p;	/* pointer to prev. holder edge */
  int error_code = NO_ERROR;

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

#if defined(WFG_DEBUG)
  error_code = wfg_check_remove_out_edges (waiter_tran_index, num_holders, holder_tran_indices_p);
  if (error_code != NO_ERROR)
    {
      goto end;
    }
#endif /* WFG_DEBUG */
  for (prev_holder_p = &(wfg_Nodes[waiter_tran_index].first_holder_edge_p); *prev_holder_p != NULL;)
    {
      /* check if the edge is in the given edges */
      if (num_holders > 0 && holder_tran_indices_p != NULL)
	{
	  for (i = 0; i < num_holders; i++)
	    {
	      if (holder_tran_indices_p[i] == (*prev_holder_p)->holder_tran_index)
		{
		  break;
		}
	    }
	}
      else
	{
	  i = num_holders - 1;
	}

      if (i < num_holders)
	{
	  /*
	   * It was found, remove previous holder from the list of waiters
	   * and the holder list.
	   */

	  /* Remove from waiter list of the holder of edge */
	  error_code =
	    wfg_remove_waiter_list_of_holder_edge (&wfg_Nodes[(*prev_holder_p)->holder_tran_index], prev_holder_p);
	  if (error_code != NO_ERROR)
	    {
	      goto end;
	    }

	  /* Remove from holder list of the waiter */
	  edge_p = *prev_holder_p;
	  *prev_holder_p = (*prev_holder_p)->next_holder_edge_p;
	  free_and_init (edge_p);
	  wfg_Total_edges--;
	}
      else
	{
	  prev_holder_p = &((*prev_holder_p)->next_holder_edge_p);
	}
    }

  if (prev_holder_p == &(wfg_Nodes[waiter_tran_index].first_holder_edge_p))
    {
      /* all outgoing edges are removed */
      wfg_Nodes[waiter_tran_index].last_holder_edge_p = NULL;
      wfg_Total_waiters--;
    }
  else
    {
      wfg_Nodes[waiter_tran_index].last_holder_edge_p =
	(WFG_EDGE *) ((char *) prev_holder_p - offsetof (WFG_EDGE, next_holder_edge_p));
    }

end:
  csect_exit (thread_p, CSECT_WFG);
  return error_code;
}

/*
 * wfg_get_status : get statistic from WFG.
 *
 * returns : NO_ERROR if all OK, ER status otherwise
 *
 *   edges(OUT)   : pointer to room to store # of edges
 *   waiters(OUT) : pointer to room to store # of waiting trans
 *
 */
int
wfg_get_status (int *num_edges_p, int *num_waiters_p)
{
  assert (num_edges_p != NULL);
  assert (num_waiters_p != NULL);

  *num_edges_p = wfg_Total_edges;
  *num_waiters_p = wfg_Total_waiters;

  return NO_ERROR;
}

/*
 * wfg_detect_cycle : finds all elementary cycles in the WFG and
 *                    transaction groups.
 *
 * returns : NO_ERROR if all OK, ER status otherwise
 *
 *   cycle_case(OUT)       : cycle_case.. One of the following values:
 *                           WFG_CYCLE_YES_PRUNE
 *                           WFG_CYCLE_YES
 *                           WFG_CYCLE_NO
 *                           WFG_CYCLE_ERROR
 *   list_cycles_p(IN/OUT) : address to list of cycles
 *                         Cycles is set as a side effect to point to list of
 *                         cycles.
 *
 * NOTE: the caller should be responsible for freeing memory allocated to the
 *       cycles after usage. See wfg_free_cycle()
 */
int
wfg_detect_cycle (THREAD_ENTRY * thread_p, WFG_CYCLE_CASE * cycle_case, WFG_CYCLE ** list_cycles_p)
{
  /* LIMIT the number of cycles */
  return wfg_internal_detect_cycle (thread_p, cycle_case, list_cycles_p, WFG_PRUNE_CYCLES_IN_CYCLE_GROUP,
				    WFG_MAX_CYCLES_TO_REPORT);
}

/*
 * wfg_internal_detect_cycle() : finds all elementary cycles in the WFG and
 *                               transaction groups.
 *
 * returns : NO_ERROR if all OK, ER status otherwise
 *
 *   cycle_case(OUT)       : cycle_case.. One of the following values:
 *                           WFG_CYCLE_YES_PRUNE
 *                           WFG_CYCLE_YES
 *                           WFG_CYCLE_NO
 *                           WFG_CYCLE_ERROR
 *   list_cycles_p(IN/OUT) : address to list of cycles
 *                           Cycles is set as a side effect to point to list of
 *                           cycles.
 *   max_cycles_in_cycle_group(IN) : Prune the number of found cycles
 *                           in a cycle group
 *   max_cycles(IN)        : max cycles to report
 *
 */
static int
wfg_internal_detect_cycle (THREAD_ENTRY * thread_p, WFG_CYCLE_CASE * cycle_case_p, WFG_CYCLE ** list_cycles_p,
			   const int max_cycles_in_cycle_group, const int max_cycles)
{
  WFG_CYCLE **next_cycle_p;	/* address of pointer to next cycle */
  WFG_CYCLE *ordinary_cycles_p = NULL;	/* ordinary cycles */
  WFG_CYCLE *tran_group_cycles_p = NULL;	/* TG(transaction group) cycles */
  WFG_CYCLE_CASE cycle_tran_group_case;
  int error_code = NO_ERROR;

  *list_cycles_p = NULL;

  error_code =
    wfg_detect_ordinary_cycle (thread_p, cycle_case_p, &ordinary_cycles_p, max_cycles_in_cycle_group, max_cycles);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  if (ordinary_cycles_p != NULL)
    {
      *list_cycles_p = ordinary_cycles_p;
    }

  error_code = wfg_detect_tran_group_cycle (thread_p, &cycle_tran_group_case, &tran_group_cycles_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  if (tran_group_cycles_p != NULL)
    {
      /* Glue the lists */
      for (next_cycle_p = list_cycles_p; *next_cycle_p != NULL; next_cycle_p = &((*next_cycle_p)->next))
	{
	  ;			/* NO-OP */
	}
      *next_cycle_p = tran_group_cycles_p;
    }

  switch (*cycle_case_p)
    {
    case WFG_CYCLE_YES_PRUNE:
      break;

    case WFG_CYCLE_YES:
      if (cycle_tran_group_case == WFG_CYCLE_YES_PRUNE)
	{
	  *cycle_case_p = cycle_tran_group_case;
	}
      break;

    case WFG_CYCLE_NO:
      *cycle_case_p = cycle_tran_group_case;
      break;

    default:
      break;
    }

  return NO_ERROR;

error:

  /* free allocated cycles */
  if (ordinary_cycles_p != NULL)
    {
      wfg_free_cycle (ordinary_cycles_p);
    }

  if (tran_group_cycles_p != NULL)
    {
      wfg_free_cycle (tran_group_cycles_p);
    }

  *list_cycles_p = NULL;

  *cycle_case_p = WFG_CYCLE_ERROR;

  return ER_FAILED;
}

/*
 * wfg_free_cycle : free memory allocated to store cycles by wfg_detect_cycle().
 *
 * return : NO_ERROR if all OK, ER status otherwise
 *
 *  list_cycles(IN/OUT) : address to list of cycles
 *
 */
int
wfg_free_cycle (WFG_CYCLE * list_cycles_p)
{
  WFG_CYCLE *current_cycle_p;	/* temp pointer to a WFG_CYCLE node */
  WFG_CYCLE *temp_p;		/* temp variable for current_cycle_p */

  if (list_cycles_p != NULL)
    {
      for (current_cycle_p = list_cycles_p; current_cycle_p != NULL;)
	{
	  if (current_cycle_p->waiters != NULL)
	    {
	      free_and_init (current_cycle_p->waiters);
	    }

	  temp_p = current_cycle_p;
	  current_cycle_p = current_cycle_p->next;
	  free_and_init (temp_p);
	}
    }

  return NO_ERROR;
}

/*
 * wfg_print_given_cycles:
 *
 * return : NO_ERROR if all OK, ER status otherwise
 *
 *   out_fp(in) : out file
 *   list_cycles_p(IN) : address to list of cycles
 *
 */
static int
wfg_dump_given_cycles (FILE * out_fp, WFG_CYCLE * list_cycles_p)
{
  int i;
  WFG_CYCLE *current_cycle_p;

  fprintf (out_fp, "----------------- CYCLES ------------------\n");

  /*
   * There are deadlocks, we must select a victim for each cycle. We try
   * to break a cycle by timeing out a transaction whenever is possible.
   * In any other case, we select a victim for an unilaterally abort.
   */

  for (current_cycle_p = list_cycles_p; current_cycle_p != NULL; current_cycle_p = current_cycle_p->next)
    {
      fprintf (out_fp, "Cycle: ");
      for (i = 0; i < current_cycle_p->num_trans; i++)
	{
	  if (i > 0)
	    {
	      fprintf (out_fp, ", ");
	      if ((i % 10) == 0)
		{
		  fprintf (out_fp, "\n       ");
		}
	    }
	  fprintf (out_fp, "%d", current_cycle_p->waiters[i].tran_index);
	}
      fprintf (out_fp, "\n");
    }

  return NO_ERROR;
}

/*
 * wfg_dump_holder_waiter:
 *
 * return : NO_ERROR if all OK, ER status otherwise
 *
 *   out_fp(in) : out file
 *   node_index(in):
 *
 */
static void
wfg_dump_holder_waiter (FILE * out_fp, int node_index)
{
  WFG_EDGE *edge_p;

  /* Print holders of node */
  fprintf (out_fp, "\t holders = { ");
  for (edge_p = wfg_Nodes[node_index].first_holder_edge_p; edge_p != NULL; edge_p = edge_p->next_holder_edge_p)
    {
      fprintf (out_fp, "%03d ", edge_p->holder_tran_index);
    }
  fprintf (out_fp, "}\n");

  /* Print waiters of node */
  fprintf (out_fp, "\t\t waiters = { ");
  for (edge_p = wfg_Nodes[node_index].first_waiter_edge_p; edge_p != NULL; edge_p = edge_p->next_waiter_edge_p)
    {
      fprintf (out_fp, "%03d ", edge_p->waiter_tran_index);
    }
  fprintf (out_fp, "}\n");

  if (wfg_Nodes[node_index].last_holder_edge_p == NULL)
    {
      fprintf (out_fp, "\t\t last holder = null,");
    }
  else
    {
      fprintf (stdout, "\t\t last holder = %03d,", wfg_Nodes[node_index].last_holder_edge_p->holder_tran_index);
    }

  if (wfg_Nodes[node_index].last_waiter_edge_p == NULL)
    {
      fprintf (out_fp, "\t\t last waiter = null\n");
    }
  else
    {
      fprintf (out_fp, "\t\t last waiter = %03d\n", wfg_Nodes[node_index].last_waiter_edge_p->waiter_tran_index);
    }
}

/*
 * wfg_dump_holder_waiter_of_tran_group:
 *
 * return : NO_ERROR if all OK, ER status otherwise
 *
 *   out_fp(in) : out file
 *   group_index(in):
 *
 */
static void
wfg_dump_holder_waiter_of_tran_group (FILE * out_fp, int group_index)
{
  WFG_TRANS_LIST *tran_list_p;

  if (wfg_Tran_group[group_index].holder_tran_list_p)
    {
      /* Print holders of TG */
      fprintf (out_fp, "\t holders = { ");
      for (tran_list_p = wfg_Tran_group[group_index].holder_tran_list_p; tran_list_p != NULL;
	   tran_list_p = tran_list_p->next)
	{
	  fprintf (out_fp, "%d ", tran_list_p->tran_index);
	}
      fprintf (out_fp, "}\n");

      if (wfg_Tran_group[group_index].waiter_tran_list_p)
	{
	  /* Print waiters of TG */
	  fprintf (out_fp, "\t waiters = { ");
	  for (tran_list_p = wfg_Tran_group[group_index].waiter_tran_list_p; tran_list_p != NULL;
	       tran_list_p = tran_list_p->next)
	    {
	      fprintf (out_fp, "%d ", tran_list_p->tran_index);
	    }
	  fprintf (out_fp, "}\n");
	}
    }
}

/*
 * wfg_dump :
 *
 * return : NO_ERROR if all OK, ER status otherwise
 *
 */
int
wfg_dump (THREAD_ENTRY * thread_p)
{
  int i;
  WFG_CYCLE *cycles_p;
  WFG_CYCLE_CASE cycle_case;

  fprintf (stdout, "--------------- WFG contents --------------\n");
  fprintf (stdout, "total_nodes = %d, total_edges = %d, total_waiters = %d\n", wfg_Total_nodes, wfg_Total_edges,
	   wfg_Total_waiters);

  fprintf (stdout, "\n");
  fprintf (stdout, "---------- Ordinary WFG contents ----------\n");

  for (i = 0; i < wfg_Total_nodes; i++)
    {
      fprintf (stdout, "[node_%03d]:", i);
      wfg_dump_holder_waiter (stdout, i);
    }

  if (wfg_Total_tran_groups > 0)
    {
      fprintf (stdout, "\n");
      fprintf (stdout, "------------- WFG_TG contents -------------\n");

      for (i = 0; i < wfg_Total_tran_groups; i++)
	{
	  fprintf (stdout, "TG[%d]:\t Num_holders %d, Num_waiters %d\n", i, wfg_Tran_group[i].num_holders,
		   wfg_Tran_group[i].num_waiters);

	  wfg_dump_holder_waiter_of_tran_group (stdout, i);
	}
    }

  /* Dump all cycles that are currently involved in the system */
  if (wfg_internal_detect_cycle (thread_p, &cycle_case, &cycles_p, -1, -1) == NO_ERROR && cycle_case == WFG_CYCLE_YES)
    {
      fprintf (stdout, "\n");
      wfg_dump_given_cycles (stdout, cycles_p);
      wfg_free_cycle (cycles_p);
    }

  return NO_ERROR;
}

/*
 * wfg_alloc_tran_group :
 *
 * return : if success, non-negative Transaction Group entry index,
 *          otherwise, ER_FAILED  value
 *
 */
int
wfg_alloc_tran_group (THREAD_ENTRY * thread_p)
{
  WFG_TRAN_GROUP *temp_p;	/* temp_p pointer to new TG table */
  size_t bytes;			/* size of new TG table */
  int error_code = NO_ERROR;

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  bytes = sizeof (WFG_TRAN_GROUP) * (wfg_Total_tran_groups + 1);
  if (wfg_Total_tran_groups == 0)
    {
      temp_p = (WFG_TRAN_GROUP *) malloc (bytes);
    }
  else
    {
      temp_p = (WFG_TRAN_GROUP *) realloc (wfg_Tran_group, bytes);
    }

  if (temp_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, bytes);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  wfg_Tran_group = temp_p;

  /* initialize the newly allocated entry */
  wfg_Tran_group[wfg_Total_tran_groups].num_holders = 0;
  wfg_Tran_group[wfg_Total_tran_groups].num_waiters = 0;
  wfg_Tran_group[wfg_Total_tran_groups].holder_tran_list_p = NULL;
  wfg_Tran_group[wfg_Total_tran_groups].waiter_tran_list_p = NULL;

  wfg_Total_tran_groups++;

end:
  csect_exit (thread_p, CSECT_WFG);

  return (wfg_Total_tran_groups - 1);
}

/*
 * wfg_insert_holder_tran_group : register the tran_index as a holder
 *                        of the Transaction Group tran_group_index.
 *
 * return : NO_ERROR if all OK, ER status otherwise
 *
 *   tran_group_index(IN)  : Transaction Group entry index
 *   holder_tran_index(IN) : tran_index to be entered
 *
 * NOTE: the behavior on invalid tran_group_index and holder_tran_index,
 *       is undefined.
 *
 */
int
wfg_insert_holder_tran_group (THREAD_ENTRY * thread_p, const int tran_group_index, const int holder_tran_index)
{
  WFG_TRANS_LIST *tran_list_p;	/* temp trans list node pointer */
  int error_code = NO_ERROR;

  /* Create a node for the tran_index and insert it to the TG's holder list */
  tran_list_p = (WFG_TRANS_LIST *) malloc (sizeof (WFG_TRANS_LIST));
  if (tran_list_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WFG_TRANS_LIST));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      free_and_init (tran_list_p);
      return ER_FAILED;
    }

#if defined(WFG_DEBUG)
  if (tran_group_index < 0 || tran_group_index > wfg_Total_tran_groups - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_tg_insert_holder: value tg_index = %d" " should be between 0 and %d\n"
		    "** OPERATION HAS BEEN IGNORED **", tran_group_index, wfg_Total_tran_groups - 1);
      error_code = ER_FAILED;
      goto end;
    }

  if (holder_tran_index < 0 || holder_tran_index > wfg_Total_nodes - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_tg_insert_holder: value htran_index = %d" " should be between 0 and %d\n"
		    " ** OPERATION HAS BEEN IGNORED **", holder_tran_index, wfg_Total_nodes - 1);
      error_code = ER_FAILED;
      goto end;
    }

  for (tran_list_p = wfg_Tran_group[tran_group_index].holder_tran_list_p; tran_list_p != NULL;
       tran_list_p = tran_list_p->next)
    if (tran_list_p->tran_index == holder_tran_index)
      {
	er_log_debug (ARG_FILE_LINE,
		      "wfg_tg_insert_holder: value" " htran_index = %d is already in holder list\n"
		      " ** OPERATION HAS BEEN IGNORED **", tran_index);
	error_code = ER_FAILED;
	goto end;
      }
#endif /* WFG_DEBUG */

  tran_list_p->tran_index = holder_tran_index;
  tran_list_p->next = wfg_Tran_group[tran_group_index].holder_tran_list_p;
  wfg_Tran_group[tran_group_index].holder_tran_list_p = tran_list_p;
  wfg_Tran_group[tran_group_index].num_holders++;

#if defined(WFG_DEBUG)
end:
#endif /* WFG_DEBUG */

  csect_exit (thread_p, CSECT_WFG);

  return error_code;
}

/*
 * wfg_remove_holder_tran_group : delete holder_tran_index from the holder list
 *                        of Transaction Group tran_group_index
 *
 * return : NO_ERROR if all OK, ER status otherwise
 *
 *   tran_group_index(IN)  : Transaction Group entry index
 *   holder_tran_index(IN) : tran_index to be removed
 *
 * NOTE: the behavior on invalid tran_group_index and holder_tran_index,
 *       is undefined.
 *
 */
int
wfg_remove_holder_tran_group (THREAD_ENTRY * thread_p, const int tran_group_index, const int holder_tran_index)
{
  WFG_TRANS_LIST **tran_list_p;	/* transaction list node */
  WFG_TRANS_LIST *temp_p;
  int error_code = NO_ERROR;

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

#if defined(WFG_DEBUG)
  if (tran_group_index < 0 || tran_group_index > wfg_Total_tran_groups - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_tg_remove_holder: value tg_index = %d" " should be between 0 and %d\n"
		    " ** OPERATION HAS BEEN IGNORED **", tran_group_index, wfg_Total_tran_groups - 1);
      error_code = ER_FAILED;
      goto end;
    }

  if (holder_tran_index < 0 || holder_tran_index > wfg_Total_nodes - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_tg_remove_holder: value htran_index = %d" " should be between 0 and %d\n"
		    " ** OPERATION HAS BEEN IGNORED **", holder_tran_index, wfg_Total_nodes - 1);
      error_code = ER_FAILED;
      goto end;
    }

  for (temp_p = wfg_Tran_group[tran_group_index].holder_tran_list_p; temp_p != NULL; temp_p = temp_p->next)
    {
      if (temp_p->tran_index == holder_tran_index)
	{
	  er_log_debug (ARG_FILE_LINE,
			"wfg_tg_remove_holder: value" " htran_index = %d is NOT in holder list\n"
			" ** OPERATION HAS NO EFFECT **", holder_tran_index);
	  error_code = ER_FAILED;
	  goto end;
	}
    }
#endif /* WFG_DEBUG */

  if (wfg_Tran_group[tran_group_index].holder_tran_list_p != NULL)
    {
      for (tran_list_p = &wfg_Tran_group[tran_group_index].holder_tran_list_p; *tran_list_p != NULL;
	   tran_list_p = &((*tran_list_p)->next))
	{
	  if ((*tran_list_p)->tran_index == holder_tran_index)
	    {
	      /* Remove it, and change the pointer in the previous structure */
	      temp_p = *tran_list_p;
	      *tran_list_p = (*tran_list_p)->next;
	      free_and_init (temp_p);
	      wfg_Tran_group[tran_group_index].num_holders--;
	      break;
	    }
	}
    }

#if defined(WFG_DEBUG)
end:
#endif /* WFG_DEBUG */

  csect_exit (thread_p, CSECT_WFG);

  return error_code;
}

/*
 * wfg_insert_waiter_tran_group : register the tran_index as a waiter
 *                                of the Transaction Group tran_group_index.
 *
 * return : NO_ERROR if all OK, ER status otherwise
 *
 *   tran_group_index(IN)    : Transaction Group entry index
 *   waiter_tran_index(IN)   : tran_index to be entered
 *   cycle_resolution_fn(IN) :
 *   args(IN)                :
 *
 * NOTE: the behavior on invalid tg_index and tran_index, is undefined.
 *
 * The implementation of this function is almost identical as that of
 * wfg_insert_holder_tran_group(). The only difference is that this function
 * replaces holder by waiter.
 *
 */
int
wfg_insert_waiter_tran_group (THREAD_ENTRY * thread_p, const int tran_group_index, const int waiter_tran_index,
			      int (*cycle_resolution_fn) (int tran_index, void *args), void *args)
{
  WFG_TRANS_LIST *tran_list_p;	/* temp trans list node pointer */
  int error_code = NO_ERROR;

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

#if defined(WFG_DEBUG)
  if (tran_group_index < 0 || tran_group_index > wfg_Total_tran_groups - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_tg_insert_waiter: value tg_index = %d should"
		    " be between 0 and %d\n ** OPERATION HAS BEEN IGNORED **", tran_group_index,
		    wfg_Total_tran_groups - 1);
      error_code = ER_FAILED;
      goto end;
    }

  if (waiter_tran_index < 0 || waiter_tran_index > wfg_Total_nodes - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_tg_insert_waiter: value tran_index = %d" " should be between 0 and %d\n"
		    " ** OPERATION HAS BEEN IGNORED **", waiter_tran_index, wfg_Total_nodes - 1);
      error_code = ER_FAILED;
      goto end;
    }

  for (tran_list_p = wfg_Tran_group[tran_group_index].waiter_tran_list_p; tran_list_p != NULL;
       tran_list_p = tran_list_p->next)
    {
      if (tran_list_p->tran_index == waiter_tran_index)
	{
	  er_log_debug (ARG_FILE_LINE,
			"wfg_tg_insert_waiter: value tran_index = %d" " is already in waiters\n"
			" ** OPERATION HAS BEEN IGNORED **", waiter_tran_index);
	  error_code = ER_FAILED;
	  goto end;
	}
    }
#endif /* WFG_DEBUG */

  /*
   * allocate a node for the waiter_tran_index and insert it to the TG's waiter
   * list
   */
  tran_list_p = (WFG_TRANS_LIST *) malloc (sizeof (WFG_TRANS_LIST));
  if (tran_list_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WFG_TRANS_LIST));
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto end;
    }

  tran_list_p->tran_index = waiter_tran_index;
  tran_list_p->next = wfg_Tran_group[tran_group_index].waiter_tran_list_p;
  wfg_Tran_group[tran_group_index].waiter_tran_list_p = tran_list_p;
  wfg_Tran_group[tran_group_index].num_waiters++;

  /* Save the function to call in the case of a cycle. */
  wfg_Nodes[waiter_tran_index].cycle_fun = cycle_resolution_fn;
  wfg_Nodes[waiter_tran_index].args = args;

end:
  csect_exit (thread_p, CSECT_WFG);

  return error_code;
}

/*
 * wfg_remove_waiter_tran_group : delete waiter tran_index from the waiter list
 *                        of Transaction Group tran_group_index
 *
 * return : return : NO_ERROR if all OK, ER status otherwise
 *
 *   tg_index(IN)          : Transaction Group entry index
 *   waiter_tran_index(IN) : tran_index to be removed
 *
 * NOTE: the behavior on invalid tran_group_index and waiter_tran_index,
 *       is undefined.
 *
 * The implementation of this function is almost identical as that of
 * wfg_remove_holder_tran_group(). The only difference is that this function
 * replaces holder by waiter.
 *
 */
int
wfg_remove_waiter_tran_group (THREAD_ENTRY * thread_p, const int tran_group_index, const int waiter_tran_index)
{
  WFG_TRANS_LIST **tran_list_p;	/* tran_index list node */
  WFG_TRANS_LIST *temp_p;
  int error_code = NO_ERROR;

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

#if defined(WFG_DEBUG)
  if (tran_group_index < 0 || tran_group_index > wfg_Total_tran_groups - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_tg_remove_waiter: value tg_index = %d should"
		    " be between 0 and %d\n ** OPERATION HAS BEEN IGNORED **", tran_group_index,
		    wfg_Total_tran_groups - 1);
      error_code = ER_FAILED;
      goto end;
    }

  if (waiter_tran_index < 0 || waiter_tran_index > wfg_Total_nodes - 1)
    {
      er_log_debug (ARG_FILE_LINE,
		    "wfg_tg_remove_waiter: value tran_index = %d" " should be between 0 and %d\n"
		    " ** OPERATION HAS BEEN IGNORED **", waiter_tran_index, wfg_Total_nodes - 1);
      error_code = ER_FAILED;
      goto end;
    }

  for (tran_list_p = &wfg_Tran_group[tran_group_index].waiter_tran_list_p; tran_list_p != NULL;
       tran_list_p = &((*tran_list_p)->next))
    {
      if ((*tran_list_p)->tran_index == waiter_tran_index)
	{
	  er_log_debug (ARG_FILE_LINE,
			"wfg_tg_remove_waiter: value tran_index = %d"
			" is NOT in waiters\n ** OPERATION HAS NO EFFECT **", waiter_tran_index);
	  error_code = ER_FAILED;
	  goto end;
	}
    }
#endif /* WFG_DEBUG */

  for (tran_list_p = &wfg_Tran_group[tran_group_index].waiter_tran_list_p; tran_list_p != NULL;
       tran_list_p = &((*tran_list_p)->next))
    {
      if ((*tran_list_p)->tran_index == waiter_tran_index)
	{
	  /* Remove it */
	  temp_p = *tran_list_p;
	  *tran_list_p = (*tran_list_p)->next;
	  free_and_init (temp_p);
	  wfg_Tran_group[tran_group_index].num_waiters--;
	  break;
	}
    }

#if defined(WFG_DEBUG)
end:
#endif /* WFG_DEBUG */

  csect_exit (thread_p, CSECT_WFG);

  return error_code;
}

/*
 * wfg_detect_ordinary_cycle :finds elementary cycles in the ordinary WFG. I.e.,
 *              these cycles does not include TG related ones.
 *              The function may prune its search for finding more cycles
 *              when many has been found. This is done for space and time
 *              efficiency. The caller has the option to call again after
 *              cleaning some of the dependencies (e.g., aborting some of the
 *              transactions).
 *
 * returns : NO_ERROR if all OK, ER status otherwise
 *
 *   cycle_case(OUT)       : cycle_case.. One of the following values:
 *                           WFG_CYCLE_YES_PRUNE
 *                           WFG_CYCLE_YES
 *                           WFG_CYCLE_NO
 *                           WFG_CYCLE_ERROR
 *   list_cycles_p(IN/OUT) : address to list of cycles, list_cycles_p is set
 *                           as a side effect to point to list of cycles.
 *   max_cycles_in_group(IN) :
 *   max_cycles(IN)        :
 *
 * Note: the caller should be responsible for freeing memory allocated
 *	 to the cycles after usage. See wfg_free_cycle()
 */
static int
wfg_detect_ordinary_cycle (THREAD_ENTRY * thread_p, WFG_CYCLE_CASE * cycle_case_p, WFG_CYCLE ** list_cycles_p,
			   const int max_cycles_in_group, const int max_cycles)
{
  int i, j;
  WFG_CYCLE **last_cycle_p;	/* ptr to addr of the last cycle */
  WFG_STACK *bottom_p = NULL;	/* bottom of WFG stack */
  WFG_STACK *top_p;		/* top of WFG stack */
  WFG_STACK *stack_elem_p;	/* pointer for stack scan */
  WFG_CYCLE *cycle_p = NULL;	/* pointer to the current cycle */
  WFG_WAITER *waiter_p;		/* Waiter transactions in the cycle */
  int htran_index;		/* index of the waiter node of the top edge */
  int cycle_group_no;		/* cycle group number */
  int num_cycles_in_group;
  int num_total_cycles = 0;
  int error_code = NO_ERROR;

  *cycle_case_p = WFG_CYCLE_NO;
  *list_cycles_p = NULL;
  last_cycle_p = list_cycles_p;

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (wfg_Total_waiters < 2)
    {
      error_code = ER_FAILED;
      goto error;
    }

  for (i = 0; i < wfg_Total_nodes; i++)
    {
      wfg_Nodes[i].status = WFG_NOT_VISITED;
      wfg_Nodes[i].cycle_group_no = -1;
    }

  /* allocate stack for DFS search */
  bottom_p = (WFG_STACK *) malloc (sizeof (WFG_STACK) * wfg_Total_waiters);
  if (bottom_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WFG_STACK) * wfg_Total_waiters);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }
  top_p = bottom_p - 1;
  cycle_group_no = 0;

  for (i = 0; i < wfg_Total_nodes; i++)
    {
      if (max_cycles > 0 && num_total_cycles > max_cycles)
	{
	  /*
	   * We have too many cycles already. It is better to return to avoid
	   * spending too much time and space among cycle groups
	   */
	  *cycle_case_p = WFG_CYCLE_YES_PRUNE;
	  break;
	}

      if (wfg_Nodes[i].status != WFG_NOT_VISITED)
	{
	  continue;
	}

      /* DFS beginning with the current node */
      cycle_group_no++;
      num_cycles_in_group = 0;

      /*
       * Optimization of special case. Used to avoid overhead of stack operations
       */
      if (wfg_Nodes[i].first_holder_edge_p == NULL)
	{
	  wfg_Nodes[i].status = WFG_OFF_STACK;
	  continue;
	}

      /* start depth-first-search from this node */
      wfg_Nodes[i].status = WFG_ON_STACK;
      wfg_push_stack (&top_p, i);

      while (top_p >= bottom_p)
	{
	  /* not empty stack */

	new_top:		/* new top entry is pushed in stack */

	  for (; top_p->current_holder_edge_p != NULL;
	       top_p->current_holder_edge_p = top_p->current_holder_edge_p->next_holder_edge_p)
	    {
	      htran_index = top_p->current_holder_edge_p->holder_tran_index;

	      switch (wfg_Nodes[htran_index].status)
		{
		case WFG_NOT_VISITED:
		  /* untouched node */
		  if (wfg_Nodes[htran_index].first_holder_edge_p == NULL)
		    {
		      wfg_Nodes[htran_index].status = WFG_OFF_STACK;
		    }
		  else
		    {
		      /* try the new node */
		      wfg_Nodes[htran_index].status = WFG_ON_STACK;
		      wfg_push_stack (&top_p, htran_index);
		      goto new_top;
		    }
		  break;

		case WFG_ON_STACK:
		  /* a cyclye has been found */

		  /* mark this cycle with cycle_group_no */
		  wfg_Nodes[htran_index].cycle_group_no = cycle_group_no;

		  for (stack_elem_p = top_p; stack_elem_p->wait_tran_index != htran_index; stack_elem_p--)
		    {
		      wfg_Nodes[stack_elem_p->wait_tran_index].cycle_group_no = cycle_group_no;
		    }

		  /* construct a cycle */
		  cycle_p = (WFG_CYCLE *) malloc (sizeof (WFG_CYCLE));
		  if (cycle_p == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WFG_CYCLE));
		      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		      goto error;
		    }

		  cycle_p->num_trans = (int) ((top_p - stack_elem_p) + 1);
		  cycle_p->next = NULL;
		  cycle_p->waiters = (WFG_WAITER *) malloc (sizeof (WFG_WAITER) * cycle_p->num_trans);
		  if (cycle_p->waiters == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
			      sizeof (WFG_WAITER) * cycle_p->num_trans);
		      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
		      free_and_init (cycle_p);
		      goto error;
		    }

		  j = 0;
		  for (stack_elem_p = top_p, waiter_p = cycle_p->waiters; j < cycle_p->num_trans;
		       j++, stack_elem_p--, waiter_p++)
		    {
		      waiter_p->tran_index = stack_elem_p->wait_tran_index;
		      waiter_p->cycle_fun = wfg_Nodes[waiter_p->tran_index].cycle_fun;
		      waiter_p->args = wfg_Nodes[waiter_p->tran_index].args;
		    }

		  *last_cycle_p = cycle_p;
		  last_cycle_p = &cycle_p->next;

		  num_cycles_in_group++;

		  if (max_cycles > 0 && num_total_cycles + num_cycles_in_group >= max_cycles)
		    {
		      *cycle_case_p = WFG_CYCLE_YES_PRUNE;
		    }
		  else if (*cycle_case_p == WFG_CYCLE_NO)
		    {
		      *cycle_case_p = WFG_CYCLE_YES;
		    }

		  break;

		case WFG_OFF_STACK:
		  /* already traversed */
		  if (wfg_Nodes[htran_index].cycle_group_no == cycle_group_no)
		    {
		      /*
		       * need to traverse again
		       *
		       * We will skip on finding more cycles for
		       * the current cycle group when we have found many cycles.
		       * This is done to avoid finding many many combinations of
		       * cycles for the same transactions.
		       */

		      if ((max_cycles_in_group > 0
			   && (num_cycles_in_group > wfg_Total_nodes || num_cycles_in_group >= max_cycles_in_group))
			  || (max_cycles > 0 && (num_total_cycles + num_cycles_in_group) >= max_cycles))
			{
			  *cycle_case_p = WFG_CYCLE_YES_PRUNE;
			  break;
			}

		      wfg_Nodes[htran_index].status = WFG_RE_ON_STACK;
		      wfg_push_stack (&top_p, htran_index);
		      goto new_top;
		    }
		  break;

		case WFG_RE_ON_STACK:
		  /* this cycle has already been detected */
		  break;

		default:
#if defined(WFG_DEBUG)
		  (void) fprintf (stdout, "wfg_detect_ordinary_cycle():");
		  (void) fprintf (stdout, "interal switch error\n");
#endif /* WFG_DEBUG */
		  break;
		}
	    }

	  /* top_p is searched exhaustedly */
	  wfg_Nodes[top_p->wait_tran_index].status = WFG_OFF_STACK;
	  error_code = wfg_pop_stack (&top_p, &bottom_p);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}

      /* empty stack: continue to next cycle group */
      num_total_cycles += num_cycles_in_group;
    }

  free_and_init (bottom_p);	/* free stack */

  csect_exit (thread_p, CSECT_WFG);
  return error_code;

error:
  if (*list_cycles_p != NULL)
    {
      wfg_free_cycle (*list_cycles_p);
      *list_cycles_p = NULL;
    }

  if (bottom_p != NULL)
    {
      free_and_init (bottom_p);
    }
  csect_exit (thread_p, CSECT_WFG);

  return error_code;
}

/*
 * wfg_add_waiters_of_tg :
 *
 * returns : NO_ERROR
 *
 *   smallest_onstack(in/out):
 *   holder_node(in):
 *   tg_index(in):
 *
 * Note:
 */
static int
wfg_add_waiters_of_tg (int *smallest_onstack, int holder_node, int tg_index)
{
  WFG_TRANS_LIST *h_tran_list_p;	/* ptr to a trans list node in TG */
  WFG_TRANS_LIST *w_tran_list_p;	/* ptr to a trans list node in TG */
  WFG_TRAN_GROUP *tg_tmp;
  WFG_NODE *w_node_p;

  /*
   * If the node i is a holder of any TG,
   * add the waiters of such TG to it.
   */
  for (; tg_index < wfg_Total_tran_groups; tg_index++)
    {
      tg_tmp = &wfg_Tran_group[tg_index];
      for (h_tran_list_p = tg_tmp->holder_tran_list_p; h_tran_list_p != NULL; h_tran_list_p = h_tran_list_p->next)
	{
	  if (h_tran_list_p->tran_index == holder_node)
	    {
	      break;
	    }
	}

      if (h_tran_list_p != NULL)
	{
	  /* Add all waiters of the TG */
	  for (w_tran_list_p = tg_tmp->waiter_tran_list_p; w_tran_list_p != NULL; w_tran_list_p = w_tran_list_p->next)
	    {
	      w_node_p = &wfg_Nodes[w_tran_list_p->tran_index];
	      if (w_node_p->status == WFG_NOT_VISITED)
		{
		  w_node_p->status = WFG_ON_STACK;
		  if (*smallest_onstack > w_tran_list_p->tran_index)
		    {
		      *smallest_onstack = w_tran_list_p->tran_index;
		    }
		}
	    }
	}
    }

  return NO_ERROR;
}

/*
 * wfg_add_waiters_normal_wfg :
 *
 * returns : NO_ERROR
 *
 *   smallest_onstack(in/out):
 *   node_index(in):
 *
 * Note:
 */
static int
wfg_add_waiters_normal_wfg (int *smallest_onstack, int node_index)
{
  WFG_EDGE *edge_p;		/* loop pointer to edge */

  /* Add the waiters of the normal WFG */
  for (edge_p = wfg_Nodes[node_index].first_waiter_edge_p; edge_p != NULL; edge_p = edge_p->next_waiter_edge_p)
    {
      if (wfg_Nodes[edge_p->waiter_tran_index].status == WFG_NOT_VISITED)
	{
	  wfg_Nodes[edge_p->waiter_tran_index].status = WFG_ON_STACK;
	  if (*smallest_onstack > edge_p->waiter_tran_index)
	    {
	      *smallest_onstack = edge_p->waiter_tran_index;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * wfg_get_all_waiting_and_add_waiter :
 *
 * returns : NO_ERROR
 *
 *   all_waiting(out):
 *   add_waiter(out):
 *   tg1_index(in):
 *   tg2_index(in):
 *
 * Note:
 */
static int
wfg_get_all_waiting_and_add_waiter (bool * all_waiting, bool * add_waiter, int tg_index)
{
  WFG_TRANS_LIST *w_tran_list_p;	/* ptr to a trans list node in TG */
  WFG_TRANS_LIST *h_tran_list_p;	/* ptr to a trans list node in TG */
  WFG_TRAN_GROUP *tg_tmp;
  WFG_NODE *w_node_p;
  bool tg_connected, tg_all_waiting;
  int i;

  /*
   * If all holders of connected transaction groups are waiting, then
   * we have a deadlock related to TGs. All TG holders will be part
   * of TG elementary cycles.
   */

  *all_waiting = true;
  *add_waiter = true;

  for (i = tg_index; i < wfg_Total_tran_groups && *all_waiting == true; i++)
    {
      tg_tmp = &wfg_Tran_group[i];
      if (i == tg_index)
	{
	  tg_connected = true;
	}
      else
	{
	  tg_connected = false;
	}
      for (w_tran_list_p = tg_tmp->waiter_tran_list_p; w_tran_list_p != NULL && tg_connected == false;
	   w_tran_list_p = w_tran_list_p->next)
	{
	  w_node_p = &wfg_Nodes[w_tran_list_p->tran_index];
	  if (w_node_p->status != WFG_NOT_VISITED)
	    {
	      tg_connected = true;
	    }
	}

      if (tg_connected == false)
	{
	  continue;
	}

      tg_all_waiting = true;
      for (h_tran_list_p = tg_tmp->holder_tran_list_p; h_tran_list_p != NULL && tg_all_waiting == true;
	   h_tran_list_p = h_tran_list_p->next)
	{
	  if (wfg_Nodes[h_tran_list_p->tran_index].status != WFG_OFF_STACK)
	    {
	      tg_all_waiting = false;
	    }
	  else if (w_tran_list_p != NULL && w_tran_list_p->tran_index == h_tran_list_p->tran_index)
	    {
	      /*
	       * The waiter is also a holder. Don't need to add
	       * the waiter at a later point.
	       */
	      *add_waiter = false;
	    }
	}
      if (tg_connected == true && tg_all_waiting == false)
	{
	  *all_waiting = false;
	}
    }

  return NO_ERROR;
}

/*
 * wfg_get_all_waiting_and_add_waiter :
 *
 * returns : NO_ERROR
 *
 *   all_waiting(out):
 *   add_waiter(out):
 *   tg1_index(in):
 *   tg2_index(in):
 *
 */
static WFG_CYCLE *
wfg_detect_tran_group_cycle_internal (WFG_CYCLE_CASE * cycle_case_p, WFG_TRANS_LIST * w_tran_list_p, bool add_waiter,
				      int tg_index, int num_tran_groups_holders)
{
  WFG_WAITER *waiter_p;		/* Waiter transactions in the cycle */
  WFG_TRANS_LIST *h_tran_list_p;	/* ptr to a trans list node in TG */
  WFG_CYCLE *cycle_p;		/* pointer to the current cycle */
  int i;

  /*
   * Construct the cycle all TG waiters that are part of TG cycles.
   */

  if (*cycle_case_p == WFG_CYCLE_NO)
    {
      *cycle_case_p = WFG_CYCLE_YES;
    }

  cycle_p = (WFG_CYCLE *) malloc (sizeof (WFG_CYCLE));
  if (cycle_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WFG_CYCLE));
      return NULL;
    }

  /* Guess the max for now. We will fix it at the end */
  cycle_p->num_trans = num_tran_groups_holders;

  if (add_waiter == true)
    {
      cycle_p->num_trans++;
    }

  cycle_p->next = NULL;
  cycle_p->waiters = (WFG_WAITER *) malloc (sizeof (WFG_WAITER) * cycle_p->num_trans);
  if (cycle_p->waiters == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WFG_WAITER) * cycle_p->num_trans);
      free_and_init (cycle_p);
      return NULL;
    }

  /* Add all TG holders that were part of the connected graph. */
  cycle_p->num_trans = 0;
  waiter_p = cycle_p->waiters;

  for (i = tg_index; i < wfg_Total_tran_groups; i++)
    {
      for (h_tran_list_p = wfg_Tran_group[i].holder_tran_list_p; h_tran_list_p != NULL;
	   h_tran_list_p = h_tran_list_p->next)
	{
	  if (wfg_Nodes[h_tran_list_p->tran_index].status == WFG_OFF_STACK)
	    {
	      waiter_p->tran_index = h_tran_list_p->tran_index;
	      waiter_p->cycle_fun = wfg_Nodes[waiter_p->tran_index].cycle_fun;
	      waiter_p->args = wfg_Nodes[waiter_p->tran_index].args;
	      cycle_p->num_trans++;
	      /*
	       * Avoid a possible duplication,
	       * it could be part of holder of another TG.
	       */
	      wfg_Nodes[h_tran_list_p->tran_index].status = WFG_ON_TG_CYCLE;
	      waiter_p++;
	    }
	}
    }

  /*
   * Add the TG waiter to the cycle. Make sure that the waiter is not
   * also a holder. That is, don't duplicate entries.
   */
  if (add_waiter == true)
    {
      if (wfg_Nodes[w_tran_list_p->tran_index].status == WFG_OFF_STACK)
	{
	  wfg_Nodes[w_tran_list_p->tran_index].status = WFG_ON_TG_CYCLE;
	  waiter_p->tran_index = w_tran_list_p->tran_index;
	  waiter_p->cycle_fun = wfg_Nodes[waiter_p->tran_index].cycle_fun;
	  waiter_p->args = wfg_Nodes[waiter_p->tran_index].args;
	}
    }

  return cycle_p;
}

/*
 * wfg_detect_tg_cycle : finds all elementary cycles related with
 *                       Transaction Groups.
 *
 * returns : NO_ERROR if all OK, ER status otherwise
 *
 *   cycle_case(OUT)       : cycle_case. One of the following values:
 *                           WFG_CYCLE_YES_PRUNE
 *                           WFG_CYCLE_YES
 *                           WFG_CYCLE_NO
 *                           WFG_CYCLE_ERROR
 *   list_cycles_p(IN/OUT) : address to list of cycles
 *                           list_cycles is set as a side effect to point
 *                           to list of cycles.
 *
 * NOTE: the caller should be responsible for freeing memory allocated
 *	to the cycles after usage. See wfg_free_cycle()
 *
 * LIMITATIONS:
 *	Current implementation does not return all transactions for each
 *      elementary cycles. Instead, just return holders of TG in cycles.
 */
static int
wfg_detect_tran_group_cycle (THREAD_ENTRY * thread_p, WFG_CYCLE_CASE * cycle_case_p, WFG_CYCLE ** list_cycles_p)
{
  int smallest_onstack;
  int tg1_index, i;
  WFG_CYCLE **last_cycle_p;	/* ptr to addr of the last cycle */
  WFG_TRANS_LIST *w_tran_list_p;	/* ptr to a trans list node in TG */
  bool all_waiting;		/* true if all holders are waiting */
  WFG_CYCLE *cycle_p;		/* pointer to the current cycle */
  bool add_waiter;
  int num_tran_groups_holders = 0;	/* Add all holders for all tran groups */
  int error_code = NO_ERROR;

  *cycle_case_p = WFG_CYCLE_NO;

  *list_cycles_p = NULL;
  last_cycle_p = list_cycles_p;

  if (wfg_Total_tran_groups < 1)
    {
      return error_code;
    }

  if (csect_enter (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  for (tg1_index = 0; tg1_index < wfg_Total_tran_groups; tg1_index++)
    {
      num_tran_groups_holders += wfg_Tran_group[tg1_index].num_holders;
      if (num_tran_groups_holders > wfg_Total_nodes)
	{
	  num_tran_groups_holders = wfg_Total_nodes;
	}
    }

  for (i = 0; i < wfg_Total_nodes; i++)
    {
      wfg_Nodes[i].status = WFG_NOT_VISITED;
    }

  /* Go over each transaction group */
  for (tg1_index = 0; tg1_index < wfg_Total_tran_groups; tg1_index++)
    {
      /*
       * Optimization of special case. Used to avoid overhead of stack operations
       */
      if (wfg_Tran_group[tg1_index].holder_tran_list_p == NULL || wfg_Tran_group[tg1_index].waiter_tran_list_p == NULL)
	{
	  continue;
	}

      /*
       * Mark status of TG waiters as WFG_ON_STACK
       */
      for (w_tran_list_p = wfg_Tran_group[tg1_index].waiter_tran_list_p; w_tran_list_p != NULL;
	   w_tran_list_p = w_tran_list_p->next)
	{
	  /*
	   * Skip if it has already been in another TG cycle.
	   * Cycle or subcycle has already been listed.
	   */
	  if (wfg_Nodes[w_tran_list_p->tran_index].status == WFG_ON_TG_CYCLE)
	    {
	      continue;
	    }

	  for (i = 0; i < wfg_Total_nodes; i++)
	    {
	      if (wfg_Nodes[i].status != WFG_ON_TG_CYCLE)
		{
		  wfg_Nodes[i].status = WFG_NOT_VISITED;
		}
	    }

	  wfg_Nodes[w_tran_list_p->tran_index].status = WFG_ON_STACK;
	  smallest_onstack = w_tran_list_p->tran_index;

	  /* Loop until there is any more new waiters on stack */
	  while (smallest_onstack < wfg_Total_nodes)
	    {
	      i = smallest_onstack;
	      smallest_onstack = wfg_Total_nodes;
	      for (; i < wfg_Total_nodes && i < smallest_onstack; i++)
		{
		  if (wfg_Nodes[i].status == WFG_ON_STACK)
		    {
		      wfg_add_waiters_of_tg (&smallest_onstack, i, tg1_index);
		      wfg_add_waiters_normal_wfg (&smallest_onstack, i);

		      /* Indicate that the current node is OFF stack */
		      wfg_Nodes[i].status = WFG_OFF_STACK;
		    }
		}
	    }

	  wfg_get_all_waiting_and_add_waiter (&all_waiting, &add_waiter, tg1_index);

	  if (all_waiting == true)
	    {
	      cycle_p =
		wfg_detect_tran_group_cycle_internal (cycle_case_p, w_tran_list_p, add_waiter, tg1_index,
						      num_tran_groups_holders);
	      if (cycle_p == NULL)
		{
		  goto error;
		}
	      *last_cycle_p = cycle_p;
	      last_cycle_p = &cycle_p->next;
	    }
	}
    }

end:
  csect_exit (thread_p, CSECT_WFG);
  return error_code;

error:
  if (*list_cycles_p != NULL)
    {
      wfg_free_cycle (*list_cycles_p);
      *list_cycles_p = NULL;
    }
  goto end;
}

/*
 * wfg_is_waiting: Find if transaction is waiting for a resource either regular
 *              or Transaction Group resource.
 *
 * returns : NO_ERROR if all OK, ER status otherwise
 *
 */
int
wfg_is_waiting (THREAD_ENTRY * thread_p, const int tran_index)
{
  int i;
  WFG_EDGE *edge_p;
  int error_code = NO_ERROR;

  if (csect_enter_as_reader (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (wfg_Total_waiters > 0)
    {
      for (i = 0; i < wfg_Total_nodes; i++)
	{
	  for (edge_p = wfg_Nodes[i].first_waiter_edge_p; edge_p != NULL; edge_p = edge_p->next_waiter_edge_p)
	    {
	      if (tran_index == edge_p->waiter_tran_index)
		{
		  goto end;
		}
	    }
	}
    }

  error_code = wfg_is_tran_group_waiting (thread_p, tran_index);

end:
  csect_exit (thread_p, CSECT_WFG);

  return error_code;
}

/*
 * wfg_is_tran_group_waiting: Find if transaction is waiting for a TG resource.
 *
 * returns : NO_ERROR if all OK, ER status otherwise
 *
 *   tran_index(IN): Transaction index
 *
 */
int
wfg_is_tran_group_waiting (THREAD_ENTRY * thread_p, const int tran_index)
{
  int i;
  WFG_TRANS_LIST *tran_list_p;
  int error_code = ER_FAILED;

  if (csect_enter_as_reader (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  for (i = 0; i < wfg_Total_tran_groups; i++)
    {
      for (tran_list_p = wfg_Tran_group[i].waiter_tran_list_p; tran_list_p != NULL; tran_list_p = tran_list_p->next)
	{
	  if (tran_index == tran_list_p->tran_index)
	    {
	      error_code = NO_ERROR;
	      goto end;
	    }
	}
    }

end:
  csect_exit (thread_p, CSECT_WFG);

  return error_code;
}

/*
 * wfg_get_tran_entries :
 *
 * return : returns : number of tran entries if all OK, ER status otherwise
 *
 *   tran_index(IN) : Transaction index
 *
 */
int
wfg_get_tran_entries (THREAD_ENTRY * thread_p, const int tran_index)
{
  int i, n = 0;

  if (csect_enter_as_reader (thread_p, CSECT_WFG, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  for (i = 0; i < wfg_Total_nodes; i++)
    {
      WFG_EDGE *e;

      for (e = wfg_Nodes[i].first_holder_edge_p; e; e = e->next_holder_edge_p)
	{
	  n += (tran_index == e->waiter_tran_index);
	}
      for (e = wfg_Nodes[i].first_waiter_edge_p; e; e = e->next_waiter_edge_p)
	{
	  n += (tran_index == e->waiter_tran_index);
	}
    }

  for (i = 0; i < wfg_Total_tran_groups; i++)
    {
      WFG_TRANS_LIST *tl;

      for (tl = wfg_Tran_group[i].holder_tran_list_p; tl; tl = tl->next)
	{
	  n += (tran_index == tl->tran_index);
	}
      for (tl = wfg_Tran_group[i].waiter_tran_list_p; tl; tl = tl->next)
	{
	  n += (tran_index == tl->tran_index);
	}
    }

  csect_exit (thread_p, CSECT_WFG);

  return n;
}
#endif /* ENABLE_UNUSED_FUNCTION */
