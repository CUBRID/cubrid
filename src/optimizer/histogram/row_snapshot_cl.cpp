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
 * row_snapshot_cl.cpp - rows of code tables read while planning (CBRD-27478)
 */

#include "row_snapshot_cl.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "authenticate.h"
#include "class_object.h"
#include "db.h"
#include "db_query.h"
#include "dbtype.h"
#include "error_manager.h"
#include "histogram_cl.hpp"
#include "intl_support.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "optimizer.h"
#include "parser.h"
#include "schema_manager.h"
#include "set_object.h"
#include "system_parameter.h"
#include "xasl_generation.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static bool row_snapshot_keeps_type (DB_TYPE type);
static bool row_snapshot_column_indexed (SM_CLASS *class_, int attr_id);
static const char *row_snapshot_attr_name (SM_CLASS *class_, int attr_id);
static bool row_snapshot_const_shape (PT_NODE *node);
static PT_NODE *row_snapshot_split_eq (PT_NODE *expr, UINTPTR spec_id, PT_NODE **out_const);
static bool row_snapshot_or_filter_prepare (PARSER_CONTEXT *parser, const ROW_SNAPSHOT *snapshot, MOP classop,
    UINTPTR spec_id, PT_NODE *pred, ROW_SNAPSHOT_FILTER *filter);

int
row_snapshot_max_rows (void)
{
  return prm_get_integer_value (PRM_ID_ROW_SNAPSHOT_MAX_ROWS);
}

/* the columns a snapshot keeps: the ones a histogram is built for, except object references,
 * whose packed form would carry OIDs a later read cannot trust */
static bool
row_snapshot_keeps_type (DB_TYPE type)
{
  return is_histogrammable_type (type) && type != DB_TYPE_OID && type != DB_TYPE_OBJECT;
}

void
row_snapshot_filter_clear (ROW_SNAPSHOT_FILTER *filter)
{
  if (filter == NULL)
    {
      return;
    }
  if (filter->values != NULL)
    {
      free_and_init (filter->values);
    }
  if (filter->elements != NULL)
    {
      free_and_init (filter->elements);
    }
  filter->n_values = 0;
}

void
row_snapshot_free (ROW_SNAPSHOT *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }
  if (snapshot->values != NULL)
    {
      for (int i = 0; i < snapshot->n_rows * snapshot->n_attrs; i++)
	{
	  pr_clear_value (&snapshot->values[i]);
	}
      free_and_init (snapshot->values);
    }
  if (snapshot->attr_ids != NULL)
    {
      free_and_init (snapshot->attr_ids);
    }
  free_and_init (snapshot);
}

int
row_snapshot_column (const ROW_SNAPSHOT *snapshot, MOP classop, const char *attr_name)
{
  int attr_id;

  if (snapshot == NULL || attr_name == NULL)
    {
      return -1;
    }
  attr_id = sm_att_id (classop, attr_name);
  if (attr_id < 0)
    {
      er_clear ();
      return -1;
    }
  for (int c = 0; c < snapshot->n_attrs; c++)
    {
      if (snapshot->attr_ids[c] == attr_id)
	{
	  return c;
	}
    }
  return -1;
}

DB_VALUE *
row_snapshot_const_value (PARSER_CONTEXT *parser, PT_NODE *node)
{
  DB_VALUE *value;
  int idx;

  if (node == NULL)
    {
      return NULL;
    }

  switch (node->node_type)
    {
    case PT_VALUE:
      if (PT_IS_SET_TYPE (node) || node->type_enum == PT_TYPE_NULL || !node->info.value.db_value_is_initialized)
	{
	  return NULL;
	}
      value = &node->info.value.db_value;
      break;

    case PT_HOST_VAR:
      /* a literal is auto-parameterized into a host variable (?:n, index >= host_var_count) before
       * the optimizer runs; a user host variable counts once it is bound -- the plan is then
       * compiled for its value, as the histogram estimates already are, and the bind fingerprint
       * (histogram_bind_fingerprint ()) watches the snapshot's answer for it */
      if (node->info.host_var.var_type != PT_HOST_IN || parser->host_variables == NULL)
	{
	  return NULL;
	}
      idx = node->info.host_var.index;
      if (idx < 0 || idx >= parser->host_var_count + parser->auto_param_count)
	{
	  return NULL;
	}
      value = &parser->host_variables[idx];
      break;

    default:
      return NULL;
    }

  if (DB_IS_NULL (value))
    {
      /* NULL matches no row, and an unbound marker is NULL too: neither says which rows survive */
      return NULL;
    }
  return value;
}

/* a node row_snapshot_const_value () can read once the statement's values are bound */
static bool
row_snapshot_const_shape (PT_NODE *node)
{
  if (node == NULL)
    {
      return false;
    }
  if (node->node_type == PT_VALUE)
    {
      return !PT_IS_SET_TYPE (node) && node->type_enum != PT_TYPE_NULL && node->info.value.db_value_is_initialized;
    }
  return node->node_type == PT_HOST_VAR && node->info.host_var.var_type == PT_HOST_IN;
}

/* the column of `col = c` (either side) when it is a plain column of spec_id, with its constant */
static PT_NODE *
row_snapshot_split_eq (PT_NODE *expr, UINTPTR spec_id, PT_NODE **out_const)
{
  PT_NODE *a, *b;

  if (expr == NULL || expr->node_type != PT_EXPR || expr->info.expr.op != PT_EQ)
    {
      return NULL;
    }
  a = expr->info.expr.arg1;
  b = expr->info.expr.arg2;
  if (a != NULL && a->node_type == PT_DOT_)
    {
      a = pt_get_end_path_node (a);
    }
  if (b != NULL && b->node_type == PT_DOT_)
    {
      b = pt_get_end_path_node (b);
    }
  if (a != NULL && a->node_type == PT_NAME && a->info.name.spec_id == spec_id)
    {
      *out_const = b;
      return a;
    }
  if (b != NULL && b->node_type == PT_NAME && b->info.name.spec_id == spec_id)
    {
      *out_const = a;
      return b;
    }
  return NULL;
}

/* `col = c1 OR col = c2 OR ...` on one column, the form an IN list over a column without an index
 * is rewritten to */
static bool
row_snapshot_or_filter_prepare (PARSER_CONTEXT *parser, const ROW_SNAPSHOT *snapshot, MOP classop, UINTPTR spec_id,
				PT_NODE *pred, ROW_SNAPSHOT_FILTER *filter)
{
  PT_NODE *disj, *col, *first_col = NULL, *c;
  int n = 0, i;

  for (disj = pred; disj != NULL; disj = disj->or_next)
    {
      col = row_snapshot_split_eq (disj, spec_id, &c);
      if (col == NULL || !row_snapshot_const_shape (c))
	{
	  return false;
	}
      if (first_col == NULL)
	{
	  first_col = col;
	}
      else if (col->info.name.original == NULL || first_col->info.name.original == NULL
	       || intl_identifier_casecmp (col->info.name.original, first_col->info.name.original) != 0)
	{
	  return false;
	}
      n++;
    }
  if (row_snapshot_column (snapshot, classop, first_col->info.name.original) < 0)
    {
      return false;
    }

  filter->column = row_snapshot_column (snapshot, classop, first_col->info.name.original);
  filter->elements = NULL;
  filter->values = (DB_VALUE **) calloc (n, sizeof (DB_VALUE *));
  if (filter->values == NULL)
    {
      return false;
    }
  filter->n_values = n;
  for (disj = pred, i = 0; disj != NULL; disj = disj->or_next, i++)
    {
      (void) row_snapshot_split_eq (disj, spec_id, &c);
      filter->values[i] = row_snapshot_const_value (parser, c);
      if (filter->values[i] == NULL)
	{
	  return false;
	}
    }
  return true;
}

bool
row_snapshot_filter_prepare (PARSER_CONTEXT *parser, const ROW_SNAPSHOT *snapshot, MOP classop, UINTPTR spec_id,
			     PT_NODE *pred, ROW_SNAPSHOT_FILTER *filter)
{
  PT_NODE *lhs, *rhs, *elem;
  DB_SET *in_set = NULL;
  int n = 0, i, set_size = 0;

  if (pred == NULL || pred->node_type != PT_EXPR)
    {
      return false;
    }
  if (pred->or_next != NULL)
    {
      return row_snapshot_or_filter_prepare (parser, snapshot, classop, spec_id, pred, filter);
    }
  lhs = pred->info.expr.arg1;
  rhs = pred->info.expr.arg2;
  if (lhs != NULL && lhs->node_type == PT_DOT_)
    {
      lhs = pt_get_end_path_node (lhs);
    }
  /* the column on the left: the optimizer has already turned `const = col` into `col = const` */
  if (lhs == NULL || lhs->node_type != PT_NAME || lhs->info.name.spec_id != spec_id || rhs == NULL)
    {
      return false;
    }
  if (row_snapshot_column (snapshot, classop, lhs->info.name.original) < 0)
    {
      return false;
    }

  /* count the constants first, then collect them */
  switch (pred->info.expr.op)
    {
    case PT_EQ:
      n = 1;
      break;

    case PT_RANGE:
      for (elem = rhs; elem != NULL; elem = elem->or_next)
	{
	  if (elem->node_type != PT_EXPR || elem->info.expr.op != PT_BETWEEN_EQ_NA)
	    {
	      return false;
	    }
	  n++;
	}
      break;

    case PT_IS_IN:
    case PT_EQ_SOME:
      if (rhs->node_type == PT_VALUE && PT_IS_SET_TYPE (rhs) && rhs->info.value.db_value_is_initialized)
	{
	  in_set = db_get_set (&rhs->info.value.db_value);
	  set_size = (in_set != NULL) ? db_set_size (in_set) : 0;
	  n = set_size;
	}
      else if (rhs->node_type == PT_FUNCTION && PT_IS_SET_TYPE (rhs))
	{
	  for (elem = rhs->info.function.arg_list; elem != NULL; elem = elem->next)
	    {
	      n++;
	    }
	}
      else
	{
	  return false;		/* a subquery or a host-variable set */
	}
      break;

    default:
      return false;
    }
  if (n <= 0)
    {
      return false;
    }

  /* malloc'd, not parser memory: the filter lives only until the read, and a statement planned
   * many times would pile up parser memory for each */
  filter->column = row_snapshot_column (snapshot, classop, lhs->info.name.original);
  filter->elements = NULL;
  filter->values = (DB_VALUE **) calloc (n, sizeof (DB_VALUE *));
  if (filter->values == NULL)
    {
      return false;
    }
  filter->n_values = n;

  switch (pred->info.expr.op)
    {
    case PT_EQ:
      filter->values[0] = row_snapshot_const_value (parser, rhs);
      break;

    case PT_RANGE:
      for (elem = rhs, i = 0; elem != NULL; elem = elem->or_next, i++)
	{
	  filter->values[i] = row_snapshot_const_value (parser, elem->info.expr.arg1);
	}
      break;

    default:
      if (in_set != NULL)
	{
	  filter->elements = (DB_VALUE *) calloc (set_size, sizeof (DB_VALUE));
	  if (filter->elements == NULL)
	    {
	      return false;
	    }
	  for (i = 0; i < set_size; i++)
	    {
	      filter->values[i] = &filter->elements[i];
	      /* peek: the set element stays owned by the parse tree */
	      if (set_get_element_nocopy (in_set, i, filter->values[i]) != NO_ERROR || DB_IS_NULL (filter->values[i]))
		{
		  filter->values[i] = NULL;
		}
	    }
	}
      else
	{
	  for (elem = rhs->info.function.arg_list, i = 0; elem != NULL; elem = elem->next, i++)
	    {
	      filter->values[i] = row_snapshot_const_value (parser, elem);
	    }
	}
      break;
    }

  for (i = 0; i < n; i++)
    {
      if (filter->values[i] == NULL)
	{
	  /* a value we cannot read means we do not know which rows survive */
	  return false;
	}
    }
  return true;
}

/* a column of the class leads an index a filter on it can scan: not a function or filtered index,
 * not a reverse one (its keys are not the values), and usable now */
static bool
row_snapshot_column_indexed (SM_CLASS *class_, int attr_id)
{
  for (SM_CLASS_CONSTRAINT *cons = class_->constraints; cons != NULL; cons = cons->next)
    {
      if ((cons->type != SM_CONSTRAINT_INDEX && cons->type != SM_CONSTRAINT_UNIQUE
	   && cons->type != SM_CONSTRAINT_PRIMARY_KEY && cons->type != SM_CONSTRAINT_FOREIGN_KEY)
	  || cons->index_status != SM_NORMAL_INDEX || cons->func_index_info != NULL || cons->filter_predicate != NULL
	  || cons->attributes == NULL || cons->attributes[0] == NULL)
	{
	  continue;
	}
      if (cons->attributes[0]->id == attr_id)
	{
	  return true;
	}
    }
  return false;
}

static const char *
row_snapshot_attr_name (SM_CLASS *class_, int attr_id)
{
  for (SM_ATTRIBUTE *att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->id == attr_id)
	{
	  return att->header.name;
	}
    }
  return NULL;
}

ROW_SNAPSHOT *
row_snapshot_columns (SM_CLASS *class_)
{
  ROW_SNAPSHOT *columns;
  SM_ATTRIBUTE *att;
  int n = 0;

  /* not the catalog: tools query it all the time and its estimates matter little */
  if (class_ == NULL || class_->class_type != SM_CLASS_CT || (class_->flags & SM_CLASSFLAG_SYSTEM) != 0)
    {
      return NULL;
    }
  for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->domain != NULL && row_snapshot_keeps_type (TP_DOMAIN_TYPE (att->domain)))
	{
	  n++;
	}
    }
  if (n == 0)
    {
      return NULL;
    }

  columns = (ROW_SNAPSHOT *) calloc (1, sizeof (ROW_SNAPSHOT));
  if (columns == NULL)
    {
      return NULL;
    }
  columns->attr_ids = (int *) calloc (n, sizeof (int));
  if (columns->attr_ids == NULL)
    {
      free_and_init (columns);
      return NULL;
    }
  for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->domain != NULL && row_snapshot_keeps_type (TP_DOMAIN_TYPE (att->domain)))
	{
	  columns->attr_ids[columns->n_attrs++] = att->id;
	}
    }
  return columns;
}

ROW_SNAPSHOT *
row_snapshot_read (MOP classop, SM_CLASS *class_, const ROW_SNAPSHOT *columns, const ROW_SNAPSHOT_FILTER *filters,
		   int n_filters, int max_rows)
{
  /* the read is a statement on the same class with the same filters: planning it must not read
   * again. The client runs one statement at a time, so a plain flag will do. */
  static bool in_read = false;
  std::string query;
  std::vector<DB_VALUE> binds;
  DB_SESSION *session = NULL;
  DB_QUERY_RESULT *result = NULL;
  ROW_SNAPSHOT *rows = NULL;
  const char *class_name, *name;
  char *saved_dump_filename;
  FILE *saved_dump_fp;
  bool saved_dump_fp_open, indexed = false, usable = true;
  int saved_level, stmt, pos, c, f, n = 0;

  if (in_read || classop == NULL || class_ == NULL || columns == NULL || n_filters <= 0 || max_rows <= 0)
    {
      return NULL;
    }
  class_name = sm_ch_name ((MOBJ) class_);
  if (class_name == NULL)
    {
      return NULL;
    }

  /* SELECT <columns> FROM <class> WHERE <col> IN (?, ...) AND ... LIMIT max_rows + 1 */
  query = "SELECT ";
  for (c = 0; c < columns->n_attrs; c++)
    {
      name = row_snapshot_attr_name (class_, columns->attr_ids[c]);
      if (name == NULL)
	{
	  return NULL;
	}
      query += (c == 0) ? "[" : ", [";
      query += name;
      query += "]";
    }
  query += " FROM [";
  query += class_name;
  query += "] WHERE ";
  for (f = 0; f < n_filters; f++)
    {
      name = row_snapshot_attr_name (class_, columns->attr_ids[filters[f].column]);
      if (name == NULL || filters[f].n_values <= 0)
	{
	  return NULL;
	}
      /* one indexed filter column is enough: the scan goes through it and the others filter */
      indexed = indexed || row_snapshot_column_indexed (class_, columns->attr_ids[filters[f].column]);
      query += (f == 0) ? "[" : " AND [";
      query += name;
      query += "] IN (";
      for (int v = 0; v < filters[f].n_values; v++)
	{
	  query += (v == 0) ? "?" : ", ?";
	  binds.push_back (*filters[f].values[v]);	/* shallow: db_push_values () copies them */
	}
      query += ")";
    }
  if (!indexed)
    {
      /* without an index the read scans the whole table at every compile */
      return NULL;
    }
  /* bound, so every read of a class with as many filter values shares one cached plan */
  query += " LIMIT ?";
  binds.emplace_back ();
  db_make_int (&binds.back (), max_rows + 1);

  /* The read is compiled and run in the middle of planning another statement, so it must leave
   * that statement's state alone: its error (the read's own failure only means "keep the
   * estimate") and its plan dump -- generating the read's XASL would close the dump file the
   * statement's XASL generation has open. The read itself runs at level 1, optimized, executed and
   * not dumped, whatever the statement's level: a plan-only level (2, the one a driver asks for to
   * show a plan) would not execute it, and the plan it shows would not be the one run. */
  in_read = true;
  er_stack_push ();
  saved_dump_filename = query_Plan_dump_filename;
  saved_dump_fp = query_Plan_dump_fp;
  saved_dump_fp_open = query_Plan_dump_fp_open;
  query_Plan_dump_filename = NULL;
  query_Plan_dump_fp = NULL;
  query_Plan_dump_fp_open = false;
  qo_get_optimization_param (&saved_level, QO_PARAM_LEVEL);
  qo_set_optimization_param (NULL, QO_PARAM_LEVEL, 1);

  session = db_open_buffer_local (query.c_str ());
  if (session != NULL)
    {
      stmt = db_compile_statement_local (session);
      if (stmt <= 0 || db_push_values (session, (int) binds.size (), binds.data ()) != NO_ERROR
	  || db_execute_statement_local (session, stmt, &result) < 0 || result == NULL)
	{
	  usable = false;
	}
    }
  else
    {
      usable = false;
    }

  if (usable)
    {
      rows = (ROW_SNAPSHOT *) calloc (1, sizeof (ROW_SNAPSHOT));
      if (rows != NULL)
	{
	  rows->n_attrs = columns->n_attrs;
	  rows->attr_ids = (int *) calloc (columns->n_attrs, sizeof (int));
	  rows->values = (DB_VALUE *) calloc ((size_t) (max_rows + 1) * columns->n_attrs, sizeof (DB_VALUE));
	}
      if (rows == NULL || rows->attr_ids == NULL || rows->values == NULL)
	{
	  usable = false;
	}
      else
	{
	  memcpy (rows->attr_ids, columns->attr_ids, columns->n_attrs * sizeof (int));
	}
    }
  for (pos = usable ? db_query_first_tuple (result) : DB_CURSOR_END; usable && pos == DB_CURSOR_SUCCESS;
       pos = db_query_next_tuple (result))
    {
      if (n == max_rows)
	{
	  /* more rows pass than a code lookup: the histograms keep the estimate */
	  usable = false;
	  break;
	}
      for (c = 0; c < columns->n_attrs; c++)
	{
	  DB_VALUE *v = &rows->values[ (size_t) n * columns->n_attrs + c];

	  db_make_null (v);
	  if (db_query_get_tuple_value (result, c, v) != NO_ERROR)
	    {
	      usable = false;
	    }
	}
      rows->n_rows = ++n;
    }
  if (pos < 0)
    {
      usable = false;
    }
  if (result != NULL)
    {
      db_query_end (result);
    }
  if (session != NULL)
    {
      db_close_session_local (session);
    }

  qo_set_optimization_param (NULL, QO_PARAM_LEVEL, saved_level);
  query_Plan_dump_filename = saved_dump_filename;
  query_Plan_dump_fp = saved_dump_fp;
  query_Plan_dump_fp_open = saved_dump_fp_open;
  er_stack_pop ();
  in_read = false;

  if (!usable || n == 0)
    {
      /* no row passes: nothing says which keys the node joins on (a code not in the table yet is
       * priced as the histograms price it) */
      row_snapshot_free (rows);
      return NULL;
    }
  return rows;
}

bool
row_snapshot_join_selectivity (const ROW_SNAPSHOT *snapshot, int column, PT_NODE *other, double nf_other,
			       double *selectivity)
{
  double sum = 0.0, sel;
  bool success = false;
  std::vector<DB_VALUE *> keys;
  int k;

  if (snapshot == NULL || snapshot->n_rows <= 0 || column < 0)
    {
      return false;
    }
  k = snapshot->n_rows;

  /* a NULL key joins no row of the other side: it stays in k and adds 0 (the sum skips it) */
  keys.reserve (k);
  for (int i = 0; i < k; i++)
    {
      keys.push_back (&snapshot->values[i * snapshot->n_attrs + column]);
    }
  histogram_get_equal_selectivity_sum (other, keys.data (), k, &sum, &success);
  if (!success)
    {
      return false;
    }

  /* histogram_get_equal_selectivity () returns the fraction of the other column's NON-null rows
   * (it divides its own null mass out). A join term's selectivity is a fraction of the whole
   * cross product, so the other side's non-null fraction is multiplied back in. This side needs
   * no such factor: the k rows are concrete values, and a NULL key among them added 0 above. */
  sel = sum / (double) k;
  if (nf_other >= 0.0 && nf_other < 1.0)
    {
      sel *= (1.0 - nf_other);
    }
  if (sel <= 0.0)
    {
      return false;
    }
  *selectivity = MIN (sel, 1.0);
  return true;
}
