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
    private:
      void *val_descr_ptr;
      void *orig_val_descr_ptr;
      SCAN_ID *scan_id;
      std::unordered_map<void *, typed_memory> m_map;
      std::atomic<int> m_obj_cnt;
  };
  template<>
  void memory_mapper::clear_and_free<heap_cache_attrinfo> (heap_cache_attrinfo *ptr);
  template<>
  void memory_mapper::clear_and_free<PRED_EXPR> (PRED_EXPR *ptr);
  template<>
  void memory_mapper::clear_and_free<DB_VALUE> (DB_VALUE *ptr);
  template<>
  void memory_mapper::clear_and_free<ARITH_TYPE> (ARITH_TYPE *ptr);
  template<>
  void memory_mapper::clear_and_free<val_descr> (val_descr *ptr);
  template<>
  val_descr *memory_mapper::copy_and_map<val_descr> (val_descr *vd);
  template<>
  struct function_node *memory_mapper::copy_and_map<struct function_node> (struct function_node *func);
  template<>
  PRED_EXPR *memory_mapper::copy_and_map<PRED_EXPR> (PRED_EXPR *src);
  template<>
  PRED *memory_mapper::copy_and_map<PRED> (PRED *dest);
  template<>
  EVAL_TERM *memory_mapper::copy_and_map<EVAL_TERM> (EVAL_TERM *dest);
  template<>
  struct regu_variable_list_node *memory_mapper::copy_and_map<struct regu_variable_list_node>
  (struct regu_variable_list_node *src_list);
  template<>
  DB_VALUE *memory_mapper::copy_and_map<DB_VALUE> (DB_VALUE *src);
  template<>
  ARITH_TYPE *memory_mapper::copy_and_map<ARITH_TYPE> (ARITH_TYPE *src);
  template<>
  SP_TYPE *memory_mapper::copy_and_map<SP_TYPE> (SP_TYPE *src);
  template<>
  heap_cache_attrinfo *memory_mapper::copy_and_map<heap_cache_attrinfo> (heap_cache_attrinfo *src);
  template<>
  REGU_VARIABLE *memory_mapper::copy_and_map<REGU_VARIABLE> (REGU_VARIABLE *regu_var);

}
#endif
#endif /* _PARALLEL_HEAP_SCAN_MEMORY_MAPPER_HPP_ */