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

/* The compiler still emits VARIABLE/LEAVE. Boundary (a) is enabled only after the
 * compiler and executor deletion tickets; the old original_domain links remain live. */
static const bool domain_plan_check_load = false;
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
  {"X-5", "collection constructor", "static element domains"},
  {"X-6", "T_EVALUATE_VARIABLE", "volatile; require compiler GATE marker"},
  {"X-7", "MEDIAN / PERCENTILE string operand", "residual"}
};

/* Temporary load records are freed before publishing the plan. No record, owner
 * link or traversal scratch survives in the hot arrays. Shared XASLs are marked
 * on entry; expression items are appended after their operands (producer order). */
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
  /* gate-dependent node: the operands its DOMAIN_GATE_LINK points at once they are published */
  REGU_VARIABLE *link[3];
  int n_link;
  const TP_DOMAIN *consumer;
};
struct DOMAIN_LOAD_BINDING
{
  DOMAIN_LOAD_BINDING *next;
  DOMAIN_PLAN_ITEM **owner;
  DOMAIN_PLAN_ITEM *target;
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
};

static void domain_walk_xasl (DOMAIN_LOAD_CONTEXT *, XASL_NODE *);
static void domain_walk_pred (DOMAIN_LOAD_CONTEXT *, PRED_EXPR *);
static void domain_walk_regu (DOMAIN_LOAD_CONTEXT *, REGU_VARIABLE *, DOMAIN_CTX = DOMAIN_CTX_FUNC_ARG);

static bool
domain_is_fixed (const TP_DOMAIN * domain)
{
  return domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE
    && domain->collation_flag != TP_DOMAIN_COLL_LEAVE;
}

/* The type axis only: the collation of a character result is merged at the gate by #338 (D-335-03). */
static bool
domain_type_is_fixed (const TP_DOMAIN * domain)
{
  return domain != NULL && TP_DOMAIN_TYPE (domain) != DB_TYPE_VARIABLE;
}

/* The gate reads a type it can trust for this operand: a GATE slot or a gate-dependent node, a bind or a literal
 * (its value), or a compiled domain the values follow (an attribute, a computed expression). A value pointer (a list
 * column, a correlated value) keeps the type its producer gave the value, which the compiled domain may not describe
 * (F-335-07: a recursive CTE column typed by its first branch); dpin-11 decides those. */
static bool
domain_operand_is_known (const REGU_VARIABLE * operand)
{
  const DOMAIN_PLAN_ITEM *item = operand->domain_plan;
  if (item == NULL)
    {
      return false;
    }
  if (item->slot >= 0 || operand->type == TYPE_POS_VALUE || operand->type == TYPE_DBVAL)
    {
      return true;
    }
  return operand->type != TYPE_CONSTANT && operand->type != TYPE_POSITION && domain_type_is_fixed (item->fixed.domain);
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

/* Makes the item just added a gate-dependent node: the gate resolves it into its own slot from these operands. */
static void
domain_mark_gate_node (DOMAIN_LOAD_CONTEXT * ctx, DOMAIN_PLAN_ITEM * item, REGU_VARIABLE * const *operands,
		       int n_operands, const TP_DOMAIN * consumer)
{
  assert (&ctx->tail->item == item && n_operands <= 3);
  item->flags |= DOMAIN_PLAN_GATE;
  item->slot = ctx->plan->n_slots++;
  item->fixed.domain = NULL;
  for (int i = 0; i < n_operands; i++)
    {
      ctx->tail->link[i] = operands[i];
    }
  ctx->tail->n_link = n_operands;
  ctx->tail->consumer = consumer;
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
  /* A node the compiler left without a result type (a late-bound operator over a slot, A8'') is resolved by the
   * gate once per execution when every operand gives it a type it can trust (#335). A session variable read (S5)
   * and a derived consumer (a list column, a correlated value, an open producer) keep the node, and every node over
   * it, on the execution path until dpin-10 and dpin-11 decide them; CONNECT_BY_ROOT and QPRIOR carry their XASL in
   * thirdptr. */
  const bool late_bound = arith->domain != NULL && TP_DOMAIN_TYPE (arith->domain) == DB_TYPE_VARIABLE;
  if (item != NULL && (marked_gate || late_bound) && arith->opcode != T_EVALUATE_VARIABLE)
    {
      REGU_VARIABLE *links[3];
      int n_links = 0;
      bool known = true;
      const int n_value_operands = (arith->opcode == T_CONNECT_BY_ROOT || arith->opcode == T_QPRIOR) ? 2 : 3;
      for (int i = 0; i < n_value_operands; i++)
	{
	  if (operands[i] != NULL)
	    {
	      known = known && domain_operand_is_known (operands[i]);
	      links[n_links++] = operands[i];
	    }
	}
      known = known && n_links > 0;
      /* ADDTIME classifies its left string by the value (D-328-06); a column gives the gate no value, so the node
       * is decided per row as today, like F10 (rule table F4') */
      REGU_VARIABLE *left = arith->leftptr;
      if (known && arith->opcode == T_ADDTIME && left != NULL && left->type != TYPE_POS_VALUE
	  && left->type != TYPE_DBVAL && left->domain_plan->slot < 0
	  && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (left->domain_plan->fixed.domain)))
	{
	  item->flags |= DOMAIN_PLAN_RESIDUAL;
	  known = false;
	}
      if (known)
	{
	  domain_mark_gate_node (ctx, item, links, n_links, arith->domain);
	  ctx->tail->cold.ctx = domain_gate_context (arith->opcode);
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
  ctx->tail->regu = regu;
  ctx->tail->output[0] = regu->vfetch_to;
  if (regu->type == TYPE_POS_VALUE)
    {
      ctx->tail->cold.val_pos = regu->value.val_pos;
    }
  if (REGU_VARIABLE_IS_FLAGED (regu, REGU_VARIABLE_GATE) && regu->type != TYPE_INARITH && regu->type != TYPE_OUTARITH)
    {
      item->flags |= DOMAIN_PLAN_GATE;
      item->slot = ctx->plan->n_slots++;
      item->fixed.domain = NULL;
    }
  if (regu->type == TYPE_POSITION)
    {
      domain_bind_item (ctx, &regu->value.pos_descr.domain_plan, item);
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
	  item->fixed = arith->domain_plan->fixed;
	  if (arith->domain_plan->flags & DOMAIN_PLAN_GATE)
	    {
	      item->flags |= DOMAIN_PLAN_ALIAS;
	      item->slot = arith->domain_plan->slot;
	    }
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

static void
domain_walk_sort (DOMAIN_LOAD_CONTEXT * ctx, SORT_LIST * list, OUTPTR_LIST * producer)
{
  for (; list != NULL && !ctx->failed; list = list->next)
    {
      QFILE_TUPLE_VALUE_POSITION *pos = &list->pos_descr;
      REGU_VARIABLE_LIST col = producer == NULL ? NULL : producer->valptrp;
      for (int i = 0; col != NULL && i < pos->pos_no; i++)
	{
	  col = col->next;
	}
      if (col != NULL)
	{
	  domain_walk_regu (ctx, &col->value, DOMAIN_CTX_LIST_COLUMN);
	  domain_bind_item (ctx, &pos->domain_plan, col->value.domain_plan);
	}
      else
	{
	  domain_add_item (ctx, &pos->domain_plan, pos->dom, OPERAND_ROW, DOMAIN_CTX_LIST_COLUMN, 0, "position");
	}
    }
}

static bool
domain_residual_aggregate (FUNC_CODE function, const TP_DOMAIN * domain)
{
  return (function == PT_MEDIAN || function == PT_PERCENTILE_CONT || function == PT_PERCENTILE_DISC)
    && domain != NULL && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (domain));
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
      DOMAIN_PLAN_ITEM *item = domain_add_item (ctx, &agg->domain_plan, agg->domain, OPERAND_ROW,
						DOMAIN_CTX_AGG, agg->function, "aggregate");
      if (item != NULL)
	{
	  ctx->tail->output[0] = agg->accumulator.value;
	}
      if (has_operand && agg->operands != NULL && item != NULL)
	{
	  domain_fixed_operand (item, 0, agg->operands->value.domain, agg->operands->value.domain, DOMAIN_CTX_FUNC_ARG);
	  if (domain_residual_aggregate (agg->function, agg->operands->value.domain))
	    {
	      item->flags |= DOMAIN_PLAN_RESIDUAL;
	    }
	  else if (agg->operands->value.domain_plan != NULL && agg->operands->value.domain_plan->slot >= 0)
	    {
	      /* the gate decides the argument, so it decides the function and accumulator domains (F7) */
	      REGU_VARIABLE *operand = &agg->operands->value;
	      domain_mark_gate_node (ctx, item, &operand, 1, agg->domain);
	    }
	}
      domain_walk_sort (ctx, agg->sort_list, NULL);
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
	      ctx->tail->output[0] = analytic->value;
	      ctx->tail->output[1] = analytic->out_value;
	    }
	  domain_fixed_operand (item, 0, analytic->operand.domain, analytic->operand.domain, DOMAIN_CTX_FUNC_ARG);
	  if (item != NULL && domain_residual_aggregate (analytic->function, analytic->operand.domain))
	    {
	      item->flags |= DOMAIN_PLAN_RESIDUAL;
	    }
	  else if (item != NULL && analytic->operand.domain_plan != NULL && analytic->operand.domain_plan->slot >= 0)
	    {
	      REGU_VARIABLE *operand = &analytic->operand;
	      domain_mark_gate_node (ctx, item, &operand, 1, analytic->domain);
	    }
	}
      domain_walk_sort (ctx, eval->sort_list, output);
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
	  domain_walk_xasl (ctx, spec->s.list_node.xasl_node);
	  domain_walk_list (ctx, spec->s.list_node.list_regu_list_pred);
	  domain_walk_list (ctx, spec->s.list_node.list_regu_list_rest);
	  domain_walk_list (ctx, spec->s.list_node.list_regu_list_build);
	  domain_walk_list (ctx, spec->s.list_node.list_regu_list_probe);
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
      domain_walk_list (ctx, xasl->proc.hashjoin.outer.regu_list_pred);
      domain_walk_list (ctx, xasl->proc.hashjoin.inner.regu_list_pred);
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
	domain_walk_list (ctx, b->g_regu_list);
	domain_walk_agg (ctx, b->g_agg_list);
	domain_walk_out (ctx, b->g_outptr_list);
	domain_walk_list (ctx, b->g_hk_sort_regu_list);
	domain_walk_list (ctx, b->g_hk_scan_regu_list);
	domain_walk_list (ctx, b->g_scan_regu_list);
	domain_walk_pred (ctx, b->g_having_pred);
	domain_walk_pred (ctx, b->g_grbynum_pred);
	domain_walk_sort (ctx, b->groupby_list, xasl->outptr_list);
	domain_walk_sort (ctx, b->after_groupby_list, b->g_outptr_list);
	domain_walk_analytic (ctx, b->a_eval_list, b->a_outptr_list);
	domain_walk_list (ctx, b->a_regu_list);
	domain_walk_list (ctx, b->a_scan_regu_list);
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
  domain_walk_sort (ctx, xasl->orderby_list, xasl->outptr_list);
  domain_walk_sort (ctx, xasl->after_iscan_list, xasl->outptr_list);
  domain_walk_xasl (ctx, xasl->scan_ptr);
  domain_walk_xasl (ctx, xasl->next);
  ctx->block = previous_block;
}

bool
domain_plan_validate (const DOMAIN_PLAN * plan)
{
  for (int i = 0; i < plan->n_items; i++)
    {
      const DOMAIN_PLAN_ITEM *item = &plan->items[i];
      if (!(item->flags & (DOMAIN_PLAN_GATE | DOMAIN_PLAN_ALIAS | DOMAIN_PLAN_RESIDUAL))
	  && !domain_is_fixed (item->fixed.domain))
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
  DOMAIN_LOAD_CONTEXT ctx = { thread_p, plan, NULL, NULL, NULL, false, NULL };
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
	      && p->item.flags == r->item.flags && p->item.fail[0] == r->item.fail[0])
	    {
	      r->alias = p;
	      break;
	    }
	}
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
      if ((r->item.flags & DOMAIN_PLAN_GATE) && !(r->cold.val_pos >= 0))
	{
	  plan->n_gate_nodes++;
	}
    }
  plan->items = (DOMAIN_PLAN_ITEM *) domain_plan_alloc (thread_p, plan->n_items, sizeof (*plan->items));
  plan->items_cold = (DOMAIN_PLAN_ITEM_COLD *) domain_plan_alloc (thread_p, plan->n_items, sizeof (*plan->items_cold));
  plan->gate_nodes = (DOMAIN_PLAN_ITEM **) domain_plan_alloc (thread_p, plan->n_gate_nodes, sizeof (*plan->gate_nodes));
  plan->gate_links = (DOMAIN_GATE_LINK *) domain_plan_alloc (thread_p, plan->n_gate_nodes, sizeof (*plan->gate_links));
  plan->const_refs = (DOMAIN_PLAN_ITEM **) domain_plan_alloc (thread_p, plan->n_const_refs, sizeof (*plan->const_refs));
  plan->volatile_refs =
    (DOMAIN_PLAN_ITEM **) domain_plan_alloc (thread_p, plan->n_volatile, sizeof (*plan->volatile_refs));
  ctx.failed = ctx.failed || (plan->n_items && (!plan->items || !plan->items_cold))
    || (plan->n_gate_nodes && (!plan->gate_nodes || !plan->gate_links))
    || (plan->n_const_refs && !plan->const_refs) || (plan->n_volatile && !plan->volatile_refs);
  int gate = 0, constant = 0, vol = 0;
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
	      if ((item->flags & DOMAIN_PLAN_GATE) && !(r->cold.val_pos >= 0))
		{
		  /* producer order: every operand record came earlier, so its owner already holds its published item */
		  DOMAIN_GATE_LINK *link = &plan->gate_links[gate];
		  memset (link, 0, sizeof (*link));
		  link->n_operands = r->n_link;
		  link->consumer = r->consumer;
		  for (int i = 0; i < r->n_link; i++)
		    {
		      link->operands[i] = r->link[i]->domain_plan;
		      link->literal[i] = r->link[i]->type == TYPE_DBVAL ? &r->link[i]->value.dbval : NULL;
		    }
		  plan->gate_nodes[gate++] = item;
		}
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
      return ER_FAILED;
    }
  return NO_ERROR;
}
