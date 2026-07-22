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
 * (most common first). A value qualifies as an MCV when it occurs in at least 1% of
 * the sampled (non-null) rows; since the candidates are sorted by descending count,
 * the MCVs are the leading run at or above that threshold. The remaining values are
 * left to the equi-depth histogram.
 *
 *   samplerows -> reservoir non-null size (the 1% threshold is taken against this)
 *   stadistinct / stanullfrac / totalrows are unused (kept for signature stability).
 */
int
stats_analyze_mcv_list (const INT64 * mcv_counts, int num_candidates, double stadistinct,
			double stanullfrac, INT64 samplerows, double totalrows)
{
  int num_mcv = 0;
  double threshold;
  int i;

  /* unused; kept for signature stability with the previous statistical criterion */
  (void) stadistinct;
  (void) stanullfrac;
  (void) totalrows;

  if (mcv_counts == NULL || num_candidates <= 0 || samplerows <= 0)
    {
      return 0;
    }

  /* an MCV must occur in at least 1% of the sampled non-null rows */
  threshold = 0.01 * (double) samplerows;
  for (i = 0; i < num_candidates; i++)
    {
      if ((double) mcv_counts[i] < threshold)
	{
	  break;
	}
      num_mcv++;
    }

  return num_mcv;
}
