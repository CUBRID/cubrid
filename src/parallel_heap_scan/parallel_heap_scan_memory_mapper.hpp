#if defined (SERVER_MODE)

#ifndef _PARALLEL_HEAP_SCAN_MEMORY_MAPPER_HPP_
#define _PARALLEL_HEAP_SCAN_MEMORY_MAPPER_HPP_

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
      };

      struct typed_memory
      {
	public:
	  Type type;
	  void *ptr;
      };

      memory_mapper() = default;
      ~memory_mapper();

      memory_mapper (const memory_mapper &) = delete;
      memory_mapper &operator= (const memory_mapper &) = delete;
      memory_mapper (memory_mapper &&) = delete;
      memory_mapper &operator= (memory_mapper &&) = delete;

      memory_mapper (SCAN_ID *scan_id);
      SCAN_ID *get_scan_id() const
      {
	return scan_id;
      }
      template<typename T>
      T *copy_and_map (T *src);
      template<typename T>
      void clear_and_free (T *src);

      // 특수화된 템플릿 함수들의 선언
      void clear_and_free (heap_cache_attrinfo *ptr);
      void clear_and_free (PRED_EXPR *ptr);
      void clear_and_free (DB_VALUE *ptr);
      void clear_and_free (ARITH_TYPE *ptr);
      void clear_and_free (val_descr *ptr);

      val_descr *copy_and_map (val_descr *vd);
      struct function_node *copy_and_map (struct function_node *func);
      PRED_EXPR *copy_and_map (PRED_EXPR *src);
      PRED *copy_and_map (PRED *dest);
      EVAL_TERM *copy_and_map (EVAL_TERM *dest);
      struct regu_variable_list_node *copy_and_map (struct regu_variable_list_node *src_list);
      DB_VALUE *copy_and_map (DB_VALUE *src);
      ARITH_TYPE *copy_and_map (ARITH_TYPE *src);
      SP_TYPE *copy_and_map (SP_TYPE *src);
      heap_cache_attrinfo *copy_and_map (heap_cache_attrinfo *src);
      REGU_VARIABLE *copy_and_map (REGU_VARIABLE *regu_var);

    private:
      void *val_descr_ptr;
      SCAN_ID *scan_id;
      std::unordered_map<void *, typed_memory> m_map;
      std::atomic<int> m_obj_cnt;
  };
}
#endif
#endif /* _PARALLEL_HEAP_SCAN_MEMORY_MAPPER_HPP_ */