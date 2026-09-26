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
 * query_evaluator.c - Predicate evaluator
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "system_parameter.h"
#include "error_manager.h"
#include "heap_file.h"
#include "fetch.h"
#include "list_file.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "regu_var.hpp"
#include "set_object.h"
#include "xasl.h"
#include "dbtype.h"
#include "query_executor.h"
#include "query_opfunc.h"
#include "dbtype.h"
#include "language_support.h"
#include "string_opfunc.h"
#include "thread_entry.hpp"
#include "xasl_predicate.hpp"
#include "perf_monitor.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define UNKNOWN_CARD   -2	/* Unknown cardinality of a set member */

/* What an ALL/SOME term compares its item with in this execution (#352, D-352-03): a constant right side's elements
 * by position (`constant`), else a computed collection's elements by their keys (`table`), else `each` for every
 * element; `all` for every value of a list or a right side that is no collection. */
struct EVAL_ELEMENTS
{
  const DOMAIN_COMPARE *all;
  const DOMAIN_COMPARE *each;
  const DOMAIN_ELEMENT_TABLE *table;
  const DOMAIN_ELEMENTS *constant;
};

static DB_LOGICAL eval_negative (DB_LOGICAL res);
static DB_LOGICAL eval_logical_result (DB_LOGICAL res1, DB_LOGICAL res2);
static DB_LOGICAL eval_value_rel_cmp (THREAD_ENTRY * thread_p, DB_VALUE * dbval1, DB_VALUE * dbval2,
				      REL_OP rel_operator, const COMP_EVAL_TERM * et_comp, const val_descr * vd,
				      const DOMAIN_COMPARE * compare, const DB_VALUE * develop2);
static DB_LOGICAL eval_some_eval (THREAD_ENTRY * thread_p, DB_VALUE * item, DB_SET * set, REL_OP rel_operator,
				  const EVAL_ELEMENTS * elements, const val_descr * vd);
static DB_LOGICAL eval_all_eval (THREAD_ENTRY * thread_p, DB_VALUE * item, DB_SET * set, REL_OP rel_operator,
				 const EVAL_ELEMENTS * elements, const val_descr * vd);
static int eval_item_card_set (THREAD_ENTRY * thread_p, DB_VALUE * item, DB_SET * set, REL_OP rel_operator);
static DB_LOGICAL eval_some_list_eval (THREAD_ENTRY * thread_p, DB_VALUE * item, QFILE_LIST_ID * list_id,
				       REL_OP rel_operator, const DOMAIN_COMPARE * compare, const val_descr * vd);
static DB_LOGICAL eval_all_list_eval (THREAD_ENTRY * thread_p, DB_VALUE * item, QFILE_LIST_ID * list_id,
				      REL_OP rel_operator, const DOMAIN_COMPARE * compare, const val_descr * vd);
static int eval_item_card_sort_list (THREAD_ENTRY * thread_p, DB_VALUE * item, QFILE_LIST_ID * list_id);
static DB_LOGICAL eval_sub_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set1, QFILE_LIST_ID * list_id);
static DB_LOGICAL eval_sub_sort_list_to_multi_set (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, DB_SET * set);
static DB_LOGICAL eval_sub_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1,
						   QFILE_LIST_ID * list_id2);
static DB_LOGICAL eval_eq_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id);
static DB_LOGICAL eval_ne_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id);
static DB_LOGICAL eval_le_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id);
static DB_LOGICAL eval_lt_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id);
static DB_LOGICAL eval_le_sort_list_to_multi_set (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, DB_SET * set);
static DB_LOGICAL eval_lt_sort_list_to_multi_set (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, DB_SET * set);
static DB_LOGICAL eval_eq_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1,
						  QFILE_LIST_ID * list_id2);
static DB_LOGICAL eval_ne_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1,
						  QFILE_LIST_ID * list_id2);
static DB_LOGICAL eval_le_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1,
						  QFILE_LIST_ID * list_id2);
static DB_LOGICAL eval_lt_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1,
						  QFILE_LIST_ID * list_id2);
static DB_LOGICAL eval_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id,
					       REL_OP rel_operator);
static DB_LOGICAL eval_sort_list_to_multi_set (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, DB_SET * set,
					       REL_OP rel_operator);
static DB_LOGICAL eval_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1,
					       QFILE_LIST_ID * list_id2, REL_OP rel_operator);
static DB_LOGICAL eval_set_list_cmp (THREAD_ENTRY * thread_p, const COMP_EVAL_TERM * et_comp, val_descr * vd,
				     DB_VALUE * dbval1, DB_VALUE * dbval2);

/*
 * eval_negative () - negate the result
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   res(in): result
 */
static DB_LOGICAL
eval_negative (DB_LOGICAL res)
{
  /* negate the result */
  if (res == V_TRUE)
    {
      return V_FALSE;
    }
  else if (res == V_FALSE)
    {
      return V_TRUE;
    }

  /* V_ERROR, V_UNKNOWN */
  return res;
}

/*
 * eval_logical_result () - evaluate the given two results
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   res1(in): first result
 *   res2(in): second result
 */
static DB_LOGICAL
eval_logical_result (DB_LOGICAL res1, DB_LOGICAL res2)
{
  if (res1 == V_ERROR || res2 == V_ERROR)
    {
      return V_ERROR;
    }

  if (res1 == V_TRUE && res2 == V_TRUE)
    {
      return V_TRUE;
    }
  else if (res1 == V_FALSE || res2 == V_FALSE)
    {
      return V_FALSE;
    }

  return V_UNKNOWN;
}

/*
 * Predicate Evaluation
 */

/*
 * Planned comparisons (#352, D-352-01/02)
 *
 * A comparison term's record holds the comparison develop's tp_value_compare_with_error makes between its sides'
 * values, decided by the load or, once per execution, by the gate: the converters in develop's order, the type whose
 * cmpval compares, the collation, and develop's outcome when a conversion fails. The row runs them; it decides nothing.
 */

/* develop's comparison of the values, for a reason no record carries (#352) */
static DOMAIN_COMPARE
eval_values_decision (DOMAIN_COMPARE_REASON reason)
{
  DOMAIN_COMPARE compare;
  compare = DOMAIN_COMPARE
  {
  };
  compare.kernel = DOMAIN_COMPARE_VALUES;
  compare.reason = (unsigned char) reason;
  compare.value[0] = compare.value[1] = compare.codeset_side = compare.site = -1;
  return compare;
}

static const DOMAIN_COMPARE eval_Compare_null = eval_values_decision (DOMAIN_REASON_NULL);
static const DOMAIN_COMPARE eval_Compare_unplanned = eval_values_decision (DOMAIN_REASON_UNPLANNED);

/* The element comparisons of a set or list comparison: the collections' elements are their data, so the comparison
 * reads the key pair table by the two values' keys (#354, D-354-01). */
static DOMAIN_COMPARE
eval_keys_decision (void)
{
  DOMAIN_COMPARE compare = eval_values_decision (DOMAIN_REASON_UNPLANNED);
  compare.kernel = DOMAIN_COMPARE_KEYS;
  return compare;
}

static const DOMAIN_COMPARE eval_Compare_elements = eval_keys_decision ();

/*
 * eval_site_compare () - the decision of a comparison record in this execution: the load's, or the gate's for a site
 *			  the gate decides
 *   return: kernel VALUES with its reason where the row compares by value: a gate site read without the gate's state
 */
static inline const DOMAIN_COMPARE *
eval_site_compare (const DOMAIN_COMPARE_PLAN * site, const val_descr * vd)
{
  const DOMAIN_COMPARE *compare = &site->fixed;
  if (compare->kernel == DOMAIN_COMPARE_AT_GATE || compare->kernel == DOMAIN_COMPARE_AT_GATE_VOLATILE)
    {
      if (vd == NULL || vd->xasl_state == NULL || vd->xasl_state->resolved.compares == NULL)
	{
	  return &eval_Compare_unplanned;
	}
      const RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved;
      /* a PX worker's own XASL clone loaded the same stream: its sites number the leader's decisions as the leader's
       * plan does, and it reads them by that number (D-M3, #340) */
      assert (compare->site >= 0 && compare->site < resolved.n_compares && resolved.plan != NULL
	      && (resolved.inherited
		  ? resolved.plan->compares[compare->site]->value[0] == site->value[0]
		  && resolved.plan->compares[compare->site]->value[1] == site->value[1]
		  : resolved.plan->compares[compare->site] == site));
      compare = &resolved.compares[compare->site];
    }
  return compare;
}

/*
 * eval_planned_compare () - the comparison this execution makes for a term (D-352-01/02)
 *   return: its record's decision; a term without a record is the load's omission: every load plans its terms, a
 *	     predicate stream's included (#354)
 */
static inline const DOMAIN_COMPARE *
eval_planned_compare (const COMP_EVAL_TERM * et_comp, const val_descr * vd)
{
  const DOMAIN_COMPARE_PLAN *site = et_comp->domain_compare;
  if (site == NULL)
    {
      return &eval_Compare_unplanned;
    }
  return eval_site_compare (site, vd);
}

/*
 * eval_planned_elements () - what an ALL/SOME term compares its item with in this execution (#352, D-352-03)
 *
 * The load's record or table, or the gate's decisions: the row reads them and decides nothing. Where neither holds,
 * the comparisons keep develop's with the reason (the execution boundary (b) for an unplanned one).
 */
static inline void
eval_planned_elements (const ALSM_EVAL_TERM * et_alsm, const val_descr * vd, EVAL_ELEMENTS * elements)
{
  const DOMAIN_ELEMENT_COMPARE_PLAN *site = et_alsm->domain_compare;
  elements->all = elements->each = &eval_Compare_unplanned;
  elements->table = NULL;
  elements->constant = NULL;
  if (site == NULL)
    {
      return;
    }
  if (site->kind == DOMAIN_ELEMENTS_PAIR)
    {
      elements->all = eval_site_compare (&site->pair, vd);
      if (elements->all->kernel == DOMAIN_COMPARE_KEYS)
	{
	  /* the key pair table (a stream's item, #354) holds for any value the right side has */
	  elements->each = elements->all;
	}
      return;
    }
  if (site->kind == DOMAIN_ELEMENTS_TABLE)
    {
      elements->table = site->table;
      return;
    }
  if (vd == NULL || vd->xasl_state == NULL || vd->xasl_state->resolved.elements == NULL)
    {
      return;
    }
  const RESOLVED_DOMAIN_TABLE & resolved = vd->xasl_state->resolved;
  /* a PX worker's own XASL clone numbers its sites as the leader's plan does (D-M3) */
  assert (site->site >= 0 && site->site < resolved.n_elements && resolved.plan != NULL
	  && site->site < resolved.plan->n_element_sites
	  && (resolved.inherited ? resolved.plan->element_sites[site->site]->kind == site->kind
	      : resolved.plan->element_sites[site->site] == site));
  const DOMAIN_ELEMENTS *decided = &resolved.elements[site->site];
  switch (decided->read)
    {
    case DOMAIN_READ_POSITIONS:
      elements->constant = decided;
      break;
    case DOMAIN_READ_TABLE:
      elements->table = decided->table;
      break;
    case DOMAIN_READ_PAIR:
      elements->all = &decided->compares[0];
      break;
    default:
      /* nothing decided: a NULL constant compares nothing, and a constant the row computes raises its error first
       * (D-352-05) */
      break;
    }
}

/* The decision a table holds for an element: by its type and, for a string or an ENUM, its collation. */
static inline const DOMAIN_COMPARE *
eval_element_compare (const DOMAIN_ELEMENT_TABLE * table, const DB_VALUE * element)
{
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (element);
  int entry = type >= 0 && type < DOMAIN_ELEMENT_TYPES ? table->first[type] : -1;
  if (entry >= 0 && TP_TYPE_HAS_COLLATION (type))
    {
      const int collation = type == DB_TYPE_ENUMERATION ? db_get_enum_collation (element)
	: db_get_string_collation (element);
      const int ordinal = collation >= 0 && collation < DOMAIN_ELEMENT_COLLATIONS ? table->ordinal[collation] : -1;
      entry = ordinal >= 0 ? entry + ordinal : -1;
    }
  return entry >= 0 ? &table->entry[entry] : &eval_Compare_unplanned;
}

/* Whether develop's comparison of two values decides anything: a coercion between their types or a merge of their
 * string collations. */
static bool
eval_compare_decides (const DB_VALUE * dbval1, const DB_VALUE * dbval2)
{
  if (DB_IS_NULL (dbval1) || DB_IS_NULL (dbval2))
    {
      return false;
    }
  const DB_TYPE type = DB_VALUE_DOMAIN_TYPE (dbval1);
  if (type != DB_VALUE_DOMAIN_TYPE (dbval2))
    {
      return true;
    }
  return TP_IS_CHAR_TYPE (type) && db_get_string_collation (dbval1) != db_get_string_collation (dbval2);
}

/* The value side i compares: the constant the gate converted once, or the row's value. */
static inline const DB_VALUE *
eval_compare_side (const DOMAIN_COMPARE * compare, const val_descr * vd, int side, const DB_VALUE * row_value)
{
  return compare->value[side] >= 0 ? vd->dbval_ptr + compare->value[side] : row_value;
}

/*
 * eval_compare_planned () - a comparison planned before any row (D-352-02): develop's NULL rule, then the kernel the
 *			     record names, on the row's values and the gate's own values of the constant sides
 */
static DB_VALUE_COMPARE_RESULT
eval_compare_planned (THREAD_ENTRY * thread_p, const DOMAIN_COMPARE * compare, const val_descr * vd,
		      const DB_VALUE * dbval1, const DB_VALUE * dbval2, int total_order, bool * can_compare)
{
  const DB_VALUE *value1 = eval_compare_side (compare, vd, 0, dbval1);
  const DB_VALUE *value2 = eval_compare_side (compare, vd, 1, dbval2);
  if (DB_IS_NULL (value1))
    {
      return DB_IS_NULL (value2) ? (total_order ? DB_EQ : DB_UNK) : (total_order ? DB_LT : DB_UNK);
    }
  if (DB_IS_NULL (value2))
    {
      return total_order ? DB_GT : DB_UNK;
    }
  if (compare->kernel == DOMAIN_COMPARE_OBJECT || compare->kernel == DOMAIN_COMPARE_KEYS)
    {
      /* an object side meets OIDs on the server, and a collection's elements are its data: the key pair table's
       * comparison of the two values' keys (#354) */
      return domain_compare_by_keys (dbval1, dbval2, 1, total_order, can_compare);
    }
  return domain_compare_values (thread_p, compare, value1, value2, total_order, can_compare);
}

#if !defined (NDEBUG)
/* One side of a planned comparison in the shadow checks' report: the regu, its domain, and the plan's view of it. */
static void
eval_report_planned_side (const char *name, const REGU_VARIABLE * regu, const DOMAIN_PLAN_ITEM * item,
			  const val_descr * vd)
{
  const TP_DOMAIN *domain = regu != NULL ? qexec_node_domain (vd, regu->domain, regu->domain_plan) : NULL;
  const TP_DOMAIN *decided = NULL;
  if (item != NULL && item->slot >= 0 && vd != NULL && vd->xasl_state != NULL
      && item->slot < vd->xasl_state->resolved.n_slots)
    {
      decided = vd->xasl_state->resolved.table[item->slot].domain;
    }
  const TP_DOMAIN *fixed = item != NULL ? item->fixed.domain : NULL;
  const int opcode = regu != NULL && (regu->type == TYPE_INARITH || regu->type == TYPE_OUTARITH)
    && regu->value.arithptr != NULL ? (int) regu->value.arithptr->opcode : -1;
  fprintf (stderr, "planned comparison   %s regu type=%d opcode=%d flags=0x%x domain=%d/%d/%d item slot=%d ref=%d "
	   "fixed=%d/%d/%d decided=%d/%d/%d\n", name, regu != NULL ? (int) regu->type : -1, opcode,
	   regu != NULL ? regu->flags : 0,
	   domain != NULL ? (int) TP_DOMAIN_TYPE (domain) : -1, domain != NULL ? domain->collation_id : -1,
	   domain != NULL ? (int) domain->collation_flag : -1, item != NULL ? item->slot : -2,
	   item != NULL ? item->ref : -2, fixed != NULL ? (int) TP_DOMAIN_TYPE (fixed) : -1,
	   fixed != NULL ? fixed->collation_id : -1, fixed != NULL ? (int) fixed->collation_flag : -1,
	   decided != NULL ? (int) TP_DOMAIN_TYPE (decided) : -1, decided != NULL ? decided->collation_id : -1,
	   decided != NULL ? (int) decided->collation_flag : -1);
}

/* The shadow checks' report of a planned comparison and its values (optdebug), before they assert. */
static void
eval_report_planned_compare (const char *what, const DOMAIN_COMPARE * compare, const DB_VALUE * dbval1,
			     const DB_VALUE * dbval2, const COMP_EVAL_TERM * et_comp, const val_descr * vd)
{
  const DB_VALUE *values[2] = { dbval1, dbval2 };
  char sides[2][96];
  for (int i = 0; i < 2; i++)
    {
      const DB_TYPE type = DB_IS_NULL (values[i]) ? DB_TYPE_NULL : DB_VALUE_DOMAIN_TYPE (values[i]);
      const int collation = TP_IS_CHAR_TYPE (type) ? db_get_string_collation (values[i])
	: type == DB_TYPE_ENUMERATION ? db_get_enum_collation (values[i]) : -1;
      const int codeset = TP_IS_CHAR_TYPE (type) ? db_get_string_codeset (values[i])
	: type == DB_TYPE_ENUMERATION ? db_get_enum_codeset (values[i]) : -1;
      snprintf (sides[i], sizeof (sides[i]), "type=%d collation=%d codeset=%d", (int) type, collation, codeset);
    }
  fprintf (stderr, "planned comparison %s: kernel=%d reason=%d first=%d source=%d,%d converted_first=%d "
	   "collation=%d codeset_side=%d value=%d,%d failed=%d | lhs %s | rhs %s\n", what, (int) compare->kernel,
	   (int) compare->reason, (int) compare->first, (int) compare->source[0], (int) compare->source[1],
	   (int) compare->converted_first, compare->collation, compare->codeset_side, compare->value[0],
	   compare->value[1], (int) compare->failed, sides[0], sides[1]);
  if (et_comp != NULL)
    {
      const DOMAIN_COMPARE_PLAN *site = et_comp->domain_compare;
      fprintf (stderr, "planned comparison   site fixed.kernel=%d site=%d constant=%d,%d literal=%d,%d\n",
	       site != NULL ? (int) site->fixed.kernel : -1, site != NULL ? site->fixed.site : -2,
	       site != NULL && site->constant[0] != NULL, site != NULL && site->constant[1] != NULL,
	       site != NULL && site->literal[0] != NULL, site != NULL && site->literal[1] != NULL);
      eval_report_planned_side ("lhs", et_comp->lhs, site != NULL ? site->operand[0] : NULL, vd);
      eval_report_planned_side ("rhs", et_comp->rhs, site != NULL ? site->operand[1] : NULL, vd);
    }
}

/*
 * eval_assert_planned_sides () - #352 shadow check before the kernel: a side read from the row has the type its
 *				  converters and cmpval were planned for (a constant side compares the gate's own value)
 */
static void
eval_assert_planned_sides (const DOMAIN_COMPARE * compare, const DB_VALUE * dbval1, const DB_VALUE * dbval2,
			   const COMP_EVAL_TERM * et_comp, const val_descr * vd)
{
  if (compare->kernel == DOMAIN_COMPARE_OBJECT || compare->kernel == DOMAIN_COMPARE_KEYS)
    {
      /* the key pair table reads the values' own keys */
      return;
    }
  const bool lhs = compare->value[0] != -1 || DB_IS_NULL (dbval1)
    || DB_VALUE_DOMAIN_TYPE (dbval1) == (DB_TYPE) compare->source[0];
  const bool rhs = compare->value[1] != -1 || DB_IS_NULL (dbval2)
    || DB_VALUE_DOMAIN_TYPE (dbval2) == (DB_TYPE) compare->source[1];
  if (!lhs || !rhs)
    {
      eval_report_planned_compare ("side type", compare, dbval1, dbval2, et_comp, vd);
    }
  assert (lhs && rhs);
}

/*
 * eval_assert_planned_compare () - #352 shadow check after the kernel: develop's comparison of the same values,
 *				    uncounted, gives the planned comparison's result, comparability and error
 *   asks_comparable(in): the caller asks whether the values compare (tp_value_compare_with_error's contract); false
 *			  for tp_value_compare's, which asks nothing (#354)
 */
static void
eval_assert_planned_compare (const DOMAIN_COMPARE * compare, const DB_VALUE * dbval1, const DB_VALUE * dbval2,
			     int total_order, DB_VALUE_COMPARE_RESULT result, bool comparable,
			     const COMP_EVAL_TERM * et_comp, const val_descr * vd, bool asks_comparable = true)
{
  const int error = comparable ? NO_ERROR : er_errid ();
  bool develop_comparable = true;
  er_stack_push ();
  const DB_VALUE_COMPARE_RESULT develop =
    tp_value_compare_uncounted (dbval1, dbval2, 1, total_order, asks_comparable ? &develop_comparable : NULL);
  const int develop_error = develop_comparable ? NO_ERROR : er_errid ();
  er_stack_pop ();
  const bool same = develop == result && develop_comparable == comparable && develop_error == error;
  if (!same)
    {
      eval_report_planned_compare ("result", compare, dbval1, dbval2, et_comp, vd);
      fprintf (stderr, "planned comparison result: planned=%d comparable=%d error=%d develop=%d comparable=%d "
	       "error=%d\n",
	       (int) result, (int) comparable, error, (int) develop, (int) develop_comparable, develop_error);
    }
  assert (same);
}
#endif

/*
 * eval_compare_values_planned () - a comparison of two values planned before any row outside a predicate term (#354):
 *				    the site's decision in this execution, develop's NULL rule and the kernel it names
 *   return: the result; *can_compare false with develop's error where the values do not compare, and at the execution
 *	     boundary (b) (ER_QPROC_DOMAIN_UNRESOLVED)
 *   site(in): the comparison's record; NULL is the load's omission
 *   vd(in): value descriptor of the execution (the gate's decisions and converted constants)
 *   can_compare(out): NULL for tp_value_compare's contract, which asks nothing: values that do not compare answer by
 *		       their rank without an error
 */
DB_VALUE_COMPARE_RESULT
eval_compare_values_planned (THREAD_ENTRY * thread_p, const DOMAIN_COMPARE_PLAN * site, const val_descr * vd,
			     const DB_VALUE * value1, const DB_VALUE * value2, int total_order, bool * can_compare)
{
  if (can_compare != NULL)
    {
      *can_compare = true;
    }
  const DOMAIN_COMPARE *compare = site != NULL ? eval_site_compare (site, vd) : &eval_Compare_unplanned;
  if (compare->kernel != DOMAIN_COMPARE_VALUES)
    {
#if !defined (NDEBUG)
      eval_assert_planned_sides (compare, value1, value2, NULL, vd);
#endif
      const DB_VALUE_COMPARE_RESULT result =
	eval_compare_planned (thread_p, compare, vd, value1, value2, total_order, can_compare);
#if !defined (NDEBUG)
      eval_assert_planned_compare (compare, value1, value2, total_order, result,
				   can_compare != NULL ? *can_compare : true, NULL, vd, can_compare != NULL);
#endif
      return result;
    }
  if (eval_compare_decides (value1, value2))
    {
      /* the execution boundary (b): no plan holds this comparison, and develop would decide it from the values */
#if !defined (NDEBUG)
      eval_report_planned_compare ("boundary", compare, value1, value2, NULL, vd);
#endif
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DOMAIN_UNRESOLVED, 4, "execute", "", -1,
	      pr_type_name (DB_VALUE_DOMAIN_TYPE (value2)));
      if (can_compare != NULL)
	{
	  *can_compare = false;
	}
      return DB_UNK;
    }
  /* NULL answers first */
  return tp_value_compare_with_error (value1, value2, 1, total_order, can_compare);
}

/*
 * eval_value_rel_cmp () - Compare two db_values according to the given
 *                       relational operator
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   dbval1(in): first db_value
 *   dbval2(in): second db_value
 *   rel_operator(in): Relational operator
 *   et_comp(in): compound evaluation term
 *   vd(in): value descriptor of the term's execution (the gate's decisions and converted constants, #352)
 *   compare(in): the decision of an element comparison (#352, D-352-03); NULL: the term's
 *   develop2(in): the right side develop compares where dbval2 is the gate's converted copy of it (the shadow checks
 *		   compare that one); NULL: dbval2
 */
static DB_LOGICAL
eval_value_rel_cmp (THREAD_ENTRY * thread_p, DB_VALUE * dbval1, DB_VALUE * dbval2, REL_OP rel_operator,
		    const COMP_EVAL_TERM * et_comp, const val_descr * vd, const DOMAIN_COMPARE * compare,
		    const DB_VALUE * develop2)
{
  int result;
  bool comparable = true;

  /*
   * we get here for either an ordinal comparison or a set comparison.
   * Set comparisons are R_SUBSET, R_SUBSETEQ, R_SUPERSET, R_SUPSERSETEQ.
   * All others are ordinal comparisons.
   */

  /* tp_value_compare_with_error () does coercion */
  switch (rel_operator)
    {
    case R_SUBSET:
    case R_SUBSETEQ:
    case R_SUPERSET:
    case R_SUPERSETEQ:
      /* do set comparison */
      result = tp_set_compare (dbval1, dbval2, 1, 0);
      break;

    default:
      {
	/* R_EQ_TORDER compares in total order; the others compare ordinally, and NULL's still yield UNKNOWN */
	const int total_order = rel_operator == R_EQ_TORDER;
	if (compare == NULL)
	  {
	    compare = et_comp != NULL ? eval_planned_compare (et_comp, vd) : &eval_Compare_unplanned;
	  }
	if (compare->kernel != DOMAIN_COMPARE_VALUES)
	  {
	    /* S-09, S-10: the comparison the load or the gate planned (D-352-01/02); a constant is converted once,
	     * into a value of its own, and the shared value stays as it is */
#if !defined (NDEBUG)
	    eval_assert_planned_sides (compare, dbval1, dbval2, et_comp, vd);
#endif
	    result = eval_compare_planned (thread_p, compare, vd, dbval1, dbval2, total_order, &comparable);
#if !defined (NDEBUG)
	    eval_assert_planned_compare (compare, dbval1, develop2 != NULL ? develop2 : dbval2, total_order,
					 (DB_VALUE_COMPARE_RESULT) result, comparable, et_comp, vd);
#endif
	  }
	else if (eval_compare_decides (dbval1, dbval2))
	  {
	    /* the execution boundary (b): no plan holds this comparison, and develop would decide it from the values
	     * (#352) */
#if !defined (NDEBUG)
	    eval_report_planned_compare ("boundary", compare, dbval1, dbval2, et_comp, vd);
#endif
	    assert (false);
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_DOMAIN_UNRESOLVED, 4, "execute", "", -1,
		    pr_type_name (DB_VALUE_DOMAIN_TYPE (dbval2)));
	    return V_ERROR;
	  }
	else
	  {
	    /* NULL answers first */
	    result = tp_value_compare_with_error (dbval1, dbval2, 1, total_order, &comparable);
	  }
      }
      break;
    }

  if (!comparable)
    {
      return V_ERROR;
    }

  if (result == DB_UNK && rel_operator != R_NULLSAFE_EQ)
    {
      return V_UNKNOWN;
    }

  switch (rel_operator)
    {
    case R_EQ:
      return ((result == DB_EQ) ? V_TRUE : V_FALSE);
    case R_EQ_TORDER:
      return ((result == DB_EQ) ? V_TRUE : V_FALSE);
    case R_LT:
      return ((result == DB_LT) ? V_TRUE : V_FALSE);
    case R_LE:
      return (((result == DB_LT) || (result == DB_EQ)) ? V_TRUE : V_FALSE);
    case R_GT:
      return ((result == DB_GT) ? V_TRUE : V_FALSE);
    case R_GE:
      return (((result == DB_GT) || (result == DB_EQ)) ? V_TRUE : V_FALSE);
    case R_NE:
      return ((result != DB_EQ) ? V_TRUE : V_FALSE);
    case R_SUBSET:
      return ((result == DB_SUBSET) ? V_TRUE : V_FALSE);
    case R_SUBSETEQ:
      return (((result == DB_SUBSET) || (result == DB_EQ)) ? V_TRUE : V_FALSE);
    case R_SUPERSET:
      return ((result == DB_SUPERSET) ? V_TRUE : V_FALSE);
    case R_SUPERSETEQ:
      return (((result == DB_SUPERSET) || (result == DB_EQ)) ? V_TRUE : V_FALSE);
    case R_NULLSAFE_EQ:
      if (result == DB_EQ)
	{
	  return V_TRUE;
	}
      else
	{
	  if (DB_IS_NULL (dbval1))
	    {
	      if (DB_IS_NULL (dbval2))
		{
		  return V_TRUE;
		}
	      else
		{
		  return V_FALSE;
		}
	    }
	  else
	    {
	      return V_FALSE;
	    }
	}
      break;
    default:
      return V_ERROR;
    }
}

/*
 * eval_some_eval () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN, V_ERROR)
 *   item(in): db_value item
 *   set(in): collection of elements
 *   rel_operator(in): relational comparison operator
 *   elements(in): the element comparisons planned for this execution (#352, D-352-03)
 *   vd(in): value descriptor of the term's execution
 */

static DB_LOGICAL
eval_some_eval (THREAD_ENTRY * thread_p, DB_VALUE * item, DB_SET * set, REL_OP rel_operator,
		const EVAL_ELEMENTS * elements, const val_descr * vd)
{
  int i;
  DB_LOGICAL res, t_res;
  DB_VALUE elem_val;

  PRIM_SET_NULL (&elem_val);

  res = V_FALSE;

  const int size = set_size (set);
  /* a constant's elements: the gate decided each one and holds its own value of it, by position */
  assert (elements->constant == NULL || elements->constant->n == size);
  for (i = 0; i < size; i++)
    {
      DB_VALUE *element = &elem_val;
      const DOMAIN_COMPARE *compare;
      const DB_VALUE *develop2 = NULL;
      if (elements->constant != NULL)
	{
	  element = &elements->constant->value[i];
	  compare = &elements->constant->compares[elements->constant->decision[i]];
#if !defined (NDEBUG)
	  /* the shadow checks compare the element as the set holds it */
	  if (set_get_element (set, i, &elem_val) != NO_ERROR)
	    {
	      return V_ERROR;
	    }
	  develop2 = &elem_val;
#endif
	}
      else
	{
	  if (set_get_element (set, i, &elem_val) != NO_ERROR)
	    {
	      return V_ERROR;
	    }
	  compare = DB_IS_NULL (&elem_val) ? &eval_Compare_null
	    : elements->table != NULL ? eval_element_compare (elements->table, &elem_val) : elements->each;
	}

      t_res = eval_value_rel_cmp (thread_p, item, element, rel_operator, NULL, vd, compare, develop2);
      pr_clear_value (&elem_val);
      if (t_res == V_TRUE)
	{
	  return V_TRUE;
	}
      else if (t_res == V_ERROR)
	{
	  return V_ERROR;
	}
      else if (t_res == V_UNKNOWN)
	{
	  res = V_UNKNOWN;	/* never returns here. we should proceed */
	}
    }

  return res;
}

/*
 * eval_all_eval () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN, V_ERROR)
 *   item(in): db_value item
 *   set(in): collection of elements
 *   rel_operator(in): relational comparison operator
 *   elements(in): the element comparisons planned for this execution (#352, D-352-03)
 *   vd(in): value descriptor of the term's execution
 *
 * Note: This routine tries to determine whether a specific relation
 *              as determined by the relational operator rel_operator holds between
 *              the given bound item value and all the members of the
 *              given set of elements. The set can be a basic set, multi_set
 *              or sequence. It returns V_TRUE, V_FALSE, V_UNKNOWN using the
 *              following reasoning:
 *
 *              V_TRUE:     - if all the values in the set are determined
 *                            to hold the relationship, or
 *                          - the set is empty
 *              V_FALSE:    - if there exists a value in the set which is
 *                            determined not hold the relationship.
 *              V_UNKNOWN:  - set is homogeneous and set element type is not
 *                            comparable with the item type
 *                          - set has no value determined to fail to hold the
 *                            rel. and at least one value which can not be
 *                            determined to hold the relationship.
 *              V_ERROR:    - an error occurred.
 *
 */
static DB_LOGICAL
eval_all_eval (THREAD_ENTRY * thread_p, DB_VALUE * item, DB_SET * set, REL_OP rel_operator,
	       const EVAL_ELEMENTS * elements, const val_descr * vd)
{
  DB_LOGICAL some_res;

  /*
   * use the some quantifier first on a negated relational operator
   * then find the result boolean value for the all quantifier
   */
  switch (rel_operator)
    {
    case R_LT:
      rel_operator = R_GE;
      break;

    case R_LE:
      rel_operator = R_GT;
      break;

    case R_GT:
      rel_operator = R_LE;
      break;

    case R_GE:
      rel_operator = R_LT;
      break;

    case R_EQ:
    case R_EQ_TORDER:
      rel_operator = R_NE;
      break;

    case R_NE:
      rel_operator = R_EQ;
      break;

    default:
      return V_ERROR;
    }

  some_res = eval_some_eval (thread_p, item, set, rel_operator, elements, vd);
  /* negate the some result */
  return eval_negative (some_res);
}

/*
 * eval_item_card_set () -
 *   return: int (cardinality)
 *           >= 0 : normal cardinality
 *           ER_FAILED : ERROR
 *           UNKNOWN_CARD : unknown cardinality value
 *   item(in): db_value item
 *   set(in): collection of elements
 *   rel_operator(in): relational comparison operator
 *
 * Note: This routine returns the number of set elements (cardinality)
 *              which are determined to hold the given relationship with the
 *              specified item value. If the relationship is the equality
 *              relationship, the returned value means the cardinality of the
 *              given element in the set and must always be less than equal
 *              to 1 for the case of basic sets.
 */
static int
eval_item_card_set (THREAD_ENTRY * thread_p, DB_VALUE * item, DB_SET * set, REL_OP rel_operator)
{
  int num, i;
  DB_LOGICAL res;
  DB_VALUE elem_val;

  PRIM_SET_NULL (&elem_val);

  num = 0;

  for (i = 0; i < set_size (set); i++)
    {
      if (set_get_element (set, i, &elem_val) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (db_value_is_null (&elem_val))
	{
	  pr_clear_value (&elem_val);
	  return UNKNOWN_CARD;
	}

      res = eval_value_rel_cmp (thread_p, item, &elem_val, rel_operator, NULL, NULL, &eval_Compare_elements, NULL);
      pr_clear_value (&elem_val);

      if (res == V_ERROR)
	{
	  return ER_FAILED;
	}

      if (res == V_TRUE)
	{
	  num++;
	}
    }

  return num;
}

/*
 * List File Related Evaluation
 */

/*
 * eval_some_list_eval () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN, V_ERROR)
 *   item(in): db_value item
 *   list_id(in): list file identifier
 *   rel_operator(in): relational comparison operator
 *   compare(in): the comparison of the item with the list's column planned for this execution (#352, D-352-03)
 *   vd(in): value descriptor of the term's execution
 *
 * Note: This routine tries to determine whether a specific relation
 *              as determined by the relational operator rel_operator holds between
 *              the given bound item value and at least one member of the
 *              given list of elements. It returns V_TRUE, V_FALSE,
 *              V_UNKNOWN, V_ERROR using the following reasoning:
 *
 *              V_TRUE:     - there exists a value in the list that is
 *                            determined to hold the relationship.
 *              V_FALSE:    - all the values in the list are determined not
 *                            to hold the relationship, or
 *                          - the list is empty
 *              V_UNKNOWN:  - list has no value determined to hold the rel.
 *                            and at least one value which can not be
 *                            determined to fail to hold the relationship.
 *              V_ERROR:    - an error occurred.
 *
 *  Note: The IN relationship can be stated as item has the equality rel. with
 *        one of the list elements.
 */
static DB_LOGICAL
eval_some_list_eval (THREAD_ENTRY * thread_p, DB_VALUE * item, QFILE_LIST_ID * list_id, REL_OP rel_operator,
		     const DOMAIN_COMPARE * compare, const val_descr * vd)
{
  DB_LOGICAL res, t_res;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  DB_VALUE list_val;
  SCAN_CODE qp_scan;
  const PR_TYPE *pr_type;
  OR_BUF buf;
  int length;
  char *ptr;

  /* assert */
  if (list_id->type_list.domp == NULL)
    {
      return V_ERROR;
    }

  PRIM_SET_NULL (&list_val);

  if (list_id->tuple_cnt == 0)
    {
      return V_FALSE;		/* empty set */
    }

  if (qfile_open_list_scan (list_id, &s_id) != NO_ERROR)
    {
      return V_ERROR;
    }

  pr_type = list_id->type_list.domp[0]->type;
  if (pr_type == NULL)
    {
      qfile_close_scan (thread_p, &s_id);
      return V_ERROR;
    }

  res = V_FALSE;
  while ((qp_scan = qfile_scan_list_next (thread_p, &s_id, &tplrec, PEEK)) == S_SUCCESS)
    {
      if (qfile_locate_tuple_value (tplrec.tpl, 0, &ptr, &length) == V_UNBOUND)
	{
	  res = V_UNKNOWN;
	}
      else
	{
	  or_init (&buf, ptr, length);

	  if (pr_type->data_readval (&buf, &list_val, list_id->type_list.domp[0], -1, true, NULL, 0) != NO_ERROR)
	    {
	      qfile_close_scan (thread_p, &s_id);
	      return V_ERROR;
	    }

	  t_res = eval_value_rel_cmp (thread_p, item, &list_val, rel_operator, NULL, vd, compare, NULL);
	  if (t_res == V_TRUE || t_res == V_ERROR)
	    {
	      pr_clear_value (&list_val);
	      qfile_close_scan (thread_p, &s_id);
	      return t_res;
	    }
	  else if (t_res == V_UNKNOWN)
	    {
	      res = V_UNKNOWN;
	    }
	  pr_clear_value (&list_val);
	}
    }

  qfile_close_scan (thread_p, &s_id);

  return (qp_scan == S_END) ? res : V_ERROR;
}

/*
 * eval_all_list_eval () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN, V_ERROR)
 *   item(in): db_value
 *   list_id(in): list file identifier
 *   rel_operator(in): relational comparison operator
 *   compare(in): the comparison of the item with the list's column planned for this execution (#352, D-352-03)
 *   vd(in): value descriptor of the term's execution
 *
 * Note: This routine tries to determine whether a specific relation
 *              as determined by the relational operator rel_operator holds between
 *              the given db_value and all the members of the
 *              given list of elements. It returns V_TRUE, V_FALSE, V_UNKNOWN
 *              or V_ERROR using following reasoning:
 *
 *              V_TRUE:     - if all the values in the list are determined
 *                            to hold the relationship, or
 *                          - the list is empty
 *              V_FALSE:    - if there exists a value in the list which is
 *                            determined not hold the relationship.
 *              V_UNKNOWN:  - list has no value determined to fail to hold the
 *                            rel. and at least one value which can not be
 *                            determined to hold the relationship.
 *              V_ERROR:    - an error occurred.
 *
 */
static DB_LOGICAL
eval_all_list_eval (THREAD_ENTRY * thread_p, DB_VALUE * item, QFILE_LIST_ID * list_id, REL_OP rel_operator,
		    const DOMAIN_COMPARE * compare, const val_descr * vd)
{
  DB_LOGICAL some_res;

  /* first use some quantifier on a negated relational operator */
  switch (rel_operator)
    {
    case R_LT:
      rel_operator = R_GE;
      break;
    case R_LE:
      rel_operator = R_GT;
      break;
    case R_GT:
      rel_operator = R_LE;
      break;
    case R_GE:
      rel_operator = R_LT;
      break;
    case R_EQ:
    case R_EQ_TORDER:
      rel_operator = R_NE;
      break;
    case R_NE:
      rel_operator = R_EQ;
      break;
    default:
      return V_ERROR;
    }

  some_res = eval_some_list_eval (thread_p, item, list_id, rel_operator, compare, vd);
  /* negate the result */
  return eval_negative (some_res);
}

/*
 * eval_item_card_sort_list () -
 *   return: int (cardinality, UNKNOWN_CARD, ER_FAILED for error cases)
 *   item(in): db_value item
 *   list_id(in): list file identifier
 *
 * Note: This routine returns the number of set elements (cardinality)
 *              which are determined to hold the equality relationship with
 *              specified item value. The list file values must have already
 *              been sorted.
 */
static int
eval_item_card_sort_list (THREAD_ENTRY * thread_p, DB_VALUE * item, QFILE_LIST_ID * list_id)
{
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tplrec = { NULL, 0 };
  DB_VALUE list_val;
  SCAN_CODE qp_scan;
  const PR_TYPE *pr_type;
  OR_BUF buf;
  DB_LOGICAL rc;
  int length;
  int card;
  char *ptr;

  /* assert */
  if (list_id->type_list.domp == NULL)
    {
      return ER_FAILED;
    }

  PRIM_SET_NULL (&list_val);
  card = 0;

  if (qfile_open_list_scan (list_id, &s_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  pr_type = list_id->type_list.domp[0]->type;
  if (pr_type == NULL)
    {
      qfile_close_scan (thread_p, &s_id);
      return ER_FAILED;
    }

  while ((qp_scan = qfile_scan_list_next (thread_p, &s_id, &tplrec, PEEK)) == S_SUCCESS)
    {
      if (qfile_locate_tuple_value (tplrec.tpl, 0, &ptr, &length) == V_UNBOUND)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return UNKNOWN_CARD;
	}

      or_init (&buf, ptr, length);

      pr_type->data_readval (&buf, &list_val, list_id->type_list.domp[0], -1, true, NULL, 0);

      rc = eval_value_rel_cmp (thread_p, item, &list_val, R_LT, NULL, NULL, &eval_Compare_elements, NULL);
      if (rc == V_ERROR)
	{
	  pr_clear_value (&list_val);
	  qfile_close_scan (thread_p, &s_id);
	  return ER_FAILED;
	}
      else if (rc == V_TRUE)
	{
	  pr_clear_value (&list_val);
	  continue;
	}

      rc = eval_value_rel_cmp (thread_p, item, &list_val, R_EQ, NULL, NULL, &eval_Compare_elements, NULL);
      pr_clear_value (&list_val);

      if (rc == V_ERROR)
	{
	  qfile_close_scan (thread_p, &s_id);
	  return ER_FAILED;
	}
      else if (rc == V_TRUE)
	{
	  card++;
	}
      else
	{
	  break;
	}
    }

  qfile_close_scan (thread_p, &s_id);

  return (qp_scan == S_END) ? card : ER_FAILED;
}

/*
 * eval_sub_multi_set_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   set1(in): DB_SET representation
 * 	 list_id(in): Sorted LIST FILE identifier
 *
 * Note: Find if given multi_set is a subset of the given list file.
 *              The list file must be of one column and treated like a
 *              multi_set. The routine uses the same semantics of finding
 *              subset relationship between two multi_sets.
 *
 * Note: in a sorted list file of one column , ALL the NULL values, tuples
 *       appear at the beginning of the list file.
 */
static DB_LOGICAL
eval_sub_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set1, QFILE_LIST_ID * list_id)
{
  int i, k, card, card1, card2;
  DB_LOGICAL res;
  DB_LOGICAL rc;
  DB_VALUE elem_val, elem_val2;
  int found;

  PRIM_SET_NULL (&elem_val);
  PRIM_SET_NULL (&elem_val2);

  card = set_size (set1);
  if (card == 0)
    {
      return V_TRUE;		/* empty set */
    }

  res = V_TRUE;
  for (i = 0; i < card; i++)
    {
      if (set_get_element (set1, i, &elem_val) != NO_ERROR)
	{
	  return V_ERROR;
	}
      if (db_value_is_null (&elem_val))
	{
	  return V_UNKNOWN;
	}

      /* search for the value to see if value has already been considered */
      found = false;
      for (k = 0; !found && k < i; k++)
	{
	  if (set_get_element (set1, k, &elem_val2) != NO_ERROR)
	    {
	      pr_clear_value (&elem_val);
	      return V_ERROR;
	    }
	  if (db_value_is_null (&elem_val2))
	    {
	      pr_clear_value (&elem_val2);
	      continue;
	    }

	  rc = eval_value_rel_cmp (thread_p, &elem_val, &elem_val2, R_EQ, NULL, NULL, &eval_Compare_elements, NULL);
	  if (rc == V_ERROR)
	    {
	      pr_clear_value (&elem_val);
	      pr_clear_value (&elem_val2);
	      return V_ERROR;
	    }
	  else if (rc == V_TRUE)
	    {
	      found = true;
	    }
	  pr_clear_value (&elem_val2);
	}

      if (found)
	{
	  pr_clear_value (&elem_val);
	  continue;
	}

      card1 = eval_item_card_set (thread_p, &elem_val, set1, R_EQ);
      if (card1 == ER_FAILED)
	{
	  pr_clear_value (&elem_val);
	  return V_ERROR;
	}
      else if (card1 == UNKNOWN_CARD)
	{
	  pr_clear_value (&elem_val);
	  return V_UNKNOWN;
	}

      card2 = eval_item_card_sort_list (thread_p, &elem_val, list_id);
      if (card2 == ER_FAILED)
	{
	  pr_clear_value (&elem_val);
	  return V_ERROR;
	}
      else if (card2 == UNKNOWN_CARD)
	{
	  pr_clear_value (&elem_val);
	  return V_UNKNOWN;
	}

      if (card1 > card2)
	{
	  pr_clear_value (&elem_val);
	  return V_FALSE;
	}
    }

  pr_clear_value (&elem_val);
  return res;
}

/*
 * eval_sub_sort_list_to_multi_set () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 * 	 list_id(in): Sorted LIST FILE identifier
 * 	 set(in): DB_SETrepresentation
 *
 * Note: Find if the given list file is a subset of the given multi_set
 *              The list file must be of one column and treated like a
 *              multi_set. The routine uses the same semantics of finding
 *              subset relationship between two multi_sets.
 *
 * Note: in a sorted list file of one column , ALL the NULL values, tuples
 *       appear at the beginning of the list file.
 */
static DB_LOGICAL
eval_sub_sort_list_to_multi_set (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, DB_SET * set)
{
  int card1, card2;
  DB_LOGICAL res, rc;
  DB_VALUE list_val, list_val2;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tplrec, p_tplrec;
  char *p_tplp;
  SCAN_CODE qp_scan;
  const PR_TYPE *pr_type;
  OR_BUF buf;
  int length;
  bool list_on;
  int tpl_len;
  char *ptr;

  /* assert */
  if (list_id->type_list.domp == NULL)
    {
      return V_ERROR;
    }

  PRIM_SET_NULL (&list_val);
  PRIM_SET_NULL (&list_val2);

  if (list_id->tuple_cnt == 0)
    {
      return V_TRUE;		/* empty set */
    }

  if (qfile_open_list_scan (list_id, &s_id) != NO_ERROR)
    {
      return V_ERROR;
    }

  res = V_TRUE;
  pr_type = list_id->type_list.domp[0]->type;

  tplrec.size = 0;
  tplrec.tpl = NULL;
  p_tplrec.size = DB_PAGESIZE;
  p_tplrec.tpl = (QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
  if (p_tplrec.tpl == NULL)
    {
      qfile_close_scan (thread_p, &s_id);
      return V_ERROR;
    }

  list_on = false;
  card1 = 0;
  while ((qp_scan = qfile_scan_list_next (thread_p, &s_id, &tplrec, PEEK)) == S_SUCCESS)
    {
      pr_clear_value (&list_val);

      if (qfile_locate_tuple_value (tplrec.tpl, 0, &ptr, &length) == V_UNBOUND)
	{
	  res = V_UNKNOWN;
	  goto end;
	}

      or_init (&buf, ptr, length);

      pr_type->data_readval (&buf, &list_val, list_id->type_list.domp[0], -1, true, NULL, 0);

      if (list_on == true)
	{
	  p_tplp = (char *) p_tplrec.tpl + QFILE_TUPLE_LENGTH_SIZE;

	  or_init (&buf, p_tplp + QFILE_TUPLE_VALUE_HEADER_SIZE, QFILE_GET_TUPLE_VALUE_LENGTH (p_tplp));

	  pr_type->data_readval (&buf, &list_val2, list_id->type_list.domp[0], -1, true, NULL, 0);

	  rc = eval_value_rel_cmp (thread_p, &list_val, &list_val2, R_EQ, NULL, NULL, &eval_Compare_elements, NULL);
	  if (rc == V_ERROR)
	    {
	      res = V_ERROR;
	      goto end;
	    }
	  else if (rc != V_TRUE)
	    {
	      card2 = eval_item_card_set (thread_p, &list_val2, set, R_EQ);
	      if (card2 == ER_FAILED)
		{
		  res = V_ERROR;
		  goto end;
		}
	      else if (card2 == UNKNOWN_CARD)
		{
		  res = V_UNKNOWN;
		  goto end;
		}

	      if (card1 > card2)
		{
		  res = V_FALSE;
		  goto end;
		}
	      card1 = 0;
	    }
	  pr_clear_value (&list_val2);
	}

      tpl_len = QFILE_GET_TUPLE_LENGTH (tplrec.tpl);
      if (p_tplrec.size < tpl_len)
	{
	  p_tplrec.size = tpl_len;
	  p_tplrec.tpl = (QFILE_TUPLE) db_private_realloc (thread_p, p_tplrec.tpl, tpl_len);
	  if (p_tplrec.tpl == NULL)
	    {
	      res = V_ERROR;
	      goto end;
	    }
	}
      memcpy (p_tplrec.tpl, tplrec.tpl, tpl_len);
      list_on = true;
      card1++;
    }

  if (qp_scan != S_END)
    {
      res = V_ERROR;
      goto end;
    }

  if (list_on == true)
    {
      p_tplp = (char *) p_tplrec.tpl + QFILE_TUPLE_LENGTH_SIZE;	/* no unbound value */

      or_init (&buf, p_tplp + QFILE_TUPLE_VALUE_HEADER_SIZE, QFILE_GET_TUPLE_VALUE_LENGTH (p_tplp));

      pr_type->data_readval (&buf, &list_val2, list_id->type_list.domp[0], -1, true, NULL, 0);

      card2 = eval_item_card_set (thread_p, &list_val2, set, R_EQ);
      if (card2 == ER_FAILED)
	{
	  res = V_ERROR;
	  goto end;
	}
      else if (card2 == UNKNOWN_CARD)
	{
	  res = V_UNKNOWN;
	  goto end;
	}
      else if (card1 > card2)
	{
	  res = V_FALSE;
	  goto end;
	}
    }

end:
  pr_clear_value (&list_val);
  pr_clear_value (&list_val2);
  qfile_close_scan (thread_p, &s_id);
  if (p_tplrec.tpl != NULL)
    {
      db_private_free_and_init (thread_p, p_tplrec.tpl);
    }
  return res;
}

/*
 * eval_sub_sort_list_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 * 	 list_id1(in): First Sorted LIST FILE identifier
 * 	 list_id2(in): Second Sorted LIST FILE identifier
 *
 * Note: Find if the first list file is a subset of the second list
 *              file. The list files must be of one column and treated like
 *              a multi_set. The routine uses the same semantics of finding
 *              subset relationship between two multi_sets.
 *
 * Note: in a sorted list file of one column , ALL the NULL values, tuples
 *       appear at the beginning of the list file.
 */
static DB_LOGICAL
eval_sub_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1, QFILE_LIST_ID * list_id2)
{
  int card1, card2;
  DB_LOGICAL res, rc;
  DB_VALUE list_val, list_val2;
  QFILE_LIST_SCAN_ID s_id;
  QFILE_TUPLE_RECORD tplrec, p_tplrec;
  char *p_tplp;
  SCAN_CODE qp_scan;
  const PR_TYPE *pr_type;
  OR_BUF buf;
  int length;
  bool list_on;
  int tpl_len;
  char *ptr;

  /* assert */
  if (list_id1->type_list.domp == NULL)
    {
      return V_ERROR;
    }

  PRIM_SET_NULL (&list_val);
  PRIM_SET_NULL (&list_val2);

  if (list_id1->tuple_cnt == 0)
    {
      return V_TRUE;		/* empty set */
    }

  if (qfile_open_list_scan (list_id1, &s_id) != NO_ERROR)
    {
      return V_ERROR;
    }

  res = V_TRUE;
  pr_type = list_id1->type_list.domp[0]->type;

  tplrec.size = 0;
  tplrec.tpl = NULL;
  p_tplrec.size = DB_PAGESIZE;
  p_tplrec.tpl = (QFILE_TUPLE) db_private_alloc (thread_p, DB_PAGESIZE);
  if (p_tplrec.tpl == NULL)
    {
      qfile_close_scan (thread_p, &s_id);
      return V_ERROR;
    }

  list_on = false;
  card1 = 0;
  while ((qp_scan = qfile_scan_list_next (thread_p, &s_id, &tplrec, PEEK)) == S_SUCCESS)
    {
      pr_clear_value (&list_val);

      if (qfile_locate_tuple_value (tplrec.tpl, 0, &ptr, &length) == V_UNBOUND)
	{
	  res = V_UNKNOWN;
	  goto end;
	}

      or_init (&buf, ptr, length);

      pr_type->data_readval (&buf, &list_val, list_id1->type_list.domp[0], -1, true, NULL, 0);

      if (list_on == true)
	{
	  p_tplp = (char *) p_tplrec.tpl + QFILE_TUPLE_LENGTH_SIZE;

	  or_init (&buf, p_tplp + QFILE_TUPLE_VALUE_HEADER_SIZE, QFILE_GET_TUPLE_VALUE_LENGTH (p_tplp));

	  pr_type->data_readval (&buf, &list_val2, list_id1->type_list.domp[0], -1, true, NULL, 0);

	  rc = eval_value_rel_cmp (thread_p, &list_val, &list_val2, R_EQ, NULL, NULL, &eval_Compare_elements, NULL);

	  if (rc == V_ERROR)
	    {
	      res = V_ERROR;
	      goto end;
	    }
	  else if (rc != V_TRUE)
	    {
	      card2 = eval_item_card_sort_list (thread_p, &list_val2, list_id2);
	      if (card2 == ER_FAILED)
		{
		  res = V_ERROR;
		  goto end;
		}
	      else if (card2 == UNKNOWN_CARD)
		{
		  res = V_UNKNOWN;
		  goto end;
		}

	      if (card1 > card2)
		{
		  res = V_FALSE;
		  goto end;
		}
	      card1 = 0;
	    }
	  pr_clear_value (&list_val2);
	}

      tpl_len = QFILE_GET_TUPLE_LENGTH (tplrec.tpl);
      if (p_tplrec.size < tpl_len)
	{
	  p_tplrec.size = tpl_len;
	  p_tplrec.tpl = (QFILE_TUPLE) db_private_realloc (thread_p, p_tplrec.tpl, tpl_len);
	  if (p_tplrec.tpl == NULL)
	    {
	      res = V_ERROR;
	      goto end;
	    }
	}
      memcpy (p_tplrec.tpl, tplrec.tpl, tpl_len);
      list_on = true;
      card1++;
    }

  if (qp_scan != S_END)
    {
      res = V_ERROR;
      goto end;
    }

  if (list_on == true)
    {
      p_tplp = (char *) p_tplrec.tpl + QFILE_TUPLE_LENGTH_SIZE;	/* no unbound value */

      or_init (&buf, p_tplp + QFILE_TUPLE_VALUE_HEADER_SIZE, QFILE_GET_TUPLE_VALUE_LENGTH (p_tplp));

      if (pr_type->data_readval (&buf, &list_val2, list_id1->type_list.domp[0], -1, true, NULL, 0) != NO_ERROR)
	{
	  res = V_ERROR;
	  goto end;
	}

      card2 = eval_item_card_sort_list (thread_p, &list_val2, list_id2);
      if (card2 == ER_FAILED)
	{
	  res = V_ERROR;
	  goto end;
	}
      else if (card2 == UNKNOWN_CARD)
	{
	  res = V_UNKNOWN;
	  goto end;
	}
      else if (card1 > card2)
	{
	  res = V_FALSE;
	  goto end;
	}
    }

end:
  pr_clear_value (&list_val);
  pr_clear_value (&list_val2);
  qfile_close_scan (thread_p, &s_id);
  if (p_tplrec.tpl != NULL)
    {
      db_private_free_and_init (thread_p, p_tplrec.tpl);
    }

  return res;
}

/*
 * eval_eq_multi_set_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   set(in): DB_SET representation
 *   list_id(in): Sorted LIST FILE identifier
 *
 * Note: Find if given multi_set is equal to the given list file.
 *              The routine uses the same semantics of finding equality
 *              relationship between two multi_sets.
 */
static DB_LOGICAL
eval_eq_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id)
{
  DB_LOGICAL res1, res2;

  res1 = eval_sub_multi_set_to_sort_list (thread_p, set, list_id);
  res2 = eval_sub_sort_list_to_multi_set (thread_p, list_id, set);

  return eval_logical_result (res1, res2);
}

/*
 * eval_ne_multi_set_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   set(in): DB_SET representation
 *   list_id(in): Sorted LIST FILE identifier
 *
 * Note: Find if given multi_set is not equal to the given list file.
 */
static DB_LOGICAL
eval_ne_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id)
{
  DB_LOGICAL res;

  res = eval_eq_multi_set_to_sort_list (thread_p, set, list_id);
  /* negate the result */
  return eval_negative (res);
}

/*
 * eval_le_multi_set_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   set(in): DB_SET representation
 *   list_id(in): Sorted LIST FILE identifier
 *
 * Note: Find if given multi_set is a subset of the given list file.
 */
static DB_LOGICAL
eval_le_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id)
{
  return eval_sub_multi_set_to_sort_list (thread_p, set, list_id);
}

/*
 * eval_lt_multi_set_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   set(in): DB_SET representation
 *   list_id(in): Sorted LIST FILE identifier
 *
 * Note: Find if given multi_set is a proper subset of the given list file.
 */
static DB_LOGICAL
eval_lt_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id)
{
  DB_LOGICAL res1, res2;

  res1 = eval_sub_multi_set_to_sort_list (thread_p, set, list_id);
  res2 = eval_ne_multi_set_to_sort_list (thread_p, set, list_id);

  return eval_logical_result (res1, res2);
}

/*
 * eval_le_sort_list_to_multi_set () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   list_id(in): Sorted LIST FILE identifier
 *   set(in): Multi_set disk representation
 *
 * Note: Find if given list file is a subset of the multi_set.
 */
static DB_LOGICAL
eval_le_sort_list_to_multi_set (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, DB_SET * set)
{
  return eval_sub_sort_list_to_multi_set (thread_p, list_id, set);
}

/*
 * eval_lt_sort_list_to_multi_set () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   list_id(in): Sorted LIST FILE identifier
 *   set(in): DB_SET representation
 *
 * Note: Find if given list file is a proper subset of the multi_set.
 */
static DB_LOGICAL
eval_lt_sort_list_to_multi_set (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, DB_SET * set)
{
  DB_LOGICAL res1, res2;

  res1 = eval_sub_sort_list_to_multi_set (thread_p, list_id, set);
  res2 = eval_ne_multi_set_to_sort_list (thread_p, set, list_id);

  return eval_logical_result (res1, res2);
}

/*
 * eval_eq_sort_list_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   list_id1(in): First Sorted LIST FILE identifier
 *   list_id2(in): Second Sorted LIST FILE identifier
 *
 * Note: Find if the first list file is equal to the second list file.
 *              The list files must be of one column and treated like
 *              multi_sets. The routine uses the same semantics of finding
 *              equality relationship between two multi_sets.
 */
static DB_LOGICAL
eval_eq_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1, QFILE_LIST_ID * list_id2)
{
  DB_LOGICAL res1, res2;

  res1 = eval_sub_sort_list_to_sort_list (thread_p, list_id1, list_id2);
  res2 = eval_sub_sort_list_to_sort_list (thread_p, list_id2, list_id1);

  return eval_logical_result (res1, res2);
}

/*
 * eval_ne_sort_list_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   list_id1(in): First Sorted LIST FILE identifier
 *   list_id2(in): Second Sorted LIST FILE identifier
 *
 * Note: Find if the first list file is not equal to the second one.
 */
static DB_LOGICAL
eval_ne_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1, QFILE_LIST_ID * list_id2)
{
  DB_LOGICAL res;

  res = eval_eq_sort_list_to_sort_list (thread_p, list_id1, list_id2);
  /* negate the result */
  return eval_negative (res);
}

/*
 * eval_le_sort_list_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   list_id1(in): First Sorted LIST FILE identifier
 *   list_id2(in): Second Sorted LIST FILE identifier
 *
 * Note: Find if the first list file is a subset if the second one.
 */
static DB_LOGICAL
eval_le_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1, QFILE_LIST_ID * list_id2)
{
  return eval_sub_sort_list_to_sort_list (thread_p, list_id1, list_id2);
}

/*
 * eval_lt_sort_list_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   list_id1(in): First Sorted LIST FILE identifier
 *   list_id2(in): Second Sorted LIST FILE identifier
 *
 * Note: Find if the first list file is a proper subset if the second
 *       list file.
 */
static DB_LOGICAL
eval_lt_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1, QFILE_LIST_ID * list_id2)
{
  DB_LOGICAL res1, res2;

  res1 = eval_sub_sort_list_to_sort_list (thread_p, list_id1, list_id2);
  res2 = eval_ne_sort_list_to_sort_list (thread_p, list_id1, list_id2);

  return eval_logical_result (res1, res2);
}

/*
 * eval_multi_set_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   set(in): DB_SET representation
 *   list_id(in): Sorted LIST FILE identifier
 *   rel_operator(in): Relational Operator
 *
 * Note: Find if given multi_set and the list file satisfy the
 *              relationship indicated by the relational operator. The list
 *              file must be of one column, sorted and is treated like a
 *              multi_set.
 */
static DB_LOGICAL
eval_multi_set_to_sort_list (THREAD_ENTRY * thread_p, DB_SET * set, QFILE_LIST_ID * list_id, REL_OP rel_operator)
{
  switch (rel_operator)
    {
    case R_LT:
      return eval_lt_multi_set_to_sort_list (thread_p, set, list_id);
    case R_LE:
      return eval_le_multi_set_to_sort_list (thread_p, set, list_id);
    case R_GT:
      return eval_lt_sort_list_to_multi_set (thread_p, list_id, set);
    case R_GE:
      return eval_le_sort_list_to_multi_set (thread_p, list_id, set);
    case R_EQ:
      return eval_eq_multi_set_to_sort_list (thread_p, set, list_id);
    case R_NE:
      return eval_ne_multi_set_to_sort_list (thread_p, set, list_id);
    default:
      return V_ERROR;
    }
}

/*
 * eval_sort_list_to_multi_set () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   list_id(in): Sorted LIST FILE identifier
 *   set(in): DB_SET representation
 *   rel_operator(in): Relational Operator
 *
 * Note: Find if given list file and the multi_set satisfy the
 *              relationship indicated by the relational operator. The list
 *              file must be of one column, sorted and is treated like a
 *              multi_set.
 */
static DB_LOGICAL
eval_sort_list_to_multi_set (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id, DB_SET * set, REL_OP rel_operator)
{
  switch (rel_operator)
    {
    case R_LT:
      return eval_lt_sort_list_to_multi_set (thread_p, list_id, set);
    case R_LE:
      return eval_le_sort_list_to_multi_set (thread_p, list_id, set);
    case R_GT:
      return eval_lt_multi_set_to_sort_list (thread_p, set, list_id);
    case R_GE:
      return eval_le_multi_set_to_sort_list (thread_p, set, list_id);
    case R_EQ:
      return eval_eq_multi_set_to_sort_list (thread_p, set, list_id);
    case R_NE:
      return eval_ne_multi_set_to_sort_list (thread_p, set, list_id);
    default:
      return V_ERROR;
    }
}

/*
 * eval_sort_list_to_sort_list () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   list_id1(in): First Sorted LIST FILE identifier
 *   list_id2(in): Second Sorted LIST FILE identifier
 *   rel_operator(in): Relational Operator
 *
 * Note: Find if first list file and the second list file satisfy the
 *              relationship indicated by the relational operator. The list
 *              files must be of one column, sorted and are treated like
 *              multi_sets.
 */
static DB_LOGICAL
eval_sort_list_to_sort_list (THREAD_ENTRY * thread_p, QFILE_LIST_ID * list_id1, QFILE_LIST_ID * list_id2,
			     REL_OP rel_operator)
{
  switch (rel_operator)
    {
    case R_LT:
      return eval_lt_sort_list_to_sort_list (thread_p, list_id1, list_id2);
    case R_LE:
      return eval_le_sort_list_to_sort_list (thread_p, list_id1, list_id2);
    case R_GT:
      return eval_lt_sort_list_to_sort_list (thread_p, list_id2, list_id1);
    case R_GE:
      return eval_le_sort_list_to_sort_list (thread_p, list_id2, list_id1);
    case R_EQ:
      return eval_eq_sort_list_to_sort_list (thread_p, list_id1, list_id2);
    case R_NE:
      return eval_ne_sort_list_to_sort_list (thread_p, list_id1, list_id2);
    default:
      return V_ERROR;
    }
}

/*
 * eval_set_list_cmp () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   et_comp(in): compound evaluation term
 *   vd(in):
 *   dbval1(in): lhs db_value, if lhs is a set.
 *   dbval2(in): rhs db_value, if rhs is a set.
 *
 * Note: Perform set/set, set/list, and list/list comparisons.
 */
static DB_LOGICAL
eval_set_list_cmp (THREAD_ENTRY * thread_p, const COMP_EVAL_TERM * et_comp, val_descr * vd, DB_VALUE * dbval1,
		   DB_VALUE * dbval2)
{
  QFILE_LIST_ID *t_list_id;
  QFILE_SORTED_LIST_ID *lhs_srlist_id, *rhs_srlist_id;

  if (et_comp->lhs->type == TYPE_LIST_ID)
    {
      /* execute linked query */
      EXECUTE_REGU_VARIABLE_XASL (thread_p, et_comp->lhs, vd);
      if (CHECK_REGU_VARIABLE_XASL_STATUS (et_comp->lhs) != XASL_SUCCESS)
	{
	  return V_ERROR;
	}

      /*
       * lhs value refers to a list file. for efficiency reasons
       * first sort the list file
       */
      lhs_srlist_id = et_comp->lhs->value.srlist_id;
      if (lhs_srlist_id->sorted == false)
	{
	  if (lhs_srlist_id->list_id->tuple_cnt > 1)
	    {
	      t_list_id = qfile_sort_list (thread_p, lhs_srlist_id->list_id, NULL, Q_ALL, true);
	      if (t_list_id == NULL)
		{
		  return V_ERROR;
		}
	    }
	  lhs_srlist_id->sorted = true;
	}

      /* rhs value can only be either a set or a list file */
      if (et_comp->rhs->type == TYPE_LIST_ID)
	{
	  /* execute linked query */
	  EXECUTE_REGU_VARIABLE_XASL (thread_p, et_comp->rhs, vd);
	  if (CHECK_REGU_VARIABLE_XASL_STATUS (et_comp->rhs) != XASL_SUCCESS)
	    {
	      return V_ERROR;
	    }

	  /*
	   * rhs value refers to a list file. for efficiency reasons
	   * first sort the list file
	   */
	  rhs_srlist_id = et_comp->rhs->value.srlist_id;
	  if (rhs_srlist_id->sorted == false)
	    {
	      if (rhs_srlist_id->list_id->tuple_cnt > 1)
		{
		  t_list_id = qfile_sort_list (thread_p, rhs_srlist_id->list_id, NULL, Q_ALL, true);
		  if (t_list_id == NULL)
		    {
		      return V_ERROR;
		    }
		}
	      rhs_srlist_id->sorted = true;
	    }

	  /* compare two list files */
	  return eval_sort_list_to_sort_list (thread_p, lhs_srlist_id->list_id, rhs_srlist_id->list_id,
					      et_comp->rel_op);
	}
      else
	{
	  /* compare list file and set */
	  return eval_sort_list_to_multi_set (thread_p, lhs_srlist_id->list_id, db_get_set (dbval2), et_comp->rel_op);
	}
    }
  else if (et_comp->rhs->type == TYPE_LIST_ID)
    {
      /* execute linked query */
      EXECUTE_REGU_VARIABLE_XASL (thread_p, et_comp->rhs, vd);
      if (CHECK_REGU_VARIABLE_XASL_STATUS (et_comp->rhs) != XASL_SUCCESS)
	{
	  return V_ERROR;
	}

      /*
       * rhs value refers to a list file. for efficiency reasons
       * first sort the list file
       */
      rhs_srlist_id = et_comp->rhs->value.srlist_id;
      if (rhs_srlist_id->sorted == false)
	{
	  if (rhs_srlist_id->list_id->tuple_cnt > 1)
	    {
	      t_list_id = qfile_sort_list (thread_p, rhs_srlist_id->list_id, NULL, Q_ALL, true);
	      if (t_list_id == NULL)
		{
		  return V_ERROR;
		}
	    }
	  rhs_srlist_id->sorted = true;
	}

      /* lhs must be a set value, compare set and list */
      return eval_multi_set_to_sort_list (thread_p, db_get_set (dbval1), rhs_srlist_id->list_id, et_comp->rel_op);
    }

  return V_UNKNOWN;
}

/*
 * Main Predicate Evaluation Routines
 */

/*
 * eval_pred () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   pr(in): Predicate Expression Tree
 *   vd(in): Value descriptor for positional values (optional)
 *   obj_oid(in): Object Identifier
 *
 * Note: This is the main predicate expression evalution routine. It
 *              evaluates the given predicate predicate expression on the
 *              specified evaluation item to see if the evaluation item
 *              satisfies the indicate predicate. It uses a 3-valued logic
 *              and returns V_TRUE, V_FALSE or V_UNKNOWN. If an error occurs,
 *              necessary error code is set and V_ERROR is returned.
 */
DB_LOGICAL
eval_pred (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid)
{
  const COMP_EVAL_TERM *et_comp;
  const ALSM_EVAL_TERM *et_alsm;
  const LIKE_EVAL_TERM *et_like;
  DB_VALUE *peek_val1, *peek_val2, *peek_val3;
  DB_LOGICAL result = V_UNKNOWN;
  int regexp_res;
  const PRED_EXPR *t_pr;
  QFILE_SORTED_LIST_ID *srlist_id;
  static int max_recursion_sql_depth = prm_get_integer_value (PRM_ID_MAX_RECURSION_SQL_DEPTH);

  peek_val1 = NULL;
  peek_val2 = NULL;
  peek_val3 = NULL;

  if (thread_get_recursion_depth (thread_p) > max_recursion_sql_depth)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_MAX_RECURSION_SQL_DEPTH, 1, max_recursion_sql_depth);

      return V_ERROR;
    }

  thread_inc_recursion_depth (thread_p);

  switch (pr->type)
    {
    case T_PRED:
      switch (pr->pe.m_pred.bool_op)
	{
	case B_AND:
	  /* 'pt_to_pred_expr()' will generate right-linear tree */
	  result = V_TRUE;
	  for (t_pr = pr; result == V_TRUE && t_pr->type == T_PRED && t_pr->pe.m_pred.bool_op == B_AND;
	       t_pr = t_pr->pe.m_pred.rhs)
	    {
	      if (result == V_UNKNOWN)
		{
		  result = eval_pred (thread_p, t_pr->pe.m_pred.lhs, vd, obj_oid);
		  result = (result == V_TRUE) ? V_UNKNOWN : result;
		}
	      else
		{
		  result = eval_pred (thread_p, t_pr->pe.m_pred.lhs, vd, obj_oid);
		}

	      if (result == V_FALSE || result == V_ERROR)
		{
		  goto exit;
		}
	    }

	  if (result == V_UNKNOWN)
	    {
	      result = eval_pred (thread_p, t_pr, vd, obj_oid);
	      result = (result == V_TRUE) ? V_UNKNOWN : result;
	    }
	  else
	    {
	      result = eval_pred (thread_p, t_pr, vd, obj_oid);
	    }
	  break;

	case B_OR:
	  /* 'pt_to_pred_expr()' will generate right-linear tree */
	  result = V_FALSE;
	  for (t_pr = pr; result == V_FALSE && t_pr->type == T_PRED && t_pr->pe.m_pred.bool_op == B_OR;
	       t_pr = t_pr->pe.m_pred.rhs)
	    {
	      if (result == V_UNKNOWN)
		{
		  result = eval_pred (thread_p, t_pr->pe.m_pred.lhs, vd, obj_oid);
		  result = (result == V_FALSE) ? V_UNKNOWN : result;
		}
	      else
		{
		  result = eval_pred (thread_p, t_pr->pe.m_pred.lhs, vd, obj_oid);
		}

	      if (result == V_TRUE || result == V_ERROR)
		{
		  goto exit;
		}
	    }

	  if (result == V_UNKNOWN)
	    {
	      result = eval_pred (thread_p, t_pr, vd, obj_oid);
	      result = (result == V_FALSE) ? V_UNKNOWN : result;
	    }
	  else
	    {
	      result = eval_pred (thread_p, t_pr, vd, obj_oid);
	    }
	  break;

	case B_XOR:
	  {
	    DB_LOGICAL result_lhs, result_rhs;

	    result_lhs = eval_pred (thread_p, pr->pe.m_pred.lhs, vd, obj_oid);
	    result_rhs = eval_pred (thread_p, pr->pe.m_pred.rhs, vd, obj_oid);

	    if (result_lhs == V_ERROR || result_rhs == V_ERROR)
	      {
		result = V_ERROR;
	      }
	    else if (result_lhs == V_UNKNOWN || result_rhs == V_UNKNOWN)
	      {
		result = V_UNKNOWN;
	      }
	    else if (result_lhs == result_rhs)
	      {
		result = V_FALSE;
	      }
	    else
	      {
		result = V_TRUE;
	      }
	  }
	  break;

	case B_IS:
	case B_IS_NOT:
	  {
	    DB_LOGICAL result_lhs, result_rhs;

	    result_lhs = eval_pred (thread_p, pr->pe.m_pred.lhs, vd, obj_oid);
	    result_rhs = eval_pred (thread_p, pr->pe.m_pred.rhs, vd, obj_oid);

	    if (result_lhs == V_ERROR || result_rhs == V_ERROR)
	      {
		result = V_ERROR;
	      }
	    else if (result_lhs == result_rhs)
	      {
		result = (pr->pe.m_pred.bool_op == B_IS) ? V_TRUE : V_FALSE;
	      }
	    else
	      {
		result = (pr->pe.m_pred.bool_op == B_IS) ? V_FALSE : V_TRUE;
	      }
	  }
	  break;

	default:
	  result = V_ERROR;
	  break;
	}
      break;

    case T_EVAL_TERM:
      switch (pr->pe.m_eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  /*
	   * compound evaluation terms are used to test relationships
	   * such as equality, greater than etc. between two items
	   * Each datatype defines its own meaning of relationship
	   * indicated by one of the relational operators.
	   */
	  et_comp = &pr->pe.m_eval_term.et.et_comp;

	  /* evaluate NULL predicate, if specified */
	  if (et_comp->rel_op == R_NULL)
	    {
	      result = eval_pred_comp1 (thread_p, pr, vd, obj_oid);
	      if (result == V_ERROR)
		{
		  goto exit;
		}
	      break;
	    }

	  /* evaluate EXISTS predicate, if specified */
	  if (et_comp->rel_op == R_EXISTS)
	    {
	      /* leaf node should refer to either a set or list file */
	      if (et_comp->lhs->type == TYPE_LIST_ID)
		{
		  /* execute linked query */
		  REGU_VARIABLE *regu_var = et_comp->lhs;
		  XASL_NODE *xasl = regu_var->xasl;
		  if (xasl && XASL_IS_FLAGED (xasl, XASL_USES_SQ_CACHE)
		      && !(SQ_CACHE_HT (xasl) && !SQ_CACHE_ENABLED (xasl)))
		    {
		      if (!sq_get (thread_p, SQ_CACHE_KEY_STRUCT (xasl), xasl, regu_var))
			{
			  SQ_KEY *key;
			  /* execute linked query */
			  EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, vd);
			  if (CHECK_REGU_VARIABLE_XASL_STATUS (regu_var) != XASL_SUCCESS)
			    {
			      result = V_ERROR;
			      goto exit;
			    }
			  if ((key = sq_make_key (thread_p, xasl)) == NULL)
			    {
			      XASL_CLEAR_FLAG (xasl, XASL_USES_SQ_CACHE);
			      result = V_ERROR;
			      goto exit;
			    }
			  if (sq_put (thread_p, key, xasl, regu_var) == ER_FAILED)
			    {
			      sq_free_key (thread_p, key);
			    }
			}
		      else
			{
			  /* FOUND */
			  xasl->status = XASL_SUCCESS;
			}
		    }
		  else
		    {
		      EXECUTE_REGU_VARIABLE_XASL (thread_p, regu_var, vd);
		      if (CHECK_REGU_VARIABLE_XASL_STATUS (regu_var) != XASL_SUCCESS)
			{
			  result = V_ERROR;
			  goto exit;
			}
		    }

		  srlist_id = et_comp->lhs->value.srlist_id;
		  result = ((srlist_id->list_id->tuple_cnt > 0) ? V_TRUE : V_FALSE);
		}
	      else
		{
		  /* must be a set */
		  if (fetch_peek_dbval (thread_p, et_comp->lhs, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
		    {
		      result = V_ERROR;
		      goto exit;
		    }
		  else if (db_value_is_null (peek_val1))
		    {
		      result = V_UNKNOWN;
		      goto exit;
		    }
		  else if (!TP_IS_SET_TYPE (DB_VALUE_DOMAIN_TYPE (peek_val1)))
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
		      result = V_ERROR;
		      goto exit;
		    }

		  result = ((db_set_size (db_get_set (peek_val1)) > 0) ? V_TRUE : V_FALSE);
		}
	      break;
	    }

	  /*
	   * fetch left hand size and right hand size values, if one of
	   * values are unbound, result = V_UNKNOWN
	   */
	  if (et_comp->lhs->type != TYPE_LIST_ID)
	    {
	      if (fetch_peek_dbval (thread_p, et_comp->lhs, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
		{
		  result = V_ERROR;
		  goto exit;
		}
	      else if (db_value_is_null (peek_val1))
		{
		  if (et_comp->rel_op != R_EQ_TORDER && et_comp->rel_op != R_NULLSAFE_EQ)
		    {
		      result = V_UNKNOWN;
		      goto exit;
		    }
		}
	    }

	  if (et_comp->rhs->type != TYPE_LIST_ID)
	    {
	      if (fetch_peek_dbval (thread_p, et_comp->rhs, vd, NULL, obj_oid, NULL, &peek_val2) != NO_ERROR)
		{
		  result = V_ERROR;
		  goto exit;
		}
	      else if (db_value_is_null (peek_val2))
		{
		  if (et_comp->rel_op != R_EQ_TORDER && et_comp->rel_op != R_NULLSAFE_EQ)
		    {
		      result = V_UNKNOWN;
		      goto exit;
		    }
		}
	    }

	  if (et_comp->lhs->type == TYPE_LIST_ID || et_comp->rhs->type == TYPE_LIST_ID)
	    {
	      result = eval_set_list_cmp (thread_p, et_comp, vd, peek_val1, peek_val2);
	    }
	  else
	    {
	      /*
	       * general case: compare values, db_value_compare will
	       * take care of any coercion necessary.
	       */
	      result = eval_value_rel_cmp (thread_p, peek_val1, peek_val2, et_comp->rel_op, et_comp, vd, NULL, NULL);
	    }
	  break;

	case T_ALSM_EVAL_TERM:
	  {
	    DB_TYPE rhs_type = DB_TYPE_UNKNOWN;
	    bool rhs_is_set = false;

	    et_alsm = &pr->pe.m_eval_term.et.et_alsm;

	    /*
	     * Note: According to ANSI, if the set or list file is empty,
	     * the result of comparison is true/false for ALL/SOME,
	     * regardless of whether lhs value is bound or not.
	     */
	    if (et_alsm->elemset->type != TYPE_LIST_ID)
	      {
		/* fetch rhs value */
		if (fetch_peek_dbval (thread_p, et_alsm->elemset, vd, NULL, obj_oid, NULL, &peek_val2) != NO_ERROR)
		  {
		    result = V_ERROR;
		    goto exit;
		  }
		else if (db_value_is_null (peek_val2))
		  {
		    result = V_UNKNOWN;
		    goto exit;
		  }

		rhs_type = DB_VALUE_TYPE (peek_val2);
		rhs_is_set = TP_IS_SET_TYPE (rhs_type);
		if (rhs_is_set && set_size (db_get_set (peek_val2)) == 0)
		  {
		    /* empty set */
		    result = (et_alsm->eq_flag == F_ALL) ? V_TRUE : V_FALSE;
		    goto exit;
		  }
	      }
	    else
	      {
		/* execute linked query */
		EXECUTE_REGU_VARIABLE_XASL (thread_p, et_alsm->elemset, vd);
		if (CHECK_REGU_VARIABLE_XASL_STATUS (et_alsm->elemset) != XASL_SUCCESS)
		  {
		    result = V_ERROR;
		    goto exit;
		  }
		else
		  {
		    /* check of empty list file */
		    srlist_id = et_alsm->elemset->value.srlist_id;
		    if (srlist_id->list_id->tuple_cnt == 0)
		      {
			result = (et_alsm->eq_flag == F_ALL) ? V_TRUE : V_FALSE;
			goto exit;
		      }
		  }
	      }

	    /* fetch lhs value */
	    if (fetch_peek_dbval (thread_p, et_alsm->elem, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
	      {
		result = V_ERROR;
		goto exit;
	      }
	    else if (db_value_is_null (peek_val1))
	      {
		result = V_UNKNOWN;
		goto exit;
	      }

	    EVAL_ELEMENTS elements;
	    eval_planned_elements (et_alsm, vd, &elements);
	    if (et_alsm->elemset->type == TYPE_LIST_ID)
	      {
		/* rhs value is a list, use list evaluation routines */
		srlist_id = et_alsm->elemset->value.srlist_id;
		if (et_alsm->eq_flag == F_ALL)
		  {
		    result = eval_all_list_eval (thread_p, peek_val1, srlist_id->list_id, et_alsm->rel_op,
						 elements.all, vd);
		  }
		else
		  {
		    result = eval_some_list_eval (thread_p, peek_val1, srlist_id->list_id, et_alsm->rel_op,
						  elements.all, vd);
		  }
	      }
	    else if (rhs_is_set)
	      {
		/* rhs value is a set, use set evaluation routines */
		if (et_alsm->eq_flag == F_ALL)
		  {
		    result =
		      eval_all_eval (thread_p, peek_val1, db_get_set (peek_val2), et_alsm->rel_op, &elements, vd);
		  }
		else
		  {
		    result =
		      eval_some_eval (thread_p, peek_val1, db_get_set (peek_val2), et_alsm->rel_op, &elements, vd);
		  }
	      }
	    else if (elements.constant != NULL)
	      {
		/* a constant that is no collection: its one element, the gate's own value */
		assert (elements.constant->n == 1);
		result =
		  eval_value_rel_cmp (thread_p, peek_val1, &elements.constant->value[0], et_alsm->rel_op, NULL, vd,
				      &elements.constant->compares[elements.constant->decision[0]], peek_val2);
	      }
	    else
	      {
		/* other cases, use general evaluation routines */
		result = eval_value_rel_cmp (thread_p, peek_val1, peek_val2, et_alsm->rel_op, NULL, vd, elements.all,
					     NULL);
	      }
	  }
	  break;

	case T_LIKE_EVAL_TERM:
	  et_like = &pr->pe.m_eval_term.et.et_like;

	  /* fetch source text expression */
	  if (fetch_peek_dbval (thread_p, et_like->src, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
	    {
	      result = V_ERROR;
	      goto exit;
	    }
	  else if (db_value_is_null (peek_val1))
	    {
	      result = V_UNKNOWN;
	      goto exit;
	    }

	  /* fetch pattern regular expression */
	  if (fetch_peek_dbval (thread_p, et_like->pattern, vd, NULL, obj_oid, NULL, &peek_val2) != NO_ERROR)
	    {
	      result = V_ERROR;
	      goto exit;
	    }
	  else if (db_value_is_null (peek_val2))
	    {
	      result = V_UNKNOWN;
	      goto exit;
	    }

	  if (et_like->esc_char)
	    {
	      /* fetch escape regular expression */
	      if (fetch_peek_dbval (thread_p, et_like->esc_char, vd, NULL, obj_oid, NULL, &peek_val3) != NO_ERROR)
		{
		  result = V_ERROR;
		  goto exit;
		}
	    }
	  /* evaluate regular expression match */
	  /* Note: Currently only STRING type is supported */
	  db_string_like (peek_val1, peek_val2, peek_val3, &regexp_res);
	  result = (DB_LOGICAL) regexp_res;
	  break;

	case T_RLIKE_EVAL_TERM:
	  /* evaluate rlike */
	  result = eval_pred_rlike7 (thread_p, pr, vd, obj_oid);
	  break;

	default:
	  result = V_ERROR;
	  break;
	}
      break;

    case T_NOT_TERM:
      result = eval_pred (thread_p, pr->pe.m_not_term, vd, obj_oid);
      /* negate the result */
      result = eval_negative (result);
      break;

    default:
      result = V_ERROR;
    }

exit:

  thread_dec_recursion_depth (thread_p);

  return result;
}

/*
 * eval_pred_comp0 () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   pr(in): Predicate Expression Tree
 *   vd(in): Value descriptor for positional values (optional)
 *   obj_oid(in): Object Identifier
 *
 * Note: single node regular comparison predicate
 */
DB_LOGICAL
eval_pred_comp0 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid)
{
  const COMP_EVAL_TERM *et_comp;
  DB_VALUE *peek_val1, *peek_val2;

  peek_val1 = NULL;
  peek_val2 = NULL;

  et_comp = &pr->pe.m_eval_term.et.et_comp;

  /*
   * fetch left hand size and right hand size values, if one of
   * values are unbound, return V_UNKNOWN
   */
  if (fetch_peek_dbval (thread_p, et_comp->lhs, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val1) && et_comp->rel_op != R_NULLSAFE_EQ)
    {
      return V_UNKNOWN;
    }

  if (fetch_peek_dbval (thread_p, et_comp->rhs, vd, NULL, obj_oid, NULL, &peek_val2) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val2) && et_comp->rel_op != R_NULLSAFE_EQ)
    {
      return V_UNKNOWN;
    }

  /*
   * general case: compare values, db_value_compare will
   * take care of any coercion necessary.
   */
  return eval_value_rel_cmp (thread_p, peek_val1, peek_val2, et_comp->rel_op, et_comp, vd, NULL, NULL);
}

/*
 * eval_pred_comp1 () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   pr(in): Predicate Expression Tree
 *   vd(in): Value descriptor for positional values (optional)
 *   obj_oid(in): Object Identifier
 *
 * Note: single leaf node NULL predicate
 */
DB_LOGICAL
eval_pred_comp1 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid)
{
  const COMP_EVAL_TERM *et_comp;
  DB_VALUE *peek_val1;

  peek_val1 = NULL;

  et_comp = &pr->pe.m_eval_term.et.et_comp;

  if (fetch_peek_dbval (thread_p, et_comp->lhs, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val1))
    {
      return V_TRUE;
    }

  if (DB_VALUE_DOMAIN_TYPE (peek_val1) == DB_TYPE_OID
      && !heap_is_object_not_null (thread_p, (OID *) NULL, db_get_oid (peek_val1)))
    {
      return V_TRUE;
    }
  else
    {
      return V_FALSE;
    }
}

/*
 * eval_pred_comp2 () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   pr(in): Predicate Expression Tree
 *   vd(in): Value descriptor for positional values (optional)
 *   obj_oid(in): Object Identifier
 *
 * Note: single node EXIST predicate
 */
DB_LOGICAL
eval_pred_comp2 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid)
{
  const COMP_EVAL_TERM *et_comp;
  DB_VALUE *peek_val1;

  peek_val1 = NULL;

  et_comp = &pr->pe.m_eval_term.et.et_comp;

  /* evaluate EXISTS predicate, if specified */
  /* leaf node should refer to either a set or list file */
  if (et_comp->lhs->type == TYPE_LIST_ID)
    {
      /* execute linked query */
      EXECUTE_REGU_VARIABLE_XASL (thread_p, et_comp->lhs, vd);
      if (CHECK_REGU_VARIABLE_XASL_STATUS (et_comp->lhs) != XASL_SUCCESS)
	{
	  return V_ERROR;
	}
      else
	{
	  QFILE_SORTED_LIST_ID *srlist_id;

	  srlist_id = et_comp->lhs->value.srlist_id;
	  return (srlist_id->list_id->tuple_cnt > 0) ? V_TRUE : V_FALSE;
	}
    }
  else
    {
      if (fetch_peek_dbval (thread_p, et_comp->lhs, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
	{
	  return V_ERROR;
	}
      else if (db_value_is_null (peek_val1))
	{
	  return V_UNKNOWN;
	}
      else if (!TP_IS_SET_TYPE (DB_VALUE_DOMAIN_TYPE (peek_val1)))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
	  return V_ERROR;
	}

      return (set_size (db_get_set (peek_val1)) > 0) ? V_TRUE : V_FALSE;
    }
}

/*
 * eval_pred_comp3 () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   pr(in): Predicate Expression Tree
 *   vd(in): Value descriptor for positional values (optional)
 *   obj_oid(in): Object Identifier
 *
 * Note: single node lhs or rhs list file predicate
 */
DB_LOGICAL
eval_pred_comp3 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid)
{
  const COMP_EVAL_TERM *et_comp;
  DB_VALUE *peek_val1, *peek_val2;

  peek_val1 = NULL;
  peek_val2 = NULL;

  et_comp = &pr->pe.m_eval_term.et.et_comp;

  /*
   * fetch left hand size and right hand size values, if one of
   * values are unbound, result = V_UNKNOWN
   */
  if (et_comp->lhs->type != TYPE_LIST_ID)
    {
      if (fetch_peek_dbval (thread_p, et_comp->lhs, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
	{
	  return V_ERROR;
	}
      else if (db_value_is_null (peek_val1))
	{
	  return V_UNKNOWN;
	}
    }

  if (et_comp->rhs->type != TYPE_LIST_ID)
    {
      if (fetch_peek_dbval (thread_p, et_comp->rhs, vd, NULL, obj_oid, NULL, &peek_val2) != NO_ERROR)
	{
	  return V_ERROR;
	}
      else if (db_value_is_null (peek_val2))
	{
	  return V_UNKNOWN;
	}
    }

  if (et_comp->lhs->type == TYPE_LIST_ID || et_comp->rhs->type == TYPE_LIST_ID)
    {
      return eval_set_list_cmp (thread_p, et_comp, vd, peek_val1, peek_val2);
    }
  else
    {
      return V_UNKNOWN;
    }
}

/*
 * eval_pred_alsm4 () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   pr(in): Predicate Expression Tree
 *   vd(in): Value descriptor for positional values (optional)
 *   obj_oid(in): Object Identifier
 *
 * Note: single node all/some predicate with a set
 */
DB_LOGICAL
eval_pred_alsm4 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid)
{
  const ALSM_EVAL_TERM *et_alsm;
  DB_VALUE *peek_val1, *peek_val2;

  peek_val1 = NULL;
  peek_val2 = NULL;

  et_alsm = &pr->pe.m_eval_term.et.et_alsm;

  /*
   * Note: According to ANSI, if the set or list file is empty,
   *       the result of comparison is true/false for ALL/SOME,
   *       regardles of whether lhs value is bound or not.
   */
  if (fetch_peek_dbval (thread_p, et_alsm->elemset, vd, NULL, obj_oid, NULL, &peek_val2) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val2))
    {
      return V_UNKNOWN;
    }
  else if (!TP_IS_SET_TYPE (DB_VALUE_DOMAIN_TYPE (peek_val2)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return V_ERROR;
    }

  if (set_size (db_get_set (peek_val2)) == 0)
    {
      /* empty set */
      return ((et_alsm->eq_flag == F_ALL) ? V_TRUE : V_FALSE);
    }

  /* fetch item value */
  if (fetch_peek_dbval (thread_p, et_alsm->elem, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val1))
    {
      return V_UNKNOWN;
    }

  /* rhs value is a set, use set evaluation routines */
  EVAL_ELEMENTS elements;
  eval_planned_elements (et_alsm, vd, &elements);
  if (et_alsm->eq_flag == F_ALL)
    {
      return eval_all_eval (thread_p, peek_val1, db_get_set (peek_val2), et_alsm->rel_op, &elements, vd);
    }
  else
    {
      return eval_some_eval (thread_p, peek_val1, db_get_set (peek_val2), et_alsm->rel_op, &elements, vd);
    }
}

/*
 * eval_pred_alsm5 () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   pr(in): Predicate Expression Tree
 *   vd(in): Value descriptor for positional values (optional)
 *   obj_oid(in): Object Identifier
 *
 * Note: single node all/some  predicate with a list file
 */
DB_LOGICAL
eval_pred_alsm5 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid)
{
  const ALSM_EVAL_TERM *et_alsm;
  DB_VALUE *peek_val1;
  QFILE_SORTED_LIST_ID *srlist_id;

  peek_val1 = NULL;

  et_alsm = &pr->pe.m_eval_term.et.et_alsm;

  /* execute linked query */
  EXECUTE_REGU_VARIABLE_XASL (thread_p, et_alsm->elemset, vd);
  if (CHECK_REGU_VARIABLE_XASL_STATUS (et_alsm->elemset) != XASL_SUCCESS)
    {
      return V_ERROR;
    }

  /*
   * Note: According to ANSI, if the set or list file is empty,
   *       the result of comparison is true/false for ALL/SOME,
   *       regardless of whether lhs value is bound or not.
   */
  srlist_id = et_alsm->elemset->value.srlist_id;
  if (srlist_id->list_id->tuple_cnt == 0)
    {
      return (et_alsm->eq_flag == F_ALL) ? V_TRUE : V_FALSE;
    }

  /* fetch item value */
  if (fetch_peek_dbval (thread_p, et_alsm->elem, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val1))
    {
      return V_UNKNOWN;
    }

  EVAL_ELEMENTS elements;
  eval_planned_elements (et_alsm, vd, &elements);
  if (et_alsm->eq_flag == F_ALL)
    {
      return eval_all_list_eval (thread_p, peek_val1, srlist_id->list_id, et_alsm->rel_op, elements.all, vd);
    }
  else
    {
      return eval_some_list_eval (thread_p, peek_val1, srlist_id->list_id, et_alsm->rel_op, elements.all, vd);
    }
}

/*
 * eval_pred_like6 () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   pr(in): Predicate Expression Tree
 *   vd(in): Value descriptor for positional values (optional)
 *   obj_oid(in): Object Identifier
 *
 * Note: single node like predicate
 */
DB_LOGICAL
eval_pred_like6 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid)
{
  const LIKE_EVAL_TERM *et_like;
  DB_VALUE *peek_val1, *peek_val2, *peek_val3;
  int regexp_res;

  peek_val1 = NULL;
  peek_val2 = NULL;
  peek_val3 = NULL;

  et_like = &pr->pe.m_eval_term.et.et_like;

  /* fetch source text expression */
  if (fetch_peek_dbval (thread_p, et_like->src, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val1))
    {
      return V_UNKNOWN;
    }

  /* fetch pattern regular expression */
  if (fetch_peek_dbval (thread_p, et_like->pattern, vd, NULL, obj_oid, NULL, &peek_val2) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val2))
    {
      return V_UNKNOWN;
    }

  if (et_like->esc_char)
    {
      /* fetch escape regular expression */
      if (fetch_peek_dbval (thread_p, et_like->esc_char, vd, NULL, obj_oid, NULL, &peek_val3) != NO_ERROR)
	{
	  return V_ERROR;
	}
    }

  /* evaluate regular expression match */
  /* Note: Currently only STRING type is supported */
  db_string_like (peek_val1, peek_val2, peek_val3, &regexp_res);

  return (DB_LOGICAL) regexp_res;
}

/*
 * eval_pred_rlike7 () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 *   pr(in): Predicate Expression Tree
 *   vd(in): Value descriptor for positional values (optional)
 *   obj_oid(in): Object Identifier
 *
 * Note: single node like predicate
 */
DB_LOGICAL
eval_pred_rlike7 (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, val_descr * vd, OID * obj_oid)
{
  const RLIKE_EVAL_TERM *et_rlike;
  DB_VALUE *peek_val1, *peek_val2, *peek_val3;
  int regexp_res;

  peek_val1 = NULL;
  peek_val2 = NULL;
  peek_val3 = NULL;

  et_rlike = &pr->pe.m_eval_term.et.et_rlike;

  /* fetch source text expression */
  if (fetch_peek_dbval (thread_p, et_rlike->src, vd, NULL, obj_oid, NULL, &peek_val1) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val1))
    {
      return V_UNKNOWN;
    }

  /* fetch pattern */
  if (fetch_peek_dbval (thread_p, et_rlike->pattern, vd, NULL, obj_oid, NULL, &peek_val2) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val2))
    {
      return V_UNKNOWN;
    }

  /* fetch case sensitiveness */
  if (fetch_peek_dbval (thread_p, et_rlike->case_sensitive, vd, NULL, obj_oid, NULL, &peek_val3) != NO_ERROR)
    {
      return V_ERROR;
    }
  else if (db_value_is_null (peek_val3))
    {
      return V_UNKNOWN;
    }

  /* evaluate regular expression match */
  db_string_rlike (peek_val1, peek_val2, peek_val3, &et_rlike->compiled_regex, &regexp_res);

  return (DB_LOGICAL) regexp_res;
}

/*
 * eval_fnc () -
 *   return:
 *   pr(in): Predicate Expression Tree
 *   single_node_type(in):
 */
PR_EVAL_FNC
eval_fnc (THREAD_ENTRY * thread_p, const PRED_EXPR * pr, DB_TYPE * single_node_type)
{
  // todo - thread_p is never used

  const COMP_EVAL_TERM *et_comp;
  const ALSM_EVAL_TERM *et_alsm;

  *single_node_type = DB_TYPE_NULL;
  if (pr == NULL)
    {
      return NULL;
    }

  if (pr->type == T_EVAL_TERM)
    {
      switch (pr->pe.m_eval_term.et_type)
	{
	case T_COMP_EVAL_TERM:
	  et_comp = &pr->pe.m_eval_term.et.et_comp;

	  /*
	   * et_comp->type can be DB_TYPE_NULL,
	   * in the case of positional variables
	   */
	  *single_node_type = et_comp->type;

	  if (et_comp->rel_op == R_NULL)
	    {
	      return (PR_EVAL_FNC) eval_pred_comp1;
	    }
	  else if (et_comp->rel_op == R_EXISTS)
	    {
	      return (PR_EVAL_FNC) eval_pred_comp2;
	    }

	  if (et_comp->lhs->type == TYPE_LIST_ID || et_comp->rhs->type == TYPE_LIST_ID)
	    {
	      return (PR_EVAL_FNC) eval_pred_comp3;
	    }

	  return (PR_EVAL_FNC) eval_pred_comp0;

	case T_ALSM_EVAL_TERM:
	  et_alsm = &pr->pe.m_eval_term.et.et_alsm;

	  /*
	   * et_alsm->item_type can be DB_TYPE_NULL,
	   * in the case of positional variables
	   */
	  *single_node_type = et_alsm->item_type;

	  return ((et_alsm->elemset->type != TYPE_LIST_ID)
		  ? (PR_EVAL_FNC) eval_pred_alsm4 : (PR_EVAL_FNC) eval_pred_alsm5);

	case T_LIKE_EVAL_TERM:
	  return (PR_EVAL_FNC) eval_pred_like6;

	case T_RLIKE_EVAL_TERM:
	  return (PR_EVAL_FNC) eval_pred_rlike7;

	default:
	  return NULL;
	}
    }

  /* general case */
  return (PR_EVAL_FNC) eval_pred;
}

/*
 * update_logical_result () - checks DB_LOGICAL value and qualification
 *   return: new DB_LOGICAL value and qualification (if needed)
 *   thread_p(in):
 *   ev_res(in): logical value to be checked
 *   qualification(in/out): a pointer to the qualification to be used in logical
 *			    value check. This member can be modified.
 */
DB_LOGICAL
update_logical_result (THREAD_ENTRY * thread_p, DB_LOGICAL ev_res, int *qualification)
{
  int q;

  if (ev_res == V_ERROR)
    {
      return ev_res;
    }

  if (qualification != NULL)
    {
      q = *qualification;
      if (q == QPROC_QUALIFIED)
	{
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      return V_FALSE;	/* not qualified, continue to the next tuple */
	    }
	}
      else if (q == QPROC_NOT_QUALIFIED)
	{
	  if (ev_res != V_FALSE)	/* V_TRUE || V_UNKNOWN */
	    {
	      return V_FALSE;	/* qualified, continue to the next tuple */
	    }
	}
      else if (q == QPROC_QUALIFIED_OR_NOT)
	{
	  if (ev_res == V_TRUE)
	    {
	      *qualification = QPROC_QUALIFIED;
	    }
	  else if (ev_res == V_FALSE)
	    {
	      *qualification = QPROC_NOT_QUALIFIED;
	    }
	  else			/* V_UNKNOWN */
	    {
	      /* nop */
	      ;
	    }
	}
      else
	{			/* invalid value; the same as QPROC_QUALIFIED */
	  if (ev_res != V_TRUE)	/* V_FALSE || V_UNKNOWN */
	    {
	      return V_FALSE;	/* not qualified, continue to the next tuple */
	    }
	}
    }

  assert (ev_res != V_ERROR);
  if (ev_res == V_TRUE)
    {
      return V_TRUE;
    }
  else
    {
      /* V_FALSE || V_UNKNOWN */
      return V_FALSE;		/* not qualified, continue to the next tuple */
    }
}

/*
 * eval_mark_lazy_always_eager_regu () - if regu (recursively, through arithmetic / function operands)
 *   references an attribute of attr_cache, flag its value slot to be read eagerly even in lazy mode.
 */
static void
eval_mark_lazy_always_eager_regu (const REGU_VARIABLE * regu, HEAP_CACHE_ATTRINFO * attr_cache)
{
  REGU_VARIABLE_LIST operand;
  int i;

  if (regu == NULL)
    {
      return;
    }

  switch (regu->type)
    {
    case TYPE_ATTR_ID:
    case TYPE_SHARED_ATTR_ID:
    case TYPE_CLASS_ATTR_ID:
      if (regu->value.attr_descr.cache_attrinfo == attr_cache)
	{
	  for (i = 0; i < attr_cache->num_values; i++)
	    {
	      if (attr_cache->values[i].attrid == regu->value.attr_descr.id)
		{
		  attr_cache->values[i].lazy_always_eager = true;
		  break;
		}
	    }
	}
      break;

    case TYPE_INARITH:
    case TYPE_OUTARITH:
      if (regu->value.arithptr != NULL)
	{
	  eval_mark_lazy_always_eager_regu (regu->value.arithptr->leftptr, attr_cache);
	  eval_mark_lazy_always_eager_regu (regu->value.arithptr->rightptr, attr_cache);
	  eval_mark_lazy_always_eager_regu (regu->value.arithptr->thirdptr, attr_cache);
	}
      break;

    case TYPE_FUNC:
      if (regu->value.funcp != NULL)
	{
	  for (operand = regu->value.funcp->operand; operand != NULL; operand = operand->next)
	    {
	      eval_mark_lazy_always_eager_regu (&operand->value, attr_cache);
	    }
	}
      break;

    default:
      break;
    }
}

/*
 * eval_mark_first_term_attrs () - find the terms eval_pred () evaluates for EVERY row: it walks the
 *   right-linear AND/OR chains left to right with short-circuit, so their leftmost term always runs, and
 *   B_XOR / B_IS / B_IS_NOT have no short-circuit, so both of their sides always run. The lazy read cannot
 *   skip those terms' attributes; it would only pay the fetch_peek_dbval_slow () dispatch on them. Flag
 *   their slots to stay on the eager path (heap_attrinfo_read_dbvalues_lazy () reads them now).
 *   return: none
 *   pr(in): data filter predicate
 *   attr_cache(in/out): predicate attribute cache
 */
void
eval_mark_first_term_attrs (const PRED_EXPR * pr, HEAP_CACHE_ATTRINFO * attr_cache)
{
  while (pr != NULL)
    {
      if (pr->type == T_PRED)
	{
	  if (pr->pe.m_pred.bool_op == B_AND || pr->pe.m_pred.bool_op == B_OR)
	    {
	      /* short-circuit: only the leftmost term is evaluated on every row */
	      pr = pr->pe.m_pred.lhs;
	    }
	  else
	    {
	      /* B_XOR / B_IS / B_IS_NOT evaluate both sides on every row - mark both */
	      eval_mark_first_term_attrs (pr->pe.m_pred.lhs, attr_cache);
	      pr = pr->pe.m_pred.rhs;
	    }
	}
      else if (pr->type == T_NOT_TERM)
	{
	  pr = pr->pe.m_not_term;
	}
      else
	{
	  break;
	}
    }
  if (pr == NULL || pr->type != T_EVAL_TERM)
    {
      return;
    }

  switch (pr->pe.m_eval_term.et_type)
    {
    case T_COMP_EVAL_TERM:
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_comp.lhs, attr_cache);
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_comp.rhs, attr_cache);
      break;
    case T_ALSM_EVAL_TERM:
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_alsm.elem, attr_cache);
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_alsm.elemset, attr_cache);
      break;
    case T_LIKE_EVAL_TERM:
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_like.src, attr_cache);
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_like.pattern, attr_cache);
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_like.esc_char, attr_cache);
      break;
    case T_RLIKE_EVAL_TERM:
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_rlike.src, attr_cache);
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_rlike.pattern, attr_cache);
      eval_mark_lazy_always_eager_regu (pr->pe.m_eval_term.et.et_rlike.case_sensitive, attr_cache);
      break;
    default:
      break;
    }
}

/*
 * eval_disable_lazy_read () - turn the lazy predicate-column read off when this scan defers nothing
 *   return: none
 *   attr_cache(in/out): predicate attribute cache, already marked by eval_mark_first_term_attrs ()
 *
 * Note: with every predicate column read up front (all slots flagged lazy_always_eager) nothing is ever
 *   deferred, so short-circuit evaluation has nothing to skip. A single predicate column, or two conditions
 *   on the same column, is exactly this case; no measurement is needed to know it cannot pay off.
 *   Also the feature's off switch: with the enable_lazy_predicate_read system parameter off, every scan
 *   starts disabled and reads exactly as before.
 */
void
eval_disable_lazy_read (HEAP_CACHE_ATTRINFO * attr_cache)
{
  int i;

  if (attr_cache == NULL || attr_cache->num_values <= 0)
    {
      return;
    }

  if (!prm_get_bool_value (PRM_ID_ENABLE_LAZY_PREDICATE_READ))
    {
      /* the feature's off switch: behave as before this optimization */
      attr_cache->lazy_disabled = true;
      return;
    }

  for (i = 0; i < attr_cache->num_values; i++)
    {
      if (!attr_cache->values[i].lazy_always_eager)
	{
	  /* this column is deferred - the lazy read can still pay off */
	  return;
	}
    }

  attr_cache->lazy_disabled = true;
}

/*
 * eval_data_filter () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 * 	 oid(in): pointer to OID
 *   recdesp(in): pointer to RECDES (record descriptor)
 *   filterp(in): pointer to FILTER_INFO (filter information)
 *
 * Note: evaluate data filter(predicates) given as FILTER_INFO.
 */
DB_LOGICAL
eval_data_filter (THREAD_ENTRY * thread_p, OID * oid, RECDES * recdesp, HEAP_SCANCACHE * scan_cache,
		  FILTER_INFO * filterp)
{
  SCAN_PRED *scan_predp;
  SCAN_ATTRS *scan_attrsp;
  DB_LOGICAL ev_res;

  if (!filterp)
    {
      return V_TRUE;
    }

  scan_predp = filterp->scan_pred;
  scan_attrsp = filterp->scan_attrs;
  if (!scan_predp)
    {
      return V_ERROR;
    }

  if (scan_attrsp != NULL && scan_attrsp->attr_cache != NULL && scan_predp->regu_list != NULL)
    {
      /* Defer reading the predicate values: mark them and stash the record, so columns skipped by
       * short-circuit evaluation in eval_pred () below are never read. heap_attrvalue_peek_lazy () reads
       * each one on demand. The first-evaluated term's column(s) were flagged lazy_always_eager at scan
       * setup (eval_mark_first_term_attrs () in scan_start_scan ()) so they stay on the eager path. For
       * class attribute scans (recdesp == NULL) this reads everything now. */
      if (heap_attrinfo_read_dbvalues_lazy (thread_p, oid, recdesp, scan_attrsp->attr_cache) != NO_ERROR)
	{
	  return V_ERROR;
	}

      if (oid == NULL && recdesp == NULL && filterp->val_list)
	{
	  /*
	   * In the case of class attribute scan, we should fetch regu_list
	   * before pred evaluation because eval_pred*() functions do not
	   * know class OID so that TYPE_CLASSOID regu cannot be handled
	   * correctly.
	   */
	  if (fetch_val_list (thread_p, scan_predp->regu_list, filterp->val_descr, filterp->class_oid, oid, NULL, PEEK)
	      != NO_ERROR)
	    {
	      return V_ERROR;
	    }
	}
    }

  /* evaluate the predicates of the data filter */
  ev_res = V_TRUE;
  if (scan_predp->pr_eval_fnc && scan_predp->pred_expr)
    {
      ev_res = (*scan_predp->pr_eval_fnc) (thread_p, scan_predp->pred_expr, filterp->val_descr, oid);
    }

  if (oid == NULL && recdesp == NULL)
    {
      /* class attribute scan case; fetch was done before evaluation */
      return ev_res;
    }

  if (ev_res == V_TRUE && scan_predp->regu_list && filterp->val_list)
    {
      /*
       * fetch the values for the regu variable list of the data filter
       * from the cached attribute information
       */
      if (fetch_val_list (thread_p, scan_predp->regu_list, filterp->val_descr, filterp->class_oid, oid, NULL, PEEK) !=
	  NO_ERROR)
	{
	  return V_ERROR;
	}
    }

  return ev_res;
}

/*
 * eval_key_filter () -
 *   return: DB_LOGICAL (V_TRUE, V_FALSE, V_UNKNOWN or V_ERROR)
 * 	 value(in): pointer to DB_VALUE (key value)
 *   filterp(in): pointer to FILTER_INFO (filter information)
 *
 * Note: evaluate key filter(predicates) given as FILTER_INFO
 */
DB_LOGICAL
eval_key_filter (THREAD_ENTRY * thread_p, DB_VALUE * value, int prefix_size, DB_VALUE * prefix_value,
		 FILTER_INFO * filterp)
{
  DB_MIDXKEY *midxkey;
  int i, j;
  SCAN_PRED *scan_predp;
  SCAN_ATTRS *scan_attrsp;
  DB_LOGICAL ev_res;
  bool found_empty_str;
  DB_TYPE type;
  HEAP_ATTRVALUE *attrvalue;
  DB_VALUE *valp;
  int prev_j_index;
  char *prev_j_ptr;
  static bool oracle_style_empty_string = prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING);

  if (value == NULL)
    {
      return V_ERROR;
    }

  if (filterp == NULL)
    {
      return V_TRUE;
    }

  scan_predp = filterp->scan_pred;
  scan_attrsp = filterp->scan_attrs;
  if (scan_predp == NULL || scan_attrsp == NULL)
    {
      return V_ERROR;
    }

  if (scan_predp->regu_list == NULL)
    {
      return V_TRUE;
    }

  ev_res = V_TRUE;

  if (scan_predp->pr_eval_fnc && scan_predp->pred_expr)
    {
      if (DB_VALUE_TYPE (value) == DB_TYPE_MIDXKEY)
	{
	  int func_idx_col_id = filterp->func_idx_col_id;

	  midxkey = db_get_midxkey (value);

	  if (filterp->btree_num_attrs <= 0 || !filterp->btree_attr_ids || !midxkey)
	    {
	      return V_ERROR;
	    }

	  DB_MIDXKEY *prefix_midxkey = NULL;
	  if (!prefix_value || prefix_size <= 0)
	    {
	      prefix_size = -1;
	    }
	  else
	    {
	      prefix_midxkey = db_get_midxkey (prefix_value);
	      if (!prefix_midxkey)
		{
		  return V_ERROR;
		}
	    }

	  prev_j_index = 0;
	  prev_j_ptr = NULL;

	  if (func_idx_col_id == -1)
	    {
	      func_idx_col_id = filterp->btree_num_attrs + 1;
	    }

	  bool filled_match_idx = (filterp->matched_attid_idx_4_keyflt && filterp->matched_attid_idx_4_keyflt[0] >= 0);

	  /* for all attributes specified in the filter */
	  for (i = 0; i < scan_attrsp->num_attrs; i++)
	    {
	      if (filled_match_idx)
		{
		  j = filterp->matched_attid_idx_4_keyflt[i];
		}
	      else
		{
		  /* for the attribute ID array of the index key */
		  for (j = 0; j < filterp->btree_num_attrs; j++)
		    {
		      if (scan_attrsp->attr_ids[i] == filterp->btree_attr_ids[j])
			{
			  if (filterp->matched_attid_idx_4_keyflt)
			    {
			      filterp->matched_attid_idx_4_keyflt[i] = j;
			    }
			  break;	/* immediately exit inner-loop */
			}
		    }
		}

	      if (j < filterp->btree_num_attrs)
		{
		  /* now, found the attr */

		  attrvalue = heap_attrvalue_locate (scan_attrsp->attr_ids[i], scan_attrsp->attr_cache);
		  if (attrvalue == NULL)
		    {
		      return V_ERROR;
		    }

		  valp = &(attrvalue->dbvalue);
		  if (pr_clear_value (valp) != NO_ERROR)
		    {
		      return V_ERROR;
		    }

		  /* get j-th element value from the midxkey */
		  // TODO: Let's find a way to reuse the value of "j" instead of finding it anew every time.         
		  if (pr_midxkey_get_element_nocopy (((j < prefix_size) ? prefix_midxkey : midxkey),
						     ((j < func_idx_col_id) ? j : j + 1),
						     valp, &prev_j_index, &prev_j_ptr) != NO_ERROR)
		    {
		      return V_ERROR;
		    }

		  found_empty_str = false;
		  if (oracle_style_empty_string && db_value_is_null (valp))
		    {
		      if (valp->need_clear)
			{
			  type = DB_VALUE_DOMAIN_TYPE (valp);
			  if (QSTR_IS_ANY_CHAR_OR_BIT (type) && valp->data.ch.medium.buf != NULL)
			    {
			      /* convert NULL into Empty-string */
			      valp->domain.general_info.is_null = 0;
			      found_empty_str = true;
			    }
			}
		    }

		  if (found_empty_str)
		    {
		      /* convert NULL into Empty-string */
		      valp->domain.general_info.is_null = 0;
		    }

		  attrvalue->state = HEAP_WRITTEN_ATTRVALUE;
		}
	      else
		{
		  /*
		   * the attribute exists in key filter scan cache, but it is
		   * not a member of attributes consisting index key
		   */
		  DB_VALUE null;

		  db_make_null (&null);
		  if (heap_attrinfo_set (NULL, scan_attrsp->attr_ids[i], &null, scan_attrsp->attr_cache) != NO_ERROR)
		    {
		      return V_ERROR;
		    }
		}
	    }
	}
      else
	{
	  if (scan_attrsp->attr_ids == NULL)
	    {
	      /* defense code */
	      assert_release (false);
	      return V_ERROR;
	    }
	  attrvalue = heap_attrvalue_locate (scan_attrsp->attr_ids[0], scan_attrsp->attr_cache);
	  if (attrvalue == NULL)
	    {
	      return V_ERROR;
	    }

	  valp = &(attrvalue->dbvalue);
	  if (pr_clear_value (valp) != NO_ERROR)
	    {
	      return V_ERROR;
	    }

	  if (pr_clone_value (value, valp) != NO_ERROR)
	    {
	      return V_ERROR;
	    }

	  found_empty_str = false;
	  if (oracle_style_empty_string && db_value_is_null (valp))
	    {
	      if (valp->need_clear)
		{
		  type = DB_VALUE_DOMAIN_TYPE (valp);
		  if (QSTR_IS_ANY_CHAR_OR_BIT (type) && valp->data.ch.medium.buf != NULL)
		    {
		      /* convert NULL into Empty-string */
		      found_empty_str = true;
		    }
		}
	    }

	  if (found_empty_str)
	    {
	      /* convert NULL into Empty-string */
	      valp->domain.general_info.is_null = 0;

	      /* set single-column key val */
	      value->domain.general_info.is_null = 0;
	    }

	  attrvalue->state = HEAP_WRITTEN_ATTRVALUE;
	}

      /*
       * evaluate the predicates of the key filter
       * using the given key value
       */
      ev_res = (*scan_predp->pr_eval_fnc) (thread_p, scan_predp->pred_expr, filterp->val_descr, NULL /* obj_oid */ );
    }

  return ev_res;
}
