#include <unordered_map>
#include <atomic>

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
      ~memory_mapper() = default;

      memory_mapper (const memory_mapper &) = delete;
      memory_mapper &operator= (const memory_mapper &) = delete;
      memory_mapper (memory_mapper &&) = delete;
      memory_mapper &operator= (memory_mapper &&) = delete;

      template<typename T>
      T *copy_and_map (T *src);
      template<typename T>
      void clear_and_free (T *src);
    private:
      void *val_descr_ptr;
      std::unordered_map<void *, typed_memory> m_map;
      std::atomic<int> m_obj_cnt;
  };
}
