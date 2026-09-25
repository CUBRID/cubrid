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
#include "dbtype.h"
#include "error_manager.h"
#include "object_primitive.h"
#include "xasl.h"
#include "xasl_aggregate.hpp"
#include "xasl_analytic.hpp"
#include "xasl_predicate.hpp"
#include "xasl_stream.hpp"
#include "xasl_unpack_info.hpp"
#include "query_hash_join.h"
#include "fetch.h"
#include "system_parameter.h"
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
/* An ALL/SOME term the walk met (#352): the list column its item compares with, bound to the published item. */
struct DOMAIN_LOAD_ELEMENT_TERM
{
  DOMAIN_LOAD_ELEMENT_TERM *next;
  ALSM_EVAL_TERM *term;
  DOMAIN_PLAN_ITEM *list_column;
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
  COMP_EVAL_TERM **compare_terms;	/* the comparison terms met, in walk order (#352) */
  int n_compare_terms;
  int max_compare_terms;
  DOMAIN_LOAD_ELEMENT_TERM *element_terms;	/* the ALL/SOME terms met, last first (#352) */
  int n_element_terms;
  INDX_INFO **indexes;		/* the index scans met, in walk order (#342) */
  int n_indexes;
  int max_indexes;
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

/* develop's fetch cached a function over constant operands only for these (the FETCH_ALL_CONST decision of
 * TYPE_FUNC); every other function computed each time it was fetched, so it is no constant the gate evaluates (#352) */
static bool
domain_function_caches (FUNC_CODE ftype)
{
  switch (ftype)
    {
    case F_JSON_ARRAY:
    case F_JSON_ARRAY_APPEND:
    case F_JSON_ARRAY_INSERT:
    case F_JSON_CONTAINS:
    case F_JSON_CONTAINS_PATH:
    case F_JSON_DEPTH:
    case F_JSON_EXTRACT:
    case F_JSON_GET_ALL_PATHS:
    case F_JSON_KEYS:
    case F_JSON_INSERT:
    case F_JSON_LENGTH:
    case F_JSON_MERGE:
    case F_JSON_MERGE_PATCH:
    case F_JSON_OBJECT:
    case F_JSON_PRETTY:
    case F_JSON_QUOTE:
    case F_JSON_REMOVE:
    case F_JSON_REPLACE:
    case F_JSON_SEARCH:
    case F_JSON_SET:
    case F_JSON_TYPE:
    case F_JSON_UNQUOTE:
    case F_JSON_VALID:
    case F_REGEXP_COUNT:
    case F_REGEXP_INSTR:
    case F_REGEXP_LIKE:
    case F_REGEXP_REPLACE:
    case F_REGEXP_SUBSTR:
    case F_INSERT_SUBSTRING:
    case F_ELT:
      return true;
    default:
      return false;
    }
}

/* develop recomputed a BENCHMARK target and a stored procedure's arguments at every call: fetch marked them not
 * constant through regu_variable_node::map_regu (arithmetic left and right operands, function operands, procedure
 * arguments, value and regu lists), so none of them is a constant the gate evaluates once (#352) */
static void
domain_force_row (REGU_VARIABLE * regu)
{
  if (regu == NULL)
    {
      return;
    }
  /* only the nodes develop marked: a bind or a literal under them stays a constant */
  const bool node = regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH || regu->type == TYPE_FUNC
    || regu->type == TYPE_SP;
  if (node && regu->domain_plan != NULL && regu->domain_plan->operand_class == OPERAND_CONST)
    {
      regu->domain_plan->operand_class = OPERAND_ROW;
    }
  switch (regu->type)
    {
    case TYPE_INARITH:
    case TYPE_OUTARITH:
      if (regu->value.arithptr != NULL)
	{
	  DOMAIN_PLAN_ITEM *arith = regu->value.arithptr->domain_plan;
	  if (arith != NULL && arith->operand_class == OPERAND_CONST)
	    {
	      arith->operand_class = OPERAND_ROW;
	    }
	  domain_force_row (regu->value.arithptr->leftptr);
	  domain_force_row (regu->value.arithptr->rightptr);
	}
      break;
    case TYPE_FUNC:
      for (REGU_VARIABLE_LIST op = regu->value.funcp->operand; op != NULL; op = op->next)
	{
	  domain_force_row (&op->value);
	}
      break;
    case TYPE_SP:
      for (REGU_VARIABLE_LIST arg = regu->value.sp_ptr->args; arg != NULL; arg = arg->next)
	{
	  domain_force_row (&arg->value);
	}
      break;
    case TYPE_REGUVAL_LIST:
      for (REGU_VALUE_ITEM * item = regu->value.reguval_list->regu_list; item != NULL; item = item->next)
	{
	  domain_force_row (item->value);
	}
      break;
    case TYPE_REGU_VAR_LIST:
      for (REGU_VARIABLE_LIST node = regu->value.regu_var_list; node != NULL; node = node->next)
	{
	  domain_force_row (&node->value);
	}
      break;
    default:
      break;
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
 * output list with them (qexec_plan_sort_list_domains). */
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

/* Every column of a set operation's or a CTE's list is a node over its branches' columns, read or not (#341): the gate
 * rejects branches it cannot unify before execution (qexec_resolve_gate_node), whether a reader asks for the column
 * or the list is the statement's result. */
static void
domain_walk_set_columns (DOMAIN_LOAD_CONTEXT * ctx, XASL_NODE * xasl)
{
  /* the list holds the columns of its first branch */
  XASL_NODE *first = xasl;
  while (first != NULL && (first->type == UNION_PROC || first->type == DIFFERENCE_PROC
			   || first->type == INTERSECTION_PROC || first->type == CTE_PROC))
    {
      first = first->type == CTE_PROC ? first->proc.cte.non_recursive_part : first->proc.union_.left;
    }
  OUTPTR_LIST *output = first == NULL ? NULL : domain_block_output (first);
  int pos = 0;
  for (REGU_VARIABLE_LIST col = output == NULL ? NULL : output->valptrp; col != NULL && !ctx->failed; col = col->next)
    {
      if (!REGU_VARIABLE_IS_FLAGED (&col->value, REGU_VARIABLE_HIDDEN_COLUMN))
	{
	  (void) domain_list_column (ctx, xasl, pos++);
	}
    }
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
      cls = domain_function_caches (regu->value.funcp->ftype) ? OPERAND_CONST : OPERAND_ROW;
      /* develop's fetch never cached these functions, even when their
       * argument list consists entirely of constants. */
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
      if (regu->value.funcp->ftype == F_BENCHMARK && regu->value.funcp->operand != NULL
	  && regu->value.funcp->operand->next != NULL)
	{
	  /* the target, recomputed at every iteration */
	  domain_force_row (&regu->value.funcp->operand->next->value);
	}
      for (REGU_VARIABLE_LIST op = regu->value.funcp->operand; op != NULL; op = op->next)
	{
	  cls = domain_merge_class (cls, op->value.domain_plan);
	}
      break;
    case TYPE_SP:
      domain_walk_list (ctx, regu->value.sp_ptr->args, DOMAIN_CTX_FUNC_ARG);
      for (REGU_VARIABLE_LIST arg = regu->value.sp_ptr->args; arg != NULL; arg = arg->next)
	{
	  domain_force_row (&arg->value);
	}
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

/* A comparison term of two values (#352): the load decides it or gives it a gate site when the plan is published.
 * The set comparisons (subset and superset, tp_set_compare) and a list side (eval_set_list_cmp) are not these. */
static void
domain_add_compare_term (DOMAIN_LOAD_CONTEXT * ctx, COMP_EVAL_TERM * term)
{
  switch (term->rel_op)
    {
    case R_EQ:
    case R_NE:
    case R_GT:
    case R_GE:
    case R_LT:
    case R_LE:
    case R_EQ_TORDER:
    case R_NULLSAFE_EQ:
      break;
    default:
      return;
    }
  if (term->lhs == NULL || term->rhs == NULL || term->lhs->type == TYPE_LIST_ID || term->rhs->type == TYPE_LIST_ID)
    {
      return;
    }
  if (ctx->n_compare_terms == ctx->max_compare_terms)
    {
      int max = ctx->max_compare_terms == 0 ? 16 : ctx->max_compare_terms * 2;
      COMP_EVAL_TERM **terms =
	(COMP_EVAL_TERM **) db_private_realloc (ctx->thread_p, ctx->compare_terms, max * sizeof (*terms));
      if (terms == NULL)
	{
	  ctx->failed = true;
	  return;
	}
      ctx->compare_terms = terms;
      ctx->max_compare_terms = max;
    }
  ctx->compare_terms[ctx->n_compare_terms++] = term;
}

/* An ALL/SOME term (#352, D-352-03): its comparisons are published with the plan; a list's column is found now. */
static void
domain_add_element_term (DOMAIN_LOAD_CONTEXT * ctx, ALSM_EVAL_TERM * term)
{
  if (term->elem == NULL || term->elemset == NULL || ctx->failed)
    {
      return;
    }
  DOMAIN_LOAD_ELEMENT_TERM *entry = (DOMAIN_LOAD_ELEMENT_TERM *) db_private_alloc (ctx->thread_p, sizeof (*entry));
  if (entry == NULL)
    {
      ctx->failed = true;
      return;
    }
  entry->term = term;
  entry->list_column = NULL;
  entry->next = ctx->element_terms;
  ctx->element_terms = entry;
  ctx->n_element_terms++;
  if (term->elemset->type == TYPE_LIST_ID)
    {
      /* the item compares with column 0 of the list its subquery produces (eval_some_list_eval) */
      domain_bind_item (ctx, &entry->list_column, domain_list_column (ctx, term->elemset->xasl, 0));
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
	      domain_add_compare_term (ctx, &term->et.et_comp);
	      break;
	    case T_ALSM_EVAL_TERM:
	      domain_walk_regu (ctx, term->et.et_alsm.elem, DOMAIN_CTX_COMPARE);
	      domain_walk_regu (ctx, term->et.et_alsm.elemset, DOMAIN_CTX_COMPARE);
	      domain_add_element_term (ctx, &term->et.et_alsm);
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
	  /* MEDIAN / PERCENTILE sort values cast to the function's domain, the type their list holds (#341): the key
	   * reads the aggregate */
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

/* An index scan the walk meets: its key plan is published once its elements' items are (#342). */
static void
domain_add_index (DOMAIN_LOAD_CONTEXT * ctx, INDX_INFO * index)
{
  for (int i = 0; i < ctx->n_indexes; i++)
    {
      if (ctx->indexes[i] == index)
	{
	  return;
	}
    }
  if (ctx->n_indexes == ctx->max_indexes)
    {
      const int max = ctx->max_indexes == 0 ? 4 : ctx->max_indexes * 2;
      INDX_INFO **indexes = (INDX_INFO **) db_private_realloc (ctx->thread_p, ctx->indexes, max * sizeof (*indexes));
      if (indexes == NULL)
	{
	  ctx->failed = true;
	  return;
	}
      ctx->indexes = indexes;
      ctx->max_indexes = max;
    }
  ctx->indexes[ctx->n_indexes++] = index;
}

static void
domain_walk_specs (DOMAIN_LOAD_CONTEXT * ctx, ACCESS_SPEC_TYPE * spec)
{
  for (; spec != NULL && !ctx->failed; spec = spec->next)
    {
      if (spec->indexptr != NULL)
	{
	  domain_add_index (ctx, spec->indexptr);
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

/*
 * domain_fix_connect_by_probe () - the probe domain of a START WITH ... CONNECT BY hash list scan (#341, S-21)
 *
 * The scan coerces its probe values to the first probe item's domain. When the join widened that domain to a float
 * NUMERIC, the first fixed-precision NUMERIC of the rest list gives its precision and scale, so integers scale the way
 * the fixed numeric column's hash keys do. Only compiled domains decide this: the load sets it once, where develop's
 * qexec_execute_connect_by set a copy at every execution.
 */
static void
domain_fix_connect_by_probe (DOMAIN_LOAD_CONTEXT * ctx, XASL_NODE * xasl)
{
  ACCESS_SPEC_TYPE *spec = xasl->spec_list;
  if (spec == NULL || spec->type != TARGET_LIST || spec->s.list_node.list_regu_list_probe == NULL)
    {
      return;
    }
  REGU_VARIABLE *probe = &spec->s.list_node.list_regu_list_probe->value;
  if (REGU_VARIABLE_GET_TYPE (probe) != DB_TYPE_NUMERIC || probe->domain == NULL
      || probe->domain->precision != DB_DEFAULT_NUMERIC_PRECISION)
    {
      return;
    }
  const TP_DOMAIN *fixed = NULL;
  for (REGU_VARIABLE_LIST rest = spec->s.list_node.list_regu_list_rest; rest != NULL && fixed == NULL;
       rest = rest->next)
    {
      if (TP_DOMAIN_TYPE (rest->value.domain) == DB_TYPE_NUMERIC
	  && rest->value.domain->precision != DB_DEFAULT_NUMERIC_PRECISION)
	{
	  fixed = rest->value.domain;
	}
    }
  if (fixed == NULL)
    {
      return;
    }
  TP_DOMAIN *domain = tp_domain_copy (probe->domain, false);
  if (domain == NULL)
    {
      ctx->failed = true;
      return;
    }
  domain->precision = fixed->precision;
  domain->scale = fixed->scale;
  /* the clone restores original_domain at every clear (qexec_clear_regu_var) */
  probe->domain = probe->original_domain = tp_domain_cache (domain);
}

/*
 * domain_mark_aggregate_operands () - flag the expressions that only feed a SUM / AVG (REGU_VARIABLE_AGG_OPERAND), once
 *   at load (#341, D-341-07; each execution marked them before its scan, and each PX worker its own copy)
 *
 * BUILDLIST reaches the aggregate through a TYPE_CONSTANT operand pointing to the DB_VALUE the scan's expression writes
 * (regu->vfetch_to); BUILDVALUE's operand is the expression itself. fetch_peek_arith evaluates a flagged expression in
 * one register pass; the shape check reads the compiled domains, which an execution's regus hold again before its scan
 * (qexec_clear_regu_var). Analytic functions read their operands from list columns: nothing to mark there.
 */
static void
domain_mark_aggregate_operands (XASL_NODE * xasl)
{
  const int budget = prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH);
  if (xasl->type == BUILDVALUE_PROC)
    {
      for (AGGREGATE_TYPE * agg = xasl->proc.buildvalue.agg_list; agg != NULL; agg = agg->next)
	{
	  if ((agg->function == PT_SUM || agg->function == PT_AVG) && agg->option != Q_DISTINCT
	      && agg->operands != NULL && agg->operands->value.type == TYPE_INARITH
	      && fetch_is_agg_expr_shape (&agg->operands->value, budget))
	    {
	      REGU_VARIABLE_SET_FLAG (&agg->operands->value, REGU_VARIABLE_AGG_OPERAND);
	    }
	}
      return;
    }
  if (xasl->type != BUILDLIST_PROC || xasl->proc.buildlist.g_agg_list == NULL || xasl->outptr_list == NULL)
    {
      return;
    }
  for (REGU_VARIABLE_LIST regu = xasl->outptr_list->valptrp; regu != NULL; regu = regu->next)
    {
      if (regu->value.type != TYPE_INARITH || regu->value.vfetch_to == NULL)
	{
	  continue;
	}
      for (AGGREGATE_TYPE * agg = xasl->proc.buildlist.g_agg_list; agg != NULL; agg = agg->next)
	{
	  if ((agg->function == PT_SUM || agg->function == PT_AVG) && agg->option != Q_DISTINCT
	      && agg->operands != NULL && agg->operands->value.type == TYPE_CONSTANT
	      && agg->operands->value.value.dbvalptr == regu->value.vfetch_to)
	    {
	      if (fetch_is_agg_expr_shape (&regu->value, budget))
		{
		  REGU_VARIABLE_SET_FLAG (&regu->value, REGU_VARIABLE_AGG_OPERAND);
		}
	      break;
	    }
	}
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
  if (xasl->type == CONNECTBY_PROC)
    {
      /* before its specs are walked: the probe item records the domain the scan coerces to */
      domain_fix_connect_by_probe (ctx, xasl);
    }
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
      domain_walk_set_columns (ctx, xasl);
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
      domain_walk_set_columns (ctx, xasl);
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
	domain_mark_aggregate_operands (xasl);
      }
      break;
    case BUILDVALUE_PROC:
      domain_walk_agg (ctx, xasl->proc.buildvalue.agg_list);
      domain_walk_arith (ctx, xasl->proc.buildvalue.outarith_list);
      domain_walk_pred (ctx, xasl->proc.buildvalue.having_pred);
      domain_mark_aggregate_operands (xasl);
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

/* The constant subtree a record's regu computes, if the gate evaluates it once (#352): a constant arithmetic node (its
 * value is the node's item's) or a constant function that caches. */
static DOMAIN_PLAN_ITEM *
domain_constant_of (const DOMAIN_LOAD_RECORD * record)
{
  const REGU_VARIABLE *regu = record->regu;
  if (regu == NULL)
    {
      return NULL;
    }
  DOMAIN_PLAN_ITEM *item = NULL;
  if ((regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH) && regu->value.arithptr != NULL)
    {
      item = regu->value.arithptr->domain_plan;
    }
  else if (regu->type == TYPE_FUNC)
    {
      item = regu->domain_plan;
    }
  return item != NULL && item->operand_class == OPERAND_CONST ? item : NULL;
}

/*
 * domain_publish_constants () - every constant subtree gets a value of its own in the gate's array, which the gate
 *   fills once before the main block (interface §10, #352): this replaces fetch's FETCH_ALL_CONST marking.
 *   Nested constants come first, in the order the walk appended them.
 */
static bool
domain_publish_constants (THREAD_ENTRY * thread_p, DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_PLAN * plan)
{
  const int base = plan->n_refs;
  for (DOMAIN_LOAD_RECORD * r = ctx->head; r != NULL; r = r->next)
    {
      DOMAIN_PLAN_ITEM *item = domain_constant_of (r);
      if (item != NULL && item->ref < 0)
	{
	  item->ref = plan->n_refs++;
	}
    }
  plan->n_constants = plan->n_refs - base;
  if (plan->n_constants == 0)
    {
      return true;
    }
  plan->constants = (DOMAIN_PLAN_CONSTANT *) domain_plan_alloc (thread_p, plan->n_constants, sizeof (*plan->constants));
  if (plan->constants == NULL)
    {
      return false;
    }
  memset (plan->constants, 0, sizeof (*plan->constants) * plan->n_constants);
  for (DOMAIN_LOAD_RECORD * r = ctx->head; r != NULL; r = r->next)
    {
      DOMAIN_PLAN_ITEM *item = domain_constant_of (r);
      if (item != NULL && item->ref >= base && plan->constants[item->ref - base].item == NULL)
	{
	  plan->constants[item->ref - base].item = item;
	  plan->constants[item->ref - base].regu = r->regu;
	}
    }
  return true;
}

/* Whether a record reads an aggregate that finalizes to DOUBLE whatever its function domain says: AVG, STDDEV* and
 * VAR* (qdata_finalize_aggregate_list), whose function domain over a late-bound argument is the argument's, as develop
 * binds it (F-352-16). Through value pointers and list positions to the producer. */
static bool
domain_reads_double_aggregate (const DOMAIN_LOAD_RECORD * record)
{
  for (int guard = 0; record != NULL && guard < 256; guard++)
    {
      if (record->alias != NULL)
	{
	  record = record->alias;
	  continue;
	}
      if (record->kind == DOMAIN_LOAD_FIXED_AGG)
	{
	  if (record->cold.ctx != DOMAIN_CTX_AGG)
	    {
	      return false;
	    }
	  switch (record->cold.opcode)
	    {
	    case PT_AVG:
	    case PT_STDDEV:
	    case PT_STDDEV_POP:
	    case PT_STDDEV_SAMP:
	    case PT_VARIANCE:
	    case PT_VAR_POP:
	    case PT_VAR_SAMP:
	      return true;
	    default:
	      return false;
	    }
	}
      if (record->kind != DOMAIN_LOAD_CONSUMER || record->producer == NULL)
	{
	  return false;
	}
      record = record->producer;
    }
  return false;
}

/* What the load knows of one side of a comparison (#352). */
enum DOMAIN_COMPARE_SIDE
{
  DOMAIN_SIDE_KNOWN,		/* its key is the plan's */
  DOMAIN_SIDE_AT_GATE,		/* a bind (it compares with its value's type, F-335-06), a slot or gate-dependent side */
  DOMAIN_SIDE_OPEN		/* the plan leaves its values' type or collation open: the row compares by value */
};

/* One side of a comparison at publication: a literal gives its value's key, a bind, a slot or a gate-dependent side
 * is the gate's, a reader of an aggregate that finalizes to DOUBLE a DOUBLE, anything else its plan domain (#352).
 * records(in): the load record of each published item, by index */
static DOMAIN_COMPARE_SIDE
domain_compare_side (const DOMAIN_PLAN * plan, DOMAIN_LOAD_RECORD * const *records, int constant_base,
		     REGU_VARIABLE * regu, DOMAIN_COMPARE_PLAN * site, int side, DOMAIN_COMPARE_KEY * key,
		     unsigned long long *volatile_reads)
{
  const DOMAIN_PLAN_ITEM *item = regu->domain_plan;
  site->operand[side] = item;
  site->domain[side] = regu->domain;
  site->value[side] = -1;
  /* a COLLATE modifier on the side itself: the fetch overwrites its value's codeset and collation with the domain's
   * (xasl_generation.c drops the T_CAST and flags the operand's regu) */
  site->collate[side] = REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_APPLY_COLLATION) && regu->domain != NULL
    && TP_TYPE_HAS_COLLATION (TP_DOMAIN_TYPE (regu->domain)) ? regu->domain : NULL;
  if (item != NULL && regu->type != TYPE_DBVAL && regu->type != TYPE_POS_VALUE
      && domain_reads_double_aggregate (records[item - plan->items]))
    {
      /* F-352-16: the value is a DOUBLE (or NULL) whatever the aggregate's decided domain says; the gate reads the
       * side's domain, not its item */
      site->operand[side] = NULL;
      site->domain[side] = &tp_Double_domain;
      domain_compare_key_of (&tp_Double_domain, key);
      return DOMAIN_SIDE_KNOWN;
    }
  if (regu->type == TYPE_POS_VALUE && item != NULL)
    {
      site->constant[side] = item;
      return DOMAIN_SIDE_AT_GATE;
    }
  if (regu->type == TYPE_DBVAL)
    {
      const DB_VALUE *literal = &regu->value.dbval;
      site->literal[side] = literal;
      domain_compare_key_of (DB_IS_NULL (literal) ? &tp_Null_domain : tp_domain_resolve_value (literal, NULL), key);
      domain_compare_key_collate (key, site->collate[side]);
      return DOMAIN_SIDE_KNOWN;
    }
  const DOMAIN_PLAN_ITEM *cached = NULL;
  if ((regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH) && regu->value.arithptr != NULL)
    {
      cached = regu->value.arithptr->domain_plan;
    }
  else if (regu->type == TYPE_FUNC)
    {
      cached = item;
    }
  if (cached != NULL && cached->ref >= constant_base)
    {
      /* a constant subtree compares with its value's key, as a bind or a literal does: the gate evaluates it before
       * any row, and its compiled domain need not describe its value (a LIKE bound's collation is its pattern
       * value's, F-352-17) */
      site->constant[side] = cached;
      site->after_constants = true;
      return DOMAIN_SIDE_AT_GATE;
    }
  if (item != NULL && item->slot >= 0)
    {
      if (plan->slot_flags[item->slot] & DOMAIN_SLOT_VOLATILE)
	{
	  *volatile_reads |= plan->slot_volatile_reads[item->slot];
	}
      return DOMAIN_SIDE_AT_GATE;
    }
  const TP_DOMAIN *domain = item != NULL ? item->fixed.domain : regu->domain;
  if (!domain_fixes_values (domain))
    {
      return DOMAIN_SIDE_OPEN;
    }
  domain_compare_key_of (domain, key);
  domain_compare_key_collate (key, site->collate[side]);
  return DOMAIN_SIDE_KNOWN;
}

/* A side the gate converts once when its comparison converts it: a literal, a bind or a constant subtree. */
static bool
domain_compare_constant_side (const DOMAIN_COMPARE_PLAN * site, int side)
{
  return site->literal[side] != NULL || site->constant[side] != NULL;
}

/*
 * domain_publish_record () - one comparison record's decision (D-352-01): the load's when both sides' keys are the
 *   plan's and no constant side needs converting, otherwise a gate site the gate decides once per execution (and
 *   converts its constant sides into values of their own). A side whose values the plan leaves open keeps develop's
 *   comparison (kernel VALUES, the boundary (b) reason OPEN).
 */
static bool
domain_publish_record (DOMAIN_PLAN * plan, DOMAIN_COMPARE_PLAN * site, DOMAIN_COMPARE_SIDE lhs, DOMAIN_COMPARE_SIDE rhs,
		       const DOMAIN_COMPARE_KEY * key, unsigned long long volatile_reads,
		       DOMAIN_COMPARE_PLAN ** gate_sites, int *n_gate)
{
  bool at_gate = false;
  if (lhs == DOMAIN_SIDE_OPEN || rhs == DOMAIN_SIDE_OPEN)
    {
      site->fixed = DOMAIN_COMPARE
      {
      };
      site->fixed.kernel = DOMAIN_COMPARE_VALUES;
      site->fixed.reason = DOMAIN_REASON_OPEN;
      site->fixed.site = -1;
      site->fixed.value[0] = site->fixed.value[1] = site->fixed.codeset_side = -1;
    }
  else if (lhs == DOMAIN_SIDE_KNOWN && rhs == DOMAIN_SIDE_KNOWN)
    {
      if (domain_resolve_comparison (&key[0], &key[1], &site->fixed) != NO_ERROR)
	{
	  return false;
	}
      /* a constant side the comparison converts is converted once, by the gate */
      at_gate = (domain_compare_constant_side (site, 0) && site->fixed.conv[0] != NULL)
	|| (domain_compare_constant_side (site, 1) && site->fixed.conv[1] != NULL);
    }
  else
    {
      at_gate = true;
    }
  if (at_gate)
    {
      site->fixed = DOMAIN_COMPARE
      {
      };
      site->fixed.kernel = volatile_reads != 0 ? DOMAIN_COMPARE_AT_GATE_VOLATILE : DOMAIN_COMPARE_AT_GATE;
      site->fixed.volatile_reads = volatile_reads;
      site->fixed.site = *n_gate;
      site->fixed.value[0] = site->fixed.value[1] = site->fixed.codeset_side = -1;
      for (int side = 0; side < 2; side++)
	{
	  if (domain_compare_constant_side (site, side))
	    {
	      site->value[side] = plan->n_refs++;
	    }
	}
      gate_sites[(*n_gate)++] = site;
    }
  return true;
}

/* A list column side of an ALL/SOME term (#352): the gate's slot, a DOUBLE for a reader of AVG, STDDEV* or VAR*
 * (F-352-16), or its plan domain; open when the plan leaves its values' type or collation open. */
static DOMAIN_COMPARE_SIDE
domain_compare_column_side (const DOMAIN_PLAN * plan, DOMAIN_LOAD_RECORD * const *records,
			    const DOMAIN_PLAN_ITEM * column, DOMAIN_COMPARE_PLAN * site, int side,
			    DOMAIN_COMPARE_KEY * key, unsigned long long *volatile_reads)
{
  site->operand[side] = column;
  site->domain[side] = NULL;
  site->value[side] = -1;
  if (column == NULL)
    {
      return DOMAIN_SIDE_OPEN;
    }
  if (domain_reads_double_aggregate (records[column - plan->items]))
    {
      site->operand[side] = NULL;
      site->domain[side] = &tp_Double_domain;
      domain_compare_key_of (&tp_Double_domain, key);
      return DOMAIN_SIDE_KNOWN;
    }
  if (column->slot >= 0)
    {
      if (plan->slot_flags[column->slot] & DOMAIN_SLOT_VOLATILE)
	{
	  *volatile_reads |= plan->slot_volatile_reads[column->slot];
	}
      return DOMAIN_SIDE_AT_GATE;
    }
  if (!domain_fixes_values (column->fixed.domain))
    {
      return DOMAIN_SIDE_OPEN;
    }
  site->domain[side] = column->fixed.domain;
  domain_compare_key_of (column->fixed.domain, key);
  return DOMAIN_SIDE_KNOWN;
}

/*
 * domain_element_keys () - the keys the elements of a collection the row computes can have (#352, D-352-03)
 *   return: false on an allocation failure
 *   keys(out): the keys; NULL for any key
 *
 * A set function's elements are its operand values as they are (qdata_convert_dbvals_to_set puts them with
 * col_put / col_add, which convert nothing): its operands' keys, when the plan has every one. A set attribute's stored
 * elements were coerced into its element domains. Anything else - a value pointer, a set expression, a stored
 * procedure's result - can hold elements its compiled element domains do not describe (a SEQUENCE OF CHAR function
 * holding VARCHAR operands, F-352-18): any key.
 */
static bool
domain_element_keys (THREAD_ENTRY * thread_p, const DOMAIN_PLAN * plan, DOMAIN_LOAD_RECORD * const *records,
		     int constant_base, REGU_VARIABLE * elemset, const DOMAIN_COMPARE_KEY ** keys, int *n_keys)
{
  *keys = NULL;
  *n_keys = 0;
  const bool set_function = elemset->type == TYPE_FUNC
    && (elemset->value.funcp->ftype == F_SET || elemset->value.funcp->ftype == F_MULTISET
	|| elemset->value.funcp->ftype == F_SEQUENCE);
  const bool set_attribute = (elemset->type == TYPE_ATTR_ID || elemset->type == TYPE_SHARED_ATTR_ID
			      || elemset->type == TYPE_CLASS_ATTR_ID) && elemset->domain != NULL;
  int n = 0;
  if (set_function)
    {
      for (REGU_VARIABLE_LIST op = elemset->value.funcp->operand; op != NULL; op = op->next)
	{
	  n++;
	}
    }
  else if (set_attribute)
    {
      for (const TP_DOMAIN * d = elemset->domain->setdomain; d != NULL; d = d->next)
	{
	  if (!domain_fixes_values (d))
	    {
	      return true;
	    }
	  n++;
	}
    }
  if (n == 0)
    {
      return true;
    }
  DOMAIN_COMPARE_KEY *list = (DOMAIN_COMPARE_KEY *) domain_plan_alloc (thread_p, n, sizeof (*list));
  if (list == NULL)
    {
      return false;
    }
  int k = 0;
  if (set_function)
    {
      for (REGU_VARIABLE_LIST op = elemset->value.funcp->operand; op != NULL; op = op->next)
	{
	  DOMAIN_COMPARE_PLAN scratch;
	  memset (&scratch, 0, sizeof (scratch));
	  unsigned long long reads = 0;
	  if (domain_compare_side (plan, records, constant_base, &op->value, &scratch, 0, &list[k], &reads)
	      != DOMAIN_SIDE_KNOWN || reads != 0)
	    {
	      /* an operand the gate types - a bind, a slot, a constant subtree - or leaves open: any key */
	      return true;
	    }
	  k++;
	}
    }
  else
    {
      for (const TP_DOMAIN * d = elemset->domain->setdomain; d != NULL; d = d->next)
	{
	  domain_compare_key_of (d, &list[k++]);
	}
    }
  *keys = list;
  *n_keys = k;
  return true;
}

/* The load's element table of a collection the row computes, for an item whose key is the plan's (#352). */
static const DOMAIN_ELEMENT_TABLE *
domain_publish_element_table (THREAD_ENTRY * thread_p, const DOMAIN_COMPARE_KEY * item, const DOMAIN_COMPARE_KEY * keys,
			      int n_keys)
{
  const size_t bytes = domain_element_table_bytes (item, keys, n_keys);
  DOMAIN_ELEMENT_TABLE *table = (DOMAIN_ELEMENT_TABLE *) domain_plan_alloc (thread_p, 1, bytes);
  if (table == NULL || domain_resolve_element_table (item, keys, n_keys, table, bytes) != NO_ERROR)
    {
      return NULL;
    }
  return table;
}

/*
 * domain_publish_elements () - one ALL/SOME term's comparisons (#352, D-352-03), each decided before any row
 *
 * The item against a list's column or a right side that is no collection: a comparison record, as a comparison
 * term's. Against a collection the row computes: the load's table of every key its elements can have, or the gate's
 * when the gate decides the item. Against a constant (a literal, a bind, a constant subtree): the gate decides and
 * converts each element once, by position. A right side the gate types is the gate's too.
 */
static bool
domain_publish_elements (THREAD_ENTRY * thread_p, DOMAIN_PLAN * plan, DOMAIN_LOAD_RECORD * const *records,
			 int constant_base, DOMAIN_LOAD_ELEMENT_TERM * entry, DOMAIN_COMPARE_PLAN ** gate_sites,
			 int *n_gate, DOMAIN_ELEMENT_COMPARE_PLAN ** element_sites, int *n_element_gate)
{
  ALSM_EVAL_TERM *term = entry->term;
  DOMAIN_ELEMENT_COMPARE_PLAN *site = (DOMAIN_ELEMENT_COMPARE_PLAN *) domain_plan_alloc (thread_p, 1, sizeof (*site));
  if (site == NULL)
    {
      return false;
    }
  memset (site, 0, sizeof (*site));
  site->site = -1;
  DOMAIN_COMPARE_PLAN *pair = &site->pair;
  DOMAIN_COMPARE_KEY key[2];
  unsigned long long volatile_reads = 0;
  const DOMAIN_COMPARE_SIDE item = domain_compare_side (plan, records, constant_base, term->elem, pair, 0, &key[0],
							&volatile_reads);
  DOMAIN_COMPARE_SIDE right;
  site->kind = DOMAIN_ELEMENTS_PAIR;
  if (term->elemset->type == TYPE_LIST_ID)
    {
      right = domain_compare_column_side (plan, records, entry->list_column, pair, 1, &key[1], &volatile_reads);
    }
  else
    {
      right = domain_compare_side (plan, records, constant_base, term->elemset, pair, 1, &key[1], &volatile_reads);
      if (domain_compare_constant_side (pair, 1))
	{
	  /* a literal, a bind or a constant subtree: the gate decides its elements by position */
	  site->kind = DOMAIN_ELEMENTS_GATE;
	}
      else if (right == DOMAIN_SIDE_AT_GATE)
	{
	  /* a slot: the gate's decision says whether its values are collections */
	  site->kind = DOMAIN_ELEMENTS_GATE;
	}
      else if (right == DOMAIN_SIDE_KNOWN && TP_IS_SET_TYPE (key[1].type))
	{
	  site->kind = item == DOMAIN_SIDE_KNOWN ? DOMAIN_ELEMENTS_TABLE : DOMAIN_ELEMENTS_GATE;
	  if (!domain_element_keys (thread_p, plan, records, constant_base, term->elemset, &site->keys, &site->n_keys))
	    {
	      return false;
	    }
	}
    }
  if (item == DOMAIN_SIDE_OPEN || right == DOMAIN_SIDE_OPEN)
    {
      site->kind = DOMAIN_ELEMENTS_PAIR;
    }
  switch (site->kind)
    {
    case DOMAIN_ELEMENTS_PAIR:
      if (!domain_publish_record (plan, pair, item, right, key, volatile_reads, gate_sites, n_gate))
	{
	  return false;
	}
      break;
    case DOMAIN_ELEMENTS_TABLE:
      site->table = domain_publish_element_table (thread_p, &key[0], site->keys, site->n_keys);
      if (site->table == NULL)
	{
	  return false;
	}
      break;
    default:
      site->site = *n_element_gate;
      site->volatile_reads = volatile_reads;
      element_sites[(*n_element_gate)++] = site;
      break;
    }
  term->domain_compare = site;
  return true;
}

/*
 * domain_publish_compares () - every comparison term gets its comparison record (D-352-01), and every ALL/SOME term
 *   its element comparisons (D-352-03)
 */
static bool
domain_publish_compares (THREAD_ENTRY * thread_p, DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_PLAN * plan, int constant_base)
{
  const int n_terms = ctx->n_compare_terms + ctx->n_element_terms;
  if (n_terms == 0)
    {
      return true;
    }
  DOMAIN_COMPARE_PLAN **gate_sites =
    (DOMAIN_COMPARE_PLAN **) db_private_alloc (thread_p, sizeof (*gate_sites) * n_terms);
  DOMAIN_ELEMENT_COMPARE_PLAN **element_sites =
    (DOMAIN_ELEMENT_COMPARE_PLAN **) db_private_alloc (thread_p, sizeof (*element_sites) * n_terms);
  DOMAIN_LOAD_RECORD **records =
    (DOMAIN_LOAD_RECORD **) db_private_alloc (thread_p, sizeof (*records) * (plan->n_items > 0 ? plan->n_items : 1));
  bool ok = gate_sites != NULL && element_sites != NULL && records != NULL;
  for (DOMAIN_LOAD_RECORD * r = ctx->head; ok && r != NULL; r = r->next)
    {
      if (r->alias == NULL)
	{
	  records[r->index] = r;
	}
    }
  int n_gate = 0, n_element_gate = 0;
  for (int t = 0; t < ctx->n_compare_terms && ok; t++)
    {
      COMP_EVAL_TERM *term = ctx->compare_terms[t];
      if (term->domain_compare != NULL)
	{
	  /* a term the walk met twice */
	  continue;
	}
      DOMAIN_COMPARE_PLAN *site = (DOMAIN_COMPARE_PLAN *) domain_plan_alloc (thread_p, 1, sizeof (*site));
      if (site == NULL)
	{
	  ok = false;
	  break;
	}
      memset (site, 0, sizeof (*site));
      DOMAIN_COMPARE_KEY key[2];
      unsigned long long volatile_reads = 0;
      const DOMAIN_COMPARE_SIDE lhs = domain_compare_side (plan, records, constant_base, term->lhs, site, 0, &key[0],
							   &volatile_reads);
      const DOMAIN_COMPARE_SIDE rhs = domain_compare_side (plan, records, constant_base, term->rhs, site, 1, &key[1],
							   &volatile_reads);
      ok = domain_publish_record (plan, site, lhs, rhs, key, volatile_reads, gate_sites, &n_gate);
      term->domain_compare = site;
    }
  for (DOMAIN_LOAD_ELEMENT_TERM * e = ctx->element_terms; e != NULL && ok; e = e->next)
    {
      if (e->term->domain_compare == NULL)
	{
	  ok = domain_publish_elements (thread_p, plan, records, constant_base, e, gate_sites, &n_gate, element_sites,
					&n_element_gate);
	}
    }
  if (ok && n_gate > 0)
    {
      plan->compares = (DOMAIN_COMPARE_PLAN **) domain_plan_alloc (thread_p, n_gate, sizeof (*plan->compares));
      ok = plan->compares != NULL;
      if (ok)
	{
	  memcpy (plan->compares, gate_sites, sizeof (*plan->compares) * n_gate);
	  plan->n_compares = n_gate;
	}
    }
  if (ok && n_element_gate > 0)
    {
      plan->element_sites =
	(DOMAIN_ELEMENT_COMPARE_PLAN **) domain_plan_alloc (thread_p, n_element_gate, sizeof (*plan->element_sites));
      ok = plan->element_sites != NULL;
      if (ok)
	{
	  memcpy (plan->element_sites, element_sites, sizeof (*plan->element_sites) * n_element_gate);
	  plan->n_element_sites = n_element_gate;
	}
    }
  if (gate_sites != NULL)
    {
      db_private_free (thread_p, gate_sites);
    }
  if (element_sites != NULL)
    {
      db_private_free (thread_p, element_sites);
    }
  if (records != NULL)
    {
      db_private_free (thread_p, records);
    }
  return ok;
}

/*
 * domain_plan_key_element () - how one column of a search key takes its value (#342, interface section 5)
 *
 * A constant is the gate's (its value, once per execution); so is an element whose domain the gate decides (its rule,
 * from that domain). The load derives the rule of any other element from its domain: the column's type, strict or kept
 * (domain_key_rule). keep_elem is the element's domain in the column's direction, which a multi-column key writes the
 * value with once any column is kept.
 */
static bool
domain_plan_key_element (domain_plan_index * index, bool midxkey, REGU_VARIABLE * regu, const TP_DOMAIN * column,
			 bool skip_value, domain_plan_key_elem * elem)
{
  elem->regu = regu;
  elem->index_elem = column;
  elem->keep_elem = column;
  elem->strict_conv = NULL;
  elem->decision = -1;
  elem->rule = DOMAIN_KEY_INDEX;
  if (skip_value)
    {
      /* an index skip scan's skip value is read from the index */
      return true;
    }
  const DOMAIN_PLAN_ITEM *item = regu != NULL ? regu->domain_plan : NULL;
  if (item != NULL && item->operand_class == OPERAND_CONST)
    {
      elem->rule = DOMAIN_KEY_CONSTANT;
      elem->decision = index->n_decisions++;
      return true;
    }
  const TP_DOMAIN *domain = item != NULL && item->slot < 0 ? item->fixed.domain : NULL;
  if (item == NULL || item->operand_class == OPERAND_VOLATILE || !domain_fixes_values (domain))
    {
      elem->rule = DOMAIN_KEY_DECIDED;
      elem->decision = index->n_decisions++;
      return true;
    }
  domain = domain_key_value_domain (domain);
  elem->rule = domain_key_rule (domain, column, midxkey, &elem->strict_conv);
  elem->keep_elem = domain_in_key_direction (domain, column);
  return elem->keep_elem != NULL;
}

/* One bound of a key range (#342): the columns of a multi-column key's F_MIDXKEY, or the single-column key itself. */
static bool
domain_plan_key_bound (THREAD_ENTRY * thread_p, domain_plan_index * index, REGU_VARIABLE * bound_regu, bool skip_first,
		       domain_plan_key * bound)
{
  memset (bound, 0, sizeof (*bound));
  bound->scratch = -1;
  if (bound_regu == NULL)
    {
      return true;
    }
  bound->midxkey = TP_DOMAIN_TYPE (index->key_type) == DB_TYPE_MIDXKEY;
  REGU_VARIABLE_LIST operand = NULL;
  int n = 1;
  if (bound->midxkey)
    {
      assert (bound_regu->type == TYPE_FUNC && bound_regu->value.funcp->ftype == F_MIDXKEY);
      operand = bound_regu->value.funcp->operand;
      n = 0;
      for (REGU_VARIABLE_LIST op = operand; op != NULL; op = op->next)
	{
	  n++;
	}
    }
  bound->elems = (domain_plan_key_elem *) domain_plan_alloc (thread_p, n, sizeof (*bound->elems));
  if (n > 0 && bound->elems == NULL)
    {
      return false;
    }
  bool mixes = false, constant = false, row = false;
  for (int i = 0; i < n; i++)
    {
      REGU_VARIABLE *regu = bound->midxkey ? &operand->value : bound_regu;
      const TP_DOMAIN *column = domain_key_column (index->key_type, i);
      if (column == NULL)
	{
	  /* more columns than the index has: not a key of this index */
	  assert (false);
	  return false;
	}
      domain_plan_key_elem *elem = &bound->elems[bound->n_elems++];
      if (!domain_plan_key_element (index, bound->midxkey, regu, column, skip_first && i == 0, elem))
	{
	  return false;
	}
      mixes = mixes || elem->rule != DOMAIN_KEY_INDEX;
      constant = constant || elem->rule == DOMAIN_KEY_CONSTANT;
      row = row || (elem->rule != DOMAIN_KEY_INDEX && elem->rule != DOMAIN_KEY_CONSTANT);
      operand = operand != NULL ? operand->next : NULL;
    }
  if (bound->midxkey && mixes)
    {
      /* constants only: the gate writes the domain once per execution; the scan fills a scratch chain otherwise, and
       * for a constant the row computes (D-352-05) */
      bound->constant = constant && !row;
      bound->scratch = index->n_scratch++;
    }
  return true;
}

/* domain_key_compare_keys () - the key comparison table's input (#342): each key column's own key, and the key its
 * values take under each load-fixed element */
int
domain_key_compare_keys (const domain_plan_index * index, int *columns, DOMAIN_COMPARE_KEY * keys)
{
  int n = 0;
  for (int b = 0; b < 2 * index->n_ranges + 1; b++)
    {
      const domain_plan_key *bound = &index->bounds[b];
      for (int i = 0; i < bound->n_elems; i++)
	{
	  const domain_plan_key_elem *elem = &bound->elems[i];
	  columns[n] = i;
	  domain_compare_key_of (elem->index_elem, &keys[n++]);
	  if (elem->rule != DOMAIN_KEY_CONSTANT && elem->rule != DOMAIN_KEY_DECIDED && elem->keep_elem != NULL)
	    {
	      columns[n] = i;
	      domain_compare_key_of (elem->keep_elem, &keys[n++]);
	    }
	}
    }
  return n;
}

/* An index's key comparison table the load can build: none of its elements waits for the gate (#342). */
static bool
domain_publish_key_compares (THREAD_ENTRY * thread_p, domain_plan_index * index)
{
  int n_elems = 0;
  for (int b = 0; b < 2 * index->n_ranges + 1; b++)
    {
      n_elems += index->bounds[b].n_elems;
    }
  if (n_elems == 0)
    {
      return true;
    }
  int *columns = (int *) db_private_alloc (thread_p, sizeof (int) * 2 * n_elems);
  DOMAIN_COMPARE_KEY *keys = (DOMAIN_COMPARE_KEY *) db_private_alloc (thread_p, sizeof (*keys) * 2 * n_elems);
  bool ok = columns != NULL && keys != NULL;
  if (ok)
    {
      const int n = domain_key_compare_keys (index, columns, keys);
      const size_t bytes = domain_key_compares_bytes (columns, keys, n);
      DOMAIN_KEY_COMPARES *table = (DOMAIN_KEY_COMPARES *) domain_plan_alloc (thread_p, 1, bytes);
      ok = table != NULL && domain_resolve_key_compares (columns, keys, n, table, bytes) == NO_ERROR;
      index->compares = ok && table->n_entries > 0 ? table : NULL;
    }
  if (columns != NULL)
    {
      db_private_free (thread_p, columns);
    }
  if (keys != NULL)
    {
      db_private_free (thread_p, keys);
    }
  return ok;
}

/*
 * domain_publish_indexes () - every index scan's key plan (#342, interface section 5): its bounds' elements and their
 *   rules, from INDX_INFO.key_type (D-318 decision 5); the scan finds it through INDX_INFO.domain_plan
 */
static bool
domain_publish_indexes (THREAD_ENTRY * thread_p, DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_PLAN * plan)
{
  plan->n_indexes = ctx->n_indexes;
  plan->indexes = (domain_plan_index *) domain_plan_alloc (thread_p, plan->n_indexes, sizeof (*plan->indexes));
  if (plan->n_indexes > 0 && plan->indexes == NULL)
    {
      return false;
    }
  for (int j = 0; j < ctx->n_indexes; j++)
    {
      INDX_INFO *indx_info = ctx->indexes[j];
      domain_plan_index *index = &plan->indexes[j];
      memset (index, 0, sizeof (*index));
      index->site = -1;
      indx_info->domain_plan = NULL;
      if (indx_info->key_type == NULL)
	{
	  /* no key domain in the stream: the scan meets the execution boundary (b) */
	  continue;
	}
      index->key_type = indx_info->key_type;
      index->asc_key_type = domain_ascending_key_type (index->key_type);
      index->n_ranges = indx_info->key_info.key_cnt;
      index->bounds =
	(domain_plan_key *) domain_plan_alloc (thread_p, 2 * index->n_ranges + 1, sizeof (*index->bounds));
      if (index->asc_key_type == NULL || index->bounds == NULL)
	{
	  return false;
	}
      const bool iss = indx_info->use_iss != 0;
      for (int i = 0; i < index->n_ranges; i++)
	{
	  KEY_RANGE *range = &indx_info->key_info.key_ranges[i];
	  /* an index skip scan's ranges start with its skip value */
	  if (!domain_plan_key_bound (thread_p, index, range->key1, iss, &index->bounds[2 * i])
	      || !domain_plan_key_bound (thread_p, index, range->key2, iss, &index->bounds[2 * i + 1]))
	    {
	      return false;
	    }
	}
      /* the index skip scan's fetch range: its one column is the skip value */
      if (!domain_plan_key_bound (thread_p, index, iss ? indx_info->iss_range.key1 : NULL, true,
				  &index->bounds[2 * index->n_ranges]))
	{
	  return false;
	}
      if (index->n_decisions > 0)
	{
	  index->site = plan->n_index_sites++;
	}
      else if (!domain_publish_key_compares (thread_p, index))
	{
	  return false;
	}
      indx_info->domain_plan = index;
    }
  return true;
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
  if (!ctx.failed)
    {
      /* #352: constant subtrees before comparisons, whose constant sides read them */
      const int constant_base = plan->n_refs;
      ctx.failed = !domain_publish_constants (thread_p, &ctx, plan)
	|| !domain_publish_compares (thread_p, &ctx, plan, constant_base)
	|| !domain_publish_indexes (thread_p, &ctx, plan);
    }
  if (ctx.gate_order != NULL)
    {
      db_private_free (thread_p, ctx.gate_order);
    }
  if (ctx.indexes != NULL)
    {
      db_private_free (thread_p, ctx.indexes);
    }
  if (ctx.compare_terms != NULL)
    {
      db_private_free (thread_p, ctx.compare_terms);
    }
  while (ctx.element_terms != NULL)
    {
      DOMAIN_LOAD_ELEMENT_TERM *e = ctx.element_terms;
      ctx.element_terms = e->next;
      db_private_free (thread_p, e);
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
