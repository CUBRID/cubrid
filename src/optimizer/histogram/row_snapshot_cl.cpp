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
 * row_snapshot_cl.cpp - rows of small tables kept with the statistics (CBRD-27478)
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
#include "object_representation.h"
#include "parser.h"
#include "schema_manager.h"
#include "set_object.h"
#include "system_parameter.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* blob layout: magic, version, total length, n_attrs, n_rows (kept), table_rows (the table's rows
 * when collected), n_attrs column ids, then the values row by row, each packed with its domain
 * (or_put_value), so a value reads back with its type and collation */
#define ROW_SNAPSHOT_MAGIC 0x52534e31	/* "RSN1" */
#define ROW_SNAPSHOT_VERSION 1
#define ROW_SNAPSHOT_HEADER_INTS 6
/* more columns than a class can have means the blob is not ours */
#define ROW_SNAPSHOT_MAX_ATTRS 10000

static bool row_snapshot_keeps_type (DB_TYPE type);
static int row_snapshot_fixed_size (int n_attrs);
static ROW_SNAPSHOT *row_snapshot_load (MOP classop, SM_CLASS *class_);
static ROW_SNAPSHOT *row_snapshot_decode (const char *blob, int length);
static void row_snapshot_check_columns (ROW_SNAPSHOT *snapshot, SM_CLASS *class_);
static bool row_snapshot_const_shape (PT_NODE *node);
static PT_NODE *row_snapshot_split_eq (PT_NODE *expr, UINTPTR spec_id, PT_NODE **out_const);
static bool row_snapshot_or_filter_prepare (PARSER_CONTEXT *parser, const ROW_SNAPSHOT *snapshot, MOP classop,
    UINTPTR spec_id, PT_NODE *pred, ROW_SNAPSHOT_FILTER *filter);

int
row_snapshot_max_rows (void)
{
  return prm_get_integer_value (PRM_ID_STATISTICS_ROW_SNAPSHOT_ROWS);
}

/* the columns a snapshot keeps: the ones a histogram is built for, except object references,
 * whose packed form would carry OIDs a later read cannot trust */
static bool
row_snapshot_keeps_type (DB_TYPE type)
{
  return is_histogrammable_type (type) && type != DB_TYPE_OID && type != DB_TYPE_OBJECT;
}

int
row_snapshot_drop (MOP classop)
{
  /* sm_drop_histogram () is a no-op for a key the class has no row for */
  return sm_drop_histogram (classop, ROW_SNAPSHOT_KEY_ATTR);
}

/* the packed size of a header and the column ids */
static int
row_snapshot_fixed_size (int n_attrs)
{
  return OR_INT_SIZE * (ROW_SNAPSHOT_HEADER_INTS + n_attrs);
}

int
row_snapshot_collect_and_store (MOP classop, INT64 total_rows, bool with_fullscan)
{
  int max_rows = row_snapshot_max_rows ();
  int partition_type = DB_NOT_PARTITIONED_CLASS;
  const char *class_name;
  std::vector<std::string> names;
  std::vector<int> ids;
  std::vector<DB_VALUE> values;	/* the kept rows, n_attrs values each */
  std::string query;
  DB_QUERY_RESULT *result = NULL;
  DB_QUERY_ERROR query_error;
  int n_attrs, n_rows = 0, size, pos, i, c, error = NO_ERROR;
  bool usable = true;
  char *blob = NULL;
  OR_BUF buf;
  char limit_clause[32];

  if (max_rows <= 0 || sm_get_class_flag (classop, SM_CLASSFLAG_ROW_SNAPSHOT) <= 0)
    {
      /* off, or a table not declared ROW_SNAPSHOT: the catalog is left exactly as it is (ALTER TABLE
       * ... NO_ROW_SNAPSHOT and DROP TABLE remove the kept rows themselves) */
      er_clear ();
      return NO_ERROR;
    }

  /* the declaration refuses these, but a table can be partitioned after it was declared */
  if (sm_partitioned_class_type (classop, &partition_type, NULL, NULL) != NO_ERROR)
    {
      return er_errid ();
    }
  if (partition_type != DB_NOT_PARTITIONED_CLASS || total_rows <= 0)
    {
      return row_snapshot_drop (classop);
    }

  for (DB_ATTRIBUTE *att = db_get_attributes (classop); att != NULL; att = db_attribute_next (att))
    {
      if (row_snapshot_keeps_type (db_attribute_type (att)))
	{
	  names.push_back (db_attribute_name (att));
	  ids.push_back (db_attribute_id (att));
	}
    }
  class_name = db_get_class_name (classop);
  if (names.empty () || class_name == NULL)
    {
      return row_snapshot_drop (classop);
    }
  n_attrs = (int) names.size ();

  query = "SELECT ";
  for (i = 0; i < n_attrs; i++)
    {
      query += (i == 0) ? "[" : ", [";
      query += names[i];
      query += "]";
    }
  query += " FROM [";
  query += class_name;
  query += "]";
  /* one row past the limit tells a table that has grown over it, whatever total_rows estimated */
  snprintf (limit_clause, sizeof (limit_clause), " LIMIT %d", max_rows + 1);
  query += limit_clause;

  /* Reading the rows is part of the statistics, not of the statement: a failure here leaves the
   * class without a snapshot (the optimizer then prices it as before) instead of failing the
   * UPDATE STATISTICS that already collected everything else. */
  if (db_compile_and_execute_local (query.c_str (), &result, &query_error) < 0)
    {
      er_clear ();
      return row_snapshot_drop (classop);
    }
  values.reserve ((size_t) MIN (total_rows, (INT64) max_rows) * n_attrs);
  size = row_snapshot_fixed_size (n_attrs);
  for (pos = db_query_first_tuple (result); pos == DB_CURSOR_SUCCESS && usable; pos = db_query_next_tuple (result))
    {
      /* All of the table or nothing: a sample would price a value it holds by its share of the
       * sample and a value it misses by the histogram, so a plan would turn on which rows the
       * sample happened to take. Over either limit the table keeps no snapshot. */
      if (n_rows == max_rows || size > ROW_SNAPSHOT_MAX_BYTES)
	{
	  usable = false;
	  break;
	}
      values.resize ((size_t) (n_rows + 1) * n_attrs);
      for (c = 0; c < n_attrs; c++)
	{
	  DB_VALUE *v = &values[ (size_t) n_rows * n_attrs + c];

	  db_make_null (v);
	  if (db_query_get_tuple_value (result, c, v) != NO_ERROR)
	    {
	      usable = false;
	      break;
	    }
	  size += or_packed_value_size (v, 0, 1, 0);
	}
      n_rows++;
    }
  if (pos < 0 || size > ROW_SNAPSHOT_MAX_BYTES)
    {
      usable = false;
    }
  db_query_end (result);
  er_clear ();

  if (usable && n_rows > 0)
    {
      blob = (char *) malloc (size);
      if (blob == NULL)
	{
	  usable = false;
	}
    }
  if (usable && n_rows > 0)
    {
      or_init (&buf, blob, size);
      if (or_put_int (&buf, ROW_SNAPSHOT_MAGIC) != NO_ERROR || or_put_int (&buf, ROW_SNAPSHOT_VERSION) != NO_ERROR
	  || or_put_int (&buf, size) != NO_ERROR || or_put_int (&buf, n_attrs) != NO_ERROR
	  || or_put_int (&buf, n_rows) != NO_ERROR
	  || or_put_int (&buf, (int) MIN (total_rows, (INT64) INT_MAX)) != NO_ERROR)
	{
	  usable = false;
	}
      for (i = 0; usable && i < n_attrs; i++)
	{
	  usable = (or_put_int (&buf, ids[i]) == NO_ERROR);
	}
      for (i = 0; usable && i < n_rows * n_attrs; i++)
	{
	  usable = (or_put_value (&buf, &values[i], 0, 1, 0) == NO_ERROR);
	}
      if (usable && buf.ptr - blob != size)
	{
	  assert (false);
	  usable = false;
	}
    }
  for (i = 0; i < (int) values.size (); i++)
    {
      pr_clear_value (&values[i]);
    }

  if (usable && n_rows > 0)
    {
      error = histogram_store_blob (classop, ROW_SNAPSHOT_KEY_ATTR, blob, size, with_fullscan);
    }
  else
    {
      error = row_snapshot_drop (classop);
    }
  if (blob != NULL)
    {
      free_and_init (blob);
    }
  return error;
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

static ROW_SNAPSHOT *
row_snapshot_decode (const char *blob, int length)
{
  ROW_SNAPSHOT *snapshot;
  OR_BUF buf;
  int magic = 0, version = 0, total = 0, n_attrs = 0, n_rows = 0, table_rows = 0, i, rc = NO_ERROR;
  char *copy;

  /* or_get_value () reads the values with the padding they were written with, relative to the
   * start of the blob; work on an aligned copy so the reads do not depend on where the catalog
   * value happens to sit */
  copy = (char *) malloc (length);
  if (copy == NULL)
    {
      return NULL;
    }
  memcpy (copy, blob, length);
  or_init (&buf, copy, length);

  /* or_get_int () checks the bound only by assert: every count is checked against the length
   * before it is trusted, so a damaged catalog value is ignored instead of read past */
  if (length < OR_INT_SIZE * ROW_SNAPSHOT_HEADER_INTS)
    {
      free_and_init (copy);
      return NULL;
    }
  magic = or_get_int (&buf, &rc);
  version = or_get_int (&buf, &rc);
  total = or_get_int (&buf, &rc);
  n_attrs = or_get_int (&buf, &rc);
  n_rows = or_get_int (&buf, &rc);
  table_rows = or_get_int (&buf, &rc);
  if (magic != ROW_SNAPSHOT_MAGIC || version != ROW_SNAPSHOT_VERSION || total != length || n_attrs <= 0
      || n_attrs > ROW_SNAPSHOT_MAX_ATTRS || n_rows <= 0 || n_rows > ROW_SNAPSHOT_MAX_ROWS
      || length - OR_INT_SIZE * ROW_SNAPSHOT_HEADER_INTS < OR_INT_SIZE * n_attrs
      || (INT64) n_attrs * n_rows > (INT64) length / OR_INT_SIZE)
    {
      /* an unknown or damaged format (every packed value takes at least its 4-byte domain, which
       * bounds the allocation below by the blob's own length) */
      free_and_init (copy);
      return NULL;
    }

  snapshot = (ROW_SNAPSHOT *) calloc (1, sizeof (ROW_SNAPSHOT));
  if (snapshot == NULL)
    {
      free_and_init (copy);
      return NULL;
    }
  snapshot->attr_ids = (int *) calloc (n_attrs, sizeof (int));
  snapshot->values = (DB_VALUE *) calloc ((size_t) n_attrs * n_rows, sizeof (DB_VALUE));
  if (snapshot->attr_ids == NULL || snapshot->values == NULL)
    {
      row_snapshot_free (snapshot);
      free_and_init (copy);
      return NULL;
    }
  snapshot->n_attrs = n_attrs;
  snapshot->table_rows = table_rows;
  for (i = 0; i < n_attrs * n_rows; i++)
    {
      db_make_null (&snapshot->values[i]);
    }

  for (i = 0; i < n_attrs && rc == NO_ERROR; i++)
    {
      snapshot->attr_ids[i] = or_get_int (&buf, &rc);
    }
  for (i = 0; i < n_attrs * n_rows && rc == NO_ERROR; i++)
    {
      if (buf.ptr >= buf.endptr)
	{
	  rc = ER_FAILED;
	  break;
	}
      rc = or_get_value (&buf, &snapshot->values[i], NULL, -1, true);
      if (rc == NO_ERROR && buf.ptr > buf.endptr)
	{
	  rc = ER_FAILED;
	}
    }
  /* n_rows only once every value is in: row_snapshot_free () clears the values of n_rows rows */
  snapshot->n_rows = n_rows;
  free_and_init (copy);

  if (rc != NO_ERROR)
    {
      row_snapshot_free (snapshot);
      er_clear ();
      return NULL;
    }
  return snapshot;
}

/* a column dropped or retyped since the collection keeps its slot, under id -1: no filter or
 * join reaches it, and the other columns stay usable */
static void
row_snapshot_check_columns (ROW_SNAPSHOT *snapshot, SM_CLASS *class_)
{
  for (int c = 0; c < snapshot->n_attrs; c++)
    {
      SM_ATTRIBUTE *att;
      DB_TYPE type = DB_TYPE_NULL;

      for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (att->id == snapshot->attr_ids[c])
	    {
	      break;
	    }
	}
      if (att == NULL || att->domain == NULL)
	{
	  snapshot->attr_ids[c] = -1;
	  continue;
	}
      for (int r = 0; r < snapshot->n_rows; r++)
	{
	  if (!DB_IS_NULL (&snapshot->values[r * snapshot->n_attrs + c]))
	    {
	      type = DB_VALUE_DOMAIN_TYPE (&snapshot->values[r * snapshot->n_attrs + c]);
	      break;
	    }
	}
      if (type != DB_TYPE_NULL && type != TP_DOMAIN_TYPE (att->domain))
	{
	  snapshot->attr_ids[c] = -1;
	}
    }
}

static ROW_SNAPSHOT *
row_snapshot_load (MOP classop, SM_CLASS *class_)
{
  DB_OBJECT *row = NULL;
  DB_VALUE blob_value;
  ROW_SNAPSHOT *snapshot = NULL;
  const char *blob;
  int bit_length = 0;

  /* the last committed snapshot, read without a lock like the histograms (CBRD-27369) */
  if (db_get_histogram_committed (classop, ROW_SNAPSHOT_KEY_ATTR, &row) != NO_ERROR || row == NULL)
    {
      er_clear ();
      return NULL;
    }

  db_make_null (&blob_value);
  if (db_get (row, "histogram_values", &blob_value) != NO_ERROR || DB_IS_NULL (&blob_value))
    {
      db_value_clear (&blob_value);
      er_clear ();
      return NULL;
    }
  blob = db_get_bit (&blob_value, &bit_length);
  if (blob != NULL && bit_length > 0)
    {
      snapshot = row_snapshot_decode (blob, bit_length / 8);
    }
  db_value_clear (&blob_value);

  if (snapshot != NULL)
    {
      row_snapshot_check_columns (snapshot, class_);
    }
  return snapshot;
}

const ROW_SNAPSHOT *
row_snapshot_get (MOP classop, SM_CLASS *class_)
{
  HIST_STATS *histogram;

  if (row_snapshot_max_rows () <= 0 || classop == NULL || class_ == NULL
      || (class_->flags & SM_CLASSFLAG_ROW_SNAPSHOT) == 0)
    {
      return NULL;
    }

  if (class_->histogram == NULL)
    {
      /* the optimizer loads the histograms with the class statistics; a caller outside it
       * (the bind fingerprint) may come first */
      if (stats_get_histogram (classop, &class_->histogram) != NO_ERROR)
	{
	  stats_free_histogram_and_init (class_->histogram);
	  class_->histogram = NULL;
	  er_clear ();
	  return NULL;
	}
    }
  histogram = class_->histogram;
  if (histogram == NULL)
    {
      return NULL;
    }

  /* looked up once per load of the statistics: a new UPDATE STATISTICS reloads them, and with
   * them the snapshot */
  if (!histogram->row_snapshot_loaded)
    {
      histogram->row_snapshot_loaded = true;
      histogram->row_snapshot = row_snapshot_load (classop, class_);
    }
  return histogram->row_snapshot;
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
  if (filter == NULL)
    {
      return true;
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

  if (filter == NULL)
    {
      /* the shape only: a host variable may not be bound yet */
      switch (pred->info.expr.op)
	{
	case PT_EQ:
	  return row_snapshot_const_shape (rhs);
	case PT_RANGE:
	  for (elem = rhs; elem != NULL; elem = elem->or_next)
	    {
	      if (!row_snapshot_const_shape (elem->info.expr.arg1))
		{
		  return false;
		}
	    }
	  return true;
	default:
	  if (in_set == NULL)
	    {
	      for (elem = rhs->info.function.arg_list; elem != NULL; elem = elem->next)
		{
		  if (!row_snapshot_const_shape (elem))
		    {
		      return false;
		    }
		}
	    }
	  return true;
	}
    }

  /* malloc'd, not parser memory: the bind fingerprint prepares filters on every check of a kept
   * statement, whose parser lives as long as the statement */
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

int
row_snapshot_survivors (const ROW_SNAPSHOT *snapshot, const ROW_SNAPSHOT_FILTER *filters, int n_filters, int *rows)
{
  int k = 0;

  for (int r = 0; r < snapshot->n_rows; r++)
    {
      bool survives = true;

      for (int f = 0; f < n_filters && survives; f++)
	{
	  const DB_VALUE *row_value = &snapshot->values[r * snapshot->n_attrs + filters[f].column];
	  bool matched = false;

	  if (DB_IS_NULL (row_value))
	    {
	      /* NULL = anything is not true */
	      survives = false;
	      break;
	    }
	  /* the comparison the executor makes: coercion allowed, so CHAR padding and numeric
	   * promotion agree with the result of the query */
	  for (int v = 0; v < filters[f].n_values && !matched; v++)
	    {
	      matched = (tp_value_compare (row_value, filters[f].values[v], 1, 1) == DB_EQ);
	    }
	  survives = matched;
	}
      if (survives)
	{
	  rows[k++] = r;
	}
    }
  return k;
}

bool
row_snapshot_join_selectivity (const ROW_SNAPSHOT *snapshot, const int *rows, int k, int column, PT_NODE *other,
			       double nf_other, double *selectivity)
{
  double sum = 0.0, sel;
  bool success = false;
  std::vector<DB_VALUE *> keys;

  if (k <= 0 || column < 0)
    {
      return false;
    }

  /* a NULL key joins no row of the other side: it stays in k and adds 0 (the sum skips it) */
  keys.reserve (k);
  for (int i = 0; i < k; i++)
    {
      keys.push_back (&snapshot->values[rows[i] * snapshot->n_attrs + column]);
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
