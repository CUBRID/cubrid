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

#include "config.h"
#include "domain_plan.h"
#include "error_manager.h"
#include "object_primitive.h"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "xasl_predicate.hpp"
#include "xasl_stream.hpp"
#include "xasl_unpack_info.hpp"
#include "query_hash_join.h"
#include <climits>
#include <cstdlib>
#include <cstring>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Load boundary (a) (#336, D-323-09; #337 removed the derived-consumer exceptions X-8..X-10): every item the compiler
 * left without a type is a gate slot, a gate-dependent node or an alias of one; anything else refuses the load with
 * ER_QPROC_DOMAIN_UNRESOLVED. A derived consumer (a value pointer, a list position, a sort key, an accumulator)
 * reads its producer: the producer's gate slot (ALIAS) or its domain. The collation axis (#338): a string the
 * compiler typed but whose collation the values give (LEAVE, ENFORCE) is a slot recording its bound value's domain,
 * a node the gate decides from its operands (COLLATION_GATE), or a consumer reading its producer. */
static const bool domain_plan_check_load = true;
struct DOMAIN_PLAN_LOAD_EXCEPTION
{
  const char *id;
  const char *site;
  const char *action;
};
static const DOMAIN_PLAN_LOAD_EXCEPTION domain_plan_load_exceptions[] = {
  {"X-1", "TYPE_REGU_VAR_LIST", "no item; visit children"},
  {"X-2", "REGU_VARIABLE_ANALYTIC_WINDOW", "alias list column"},
  {"X-3", "set-operation position", "alias producer"},
  {"X-4", "TYPE_LIST_ID / TYPE_ORDERBY_NUM", "no value-domain item"},
  {"X-5", "collection constructor", "static element domains"}
};

/* Temporary load records are freed before publishing the plan. No record, owner
 * link or traversal scratch survives in the hot arrays. Shared XASLs are marked
 * on entry; expression items are appended after their operands (producer order). */

/* What the resolution pass decides for a record after the walk (#337). */
enum DOMAIN_LOAD_KIND
{
  DOMAIN_LOAD_LEAF,		/* a bind, a literal, an attribute, a compiled node: nothing to derive */
  DOMAIN_LOAD_CONSUMER,		/* a value pointer, a list position, a set-operation column: reads its producer */
  DOMAIN_LOAD_NODE,		/* a node the compiler left without a type: a gate-dependent node when every operand
				 * is known */
  DOMAIN_LOAD_ARITH_REGU,	/* the regu wrapping an arithmetic node: carries that node's answer */
  DOMAIN_LOAD_FIXED_AGG		/* a compiled aggregate or analytic: accumulator derived once (L-43) */
};

struct DOMAIN_LOAD_RECORD
{
  DOMAIN_LOAD_RECORD *next;
  DOMAIN_PLAN_ITEM item;
  DOMAIN_PLAN_ITEM_COLD cold;
  DOMAIN_PLAN_ITEM **owner;
  int index;
  DB_VALUE *output[2];
  REGU_VARIABLE *regu;
  DOMAIN_LOAD_RECORD *alias;
  unsigned char kind;		/* DOMAIN_LOAD_KIND */
  unsigned char state;		/* resolution pass: 0 open, 1 in progress, 2 done */
  bool known;			/* after resolution: the gate knows this operand's type (slot, value or domain) */
  bool literal_value;		/* a bind or a literal: the gate reads its value */
  bool follows_producer;	/* after resolution: this record carries its producer's answer (a link source goes
				 * through it to the producer, a bind's value included) */
  DOMAIN_LOAD_RECORD *producer;	/* CONSUMER / ARITH_REGU: whose answer this record reads */
  /* NODE / FIXED_AGG: the operands in operand order, and the literal a TYPE_DBVAL operand carries */
  DOMAIN_PLAN_ITEM *link[3];
  const DB_VALUE *literal[3];
  int n_link;
  const TP_DOMAIN *consumer;
  const TP_DOMAIN *argument;	/* FIXED_AGG: the argument's compiled domain when it is not open (DOMAIN_GATE_LINK) */
  int gate_order;		/* index into plan->gate_nodes once this record is a gate-dependent node */
  DOMAIN_PLAN_ITEM *self_owner;	/* owner storage of a synthetic record (a set-operation column) */
};
struct DOMAIN_LOAD_BINDING
{
  DOMAIN_LOAD_BINDING *next;
  DOMAIN_PLAN_ITEM **owner;
  DOMAIN_PLAN_ITEM *target;
};
/* A set-operation or CTE list column, unified from its branches (X-3), made once per (list, column). */
struct DOMAIN_LOAD_LIST_COLUMN
{
  DOMAIN_LOAD_LIST_COLUMN *next;
  const XASL_NODE *xasl;
  int pos;
  DOMAIN_PLAN_ITEM *item;
};
struct DOMAIN_LOAD_CONTEXT
{
  THREAD_ENTRY *thread_p;
  DOMAIN_PLAN *plan;
  DOMAIN_LOAD_RECORD *head;
  DOMAIN_LOAD_RECORD *tail;
  DOMAIN_LOAD_BINDING *bindings;
  bool failed;
  XASL_NODE *block;
  /* the list the TYPE_POSITION regus being walked read: its producer XASL, or its producer columns */
  XASL_NODE *position_source;
  REGU_VARIABLE_LIST position_columns;
  DOMAIN_LOAD_LIST_COLUMN *list_columns;
  DOMAIN_LOAD_RECORD **gate_order;	/* gate-dependent nodes in resolution order (producers first) */
  int n_gate_order;
  int max_gate_order;
};

static void domain_walk_xasl (DOMAIN_LOAD_CONTEXT *, XASL_NODE *);
static void domain_walk_pred (DOMAIN_LOAD_CONTEXT *, PRED_EXPR *);
static void domain_walk_regu (DOMAIN_LOAD_CONTEXT *, REGU_VARIABLE *, DOMAIN_CTX = DOMAIN_CTX_FUNC_ARG);
static OUTPTR_LIST *domain_block_output (XASL_NODE * xasl);
static DOMAIN_PLAN_ITEM *domain_list_column (DOMAIN_LOAD_CONTEXT * ctx, XASL_NODE * xasl, int pos);

static bool
domain_is_fixed (const TP_DOMAIN * domain)
{
  return domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE
    && domain->collation_flag != TP_DOMAIN_COLL_LEAVE;
}

/* The type axis only: the collation of a character result is merged at the gate (#338, D-335-03). */
static bool
domain_type_is_fixed (const TP_DOMAIN * domain)
{
  return domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE;
}

/* A typed character domain whose collation the compiler left to the values (LEAVE) or enforced over an operand it
 * could not type (ENFORCE): the values give it, so the gate decides it (#338). */
static bool
domain_character_open (const TP_DOMAIN * domain)
{
  return domain != NULL && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (domain))
    && TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL;
}

/* The load record an item lives in: every item is a record's embedded item until the plan is published. */
static DOMAIN_LOAD_RECORD *
domain_record_of (const DOMAIN_PLAN_ITEM * item)
{
  return item == NULL ? NULL
    : (DOMAIN_LOAD_RECORD *) ((char *) const_cast < DOMAIN_PLAN_ITEM * >(item) - offsetof (DOMAIN_LOAD_RECORD, item));
}

/* The resolver context of a gate-dependent operator node. */
static DOMAIN_CTX
domain_gate_context (OPERATOR_TYPE opcode)
{
  switch (opcode)
    {
    case T_ADD:
    case T_SUB:
    case T_MUL:
    case T_DIV:
    case T_MOD:
    case T_UNMINUS:
    case T_ABS:
    case T_FLOOR:
    case T_CEIL:
    case T_ROUND:
    case T_TRUNC:
      return DOMAIN_CTX_ARITH;
    case T_NVL:
    case T_NVL2:
    case T_IFNULL:
    case T_COALESCE:
    case T_NULLIF:
    case T_LEAST:
    case T_GREATEST:
      return DOMAIN_CTX_COMMON_VALUE;
    default:
      return DOMAIN_CTX_FUNC_ARG;
    }
}

/* Records the operands a node the compiler left without a type is decided from (resolution pass, #337). */
static void
domain_set_links (DOMAIN_LOAD_RECORD * record, REGU_VARIABLE * const *operands, int n_operands,
		  const TP_DOMAIN * consumer)
{
  assert (n_operands <= 3);
  record->n_link = 0;
  for (int i = 0; i < n_operands; i++)
    {
      if (operands[i] != NULL && operands[i]->domain_plan != NULL)
	{
	  record->literal[record->n_link] = operands[i]->type == TYPE_DBVAL ? &operands[i]->value.dbval : NULL;
	  record->link[record->n_link++] = operands[i]->domain_plan;
	}
    }
  record->consumer = consumer;
}

/* Makes a resolved node a gate-dependent node: the gate decides it into its own slot once per execution, after every
 * operand (the resolution pass appends it after its producers). */
static void
domain_mark_gate_node (DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_LOAD_RECORD * record)
{
  if (ctx->n_gate_order == ctx->max_gate_order)
    {
      int max = ctx->max_gate_order == 0 ? 16 : ctx->max_gate_order * 2;
      DOMAIN_LOAD_RECORD **order =
	(DOMAIN_LOAD_RECORD **) db_private_realloc (ctx->thread_p, ctx->gate_order, max * sizeof (*order));
      if (order == NULL)
	{
	  ctx->failed = true;
	  return;
	}
      ctx->gate_order = order;
      ctx->max_gate_order = max;
    }
  record->item.flags |= DOMAIN_PLAN_GATE;
  record->item.slot = ctx->plan->n_slots++;
  record->item.fixed.domain = NULL;
  record->gate_order = ctx->n_gate_order;
  ctx->gate_order[ctx->n_gate_order++] = record;
}

static DOMAIN_PLAN_ITEM *
domain_add_item (DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_PLAN_ITEM ** owner, const TP_DOMAIN * domain,
		 DOMAIN_OPERAND_CLASS operand_class, DOMAIN_CTX context, int opcode, const char *name)
{
  if (ctx->failed || *owner != NULL)
    {
      return *owner;
    }
  DOMAIN_LOAD_RECORD *record = (DOMAIN_LOAD_RECORD *) db_private_alloc (ctx->thread_p, sizeof (*record));
  if (record == NULL)
    {
      ctx->failed = true;
      return NULL;
    }
  memset (record, 0, sizeof (*record));
  record->owner = owner;
  record->gate_order = -1;
  record->index = ctx->plan->n_items++;
  record->item.slot = -1;
  record->item.ref = -1;
  record->item.operand_class = operand_class;
  record->item.fixed.domain = domain;
  for (int i = 0; i < 3; i++)
    {
      record->item.fail[i] = context == DOMAIN_CTX_COMPARE || context == DOMAIN_CTX_KEY_ELEM ? DOMAIN_FAIL_KEEP
	: context == DOMAIN_CTX_ASSIGN ? DOMAIN_FAIL_ERROR : DOMAIN_FAIL_NULL;
    }
  record->cold.val_pos = -1;
  record->cold.ctx = context;
  record->cold.opcode = opcode;
  record->cold.name = name;
  if (ctx->tail == NULL)
    {
      ctx->head = record;
    }
  else
    {
      ctx->tail->next = record;
    }
  ctx->tail = record;
  *owner = &record->item;
  return *owner;
}

static void
domain_bind_item (DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_PLAN_ITEM ** owner, DOMAIN_PLAN_ITEM * target)
{
  if (*owner != NULL || target == NULL || ctx->failed)
    {
      return;
    }
  DOMAIN_LOAD_BINDING *binding = (DOMAIN_LOAD_BINDING *) db_private_alloc (ctx->thread_p, sizeof (*binding));
  if (binding == NULL)
    {
      ctx->failed = true;
      return;
    }
  binding->owner = owner;
  binding->target = target;
  binding->next = ctx->bindings;
  ctx->bindings = binding;
  *owner = target;
}

static DOMAIN_OPERAND_CLASS
domain_merge_class (DOMAIN_OPERAND_CLASS lhs, const DOMAIN_PLAN_ITEM * rhs)
{
  if (rhs == NULL)
    {
      return lhs;
    }
  DOMAIN_OPERAND_CLASS other = (DOMAIN_OPERAND_CLASS) rhs->operand_class;
  if (lhs == OPERAND_VOLATILE || other == OPERAND_VOLATILE)
    {
      return OPERAND_VOLATILE;
    }
  if (lhs == OPERAND_ROW || other == OPERAND_ROW)
    {
      return OPERAND_ROW;
    }
  return lhs == OPERAND_CORRELATED || other == OPERAND_CORRELATED ? OPERAND_CORRELATED : OPERAND_CONST;
}

static bool
domain_volatile_operator (OPERATOR_TYPE opcode)
{
  switch (opcode)
    {
    case T_INCR:
    case T_DECR:
    case T_CURRENT_VALUE:
    case T_NEXT_VALUE:
    case T_CASE:
    case T_DECODE:
    case T_IF:
    case T_PREDICATE:
    case T_ROW_COUNT:
    case T_LAST_INSERT_ID:
    case T_EVALUATE_VARIABLE:
    case T_DEFINE_VARIABLE:
    case T_RAND:
    case T_RANDOM:
    case T_DRAND:
    case T_DRANDOM:
    case T_SYS_GUID:
    case T_UUID:
    case T_EXEC_STATS:
    case T_TRACE_STATS:
    case T_SLEEP:
      return true;
    default:
      return false;
    }
}

static void
domain_fixed_operand (DOMAIN_PLAN_ITEM * item, int i, const TP_DOMAIN * source,
		      const TP_DOMAIN * target, DOMAIN_CTX mode)
{
  if (item == NULL)
    {
      return;
    }
  item->fixed.operand_domain[i] = target;
  if (domain_is_fixed (source) && domain_is_fixed (target))
    {
      item->fixed.conv[i] = domain_lookup_converter (TP_DOMAIN_TYPE (source), target, mode);
    }
}

static void
domain_walk_list (DOMAIN_LOAD_CONTEXT * ctx, REGU_VARIABLE_LIST list, DOMAIN_CTX context = DOMAIN_CTX_FUNC_ARG)
{
  for (; list != NULL && !ctx->failed; list = list->next)
    {
      domain_walk_regu (ctx, &list->value, context);
    }
}

/* Walks regu lists that read the list file of `source` (or whose columns are `columns`): their TYPE_POSITION regus
 * read that list's columns (#337). */
static void
domain_walk_position_list (DOMAIN_LOAD_CONTEXT * ctx, REGU_VARIABLE_LIST list, XASL_NODE * source,
			   REGU_VARIABLE_LIST columns)
{
  XASL_NODE *saved_source = ctx->position_source;
  REGU_VARIABLE_LIST saved_columns = ctx->position_columns;
  ctx->position_source = source;
  ctx->position_columns = columns;
  domain_walk_list (ctx, list);
  ctx->position_source = saved_source;
  ctx->position_columns = saved_columns;
}

/* The item of column `pos` of a column list. A list file does not store hidden columns; a sort list numbers the
 * output list with them (qexec_resolve_domains_on_sort_list). */
static DOMAIN_PLAN_ITEM *
domain_column_item (DOMAIN_LOAD_CONTEXT * ctx, REGU_VARIABLE_LIST columns, int pos, bool skip_hidden)
{
  for (REGU_VARIABLE_LIST col = columns; col != NULL && pos >= 0; col = col->next)
    {
      if (skip_hidden && REGU_VARIABLE_IS_FLAGED (&col->value, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  continue;
	}
      if (pos-- == 0)
	{
	  /* a producer column is walked in its own list's context, not in the reader's */
	  XASL_NODE *saved_source = ctx->position_source;
	  REGU_VARIABLE_LIST saved_columns = ctx->position_columns;
	  ctx->position_source = NULL;
	  ctx->position_columns = NULL;
	  domain_walk_regu (ctx, &col->value, DOMAIN_CTX_LIST_COLUMN);
	  ctx->position_source = saved_source;
	  ctx->position_columns = saved_columns;
	  return col->value.domain_plan;
	}
    }
  return NULL;
}

/* The name of a synthetic list-column item: boundary (a) checks its readers, not the item (it has no XASL node). */
static const char domain_list_column_name[] = "list column";

/* A synthetic record owning an item no XASL node points at: a set-operation or CTE list column (X-3). */
static DOMAIN_LOAD_RECORD *
domain_add_synthetic (DOMAIN_LOAD_CONTEXT * ctx, const char *name)
{
  DOMAIN_PLAN_ITEM *owner = NULL;
  DOMAIN_PLAN_ITEM *item =
    domain_add_item (ctx, &owner, &tp_Variable_domain, OPERAND_ROW, DOMAIN_CTX_LIST_COLUMN, 0, name);
  if (item == NULL)
    {
      return NULL;
    }
  DOMAIN_LOAD_RECORD *record = domain_record_of (item);
  /* the record keeps its own owner slot: publishing writes the published item there */
  record->self_owner = item;
  record->owner = &record->self_owner;
  return record;
}

/*
 * domain_list_column () - the item giving column `pos` of the list file `xasl` produces (L-41)
 *
 * A block's list holds its output columns. A set operation's list unifies its branches' lists, and a CTE's list its
 * non-recursive part's with the rows its recursive part appends (qfile_unify_types): that column is a node over the
 * branch columns (X-3), made once per (list, column).
 */
static DOMAIN_PLAN_ITEM *
domain_list_column (DOMAIN_LOAD_CONTEXT * ctx, XASL_NODE * xasl, int pos)
{
  if (xasl == NULL || pos < 0 || ctx->failed)
    {
      return NULL;
    }
  XASL_NODE *branches[2] = { NULL, NULL };
  switch (xasl->type)
    {
    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
      branches[0] = xasl->proc.union_.left;
      branches[1] = xasl->proc.union_.right;
      break;
    case CTE_PROC:
      branches[0] = xasl->proc.cte.non_recursive_part;
      branches[1] = xasl->proc.cte.recursive_part;
      break;
    default:
      {
	OUTPTR_LIST *output = domain_block_output (xasl);
	return output == NULL ? NULL : domain_column_item (ctx, output->valptrp, pos, true);
      }
    }
  for (DOMAIN_LOAD_LIST_COLUMN * c = ctx->list_columns; c != NULL; c = c->next)
    {
      if (c->xasl == xasl && c->pos == pos)
	{
	  return c->item;
	}
    }
  DOMAIN_LOAD_LIST_COLUMN *entry = (DOMAIN_LOAD_LIST_COLUMN *) db_private_alloc (ctx->thread_p, sizeof (*entry));
  DOMAIN_LOAD_RECORD *record = entry == NULL ? NULL : domain_add_synthetic (ctx, domain_list_column_name);
  if (record == NULL)
    {
      if (entry != NULL)
	{
	  db_private_free (ctx->thread_p, entry);
	}
      ctx->failed = true;
      return NULL;
    }
  /* registered before the branches are asked: a recursive part reads this very column */
  entry->xasl = xasl;
  entry->pos = pos;
  entry->item = &record->item;
  entry->next = ctx->list_columns;
  ctx->list_columns = entry;
  record->kind = DOMAIN_LOAD_NODE;
  record->n_link = 0;
  for (int i = 0; i < 2; i++)
    {
      DOMAIN_PLAN_ITEM *column = domain_list_column (ctx, branches[i], pos);
      if (column != NULL)
	{
	  record->link[record->n_link++] = column;
	}
    }
  if (record->n_link == 0)
    {
      record->n_link = -1;
    }
  return &record->item;
}

/* The output list whose columns a block's result list file holds: the analytic or GROUP BY output when the block
 * has one (a BUILDLIST has at most one of the two). */
static OUTPTR_LIST *
domain_block_output (XASL_NODE * xasl)
{
  if (xasl->type == BUILDLIST_PROC)
    {
      BUILDLIST_PROC_NODE *b = &xasl->proc.buildlist;
      if (b->a_eval_list != NULL && b->a_outptr_list != NULL)
	{
	  return b->a_outptr_list;
	}
      if (b->groupby_list != NULL && b->g_outptr_list != NULL)
	{
	  return b->g_outptr_list;
	}
    }
  return xasl->outptr_list;
}

/* The producer of a TYPE_POSITION regu being walked: the column of the list it reads. */
static DOMAIN_PLAN_ITEM *
domain_position_producer (DOMAIN_LOAD_CONTEXT * ctx, int pos)
{
  if (ctx->position_columns != NULL)
    {
      return domain_column_item (ctx, ctx->position_columns, pos, true);
    }
  return domain_list_column (ctx, ctx->position_source, pos);
}

static void
domain_walk_out (DOMAIN_LOAD_CONTEXT * ctx, OUTPTR_LIST * list)
{
  if (list != NULL)
    {
      domain_walk_list (ctx, list->valptrp, DOMAIN_CTX_LIST_COLUMN);
    }
}

static void
domain_walk_arith (DOMAIN_LOAD_CONTEXT * ctx, ARITH_TYPE * arith, bool marked_gate = false)
{
  if (arith == NULL || arith->domain_plan != NULL || ctx->failed)
    {
      return;
    }
  bool is_cast = arith->opcode == T_CAST || arith->opcode == T_CAST_WRAP;
  REGU_VARIABLE *operands[] = { arith->leftptr, arith->rightptr, arith->thirdptr };
  DOMAIN_OPERAND_CLASS cls = domain_volatile_operator (arith->opcode) || arith->pred != NULL
    ? OPERAND_VOLATILE : OPERAND_CONST;
  for (int operand_index = 0; operand_index < 3; operand_index++)
    {
      REGU_VARIABLE *operand = operands[operand_index];
      domain_walk_regu (ctx, operand, is_cast ? DOMAIN_CTX_ASSIGN : DOMAIN_CTX_ARITH);
      if (operand != NULL)
	{
	  cls = domain_merge_class (cls, operand->domain_plan);
	}
    }
  domain_walk_pred (ctx, arith->pred);
  DOMAIN_PLAN_ITEM *item = domain_add_item (ctx, &arith->domain_plan, arith->domain, cls,
					    is_cast ? DOMAIN_CTX_ASSIGN : DOMAIN_CTX_ARITH, arith->opcode, "arith");
  if (item != NULL)
    {
      ctx->tail->output[0] = arith->value;
    }
  /* A node the compiler left without a result type (a late-bound operator over a slot, A8'') is decided by the gate
   * once per execution when every operand gives it a type the gate knows (#335). Whether an operand is known is
   * settled only after derived consumers read their producers, so the resolution pass decides it (#337); a session
   * variable read (S5, #336) is such a node too. CONNECT_BY_ROOT and QPRIOR carry their XASL in thirdptr. */
  const bool late_bound = arith->domain != NULL && TP_DOMAIN_TYPE (arith->domain) == DB_TYPE_VARIABLE;
  /* #338: a string the compiler typed but whose collation its values give (LEAVE, ENFORCE) is decided by the gate
   * from its operands' decided domains, as a gate-dependent node on the collation axis */
  const bool collation_open = !marked_gate && !late_bound && domain_character_open (arith->domain);
  if (item != NULL && (marked_gate || late_bound || collation_open))
    {
      DOMAIN_LOAD_RECORD *record = domain_record_of (item);
      const int n_value_operands = (arith->opcode == T_CONNECT_BY_ROOT || arith->opcode == T_QPRIOR) ? 2 : 3;
      record->kind = DOMAIN_LOAD_NODE;
      if (collation_open)
	{
	  item->flags |= DOMAIN_PLAN_COLLATION_GATE;
	}
      domain_set_links (record, operands, n_value_operands, arith->domain);
      for (int i = 0; i < n_value_operands; i++)
	{
	  if (operands[i] != NULL && operands[i]->domain_plan == NULL)
	    {
	      /* an operand without a value domain (X-4) leaves the node undecided */
	      record->n_link = -1;
	    }
	}
    }
  for (int i = 0; i < 3; i++)
    {
      if (operands[i] != NULL)
	{
	  /* Static operand targets come from compiled operand domains, never
	   * from an arithmetic result domain (DATE + INTEGER is not DATE + DATE).
	   * CAST alone explicitly supplies its consumer target. The G grid is dpin-07. */
	  domain_fixed_operand (item, i, operands[i]->domain, is_cast ? arith->domain : operands[i]->domain,
				DOMAIN_CTX_ASSIGN);
	}
    }
}

static bool
domain_value_in_list (VAL_LIST * list, DB_VALUE * value)
{
  for (QPROC_DB_VALUE_LIST p = list == NULL ? NULL : list->valp; p != NULL; p = p->next)
    {
      if (p->val == value)
	{
	  return true;
	}
    }
  return false;
}

static bool
domain_local_value (XASL_NODE * block, DB_VALUE * value)
{
  if (block == NULL || value == NULL)
    {
      return false;
    }
  if (domain_value_in_list (block->val_list, value) || domain_value_in_list (block->merge_val_list, value))
    {
      return true;
    }
  AGGREGATE_TYPE *aggregate = block->type == BUILDVALUE_PROC ? block->proc.buildvalue.agg_list
    : block->type == BUILDLIST_PROC ? block->proc.buildlist.g_agg_list : NULL;
  for (; aggregate != NULL; aggregate = aggregate->next)
    {
      if (aggregate->accumulator.value == value)
	{
	  return true;
	}
    }
  if (block->type != BUILDLIST_PROC)
    {
      return false;
    }
  for (ANALYTIC_EVAL_TYPE *eval = block->proc.buildlist.a_eval_list; eval != NULL; eval = eval->next)
    {
      for (ANALYTIC_TYPE *analytic = eval->head; analytic != NULL; analytic = analytic->next)
	{
	  if (analytic->value == value || analytic->out_value == value)
	    {
	      return true;
	    }
	}
    }
  return domain_value_in_list (block->proc.buildlist.g_val_list, value)
    || domain_value_in_list (block->proc.buildlist.a_val_list, value);
}

static void
domain_walk_regu (DOMAIN_LOAD_CONTEXT * ctx, REGU_VARIABLE * regu, DOMAIN_CTX context)
{
  if (regu == NULL || regu->domain_plan != NULL || ctx->failed)
    {
      return;
    }
  domain_walk_xasl (ctx, regu->xasl);
  if (regu->domain_plan != NULL || ctx->failed)
    {
      return;
    }
  DOMAIN_OPERAND_CLASS cls = OPERAND_ROW;
  switch (regu->type)
    {
    case TYPE_REGU_VAR_LIST:
      domain_walk_list (ctx, regu->value.regu_var_list, context);
      return;
    case TYPE_LIST_ID:
    case TYPE_ORDERBY_NUM:
      return;
    case TYPE_POS_VALUE:
      if (regu->domain == NULL)
	{
	  /* An allocated but unset regu (regu_init: TYPE_POS_VALUE 0) is no bind reference;
	   * every host variable regu carries a domain, VARIABLE for a GATE slot. */
	  return;
	}
      cls = OPERAND_CONST;
      break;
    case TYPE_DBVAL:
      cls = OPERAND_CONST;
      break;
    case TYPE_CONSTANT:
      cls = domain_local_value (ctx->block, regu->value.dbvalptr) ? OPERAND_ROW : OPERAND_CORRELATED;
      break;
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      domain_walk_arith (ctx, regu->value.arithptr, REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_GATE));
      if (regu->value.arithptr->domain_plan != NULL)
	{
	  cls = (DOMAIN_OPERAND_CLASS) regu->value.arithptr->domain_plan->operand_class;
	}
      break;
    case TYPE_FUNC:
      domain_walk_list (ctx, regu->value.funcp->operand, context);
      cls = OPERAND_CONST;
      /* These functions are unconditionally FETCH_NOT_CONST in fetch.c, even
       * when their argument list consists entirely of constants. */
      switch (regu->value.funcp->ftype)
	{
	case F_SET:
	case F_MULTISET:
	case F_SEQUENCE:
	case F_VID:
	case F_TABLE_SET:
	case F_TABLE_MULTISET:
	case F_TABLE_SEQUENCE:
	case F_GENERIC:
	case F_CLASS_OF:
	case F_BENCHMARK:
	  cls = OPERAND_VOLATILE;
	  break;
	default:
	  break;
	}
      for (REGU_VARIABLE_LIST op = regu->value.funcp->operand; op != NULL; op = op->next)
	{
	  cls = domain_merge_class (cls, op->value.domain_plan);
	}
      break;
    case TYPE_SP:
      domain_walk_list (ctx, regu->value.sp_ptr->args, DOMAIN_CTX_FUNC_ARG);
      cls = OPERAND_VOLATILE;
      break;
    case TYPE_REGUVAL_LIST:
      for (REGU_VALUE_ITEM * p = regu->value.reguval_list->regu_list; p != NULL; p = p->next)
	{
	  domain_walk_regu (ctx, p->value, context);
	}
      break;
    default:
      break;
    }
  if (REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_CORRELATED) || regu->xasl != NULL)
    {
      cls = cls == OPERAND_VOLATILE ? cls : OPERAND_CORRELATED;
    }
  DOMAIN_PLAN_ITEM *item = domain_add_item (ctx, &regu->domain_plan, regu->domain, cls, context, regu->type, "regu");
  if (item == NULL)
    {
      return;
    }
  DOMAIN_LOAD_RECORD *record = domain_record_of (item);
  record->regu = regu;
  record->output[0] = regu->vfetch_to;
  record->literal_value = regu->type == TYPE_POS_VALUE || regu->type == TYPE_DBVAL;
  if (regu->type == TYPE_POS_VALUE)
    {
      record->cold.val_pos = regu->value.val_pos;
    }
  if (REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_GATE) && regu->type != TYPE_INARITH && regu->type != TYPE_OUTARITH)
    {
      item->flags |= DOMAIN_PLAN_GATE;
      item->slot = ctx->plan->n_slots++;
      item->fixed.domain = NULL;
    }
  else if (regu->type == TYPE_POS_VALUE && domain_character_open (regu->domain))
    {
      /* a string slot the compiler typed but whose collation is the bound value's (C3/C12, LEAVE) or enforced over
       * the value the client sends as it is (an auto-parameter, ENFORCE): the gate records the value domain in this
       * slot; the compiled type stays the plan type (#336, #338) */
      item->flags |= DOMAIN_PLAN_COLLATION_GATE;
      item->slot = ctx->plan->n_slots++;
    }
  if (regu->type == TYPE_CONSTANT)
    {
      /* a value pointer holds what its producer wrote there, whatever domain the reader was compiled with (an
       * INSERT ... SELECT reader carries the target column's domain): its producer is found by value identity after
       * the walk (#337, F-335-07) */
      record->kind = DOMAIN_LOAD_CONSUMER;
    }
  else if (regu->type == TYPE_POSITION)
    {
      /* a list position reads its list's column: the gate's slot for it, or its domain (X-2, X-3, L-41) */
      record->kind = DOMAIN_LOAD_CONSUMER;
      record->producer = domain_record_of (domain_position_producer (ctx, regu->value.pos_descr.pos_no));
      domain_bind_item (ctx, &regu->value.pos_descr.domain_plan, item);
    }
  else if (regu->type == TYPE_REGUVAL_LIST && regu->value.reguval_list->regu_list != NULL
	   && !domain_type_is_fixed (regu->domain))
    {
      /* a multi-row VALUES column takes its first row's domain; later rows are checked against it as they are read
       * (fetch_peek_dbval_slow, U3) */
      record->kind = DOMAIN_LOAD_CONSUMER;
      record->producer = domain_record_of (regu->value.reguval_list->regu_list->value->domain_plan);
    }
  else if (regu->type == TYPE_FUNC && domain_character_open (regu->domain) && regu->value.funcp->operand != NULL)
    {
      /* #338: a function the compiler typed as a string whose collation its values give is decided by the gate from
       * its string operands. A node links three operands; a function with more strings than that is left to the row
       * (cold opcode -1: the gate records no decision). */
      REGU_VARIABLE *operands[3] = { NULL, NULL, NULL };
      int n_operands = 0;
      bool overflow = false;
      for (REGU_VARIABLE_LIST op = regu->value.funcp->operand; op != NULL; op = op->next)
	{
	  const TP_DOMAIN *d = op->value.domain;
	  if (d != NULL && TP_DOMAIN_TYPE (d) != DB_TYPE_VARIABLE && !TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (d)))
	    {
	      continue;
	    }
	  if (n_operands == 3)
	    {
	      overflow = true;
	      break;
	    }
	  operands[n_operands++] = &op->value;
	}
      if (n_operands == 0)
	{
	  operands[n_operands++] = &regu->value.funcp->operand->value;
	}
      record->kind = DOMAIN_LOAD_NODE;
      item->flags |= DOMAIN_PLAN_COLLATION_GATE;
      record->cold.opcode = overflow ? -1 : regu->value.funcp->ftype;
      domain_set_links (record, operands, n_operands, regu->domain);
      for (int i = 0; i < n_operands; i++)
	{
	  if (operands[i]->domain_plan == NULL)
	    {
	      record->n_link = -1;
	    }
	}
    }
  if (regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH)
    {
      ARITH_TYPE *arith = regu->value.arithptr;
      if ((arith->opcode == T_CAST || arith->opcode == T_CAST_WRAP)
	  && !REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_STRICT_TYPE_CAST))
	{
	  REGU_VARIABLE *operand = arith->rightptr != NULL ? arith->rightptr : arith->leftptr;
	  if (operand != NULL && operand->type != TYPE_POS_VALUE && operand->domain_plan != NULL)
	    {
	      operand->domain_plan->flags |= DOMAIN_PLAN_TRUNCATE_OK;
	    }
	}
      if (arith->domain_plan != NULL)
	{
	  /* the wrapper carries its node's answer once the resolution pass has it */
	  item->fixed = arith->domain_plan->fixed;
	  record->kind = DOMAIN_LOAD_ARITH_REGU;
	  record->producer = domain_record_of (arith->domain_plan);
	}
    }
  else
    {
      DOMAIN_CTX mode = context == DOMAIN_CTX_ASSIGN ? DOMAIN_CTX_ASSIGN
	: context == DOMAIN_CTX_COMPARE || context == DOMAIN_CTX_KEY_ELEM ? DOMAIN_CTX_COMPARE : DOMAIN_CTX_FUNC_ARG;
      domain_fixed_operand (item, 0, regu->domain, regu->domain, mode);
    }
}

static void
domain_walk_pred (DOMAIN_LOAD_CONTEXT * ctx, PRED_EXPR * pred)
{
  while (pred != NULL && !ctx->failed)
    {
      if (pred->type == T_PRED)
	{
	  domain_walk_pred (ctx, pred->pe.m_pred.lhs);
	  pred = pred->pe.m_pred.rhs;
	  continue;
	}
      else if (pred->type == T_NOT_TERM)
	{
	  pred = pred->pe.m_not_term;
	  continue;
	}
      else
	{
	  EVAL_TERM *term = &pred->pe.m_eval_term;
	  switch (term->et_type)
	    {
	    case T_COMP_EVAL_TERM:
	      domain_walk_regu (ctx, term->et.et_comp.lhs, DOMAIN_CTX_COMPARE);
	      domain_walk_regu (ctx, term->et.et_comp.rhs, DOMAIN_CTX_COMPARE);
	      break;
	    case T_ALSM_EVAL_TERM:
	      domain_walk_regu (ctx, term->et.et_alsm.elem, DOMAIN_CTX_COMPARE);
	      domain_walk_regu (ctx, term->et.et_alsm.elemset, DOMAIN_CTX_COMPARE);
	      break;
	    case T_LIKE_EVAL_TERM:
	      domain_walk_regu (ctx, term->et.et_like.src, DOMAIN_CTX_COMPARE);
	      domain_walk_regu (ctx, term->et.et_like.pattern, DOMAIN_CTX_COMPARE);
	      domain_walk_regu (ctx, term->et.et_like.esc_char, DOMAIN_CTX_COMPARE);
	      break;
	    case T_RLIKE_EVAL_TERM:
	      domain_walk_regu (ctx, term->et.et_rlike.src, DOMAIN_CTX_COMPARE);
	      domain_walk_regu (ctx, term->et.et_rlike.pattern, DOMAIN_CTX_COMPARE);
	      domain_walk_regu (ctx, term->et.et_rlike.case_sensitive, DOMAIN_CTX_COMPARE);
	      break;
	    }
	}
      return;
    }
}

/* A sort key reads column pos_no of the list it sorts: the producer's item is the key's item (X-2). An aggregate's
 * ORDER BY sorts the aggregate's own list, whose columns are its operands. */
static void
domain_walk_sort (DOMAIN_LOAD_CONTEXT * ctx, SORT_LIST * list, REGU_VARIABLE_LIST columns, XASL_NODE * source = NULL)
{
  for (; list != NULL && !ctx->failed; list = list->next)
    {
      QFILE_TUPLE_VALUE_POSITION *pos = &list->pos_descr;
      /* a block without an output list (a set operation) sorts its own list file: the key reads that list's column */
      DOMAIN_PLAN_ITEM *column = columns != NULL ? domain_column_item (ctx, columns, pos->pos_no, false)
	: domain_list_column (ctx, source, pos->pos_no);
      if (column != NULL)
	{
	  domain_bind_item (ctx, &pos->domain_plan, column);
	}
      else
	{
	  (void) domain_add_item (ctx, &pos->domain_plan, pos->dom, OPERAND_ROW, DOMAIN_CTX_LIST_COLUMN, 0,
				  "position");
	}
    }
}

static void
domain_walk_agg (DOMAIN_LOAD_CONTEXT * ctx, AGGREGATE_TYPE * agg)
{
  for (; agg != NULL && !ctx->failed; agg = agg->next)
    {
      if (agg->domain_plan != NULL)
	{
	  continue;
	}
      /* COUNT(*) and GROUPBY_NUM pack a default regu (TYPE_POS_VALUE 0, xasl_generation.c
       * "hack") only to carry a domain. It is never fetched, so it is no bind reference. */
      const bool has_operand = agg->function != PT_COUNT_STAR && agg->function != PT_GROUPBY_NUM;
      if (has_operand)
	{
	  domain_walk_list (ctx, agg->operands, DOMAIN_CTX_AGG);
	}
      if (agg->function == PT_PERCENTILE_CONT || agg->function == PT_PERCENTILE_DISC)
	{
	  /* the fraction is fetched with the execution's value descriptor (qdata_evaluate_aggregate_list) (#340) */
	  domain_walk_regu (ctx, agg->info.percentile.percentile_reguvar);
	}
      DOMAIN_PLAN_ITEM *item = domain_add_item (ctx, &agg->domain_plan, agg->domain, OPERAND_ROW,
						DOMAIN_CTX_AGG, agg->function, "aggregate");
      if (item != NULL)
	{
	  DOMAIN_LOAD_RECORD *record = domain_record_of (item);
	  record->output[0] = agg->accumulator.value;
	  if (has_operand && agg->operands != NULL)
	    {
	      REGU_VARIABLE *operand = &agg->operands->value;
	      domain_fixed_operand (item, 0, operand->domain, operand->domain, DOMAIN_CTX_FUNC_ARG);
	      /* the function, accumulator and list domains follow the argument (F7, L-43): the gate decides them when
	       * it decides the argument or the compiler left the function open; a compiled one gets its accumulator
	       * derived once (resolution pass) */
	      record->kind = DOMAIN_LOAD_FIXED_AGG;
	      domain_set_links (record, &operand, 1, agg->domain);
	      record->argument = agg->opr_dbtype != DB_TYPE_VARIABLE && domain_type_is_fixed (operand->domain)
		? operand->domain : NULL;
	      if (operand->domain_plan == NULL)
		{
		  record->n_link = -1;
		}
	    }
	}
      if (QPROC_IS_INTERPOLATION_FUNC (agg) && agg->domain_plan != NULL)
	{
	  /* MEDIAN / PERCENTILE sort values cast to the function's domain
	   * (qdata_update_agg_interpolation_func_value_and_domain sets the key to it): the key reads the aggregate */
	  for (SORT_LIST * key = agg->sort_list; key != NULL && !ctx->failed; key = key->next)
	    {
	      domain_bind_item (ctx, &key->pos_descr.domain_plan, agg->domain_plan);
	    }
	}
      else
	{
	  /* CUME_DIST / PERCENT_RANK wrap their ORDER BY values in one TYPE_REGU_VAR_LIST operand (X-1): those values
	   * are the columns of the list the key sorts */
	  REGU_VARIABLE_LIST columns = agg->operands;
	  if (columns != NULL && columns->value.type == TYPE_REGU_VAR_LIST)
	    {
	      columns = columns->value.value.regu_var_list;
	    }
	  domain_walk_sort (ctx, agg->sort_list, columns);
	}
    }
}

static void
domain_walk_analytic (DOMAIN_LOAD_CONTEXT * ctx, ANALYTIC_EVAL_TYPE * eval, OUTPTR_LIST * output)
{
  for (; eval != NULL && !ctx->failed; eval = eval->next)
    {
      for (ANALYTIC_TYPE * analytic = eval->head; analytic != NULL; analytic = analytic->next)
	{
	  if (analytic->domain_plan != NULL)
	    {
	      continue;
	    }
	  domain_walk_regu (ctx, &analytic->operand, DOMAIN_CTX_ANALYTIC);
	  DOMAIN_PLAN_ITEM *item = domain_add_item (ctx, &analytic->domain_plan, analytic->domain, OPERAND_ROW,
						    DOMAIN_CTX_ANALYTIC, analytic->function, "analytic");
	  if (item != NULL)
	    {
	      DOMAIN_LOAD_RECORD *record = domain_record_of (item);
	      REGU_VARIABLE *operand = &analytic->operand;
	      record->output[0] = analytic->value;
	      record->output[1] = analytic->out_value;
	      domain_fixed_operand (item, 0, operand->domain, operand->domain, DOMAIN_CTX_FUNC_ARG);
	      /* as an aggregate's: the operand is a value pointer into a_val_list, so its producer decides (#337) */
	      record->kind = DOMAIN_LOAD_FIXED_AGG;
	      domain_set_links (record, &operand, 1, analytic->domain);
	      record->argument = analytic->opr_dbtype != DB_TYPE_VARIABLE && domain_type_is_fixed (operand->domain)
		? operand->domain : NULL;
	      if (operand->domain_plan == NULL)
		{
		  record->n_link = -1;
		}
	    }
	}
      domain_walk_sort (ctx, eval->sort_list, output == NULL ? NULL : output->valptrp);
    }
}

static void
domain_walk_specs (DOMAIN_LOAD_CONTEXT * ctx, ACCESS_SPEC_TYPE * spec)
{
  for (; spec != NULL && !ctx->failed; spec = spec->next)
    {
      if (spec->indexptr != NULL)
	{
	  KEY_INFO *key = &spec->indexptr->key_info;
	  for (int i = 0; i < key->key_cnt; i++)
	    {
	      domain_walk_regu (ctx, key->key_ranges[i].key1, DOMAIN_CTX_KEY_ELEM);
	      domain_walk_regu (ctx, key->key_ranges[i].key2, DOMAIN_CTX_KEY_ELEM);
	    }
	  domain_walk_regu (ctx, key->key_limit_l);
	  domain_walk_regu (ctx, key->key_limit_u);
	}
      domain_walk_pred (ctx, spec->where_key);
      domain_walk_pred (ctx, spec->where_pred);
      domain_walk_pred (ctx, spec->where_range);
      switch (spec->type)
	{
	case TARGET_CLASS:
	case TARGET_CLASS_ATTR:
	  domain_walk_list (ctx, spec->s.cls_node.cls_regu_list_key);
	  domain_walk_list (ctx, spec->s.cls_node.cls_regu_list_pred);
	  domain_walk_list (ctx, spec->s.cls_node.cls_regu_list_rest);
	  domain_walk_list (ctx, spec->s.cls_node.cls_regu_list_range);
	  domain_walk_list (ctx, spec->s.cls_node.cls_regu_val_list);
	  domain_walk_list (ctx, spec->s.cls_node.cls_regu_list_reserved);
	  domain_walk_out (ctx, spec->s.cls_node.cls_output_val_list);
	  break;
	case TARGET_LIST:
	  {
	    /* the list regus read the list file of the spec's XASL: its columns are their producers */
	    XASL_NODE *source = spec->s.list_node.xasl_node;
	    domain_walk_xasl (ctx, source);
	    domain_walk_position_list (ctx, spec->s.list_node.list_regu_list_pred, source, NULL);
	    domain_walk_position_list (ctx, spec->s.list_node.list_regu_list_rest, source, NULL);
	    domain_walk_position_list (ctx, spec->s.list_node.list_regu_list_build, source, NULL);
	    domain_walk_position_list (ctx, spec->s.list_node.list_regu_list_probe, source, NULL);
	  }
	  break;
	case TARGET_JSON_TABLE:
	  domain_walk_regu (ctx, spec->s.json_table_node.m_json_reguvar);
	  break;
	case TARGET_SET:
	  domain_walk_regu (ctx, spec->s.set_node.set_ptr);
	  domain_walk_list (ctx, spec->s.set_node.set_regu_list);
	  break;
	case TARGET_METHOD:
	  domain_walk_xasl (ctx, spec->s.method_node.xasl_node);
	  domain_walk_list (ctx, spec->s.method_node.method_regu_list);
	  break;
	case TARGET_SHOWSTMT:
	  domain_walk_list (ctx, spec->s.showstmt_node.arg_list);
	  break;
	case TARGET_DBLINK:
	  domain_walk_list (ctx, spec->s.dblink_node.dblink_regu_list_pred);
	  domain_walk_list (ctx, spec->s.dblink_node.dblink_regu_list_rest);
	  domain_walk_list (ctx, spec->s.dblink_node.corr_key_regu_list);
	  break;
	case TARGET_REGUVAL_LIST:
	  domain_walk_out (ctx, spec->s.reguval_list_node.valptr_list);
	  break;
	default:
	  break;
	}
    }
}

static void
domain_walk_assignments (DOMAIN_LOAD_CONTEXT * ctx, UPDATE_ASSIGNMENT * assignments, int count)
{
  for (int i = 0; i < count; i++)
    {
      domain_walk_regu (ctx, assignments[i].regu_var, DOMAIN_CTX_ASSIGN);
    }
}

static void
domain_walk_xasl (DOMAIN_LOAD_CONTEXT * ctx, XASL_NODE * xasl)
{
  if (xasl == NULL || xasl->domain_plan != NULL || ctx->failed)
    {
      return;
    }
  XASL_NODE *previous_block = ctx->block;
  ctx->block = xasl;
  xasl->domain_plan = ctx->plan;
  domain_walk_xasl (ctx, xasl->aptr_list);
  domain_walk_xasl (ctx, xasl->bptr_list);
  domain_walk_xasl (ctx, xasl->dptr_list);
  domain_walk_xasl (ctx, xasl->fptr_list);
  domain_walk_xasl (ctx, xasl->connect_by_ptr);
  switch (xasl->type)
    {
    case BUILDLIST_PROC:
      domain_walk_xasl (ctx, xasl->proc.buildlist.eptr_list);
      break;
    case UNION_PROC:
    case DIFFERENCE_PROC:
    case INTERSECTION_PROC:
      domain_walk_xasl (ctx, xasl->proc.union_.left);
      domain_walk_xasl (ctx, xasl->proc.union_.right);
      break;
    case MERGELIST_PROC:
      domain_walk_xasl (ctx, xasl->proc.mergelist.outer_xasl);
      domain_walk_xasl (ctx, xasl->proc.mergelist.inner_xasl);
      domain_walk_specs (ctx, xasl->proc.mergelist.outer_spec_list);
      domain_walk_specs (ctx, xasl->proc.mergelist.inner_spec_list);
      break;
    case HASHJOIN_PROC:
      domain_walk_xasl (ctx, xasl->proc.hashjoin.outer.xasl);
      domain_walk_xasl (ctx, xasl->proc.hashjoin.inner.xasl);
      domain_walk_position_list (ctx, xasl->proc.hashjoin.outer.regu_list_pred, xasl->proc.hashjoin.outer.xasl, NULL);
      domain_walk_position_list (ctx, xasl->proc.hashjoin.inner.regu_list_pred, xasl->proc.hashjoin.inner.xasl, NULL);
      break;
    case CTE_PROC:
      domain_walk_xasl (ctx, xasl->proc.cte.non_recursive_part);
      domain_walk_xasl (ctx, xasl->proc.cte.recursive_part);
      break;
    case MERGE_PROC:
      domain_walk_xasl (ctx, xasl->proc.merge.update_xasl);
      domain_walk_xasl (ctx, xasl->proc.merge.insert_xasl);
      break;
    default:
      break;
    }
  domain_walk_specs (ctx, xasl->spec_list);
  domain_walk_specs (ctx, xasl->merge_spec);
  domain_walk_pred (ctx, xasl->during_join_pred);
  domain_walk_pred (ctx, xasl->after_join_pred);
  domain_walk_pred (ctx, xasl->if_pred);
  domain_walk_pred (ctx, xasl->instnum_pred);
  domain_walk_pred (ctx, xasl->ordbynum_pred);
  domain_walk_regu (ctx, xasl->orderby_limit);
  domain_walk_regu (ctx, xasl->limit_offset);
  domain_walk_regu (ctx, xasl->limit_row_count);
  domain_walk_regu (ctx, xasl->level_regu);
  domain_walk_regu (ctx, xasl->isleaf_regu);
  domain_walk_regu (ctx, xasl->iscycle_regu);
  for (SELUPD_LIST * p = xasl->selected_upd_list; p != NULL; p = p->next)
    {
      for (REGU_VARLIST_LIST list = p->select_list; list != NULL; list = list->next)
	{
	  domain_walk_list (ctx, list->list);
	}
    }
  switch (xasl->type)
    {
    case BUILDLIST_PROC:
      {
	BUILDLIST_PROC_NODE *b = &xasl->proc.buildlist;
	/* the scan output (the intermediate list the GROUP BY and analytic passes read) first: producers before
	 * their readers */
	domain_walk_out (ctx, xasl->outptr_list);
	REGU_VARIABLE_LIST scan_columns = xasl->outptr_list == NULL ? NULL : xasl->outptr_list->valptrp;
	domain_walk_list (ctx, b->g_scan_regu_list);
	domain_walk_position_list (ctx, b->g_regu_list, NULL, scan_columns);
	domain_walk_agg (ctx, b->g_agg_list);
	domain_walk_out (ctx, b->g_outptr_list);
	domain_walk_position_list (ctx, b->g_hk_sort_regu_list, NULL, scan_columns);
	domain_walk_list (ctx, b->g_hk_scan_regu_list);
	domain_walk_pred (ctx, b->g_having_pred);
	domain_walk_pred (ctx, b->g_grbynum_pred);
	domain_walk_sort (ctx, b->groupby_list, scan_columns);
	domain_walk_sort (ctx, b->after_groupby_list, b->g_outptr_list == NULL ? NULL : b->g_outptr_list->valptrp);
	domain_walk_list (ctx, b->a_scan_regu_list);
	domain_walk_position_list (ctx, b->a_regu_list, NULL, scan_columns);
	domain_walk_analytic (ctx, b->a_eval_list, b->a_outptr_list_ex);
	domain_walk_out (ctx, b->a_outptr_list);
	domain_walk_out (ctx, b->a_outptr_list_ex);
	domain_walk_out (ctx, b->a_outptr_list_interm);
      }
      break;
    case BUILDVALUE_PROC:
      domain_walk_agg (ctx, xasl->proc.buildvalue.agg_list);
      domain_walk_arith (ctx, xasl->proc.buildvalue.outarith_list);
      domain_walk_pred (ctx, xasl->proc.buildvalue.having_pred);
      break;
    case OBJFETCH_PROC:
      domain_walk_pred (ctx, xasl->proc.fetch.set_pred);
      break;
    case CONNECTBY_PROC:
      {
	CONNECTBY_PROC_NODE *b = &xasl->proc.connect_by;
	domain_walk_pred (ctx, b->start_with_pred);
	domain_walk_pred (ctx, b->after_connect_by_pred);
	domain_walk_list (ctx, b->regu_list_pred);
	domain_walk_list (ctx, b->regu_list_rest);
	domain_walk_list (ctx, b->prior_regu_list_pred);
	domain_walk_list (ctx, b->prior_regu_list_rest);
	domain_walk_list (ctx, b->after_cb_regu_list_pred);
	domain_walk_list (ctx, b->after_cb_regu_list_rest);
	domain_walk_out (ctx, b->prior_outptr_list);
      }
      break;
    case UPDATE_PROC:
      domain_walk_pred (ctx, xasl->proc.update.cons_pred);
      domain_walk_assignments (ctx, xasl->proc.update.assigns, xasl->proc.update.num_assigns);
      break;
    case INSERT_PROC:
      domain_walk_pred (ctx, xasl->proc.insert.cons_pred);
      for (int i = 0; i < xasl->proc.insert.num_val_lists; i++)
	{
	  if (xasl->proc.insert.valptr_lists[i] != NULL)
	    {
	      domain_walk_list (ctx, xasl->proc.insert.valptr_lists[i]->valptrp, DOMAIN_CTX_ASSIGN);
	    }
	}
      if (xasl->proc.insert.odku != NULL)
	{
	  domain_walk_pred (ctx, xasl->proc.insert.odku->cons_pred);
	  domain_walk_assignments (ctx, xasl->proc.insert.odku->assignments, xasl->proc.insert.odku->num_assigns);
	}
      break;
    default:
      break;
    }
  domain_walk_out (ctx, xasl->outptr_list);
  OUTPTR_LIST *output = domain_block_output (xasl);
  domain_walk_sort (ctx, xasl->orderby_list, output == NULL ? NULL : output->valptrp, xasl);
  domain_walk_sort (ctx, xasl->after_iscan_list, xasl->outptr_list == NULL ? NULL : xasl->outptr_list->valptrp);
  if (xasl->single_tuple != NULL)
    {
      /* a single-row subquery copies its result row into single_tuple (qdata_get_single_tuple_from_list_id):
       * value k there is written by list column k, which a scalar-subquery reader's value pointer then finds */
      int k = 0;
      for (QPROC_DB_VALUE_LIST value = xasl->single_tuple->valp; value != NULL && !ctx->failed; value = value->next)
	{
	  DOMAIN_LOAD_RECORD *column = domain_record_of (domain_list_column (ctx, xasl, k++));
	  if (column != NULL && column->output[1] == NULL)
	    {
	      column->output[1] = value->val;
	    }
	}
    }
  /* an uncorrelated scalar subquery runs once before the scan that reads it (qexec_execute_mainblock_internal), fetched
   * through the regu that owns it: a predicate operand, or a regu no predicate holds (an index key range reads a copy)
   * (#340) */
  domain_walk_regu (ctx, xasl->precomp_owner_regu, DOMAIN_CTX_COMPARE);
  domain_walk_xasl (ctx, xasl->scan_ptr);
  domain_walk_xasl (ctx, xasl->next);
  ctx->block = previous_block;
}

/*
 * Resolution pass (#337): after the walk every derived consumer reads its producer and every node the compiler left
 * without a type becomes a gate-dependent node when all its operands are known. It recurses producer first, so the
 * gate nodes come out in an order the gate can decide them in, whatever order the walk met them.
 */

static DOMAIN_LOAD_RECORD *
domain_owner_record (DOMAIN_LOAD_RECORD * record)
{
  return record != NULL && record->alias != NULL ? record->alias : record;
}

static bool
domain_is_value_pointer (const DOMAIN_LOAD_RECORD * record)
{
  return record->regu != NULL && record->regu->type == TYPE_CONSTANT;
}

/* GROUP_CONCAT accumulates in its compiled string type under the function's collation (qdata_group_concat_first_value):
 * the function domain the gate decides follows the argument (a CHAR bind makes it CHAR), the accumulator does not. An
 * output column reading it is retyped with the function domain before its first fetch (qexec_end_one_iteration, the
 * GROUP BY setup), so the list carries that domain; any other reader sees the accumulator's own value. */
static bool
domain_reads_group_concat_value (const DOMAIN_LOAD_RECORD * reader, const DOMAIN_LOAD_RECORD * producer)
{
  return producer->kind == DOMAIN_LOAD_FIXED_AGG && producer->cold.ctx == DOMAIN_CTX_AGG
    && producer->cold.opcode == PT_GROUP_CONCAT && reader->cold.ctx != DOMAIN_CTX_LIST_COLUMN;
}

static void domain_resolve_record (DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_LOAD_RECORD * record);

/* The item a node's operand is decided from: through value pointers, list positions and wrappers that share a slot
 * to the bind or node owning it, so the gate sees a bind's value (value classification, D-328-06). */
static DOMAIN_PLAN_ITEM *
domain_link_source (DOMAIN_PLAN_ITEM * item)
{
  for (int guard = 0; item != NULL && guard < 256; guard++)
    {
      DOMAIN_LOAD_RECORD *record = domain_owner_record (domain_record_of (item));
      if (!record->follows_producer || record->producer == NULL)
	{
	  return &record->item;
	}
      item = &record->producer->item;
    }
  return item;
}

/* A derived consumer reads its producer: the producer's slot (ALIAS) or its domain. A value pointer takes the
 * producer's domain even when it was compiled with another one (the reader's), because the value is the producer's;
 * a compiled list position or VALUES column keeps its domain, which is what its list holds. */
static void
domain_link_producer (DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_LOAD_RECORD * record)
{
  DOMAIN_PLAN_ITEM *item = &record->item;
  const bool value_pointer = domain_is_value_pointer (record);
  /* a compiled position keeps its domain unless the values give its collation (#338): then its list's column does */
  if (!value_pointer && domain_type_is_fixed (item->fixed.domain) && !domain_character_open (item->fixed.domain))
    {
      record->known = true;
      if (record->regu != NULL && record->regu->type == TYPE_POSITION && record->producer != NULL)
	{
	  /* the list holds its producer's values: when the producer carries a literal or a bind of the position's
	   * type, a node over the position classifies that value (D-328-06) as develop's first value does */
	  domain_resolve_record (ctx, record->producer);
	  DOMAIN_LOAD_RECORD *producer = domain_owner_record (record->producer);
	  const DOMAIN_LOAD_RECORD *root = domain_owner_record (domain_record_of (domain_link_source (&producer->item)));
	  const TP_DOMAIN *carried = root->item.fixed.domain;
	  if (root->literal_value && carried != NULL
	      && (TP_DOMAIN_TYPE (carried) == TP_DOMAIN_TYPE (item->fixed.domain)
		  || (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (carried)) && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (item->fixed.domain)))))
	    {
	      record->producer = producer;
	      record->follows_producer = true;
	    }
	}
      return;
    }
  if (record->producer == NULL)
    {
      /* a value pointer no walked node writes (an execution counter such as inst_num): its compiled domain */
      record->known = domain_type_is_fixed (item->fixed.domain);
      return;
    }
  domain_resolve_record (ctx, record->producer);
  DOMAIN_LOAD_RECORD *producer = domain_owner_record (record->producer);
  if (producer->state == 1 && producer->regu == NULL && producer->kind == DOMAIN_LOAD_NODE && producer->n_link > 0)
    {
      /* a recursive CTE part reads the column it is producing: its first iteration reads the non-recursive part's
       * rows (qexec_execute_cte), so that column is its producer; the CTE column itself unifies both parts */
      DOMAIN_LOAD_RECORD *first = domain_owner_record (domain_record_of (producer->link[0]));
      domain_resolve_record (ctx, first);
      producer = first;
    }
  if (!producer->known)
    {
      record->known = false;
      return;
    }
  if (value_pointer && domain_reads_group_concat_value (record, producer) && producer->item.slot >= 0
      && domain_character_open (item->fixed.domain))
    {
      /* a reader of the accumulator of a GROUP_CONCAT the gate decides takes the accumulator's string under the decided
       * collation, a gate-dependent node on the collation axis (#340): the resolver's GROUP_CONCAT rule */
      record->kind = DOMAIN_LOAD_NODE;
      record->cold.opcode = PT_GROUP_CONCAT;
      record->link[0] = &producer->item;
      record->literal[0] = NULL;
      record->n_link = 1;
      record->consumer = item->fixed.domain;
      item->flags |= DOMAIN_PLAN_COLLATION_GATE;
      domain_mark_gate_node (ctx, record);
      record->known = !ctx->failed;
      return;
    }
  if (producer->item.slot >= 0)
    {
      item->flags |= DOMAIN_PLAN_ALIAS;
      item->slot = producer->item.slot;
    }
  else
    {
      item->fixed.domain = producer->item.fixed.domain;
    }
  record->producer = producer;
  record->follows_producer = true;
  record->known = true;
}

/* A node over known operands becomes a gate-dependent node; a compiled aggregate or analytic gets its accumulator
 * domain derived once from its operand's (L-43). A set-operation column over compiled branches of one type is that
 * type. */
static void
domain_resolve_node (DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_LOAD_RECORD * record)
{
  DOMAIN_PLAN_ITEM *item = &record->item;
  const bool compiled = domain_type_is_fixed (item->fixed.domain);
  if (record->n_link <= 0)
    {
      record->known = record->kind == DOMAIN_LOAD_FIXED_AGG && compiled;
      return;
    }
  bool known = true, any_slot = false;
  for (int i = 0; i < record->n_link; i++)
    {
      record->link[i] = domain_link_source (record->link[i]);
      DOMAIN_LOAD_RECORD *operand = domain_owner_record (domain_record_of (record->link[i]));
      domain_resolve_record (ctx, operand);
      /* the link may have moved through a chain: re-read the source after the operand is resolved */
      record->link[i] = domain_link_source (&operand->item);
      operand = domain_owner_record (domain_record_of (record->link[i]));
      known = known && operand->known;
      any_slot = any_slot || operand->item.slot >= 0;
      if (operand->regu != NULL && operand->regu->type == TYPE_DBVAL)
	{
	  record->literal[i] = &operand->regu->value.dbval;
	}
    }
  if (record->kind == DOMAIN_LOAD_FIXED_AGG
      && (record->cold.opcode == PT_MEDIAN || record->cold.opcode == PT_PERCENTILE_CONT
	  || record->cold.opcode == PT_PERCENTILE_DISC))
    {
      /* D-335-10 at execution: a value argument is classified, a value-less string is DOUBLE. The argument of a
       * function over a list (GROUP BY, analytic) is a value pointer; its source tells which. */
      const DOMAIN_LOAD_RECORD *argument = domain_owner_record (domain_record_of (record->link[0]));
      if (argument->literal_value
	  || (argument->regu == NULL && argument->kind == DOMAIN_LOAD_NODE && argument->cold.opcode == T_EVALUATE_VARIABLE))
	{
	  item->flags |= DOMAIN_PLAN_VALUE_ARGUMENT;
	}
    }
  if (!known)
    {
      record->known = compiled && record->kind == DOMAIN_LOAD_FIXED_AGG;
      return;
    }
  if (record->kind == DOMAIN_LOAD_FIXED_AGG && compiled && !any_slot)
    {
      const DOMAIN_PLAN_ITEM *argument = record->link[0];
      DOMAIN_OPERAND operand = { argument->fixed.domain, TP_DOMAIN_TYPE (argument->fixed.domain), -1, -1, false };
      RESOLVED_DOMAIN resolved;
      bool needs_gate = false;
      if (domain_type_is_fixed (argument->fixed.domain)
	  && domain_resolve ((DOMAIN_CTX) record->cold.ctx, record->cold.opcode, &operand, 1, item->fixed.domain,
			     &resolved, &needs_gate) == NO_ERROR && !needs_gate && resolved.domain != NULL)
	{
	  /* the function domain stays the compiled one; the accumulator is the operand's rule */
	  item->fixed.operand_domain[0] = resolved.operand_domain[0];
	  item->fixed.conv[0] = resolved.conv[0];
	  item->flags |= DOMAIN_PLAN_ACCUMULATOR;
	}
      record->known = true;
      return;
    }
  if (record->regu == NULL && record->kind == DOMAIN_LOAD_NODE && record->cold.ctx == DOMAIN_CTX_LIST_COLUMN
      && !any_slot)
    {
      /* a set-operation column over compiled branches: one type, or the gate unifies them at execution */
      const TP_DOMAIN *first = record->link[0]->fixed.domain;
      bool same = true;
      for (int i = 1; i < record->n_link; i++)
	{
	  same = same && TP_DOMAIN_TYPE (record->link[i]->fixed.domain) == TP_DOMAIN_TYPE (first);
	}
      if (same)
	{
	  item->fixed.domain = first;
	  record->known = true;
	  return;
	}
    }
  domain_mark_gate_node (ctx, record);
  if (record->kind == DOMAIN_LOAD_NODE && record->cold.ctx != DOMAIN_CTX_LIST_COLUMN)
    {
      record->cold.ctx = domain_gate_context ((OPERATOR_TYPE) record->cold.opcode);
    }
  record->known = !ctx->failed;
}

static void
domain_resolve_record (DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_LOAD_RECORD * record)
{
  record = domain_owner_record (record);
  if (record == NULL || record->state != 0 || ctx->failed)
    {
      /* done, or in progress: a recursive CTE part reads the column it produces; the reader stays open */
      return;
    }
  record->state = 1;
  DOMAIN_PLAN_ITEM *item = &record->item;
  switch (record->kind)
    {
    case DOMAIN_LOAD_CONSUMER:
      domain_link_producer (ctx, record);
      break;
    case DOMAIN_LOAD_ARITH_REGU:
      {
	domain_resolve_record (ctx, record->producer);
	DOMAIN_LOAD_RECORD *node = domain_owner_record (record->producer);
	item->fixed = node->item.fixed;
	if (node->item.flags & DOMAIN_PLAN_GATE)
	  {
	    item->flags |= DOMAIN_PLAN_ALIAS;
	    item->slot = node->item.slot;
	  }
	record->producer = node;
	record->follows_producer = true;
	record->known = node->known;
      }
      break;
    case DOMAIN_LOAD_NODE:
    case DOMAIN_LOAD_FIXED_AGG:
      domain_resolve_node (ctx, record);
      break;
    default:
      record->known = item->slot >= 0 || record->literal_value || domain_type_is_fixed (item->fixed.domain);
      break;
    }
  record->state = 2;
}

/* Boundary (a): both axes are strict (#338 removed X-11): an item the gate does not decide has a fixed type, and a
 * fixed string whose collation the values give is a slot recording its bound value's domain. */
bool
domain_plan_validate (const DOMAIN_PLAN * plan)
{
  for (int i = 0; i < plan->n_items; i++)
    {
      const DOMAIN_PLAN_ITEM *item = &plan->items[i];
      if ((item->flags & (DOMAIN_PLAN_GATE | DOMAIN_PLAN_ALIAS)) || plan->items_cold[i].name == domain_list_column_name)
	{
	  /* a synthetic list column no reader could use leaves its readers open, and they answer for it */
	  continue;
	}
      if (!domain_type_is_fixed (item->fixed.domain))
	{
	  return false;
	}
      if (domain_character_open (item->fixed.domain) && !(item->flags & DOMAIN_PLAN_COLLATION_GATE))
	{
	  return false;
	}
    }
  return true;
}

static void *
domain_plan_alloc (THREAD_ENTRY * thread_p, int count, size_t size)
{
  if (count == 0)
    {
      return NULL;
    }
  if (count < 0 || size > INT_MAX / (size_t) count)
    {
      return NULL;
    }
  return stx_alloc_struct (thread_p, (int) (count * size));
}

static int
domain_compare_refs (const void *lhs, const void *rhs)
{
  const DOMAIN_PLAN_ITEM *a = *(const DOMAIN_PLAN_ITEM * const *) lhs;
  const DOMAIN_PLAN_ITEM *b = *(const DOMAIN_PLAN_ITEM * const *) rhs;
  if (a->ref != b->ref)
    {
      return a->ref < b->ref ? -1 : 1;
    }
  return a < b ? -1 : a > b ? 1 : 0;
}

int
stx_build_domain_plan (THREAD_ENTRY * thread_p, XASL_NODE * root, XASL_UNPACK_INFO * unpack_info, bool is_pred_stream)
{
  if (root->domain_plan != NULL)
    {
      return NO_ERROR;
    }
  assert (unpack_info == get_xasl_unpack_info_ptr (thread_p));
  DOMAIN_PLAN *plan = (DOMAIN_PLAN *) stx_alloc_struct (thread_p, sizeof (*plan));
  if (plan == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memset (plan, 0, sizeof (*plan));
  plan->dbval_cnt = root->dbval_cnt;
  plan->n_refs = root->dbval_cnt;
  DOMAIN_LOAD_CONTEXT ctx;
  memset (&ctx, 0, sizeof (ctx));
  ctx.thread_p = thread_p;
  ctx.plan = plan;
  domain_walk_xasl (&ctx, root);
  /* Output/list readers borrow their producer's answer. Match the restored
   * value identity, not a column ordinal from a different XASL block. Keep an
   * independent item when the consumer has a different compiled domain. */
  for (DOMAIN_LOAD_RECORD * r = ctx.head; r != NULL && !ctx.failed; r = r->next)
    {
      if (r->regu == NULL || r->regu->type != TYPE_CONSTANT || r->regu->value.dbvalptr == NULL)
	{
	  continue;
	}
      for (DOMAIN_LOAD_RECORD * p = ctx.head; p != NULL; p = p->next)
	{
	  if (p == r || (p->regu != NULL && p->regu->type == TYPE_CONSTANT))
	    {
	      continue;
	    }
	  if ((p->output[0] == r->regu->value.dbvalptr || p->output[1] == r->regu->value.dbvalptr)
	      && p->item.fixed.domain == r->item.fixed.domain
	      && p->item.operand_class == r->item.operand_class
	      && p->item.flags == r->item.flags && p->item.fail[0] == r->item.fail[0]
	      && !domain_reads_group_concat_value (r, p))
	    {
	      r->alias = p;
	      break;
	    }
	}
    }
  /* A value pointer's producer is the node writing the value it points at (#337, F-335-07): a fetch into a value
   * list, an arithmetic result, an accumulator, an analytic result, a single-row subquery's column. */
  for (DOMAIN_LOAD_RECORD * r = ctx.head; r != NULL && !ctx.failed; r = r->next)
    {
      if (r->alias != NULL || r->regu == NULL || r->regu->type != TYPE_CONSTANT || r->regu->value.dbvalptr == NULL)
	{
	  continue;
	}
      /* a writer that is itself a value pointer counts too: a scalar subquery's BUILDVALUE column points at its
       * accumulator and is copied into single_tuple */
      for (DOMAIN_LOAD_RECORD * p = ctx.head; p != NULL; p = p->next)
	{
	  if (p != r && (p->output[0] == r->regu->value.dbvalptr || p->output[1] == r->regu->value.dbvalptr))
	    {
	      r->producer = p;
	      break;
	    }
	}
    }
  /* CTE columns first: a recursive part then meets its own column in progress and reads the non-recursive column in
   * its place, whichever record the walk met first */
  for (DOMAIN_LOAD_LIST_COLUMN * c = ctx.list_columns; c != NULL && !ctx.failed; c = c->next)
    {
      if (c->xasl->type == CTE_PROC)
	{
	  domain_resolve_record (&ctx, domain_record_of (c->item));
	}
    }
  for (DOMAIN_LOAD_RECORD * r = ctx.head; r != NULL && !ctx.failed; r = r->next)
    {
      domain_resolve_record (&ctx, r);
    }
  plan->n_items = 0;
  for (DOMAIN_LOAD_RECORD * r = ctx.head; r != NULL; r = r->next)
    {
      if (r->alias == NULL)
	{
	  r->index = plan->n_items++;
	}
    }
  for (DOMAIN_LOAD_RECORD * r = ctx.head; r != NULL; r = r->next)
    {
      if (r->alias != NULL)
	{
	  r->index = r->alias->index;
	}
    }
  /* A nested parser_generate_xasl () restarts parser->dbval_cnt, so root->dbval_cnt can
   * understate the positions the tree references (qmgr then sees surplus values). The
   * plan covers every referenced position so secondary references never overlap one. */
  for (DOMAIN_LOAD_RECORD * r = ctx.head; r != NULL; r = r->next)
    {
      if (r->cold.val_pos >= plan->dbval_cnt)
	{
	  plan->dbval_cnt = r->cold.val_pos + 1;
	}
    }
  plan->n_refs = plan->dbval_cnt;
  /* References are assigned in deterministic traversal order. The first use of
   * each bind keeps val_pos; only a different (domain, failure policy) adds a value. */
  for (DOMAIN_LOAD_RECORD * r = ctx.head; r != NULL && !ctx.failed; r = r->next)
    {
      if (r->alias != NULL)
	{
	  continue;
	}
      if (r->cold.val_pos >= 0)
	{
	  bool first = true;
	  for (DOMAIN_LOAD_RECORD * p = ctx.head; p != r; p = p->next)
	    {
	      if (p->cold.val_pos != r->cold.val_pos)
		{
		  continue;
		}
	      first = false;
	      if (p->item.fixed.domain == r->item.fixed.domain && p->item.fail[0] == r->item.fail[0])
		{
		  r->item.ref = p->item.ref;
		  break;
		}
	    }
	  if (r->item.ref < 0)
	    {
	      r->item.ref = first ? r->cold.val_pos : plan->n_refs++;
	    }
	}
      if (r->item.operand_class == OPERAND_CONST)
	{
	  plan->n_const_refs++;
	}
      if (r->item.operand_class == OPERAND_VOLATILE)
	{
	  plan->n_volatile++;
	}
    }
  /* gate-dependent nodes in resolution order: producers first (#337) */
  plan->n_gate_nodes = ctx.n_gate_order;
  plan->items = (DOMAIN_PLAN_ITEM *) domain_plan_alloc (thread_p, plan->n_items, sizeof (*plan->items));
  plan->items_cold = (DOMAIN_PLAN_ITEM_COLD *) domain_plan_alloc (thread_p, plan->n_items, sizeof (*plan->items_cold));
  plan->gate_nodes = (DOMAIN_PLAN_ITEM **) domain_plan_alloc (thread_p, plan->n_gate_nodes, sizeof (*plan->gate_nodes));
  plan->gate_links = (DOMAIN_GATE_LINK *) domain_plan_alloc (thread_p, plan->n_gate_nodes, sizeof (*plan->gate_links));
  plan->const_refs = (DOMAIN_PLAN_ITEM **) domain_plan_alloc (thread_p, plan->n_const_refs, sizeof (*plan->const_refs));
  plan->volatile_refs =
    (DOMAIN_PLAN_ITEM **) domain_plan_alloc (thread_p, plan->n_volatile, sizeof (*plan->volatile_refs));
  plan->slot_gate_node = (int *) domain_plan_alloc (thread_p, plan->n_slots, sizeof (*plan->slot_gate_node));
  plan->slot_flags = (unsigned char *) domain_plan_alloc (thread_p, plan->n_slots, sizeof (*plan->slot_flags));
  plan->slot_volatile_reads =
    (unsigned long long *) domain_plan_alloc (thread_p, plan->n_slots, sizeof (*plan->slot_volatile_reads));
  ctx.failed = ctx.failed || (plan->n_items && (!plan->items || !plan->items_cold))
    || (plan->n_gate_nodes && (!plan->gate_nodes || !plan->gate_links))
    || (plan->n_const_refs && !plan->const_refs) || (plan->n_volatile && !plan->volatile_refs)
    || (plan->n_slots && (!plan->slot_gate_node || !plan->slot_flags || !plan->slot_volatile_reads));
  int constant = 0, vol = 0;
  for (DOMAIN_LOAD_RECORD * r = ctx.head; r != NULL; r = r->next)
    {
      if (!ctx.failed)
	{
	  DOMAIN_PLAN_ITEM *item = &plan->items[r->index];
	  *r->owner = item;
	  if (r->alias == NULL)
	    {
	      *item = r->item;
	      plan->items_cold[r->index] = r->cold;
	      if (item->operand_class == OPERAND_CONST)
		{
		  plan->const_refs[constant++] = item;
		}
	      if (item->operand_class == OPERAND_VOLATILE)
		{
		  plan->volatile_refs[vol++] = item;
		}
	    }
	  for (DOMAIN_LOAD_BINDING * b = ctx.bindings; b != NULL; b = b->next)
	    {
	      if (b->target == &r->item)
		{
		  *b->owner = item;
		}
	    }
	}
      else
	{
	  *r->owner = NULL;
	}
    }
  if (!ctx.failed)
    {
      for (int i = 0; i < plan->n_slots; i++)
	{
	  plan->slot_gate_node[i] = -1;
	  plan->slot_flags[i] = 0;
	  plan->slot_volatile_reads[i] = 0;
	}
      int n_reads = 0;
      for (int g = 0; g < ctx.n_gate_order; g++)
	{
	  /* every operand is a record's item; its published copy sits at that record's index */
	  DOMAIN_LOAD_RECORD *r = ctx.gate_order[g];
	  DOMAIN_GATE_LINK *link = &plan->gate_links[g];
	  memset (link, 0, sizeof (*link));
	  link->n_operands = r->n_link;
	  link->consumer = r->consumer;
	  link->argument = r->argument;
	  /* a node's decision inherits its sources' limits (producers come first, so theirs are set) */
	  unsigned char flags = 0;
	  unsigned long long reads = 0;
	  if (r->item.operand_class == OPERAND_VOLATILE)
	    {
	      flags |= DOMAIN_SLOT_VOLATILE;
	    }
	  if (r->cold.opcode == T_EVALUATE_VARIABLE)
	    {
	      /* a session variable read is its own source: a decision above it holds while no read it depends on has
	       * left the gate's decision (D-336-E, #340) */
	      reads = n_reads < 63 ? 1ULL << n_reads : 1ULL << 63;
	      n_reads++;
	    }
	  for (int i = 0; i < r->n_link; i++)
	    {
	      const DOMAIN_LOAD_RECORD *source = domain_owner_record (domain_record_of (r->link[i]));
	      link->operands[i] = &plan->items[source->index];
	      link->literal[i] = r->literal[i];
	      if (source->item.slot >= 0)
		{
		  flags |= plan->slot_flags[source->item.slot];
		  reads |= plan->slot_volatile_reads[source->item.slot];
		}
	    }
	  plan->gate_nodes[g] = &plan->items[r->index];
	  plan->slot_gate_node[r->item.slot] = g;
	  plan->slot_flags[r->item.slot] = flags;
	  plan->slot_volatile_reads[r->item.slot] = reads;
	}
    }
  if (ctx.gate_order != NULL)
    {
      db_private_free (thread_p, ctx.gate_order);
    }
  while (ctx.list_columns != NULL)
    {
      DOMAIN_LOAD_LIST_COLUMN *c = ctx.list_columns;
      ctx.list_columns = c->next;
      db_private_free (thread_p, c);
    }
  while (ctx.bindings != NULL)
    {
      DOMAIN_LOAD_BINDING *b = ctx.bindings;
      ctx.bindings = b->next;
      if (ctx.failed)
	{
	  *b->owner = NULL;
	}
      db_private_free (thread_p, b);
    }
  while (ctx.head != NULL)
    {
      DOMAIN_LOAD_RECORD *r = ctx.head;
      ctx.head = r->next;
      db_private_free (thread_p, r);
    }
  if (ctx.failed)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  if (plan->n_const_refs > 1)
    {
      qsort (plan->const_refs, plan->n_const_refs, sizeof (*plan->const_refs), domain_compare_refs);
    }
  /* Predicate streams are wired at the final boundary ticket. They have no
   * XASL root today; no persisted regu/arith/predicate layout changes here. */
  (void) is_pred_stream;
  (void) domain_plan_load_exceptions;
  if (domain_plan_check_load && !domain_plan_validate (plan))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DOMAIN_UNRESOLVED, 4, "load",
	      root->query_alias != NULL ? root->query_alias : "", plan->n_items, pr_type_name (DB_TYPE_VARIABLE));
      return ER_QPROC_DOMAIN_UNRESOLVED;
    }
  return NO_ERROR;
}
