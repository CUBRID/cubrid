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
 * statistics_ndv.c - NDV (number of distinct values) estimation from a sample
 *
 * Dedicated NDV path (CBRD-26667). This is a pure, query-independent estimator: given
 * the distinct / singleton counts of a uniform sample plus an expansion weight, it
 * extrapolates the population NDV. It is intentionally separated from the query domain;
 * callers feed it sample statistics gathered by a dedicated server-side scan, never by
 * executing SQL.
 */

#ident "$Id$"

#include "config.h"

#include "statistics.h"

#include <math.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * stats_estimate_ndv_from_sample () - estimate population NDV from a sample
 *
 *   return: estimated NDV for non-null values (>= 1)
 *
 * Notation:
 *   n      - non-null rows in the sample (sample_rows - sample_nulls)
 *   d      - distinct non-null values in the sample
 *   f1     - values that appear exactly once in the sample
 *   N_nn   - estimated non-null rows in the table (sample_rows * weight * (1 - null_frac))
 *
 * Estimation pipeline:
 *   1. Full scan / sample already covers the table -> exact distinct count.
 *   2. No repeated value (all singletons) -> unique-like, return N_nn.
 *   3. No singleton -> fixed-domain-like, return observed d.
 *   4. Otherwise duplicate-aware estimator:  n * d / ((n - f1) + f1 * n / N_nn)
 *   5. Clamp to [d, N_nn].
 */
INT64
stats_estimate_ndv_from_sample (const STATS_NDV_SAMPLE_INPUT * in)
{
  double n;
  double d;
  double f1;
  double nmultiple;
  double total_rows;
  double null_frac;
  double n_nn_total;
  double est;
  INT64 ndv;
  int weight;

  if (in == NULL)
    {
      return 1;
    }

  weight = (in->sampling_weight > 0) ? in->sampling_weight : 1;

  if (in->sample_rows <= 0)
    {
      return 1;
    }

  /* Estimate table rows before null adjustment. */
  total_rows = (double) in->sample_rows * (double) weight;
  if (total_rows < 1.0)
    {
      total_rows = 1.0;
    }

  null_frac = (double) in->sample_nulls / (double) in->sample_rows;
  if (null_frac < 0.0)
    {
      null_frac = 0.0;
    }
  else if (null_frac > 1.0)
    {
      null_frac = 1.0;
    }

  n_nn_total = total_rows * (1.0 - null_frac);

  /* full reservoir scan knows the exact population non-null row count; prefer it over
   * the lossy integer sampling_weight reconstruction (which over-estimates unique-like
   * columns, e.g. 30000 * round(200000/30000) = 210000 != 200000). */
  if (in->total_nn_rows > 0)
    {
      n_nn_total = (double) in->total_nn_rows;
    }

  n = (double) (in->sample_rows - in->sample_nulls);
  d = (double) in->sample_distinct;
  f1 = (double) in->sample_singleton;

  if (n <= 0.0 || d <= 0.0 || n_nn_total <= 0.0)
    {
      return 1;
    }

  /* sanitize inconsistent input */
  if (d > n)
    {
      d = n;
    }
  if (f1 < 0.0)
    {
      f1 = 0.0;
    }
  else if (f1 > d)
    {
      f1 = d;
    }

  /* full scan, or the sample already covers the estimated non-null table rows */
  if (weight <= 1 && d >= n_nn_total - 0.5)
    {
      ndv = (INT64) floor (n_nn_total + 0.5);
      return (ndv < 1) ? 1 : ndv;
    }

  nmultiple = d - f1;

  /* no repeated non-null value in the sample -> unique-like */
  if (nmultiple <= 0.0)
    {
      ndv = (INT64) floor (n_nn_total + 0.5);
      return (ndv < 1) ? 1 : ndv;
    }

  /* every distinct value appeared at least twice -> use observed d */
  if (f1 <= 0.0)
    {
      ndv = (INT64) floor (d + 0.5);
      if (ndv < 1)
	{
	  ndv = 1;
	}
      if (ndv > (INT64) floor (n_nn_total + 0.5))
	{
	  ndv = (INT64) floor (n_nn_total + 0.5);
	}
      return ndv;
    }

  /* duplicate-aware NDV extrapolation */
  est = (n * d) / ((n - f1) + (f1 * n / n_nn_total));

  /* clamp to sane bounds */
  if (est < d)
    {
      est = d;
    }
  if (est > n_nn_total)
    {
      est = n_nn_total;
    }

  ndv = (INT64) floor (est + 0.5);
  if (ndv < 1)
    {
      ndv = 1;
    }

  return ndv;
}

/*
 * stats_analyze_mcv_list () - decide how many candidate values qualify as MCVs
 *
 *   return: number of leading candidates worth keeping in the MCV list (>= 0)
 *
 * mcv_counts holds the sample counts of the most common values, sorted descending
 * (most common first). A value is kept only when its sample count is significantly
 * higher than the selectivity it would otherwise receive as a non-MCV value, using
 * the lower end of a continuity-corrected Wald-type confidence interval for the
 * hypergeometric distribution (sampling without replacement).
 *
 * Mapping for the full reservoir scan (analysis done in non-null space):
 *   stadistinct -> population non-null NDV (D_pop), always > 0 here
 *   stanullfrac -> 0.0 (nulls excluded from the candidate space)
 *   samplerows  -> reservoir non-null size
 *   totalrows   -> exact population non-null rows (N_nn)
 */
int
stats_analyze_mcv_list (const INT64 * mcv_counts, int num_candidates, double stadistinct,
			double stanullfrac, INT64 samplerows, double totalrows)
{
  double ndistinct_table;
  double sumcount;
  int num_mcv;
  int i;

  if (mcv_counts == NULL || num_candidates <= 0)
    {
      return 0;
    }

  num_mcv = num_candidates;

  /* If the entire table was sampled, keep the whole list. This also protects us
   * against division by zero in the code below. */
  if ((double) samplerows >= totalrows || totalrows <= 1.0)
    {
      return num_mcv;
    }

  /* Estimated number of distinct nonnull values in the table. A negative
   * stadistinct as a fraction of totalrows; our D_pop is always a positive absolute
   * count, but keep the decode for parity. */
  ndistinct_table = stadistinct;
  if (ndistinct_table < 0)
    {
      ndistinct_table = -ndistinct_table * totalrows;
    }

  /* sumcount tracks the total count of all but the last (least common) value. */
  sumcount = 0.0;
  for (i = 0; i < num_mcv - 1; i++)
    {
      sumcount += (double) mcv_counts[i];
    }

  while (num_mcv > 0)
    {
      double selec, otherdistinct, N, n, K, variance, stddev;

      /* Estimated selectivity the least common value would have if it weren't in
       * the MCV list (c.f. eqsel()). */
      selec = 1.0 - sumcount / (double) samplerows - stanullfrac;
      if (selec < 0.0)
	{
	  selec = 0.0;
	}
      if (selec > 1.0)
	{
	  selec = 1.0;
	}
      otherdistinct = ndistinct_table - (num_mcv - 1);
      if (otherdistinct > 1)
	{
	  selec /= otherdistinct;
	}

      /* Lower end of a continuity-corrected Wald-type confidence interval for the
       * hypergeometric distribution (sampling without replacement). */
      N = totalrows;
      n = (double) samplerows;
      K = N * (double) mcv_counts[num_mcv - 1] / n;
      variance = n * K * (N - K) * (N - n) / (N * N * (N - 1));
      stddev = sqrt (variance);

      if ((double) mcv_counts[num_mcv - 1] > selec * (double) samplerows + 2 * stddev + 0.5)
	{
	  /* Significantly more common than the non-MCV selectivity would suggest.
	   * Keep it and all the more common values. */
	  break;
	}
      else
	{
	  /* Discard this value and consider the next least common value. */
	  num_mcv--;
	  if (num_mcv == 0)
	    {
	      break;
	    }
	  sumcount -= (double) mcv_counts[num_mcv - 1];
	}
    }

  return num_mcv;
}
