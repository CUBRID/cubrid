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

#include "xasl_iteration.hpp"
#include <iostream>
#include <sstream>
#include "error_manager.h"
#include "tsc_timer.h"
#include "memoize.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

extern "C" {
  XASL_NODE *xasl_find_by_id (XASL_NODE *xasl, int target_id)
  {
    if (xasl == nullptr)
      {
	return nullptr;
      }
    std::function<XASL_NODE* (XASL_NODE *)> find_by_id = [target_id] (XASL_NODE* node) -> XASL_NODE*
    {
      if (node->header.id == target_id)
	{
	  return node;
	}
      return nullptr;
    };

    return cubxasl::iterate_xasl_tree<XASL_NODE *> (xasl,find_by_id,nullptr);
  }

  void xasl_dump_with_id (XASL_NODE *xasl)
  {
    const char *xasl_type_string[] =
    {
      "UNION_PROC",
      "DIFFERENCE_PROC",
      "INTERSECTION_PROC",
      "OBJFETCH_PROC",
      "BUILDLIST_PROC",
      "BUILDVALUE_PROC",
      "SCAN_PROC",
      "MERGELIST_PROC",
      "HASHJOIN_PROC",
      "UPDATE_PROC",
      "DELETE_PROC",
      "INSERT_PROC",
      "CONNECTBY_PROC",
      "DO_PROC",
      "MERGE_PROC",
      "BUILD_SCHEMA_PROC",
      "CTE_PROC"
    };

    std::ostringstream oss;
    std::function<bool (XASL_NODE *)> dump_with_id = [xasl_type_string, &oss] (XASL_NODE* node) -> bool
    {
      if (XASL_IS_FLAGED (node, XASL_TOP_MOST_XASL))
	{
	  oss << "QUERY : " << node->query_alias << std::endl;
	}
      oss << "XASL : " << node << " id: " << node->header.id << " type: " << xasl_type_string[node->type] << std::endl;
      return true;
    };
    cubxasl::iterate_xasl_tree<bool> (xasl,dump_with_id,true);

    std::string result = oss.str();
    // the dump holds the query text, which can carry printf conversions such as %n: never pass it as the format
    _er_log_debug (ARG_FILE_LINE, "%s", result.c_str());
  }

  /* the statistics of one partition of a clone's scan into the main thread's (xasl_merge_stats ()) */
  static void xasl_merge_partition_stats (SCAN_STATS *dst, const SCAN_STATS *src)
  {
    if (timercmp (&src->elapsed_scan, &dst->elapsed_scan, >))
      {
	dst->elapsed_scan = src->elapsed_scan;
      }
    dst->num_fetches += src->num_fetches;
    dst->num_ioreads += src->num_ioreads;
    dst->read_rows += src->read_rows;
    dst->qualified_rows += src->qualified_rows;
    dst->read_keys += src->read_keys;
    dst->qualified_keys += src->qualified_keys;
    dst->key_qualified_rows += src->key_qualified_rows;
    dst->data_qualified_rows += src->data_qualified_rows;
    if (timercmp (&src->elapsed_lookup, &dst->elapsed_lookup, >))
      {
	dst->elapsed_lookup = src->elapsed_lookup;
      }
    dst->covered_index = dst->covered_index ? true : src->covered_index;
    dst->multi_range_opt = dst->multi_range_opt ? true : src->multi_range_opt;
    dst->index_skip_scan = dst->index_skip_scan ? true : src->index_skip_scan;
    dst->loose_index_scan = dst->loose_index_scan ? true : src->loose_index_scan;
    dst->noscan = dst->noscan ? src->noscan : false;
    dst->agl = NULL;
  }

  void xasl_merge_stats (XASL_NODE *src, XASL_NODE *dst)
  {
    std::function<bool (XASL_NODE *)> merge_stats = [dst] (XASL_NODE* src_node) -> bool
    {
      XASL_NODE *dst_node = xasl_find_by_id (dst, src_node->header.id);
      if (dst_node == nullptr)
	{
	  return true;
	}

      if (timercmp (&src_node->xasl_stats.elapsed_time, &dst_node->xasl_stats.elapsed_time, >))
	{
	  dst_node->xasl_stats.elapsed_time = src_node->xasl_stats.elapsed_time;
	}
      dst_node->xasl_stats.fetch_time += src_node->xasl_stats.fetch_time;
      dst_node->xasl_stats.ioreads += src_node->xasl_stats.ioreads;
      dst_node->xasl_stats.fetches += src_node->xasl_stats.fetches;

      /* HS_NONE < HS_ACCEPT_ALL < HS_REJECT_ALL: one clone that gave up hashing keeps the GROUP BY partial */
      if (src_node->groupby_stats.groupby_hash > dst_node->groupby_stats.groupby_hash)
	{
	  dst_node->groupby_stats.groupby_hash = src_node->groupby_stats.groupby_hash;
	}
      dst_node->groupby_stats.groupby_sort = dst_node->groupby_stats.groupby_sort ? true : src_node->groupby_stats.groupby_sort;
      dst_node->groupby_stats.run_groupby = dst_node->groupby_stats.run_groupby ? true : src_node->groupby_stats.run_groupby;
      dst_node->groupby_stats.groupby_pages += src_node->groupby_stats.groupby_pages;
      dst_node->groupby_stats.groupby_ioreads += src_node->groupby_stats.groupby_ioreads;
      dst_node->groupby_stats.rows += src_node->groupby_stats.rows;
      if (timercmp (&src_node->groupby_stats.groupby_time, &dst_node->groupby_stats.groupby_time, >))
	{
	  dst_node->groupby_stats.groupby_time = src_node->groupby_stats.groupby_time;
	}

      /* set operation and hash join nodes have no spec_list */
      if (src_node->spec_list != nullptr && dst_node->spec_list != nullptr)
	{
	  if (timercmp (&src_node->spec_list->s_id.scan_stats.elapsed_scan, &dst_node->spec_list->s_id.scan_stats.elapsed_scan,
			>))
	    {
	      dst_node->spec_list->s_id.scan_stats.elapsed_scan = src_node->spec_list->s_id.scan_stats.elapsed_scan;
	    }
	  dst_node->spec_list->s_id.scan_stats.num_fetches += src_node->spec_list->s_id.scan_stats.num_fetches;
	  dst_node->spec_list->s_id.scan_stats.num_ioreads += src_node->spec_list->s_id.scan_stats.num_ioreads;
	  dst_node->spec_list->s_id.scan_stats.read_rows += src_node->spec_list->s_id.scan_stats.read_rows;
	  dst_node->spec_list->s_id.scan_stats.qualified_rows += src_node->spec_list->s_id.scan_stats.qualified_rows;
	  dst_node->spec_list->s_id.type = dst_node->spec_list->s_id.type == 0 ? src_node->spec_list->s_id.type :
					   dst_node->spec_list->s_id.type;
	  if (timercmp (&src_node->spec_list->s_id.scan_stats.elapsed_lookup,
			&dst_node->spec_list->s_id.scan_stats.elapsed_lookup, >))
	    {
	      dst_node->spec_list->s_id.scan_stats.elapsed_lookup = src_node->spec_list->s_id.scan_stats.elapsed_lookup;
	    }
	  dst_node->spec_list->s_id.scan_stats.read_keys += src_node->spec_list->s_id.scan_stats.read_keys;
	  dst_node->spec_list->s_id.scan_stats.qualified_keys += src_node->spec_list->s_id.scan_stats.qualified_keys;
	  dst_node->spec_list->s_id.scan_stats.key_qualified_rows += src_node->spec_list->s_id.scan_stats.key_qualified_rows;
	  dst_node->spec_list->s_id.scan_stats.data_qualified_rows += src_node->spec_list->s_id.scan_stats.data_qualified_rows;
	  dst_node->spec_list->s_id.scan_stats.index_skip_scan = dst_node->spec_list->s_id.scan_stats.index_skip_scan ? true :
	      src_node->spec_list->s_id.scan_stats.index_skip_scan;
	  dst_node->spec_list->s_id.scan_stats.loose_index_scan = dst_node->spec_list->s_id.scan_stats.loose_index_scan ? true :
	      src_node->spec_list->s_id.scan_stats.loose_index_scan;
	  dst_node->spec_list->s_id.scan_stats.noscan = dst_node->spec_list->s_id.scan_stats.noscan ? true :
	      src_node->spec_list->s_id.scan_stats.noscan;
	  dst_node->spec_list->s_id.scan_stats.min_max_only_scan = dst_node->spec_list->s_id.scan_stats.min_max_only_scan ? true :
	      src_node->spec_list->s_id.scan_stats.min_max_only_scan;
	  dst_node->spec_list->s_id.scan_stats.covered_index = dst_node->spec_list->s_id.scan_stats.covered_index ? true :
	      src_node->spec_list->s_id.scan_stats.covered_index;
	  dst_node->spec_list->s_id.scan_stats.multi_range_opt = dst_node->spec_list->s_id.scan_stats.multi_range_opt ? true :
	      src_node->spec_list->s_id.scan_stats.multi_range_opt;
	  /* agl copy needed..? */

	  /* partition stats copy needed */
	  if (XASL_IS_PER_OUTER_ROW (src_node) && src_node->spec_list->parts != nullptr
	      && dst_node->spec_list->parts != nullptr)
	    {
	      /* a clone walking the partitions per outer row (CBRD-27493, CBRD-27866) scans every partition itself, not
	       * only the one the main thread's scan block iteration opened, so each partition goes to the main thread's
	       * partition of the same class */
	      for (PARTITION_SPEC_TYPE *src_part = src_node->spec_list->parts; src_part != nullptr; src_part = src_part->next)
		{
		  for (PARTITION_SPEC_TYPE *dst_part = dst_node->spec_list->parts; dst_part != nullptr;
		       dst_part = dst_part->next)
		    {
		      if (OID_EQ (&src_part->oid, &dst_part->oid))
			{
			  xasl_merge_partition_stats (&dst_part->scan_stats, &src_part->scan_stats);
			  break;
			}
		    }
		}
	    }
	  else if (dst_node->spec_list->s_id.partition_stats && src_node->spec_list->s_id.partition_stats)
	    {
	      xasl_merge_partition_stats (dst_node->spec_list->s_id.partition_stats,
					  src_node->spec_list->s_id.partition_stats);
	    }
	}

      if (dst_node->sq_cache != nullptr && src_node->sq_cache != nullptr)
	{
	  dst_node->sq_cache->stats.hit += src_node->sq_cache->stats.hit;
	  dst_node->sq_cache->stats.miss += src_node->sq_cache->stats.miss;
	  dst_node->sq_cache->size += src_node->sq_cache->size;
	  dst_node->sq_cache->size_max += src_node->sq_cache->size_max;
	  dst_node->sq_cache->enabled = dst_node->sq_cache->enabled ? true : src_node->sq_cache->enabled;
	}

      if (dst_node->memoize_storage != nullptr && src_node->memoize_storage != nullptr)
	{
	  if (!dst_node->memoize_storage->is_disabled())
	    {
	      dst_node->memoize_storage->set_disabled_flag (src_node->memoize_storage->is_disabled());
	    }
	  dst_node->memoize_storage->hit += src_node->memoize_storage->hit;
	  dst_node->memoize_storage->miss += src_node->memoize_storage->miss;
	  dst_node->memoize_storage->add_size_for_merge_stats (src_node->memoize_storage->get_size_for_merge_stats ());
	}

      if (dst_node->memoize_storage != nullptr && src_node->memoize_storage == nullptr)
	{
	  clear_memoize_storage (nullptr, dst_node);
	}

      return true;
    };
    cubxasl::iterate_xasl_tree<bool> (src,merge_stats,true);
  }
}

