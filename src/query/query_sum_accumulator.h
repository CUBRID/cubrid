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
 * query_sum_accumulator.h - running-sum accumulator for aggregate and
 *                           analytic SUM/AVG
 *
 * This header defines only the accumulator container, allowing XASL
 * accumulator structs to embed it without pulling in arithmetic code.
 * Word-mode arithmetic is implemented in numeric_opfunc.c (numeric_sum_acc_*),
 * while type dispatch and typed modes are implemented in query_opfunc.c
 * (qdata_sum_acc_*).
 */

#ifndef _QUERY_SUM_ACCUMULATOR_H_
#define _QUERY_SUM_ACCUMULATOR_H_

#ident "$Id$"

#include "dbtype_def.h"

#include <stdint.h>

/*
 * SUM_ACC: accumulator for aggregate/analytic SUM/AVG.
 *
 * NUMERIC accumulates raw words and defers digit counting, overflow checking,
 * rounding, and DB_VALUE packing until finalize, so rounding occurs exactly
 * once per group.
 *
 * SHORT/INTEGER/BIGINT/DOUBLE use a typed mode. The running sum is kept in
 * v.int_sum or v.dbl_sum, and sum_type records its accumulation type. Typed
 * adds preserve the per-row input-type range semantics (for example,
 * SUM(SHORT) overflows past 32767); INTEGER additions are exact and DOUBLE
 * uses the same IEEE operations in the same order. The optimization removes
 * per-row DB_VALUE dispatch, not the arithmetic.
 *
 * FLOAT uses sum_type DOUBLE, matching the per-row accumulation domain. A FLOAT
 * sum may exceed FLT_MAX during accumulation; only the final demotion to FLOAT
 * can raise ER_IT_DATA_OVERFLOW.
 *
 * sum_type records the DB_TYPE used for accumulation and discriminates the
 * union. It is valid only while is_active is set.
 */
#define SUM_ACC_NUMERIC_WORDS  (3)	/* the native NUMERIC width, keeping the running sum exact up to 57 digits */

typedef struct sum_acc SUM_ACC;
struct sum_acc
{
  union
  {
    int64_t int_sum;		/* SHORT/INTEGER/BIGINT */
    double dbl_sum;		/* FLOAT/DOUBLE */
    uint64_t words[SUM_ACC_NUMERIC_WORDS];	/* sign-magnitude coefficient; big-endian, last word is the LSW */
  } v;

  /* NUMERIC mode */
  int scale;
  bool is_negative;		/* the typed sums carry their own sign */

  /* common */
  bool is_active;		/* false until the first value */
  DB_TYPE sum_type;		/* accumulation domain; valid only while is_active */
};

/* These tests define the accumulator's core contract and are used directly by
 * qdata_sum_acc_start/add_dbv/accumulate. Aggregate and analytic entry points
 * apply their own policies on top and test those predicates instead.
 */

/* Input types the accumulator supports; everything else stays on the per-row add. */
#define SUM_ACC_IS_SUPPORTED_TYPE(t) \
  ((t) == DB_TYPE_NUMERIC || (t) == DB_TYPE_INTEGER || (t) == DB_TYPE_BIGINT \
   || (t) == DB_TYPE_SHORT || (t) == DB_TYPE_DOUBLE || (t) == DB_TYPE_FLOAT)

/* The sum_type used for each input type: NUMERIC keeps word mode, FLOAT widens
 * to DOUBLE (the per-row accumulation domain), and other typed inputs keep their
 * own type; DB_TYPE_NULL = unsupported input. */
static inline DB_TYPE
sum_acc_sum_type_for (DB_TYPE t)
{
  switch (t)
    {
    case DB_TYPE_NUMERIC:
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DOUBLE:
      return t;
    case DB_TYPE_FLOAT:
      return DB_TYPE_DOUBLE;
    default:
      return DB_TYPE_NULL;
    }
}

/* Aggregate entry policy: accepts everything the accumulator supports. */
#define SUM_ACC_IS_AGG_SUPPORTED_TYPE(t) SUM_ACC_IS_SUPPORTED_TYPE (t)

static inline DB_TYPE
sum_acc_agg_sum_type_for (DB_TYPE t)
{
  return sum_acc_sum_type_for (t);
}

/* Analytic entry policy: FLOAT is excluded because the per-row analytic path
 * accumulates in FLOAT, rounding after each add; DOUBLE accumulation cannot
 * reproduce those semantics.
 */
#define SUM_ACC_IS_ANALYTIC_SUPPORTED_TYPE(t) \
  ((t) == DB_TYPE_NUMERIC || (t) == DB_TYPE_INTEGER || (t) == DB_TYPE_BIGINT \
   || (t) == DB_TYPE_SHORT || (t) == DB_TYPE_DOUBLE)

static inline DB_TYPE
sum_acc_analytic_sum_type_for (DB_TYPE t)
{
  switch (t)
    {
    case DB_TYPE_NUMERIC:
    case DB_TYPE_SHORT:
    case DB_TYPE_INTEGER:
    case DB_TYPE_BIGINT:
    case DB_TYPE_DOUBLE:
      return t;
    default:
      return DB_TYPE_NULL;
    }
}

#endif /* _QUERY_SUM_ACCUMULATOR_H_ */
