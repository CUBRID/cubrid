/*
 *
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
 * px_heap_scan_memory_mapper.hpp - module created to perform deep copying of
 * heap scan-related information from the XASL structure.
 */

#ifndef _PX_HEAP_SCAN_MEMORY_MAPPER_HPP_
#define _PX_HEAP_SCAN_MEMORY_MAPPER_HPP_

#include <unordered_map>
#include <atomic>
#include "scan_manager.h"
#include "xasl_predicate.hpp"

namespace parallel_heap_scan
{
  class memory_mapper
  {
    public:
      enum class Type
      {
	REGU_VARIABLE,
	VAL_DESCR,
	PRED_EXPR,
	REGU_VARIABLE_LIST,
	DB_VALUE,
	ARITH_TYPE,
	HEAP_CACHE_ATTRINFO,
	FUNCTION_NODE,
	SP_TYPE,
	OUTPTR_LIST,
      };

      struct typed_memory
      {
	public:
	  Type type;
	  void *ptr;
      };

      struct px_stats
      {
	struct timeval elapsed_scan;
	struct timeval elapsed_page_lock;
	struct timeval elapsed_enqueue;
      } stats;

      memory_mapper() = default;
      ~memory_mapper();

      memory_mapper (const memory_mapper &) = delete;
      memory_mapper &operator= (const memory_mapper &) = delete;
      memory_mapper (memory_mapper &&) = delete;
      memory_mapper &operator= (memory_mapper &&) = delete;

      memory_mapper (SCAN_ID *scan_id, OUTPTR_LIST *outptr_list);
      SCAN_ID *get_scan_id() const
      {
	return scan_id;
      }
      OUTPTR_LIST *get_outptr_list() const
      {
	return m_outptr_list;
      }
      template<typename T>
      T *copy_and_map (T *src)
      {
	assert (false);
	return nullptr;
      }
      template<typename T>
      void clear_and_free (T *src)
      {
	free (src);
	m_obj_cnt--;
      }

      void clear_and_free (heap_cache_attrinfo *ptr);
      void clear_and_free (PRED_EXPR *ptr);
      void clear_and_free (DB_VALUE *ptr);
      void clear_and_free (ARITH_TYPE *ptr);
      void clear_and_free (val_descr *ptr);

      val_descr *copy_and_map (val_descr *vd);
      FUNCTION_TYPE *copy_and_map (FUNCTION_TYPE *func);
      PRED_EXPR *copy_and_map (PRED_EXPR *src);
      PRED *copy_and_map (PRED *dest);
      EVAL_TERM *copy_and_map (EVAL_TERM *dest);
      regu_variable_list_node *copy_and_map (regu_variable_list_node *src_list);
      DB_VALUE *copy_and_map (DB_VALUE *src);
      ARITH_TYPE *copy_and_map (ARITH_TYPE *src);
      SP_TYPE *copy_and_map (SP_TYPE *src);
      heap_cache_attrinfo *copy_and_map (heap_cache_attrinfo *src);
      REGU_VARIABLE *copy_and_map (REGU_VARIABLE *regu_var);
      OUTPTR_LIST *copy_and_map (OUTPTR_LIST *src);

      bool add_resolved_dbval_all();

      void set_all_regu_var_domain_refer_to_clone();

    private:
      void *val_descr_ptr;
      void *orig_val_descr_ptr;
      SCAN_ID *scan_id;
      std::unordered_map<void *, typed_memory> m_map;
      std::unordered_map<void *, DB_VALUE *> m_resolved_dbval_map;
      std::atomic<int> m_obj_cnt;
      OUTPTR_LIST *m_outptr_list;
  };
}
#endif /*_PX_HEAP_SCAN_MEMORY_MAPPER_HPP_ */
