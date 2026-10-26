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
 * histogram_bucketizer.hpp - build equi-depth histogram buckets from a sample
 *
 *  Buckets the non-MCV values of a column. The caller removes MCVs first (see
 *  histogram_sampler_sr); this routine takes the remaining distinct (value, count)
 *  pairs, walks them in ascending value order, and packs them into buckets of
 *  roughly equal row count (cap = ceil (non_mcv_rows / max_buckets)).
 *
 *  Each emitted bucket records:
 *      endpoint   - largest value in the bucket
 *      rows       - total rows in the bucket
 *      approx_ndv - distinct values in the bucket
 *      cumulative - running row total through this bucket
 *
 *  All counts are sample-relative; the caller scales them to the population.
 */

#ifndef _HISTOGRAM_BUCKETIZER_HPP_
#define _HISTOGRAM_BUCKETIZER_HPP_

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace hist
{
  template <typename V>
  struct sample_bucket
  {
    V endpoint;			/* upper bound value of the bucket */
    std::int64_t rows_in_bucket;
    std::int64_t cumulative;	/* running sum of rows_in_bucket over endpoint order */
    std::int64_t approx_ndv;	/* distinct values represented by the bucket (sample) */
    std::int64_t f1;		/* distinct values that occur exactly once in the bucket (sample singletons) */
  };

  /*
   * bucketize_sample () - build equi-depth histogram buckets from distinct (value, count) pairs.
   *   value_counts(in): distinct NON-MCV values with their observed counts (any order)
   *   max_buckets(in)  : target number of equi-depth buckets
   *   return           : buckets ordered by endpoint, with cumulative filled in
   */
  template <typename V>
  std::vector<sample_bucket<V>>
			     bucketize_sample (std::vector<std::pair<V, std::int64_t>> value_counts, int max_buckets)
  {
    std::vector<sample_bucket<V>> result;
    if (value_counts.empty ())
      {
	return result;
      }
    if (max_buckets < 1)
      {
	max_buckets = 1;
      }

    /* canonical order: by value ascending */
    std::sort (value_counts.begin (), value_counts.end (),
	       [] (const std::pair<V, std::int64_t> &a, const std::pair<V, std::int64_t> &b)
    {
      return a.first < b.first;
    });

    const std::size_t n = value_counts.size ();

    /* total rows -> equi-depth capacity */
    std::int64_t total_rows = 0;
    for (std::size_t i = 0; i < n; i++)
      {
	total_rows += value_counts[i].second;
      }
    std::int64_t cap = 1;
    if (total_rows > 0)
      {
	cap = (total_rows + max_buckets - 1) / max_buckets;	/* ceil */
	if (cap < 1)
	  {
	    cap = 1;
	  }
      }

    /* walk values ascending; equi-depth bucket by running count using `cap`. */
    std::int64_t running_cum = 0;	/* running count over all values */
    std::int64_t cur_bid = -1;
    bool bucket_open = false;
    sample_bucket<V> cur{};

    auto flush_bucket = [&result, &cur, &bucket_open] ()
    {
      if (bucket_open)
	{
	  result.push_back (cur);
	  bucket_open = false;
	}
    };

    for (std::size_t i = 0; i < n; i++)
      {
	running_cum += value_counts[i].second;
	std::int64_t bid = (running_cum - 1) / cap;	/* floor */

	if (!bucket_open || bid != cur_bid)
	  {
	    flush_bucket ();
	    cur = sample_bucket<V> {};
	    cur.endpoint = value_counts[i].first;
	    cur.rows_in_bucket = value_counts[i].second;
	    cur.approx_ndv = 1;
	    cur.f1 = (value_counts[i].second == 1) ? 1 : 0;
	    cur_bid = bid;
	    bucket_open = true;
	  }
	else
	  {
	    cur.endpoint = value_counts[i].first;	/* MAX(val) within bucket (ascending) */
	    cur.rows_in_bucket += value_counts[i].second;
	    cur.approx_ndv += 1;
	    cur.f1 += (value_counts[i].second == 1) ? 1 : 0;
	  }
      }
    flush_bucket ();

    /* order by endpoint, fill cumulative */
    std::sort (result.begin (), result.end (),
	       [] (const sample_bucket<V> &a, const sample_bucket<V> &b)
    {
      return a.endpoint < b.endpoint;
    });
    std::int64_t running = 0;
    for (auto &b : result)
      {
	running += b.rows_in_bucket;
	b.cumulative = running;
      }
    return result;
  }

} // namespace hist

#endif // _HISTOGRAM_BUCKETIZER_HPP_
