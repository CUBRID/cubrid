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
 * row_snapshot_cl.hpp - rows of code tables read while planning (CBRD-27478)
 *
 * Histograms are kept per column, so a constant filter on one column of a code table tells the
 * optimizer how many rows survive but not WHICH keys they carry: `it.info = 'release dates'
 * AND it.id = mi.info_type_id` is priced at 1/NDV of mi.info_type_id whatever id 'release dates'
 * has. For a node joined by an equality, the optimizer reads the rows its constant filters
 * select from the table itself while it plans, and prices each equi-join of the node by those
 * rows' keys, through the other column's histogram. Nothing is stored.
 *
 * The read goes through an index (a filter column has to lead one) and stops past
 * row_snapshot_max_rows rows passing every filter; it is not made when the statistics expect the
 * index to give more than ROW_SNAPSHOT_SCAN_RATIO times that many (an indexed filter that selects
 * many rows beside a selective one without an index), so its cost does not grow with the table.
 * A filter that selects more rows keeps the histogram estimate. It does not wait for a lock: a
 * class another transaction has locked keeps the estimate, and the compile neither waits nor
 * deadlocks where it took only SCH_S before. The rows are the ones the statement's transaction
 * sees when it is compiled; a plan already cached is not compiled again for new rows, as it is
 * not for new statistics, nor for another literal of the same statement text.
 *
 * It is not made under REPEATABLE READ or above, where it would take the transaction's snapshot at
 * compile, nor for a statement compiled outside the XASL cache (a trigger action, a statement that
 * cannot be prepared), which would read again at every execution. LEADING / USE_* hints that name
 * no table, or a plan the hints cannot give, drop ROW_SNAPSHOT / NO_ROW_SNAPSHOT with the others.
 */

#ifndef _ROW_SNAPSHOT_CL_HPP_
#define _ROW_SNAPSHOT_CL_HPP_

#include "system.h"
#include "dbtype_def.h"

struct parser_node;
typedef struct parser_node PT_NODE;
struct parser_context;
typedef struct parser_context PARSER_CONTEXT;
struct sm_class;
typedef struct sm_class SM_CLASS;

/* the rows of a table a node's constant filters select; with no rows, only the columns
 * (row_snapshot_columns ()) */
struct row_snapshot
{
  int n_attrs;			/* columns read */
  int n_rows;			/* rows read */
  int *attr_ids;		/* column id per slot */
  DB_VALUE *values;		/* n_rows x n_attrs, row-major */
};
typedef struct row_snapshot ROW_SNAPSHOT;

/* one constant filter of a snapshot node, resolved to its column: the row has to equal one of
 * the values */
struct row_snapshot_filter
{
  int column;			/* slot in ROW_SNAPSHOT.attr_ids */
  int n_values;
  DB_VALUE **values;		/* malloc'd; the values themselves stay owned by the statement */
  DB_VALUE *elements;		/* malloc'd peeks of the elements of a folded IN set, or NULL */
  double selectivity;		/* the optimizer's estimate of the share of rows it passes */
};
typedef struct row_snapshot_filter ROW_SNAPSHOT_FILTER;

/* row_snapshot_max_rows; 0 (the default) = read only for a statement hinted ROW_SNAPSHOT */
extern int row_snapshot_max_rows (void);
/* the limit of a statement hinted ROW_SNAPSHOT while row_snapshot_max_rows is 0 */
#define ROW_SNAPSHOT_HINT_MAX_ROWS 100
/* a statement hinted ROW_SNAPSHOT also reads a class without an index on the filter column when
 * the statistics count at most this many rows: the hint takes them to be current */
#define ROW_SNAPSHOT_HINT_SCAN_ROWS 1000
/* a read is not made when the statistics expect its index range to hold more than this many times
 * max_rows rows */
#define ROW_SNAPSHOT_SCAN_RATIO 10

/* the columns of a user class that a read can return, with no rows: what the filters resolve
 * their columns against. NULL for a system class or a class with none. */
extern ROW_SNAPSHOT *row_snapshot_columns (SM_CLASS *class_);

/* read the rows of classop that pass every filter, at most max_rows of them; n_card is the rows
 * the statistics count. NULL when no filter column leads an index of the class (the read would
 * scan the table) and scan is false, when the rows the read would go through are expected past
 * ROW_SNAPSHOT_SCAN_RATIO * max_rows, when more rows pass, or on any failure; the error and the
 * plan dump of the statement being planned are left as they were. *error is NO_ERROR then, except
 * when the transaction was aborted or the server is gone: that error is left set for the caller
 * to fail the statement with. */
extern ROW_SNAPSHOT *row_snapshot_read (MOP classop, SM_CLASS *class_, const ROW_SNAPSHOT *columns,
					const ROW_SNAPSHOT_FILTER *filters, int n_filters, int max_rows, bool scan,
					double n_card, int *error);

extern void row_snapshot_free (ROW_SNAPSHOT *snapshot);

/* the slot of a column in the snapshot, -1 when it is not read */
extern int row_snapshot_column (const ROW_SNAPSHOT *snapshot, MOP classop, const char *attr_name);

/* the constant a filter compares against: a literal, an auto-parameterized literal or a bound
 * host variable. NULL when the node is none of those, or its value is NULL or not bound. */
extern DB_VALUE *row_snapshot_const_value (PARSER_CONTEXT *parser, PT_NODE *node);

/* resolve pred, a predicate over the spec spec_id only, into a filter on the snapshot's columns.
 * Accepts `col = c`, `col IN (c1, c2, ...)`, the rewritten `col RANGE (c1 =, c2 =, ...)` and an
 * OR of one column's equalities; false for anything else, which the caller takes as "the node
 * cannot be evaluated". */
extern bool row_snapshot_filter_prepare (PARSER_CONTEXT *parser, const ROW_SNAPSHOT *snapshot, MOP classop,
    UINTPTR spec_id, PT_NODE *pred, ROW_SNAPSHOT_FILTER *filter);

/* release what row_snapshot_filter_prepare () allocated; a zeroed filter is fine */
extern void row_snapshot_filter_clear (ROW_SNAPSHOT_FILTER *filter);

/* selectivity of `snapshot.column = other` over the cross product, given the k = n_rows rows read:
 * (1 / k) * SUM P (other = v_i) * (1 - nf_other). false when a key cannot be priced. */
extern bool row_snapshot_join_selectivity (const ROW_SNAPSHOT *snapshot, int column, PT_NODE *other, double nf_other,
    double *selectivity);

#endif /* _ROW_SNAPSHOT_CL_HPP_ */
