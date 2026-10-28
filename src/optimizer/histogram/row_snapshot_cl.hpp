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
 * row_snapshot_cl.hpp - rows of code tables kept with the statistics (CBRD-27478)
 *
 * Histograms are kept per column, so a constant filter on one column of a code table tells the
 * optimizer how many rows survive but not WHICH keys they carry: `it.info = 'release dates'
 * AND it.id = mi.info_type_id` is priced at 1/NDV of mi.info_type_id whatever id 'release dates'
 * has. For a table declared ROW_SNAPSHOT (CREATE/ALTER TABLE), UPDATE STATISTICS also keeps its
 * rows -- all of them, and none when there are more than statistics_row_snapshot_rows --
 * as one more _db_histogram row of the class under a reserved key_attr. The declaration is a class
 * flag (SM_CLASSFLAG_ROW_SNAPSHOT), so it travels with the schema (SHOW CREATE TABLE, unloaddb,
 * HA); the rows are a statistic. The optimizer applies the constant filters of such a node to the
 * rows and prices each equi-join of the node by the surviving keys, through the other column's
 * histogram.
 *
 * The snapshot is a statistic like the histograms: it is refreshed by the next UPDATE STATISTICS
 * and is not invalidated by DML. A constant that matches no row of it (a code added since)
 * leaves the estimate where the histograms put it.
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

/* the key_attr the snapshot is stored under in _db_histogram. A column name cannot contain a
 * dot, so no column's histogram can take this row. */
#define ROW_SNAPSHOT_KEY_ATTR ".row_snapshot"

/* the largest snapshot kept, packed; a table whose rows take more keeps none */
#define ROW_SNAPSHOT_MAX_BYTES (4 * 1024 * 1024)
/* the most rows a snapshot can hold: the upper bound of statistics_row_snapshot_rows */
#define ROW_SNAPSHOT_MAX_ROWS 10000

/* the rows of a table, as the statistics last saw them */
struct row_snapshot
{
  int n_attrs;			/* columns kept */
  int n_rows;			/* rows kept: all of the table's */
  int table_rows;		/* rows of the table when they were collected */
  int *attr_ids;		/* column id per slot; -1 = a column that no longer matches the class */
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
};
typedef struct row_snapshot_filter ROW_SNAPSHOT_FILTER;

/* statistics_row_snapshot_rows; 0 = the feature is off */
extern int row_snapshot_max_rows (void);

/* UPDATE STATISTICS: when classop is declared ROW_SNAPSHOT, keep all of its rows, or none when it
 * has more than statistics_row_snapshot_rows of them or they take more than ROW_SNAPSHOT_MAX_BYTES.
 * Does nothing for other classes or while the feature is off. A failure to read the rows only
 * leaves the class without a snapshot; a failure to write the catalog is returned. */
extern int row_snapshot_collect_and_store (MOP classop, INT64 total_rows, bool with_fullscan);
/* remove the snapshot of classop, if any (NO_ROW_SNAPSHOT, DROP HISTOGRAM) */
extern int row_snapshot_drop (MOP classop);

/* the snapshot of a class for planning, loaded once into its statistics cache; NULL when the
 * feature is off, the class is not declared ROW_SNAPSHOT or has no rows kept yet */
extern const ROW_SNAPSHOT *row_snapshot_get (MOP classop, SM_CLASS *class_);
extern void row_snapshot_free (ROW_SNAPSHOT *snapshot);

/* the slot of a column in the snapshot, -1 when it is not kept */
extern int row_snapshot_column (const ROW_SNAPSHOT *snapshot, MOP classop, const char *attr_name);

/* the constant a filter compares against: a literal, an auto-parameterized literal or a bound
 * host variable. NULL when the node is none of those, or its value is NULL or not bound. */
extern DB_VALUE *row_snapshot_const_value (PARSER_CONTEXT *parser, PT_NODE *node);

/* resolve pred, a predicate over the spec spec_id only, into a filter on the snapshot. Accepts
 * `col = c`, `col IN (c1, c2, ...)` and the rewritten `col RANGE (c1 =, c2 =, ...)`; false for
 * anything else, which the caller takes as "the node cannot be evaluated". With filter NULL only
 * the shape is checked, so a host variable that is not bound yet passes. */
extern bool row_snapshot_filter_prepare (PARSER_CONTEXT *parser, const ROW_SNAPSHOT *snapshot, MOP classop,
    UINTPTR spec_id, PT_NODE *pred, ROW_SNAPSHOT_FILTER *filter);

/* release what row_snapshot_filter_prepare () allocated; a zeroed filter is fine */
extern void row_snapshot_filter_clear (ROW_SNAPSHOT_FILTER *filter);

/* the rows that satisfy every filter: their indexes go to rows (sized n_rows), the count is
 * returned */
extern int row_snapshot_survivors (const ROW_SNAPSHOT *snapshot, const ROW_SNAPSHOT_FILTER *filters,
				   int n_filters, int *rows);

/* selectivity of `snapshot.column = other` over the cross product, given the k surviving rows:
 * (1 / k) * SUM P (other = v_i) * (1 - nf_other). false when a key cannot be priced. */
extern bool row_snapshot_join_selectivity (const ROW_SNAPSHOT *snapshot, const int *rows, int k, int column,
    PT_NODE *other, double nf_other, double *selectivity);

#endif /* _ROW_SNAPSHOT_CL_HPP_ */
