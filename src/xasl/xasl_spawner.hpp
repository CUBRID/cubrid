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
 * xasl_spawner.hpp
 */

#pragma once

#include <unordered_map>

#include "dbtype_def.h"
#include "heap_attrinfo.h"
#include "query_list.h"
#include "regu_var.hpp"
#include "xasl_predicate.hpp"
#include "xasl_sp.hpp"

/*
 * Forward Declarations
 */

struct qproc_db_value_list;
struct val_descr;
struct val_list_node;

typedef struct qproc_db_value_list *QPROC_DB_VALUE_LIST;
typedef struct val_descr VAL_DESCR;
typedef struct val_list_node VAL_LIST;

/*
 * Class Definitions
 */

namespace cubxasl
{
  class spawner
  {
    public:
      spawner (cubthread::entry &main_thread_ref);
      ~spawner ();

      PRED_EXPR *spawn (const PRED_EXPR *pred_expr);

      int spawn (const PRED *src, PRED *dest);
      int spawn (const EVAL_TERM *src, EVAL_TERM *dest);

      int spawn (const COMP_EVAL_TERM *src, COMP_EVAL_TERM *dest);
      int spawn (const ALSM_EVAL_TERM *src, ALSM_EVAL_TERM *dest);
      int spawn (const LIKE_EVAL_TERM *src, LIKE_EVAL_TERM *dest);

      int spawn (const RLIKE_EVAL_TERM *src, RLIKE_EVAL_TERM *dest);
      cub_compiled_regex *spawn (const cub_compiled_regex *src);

      REGU_VARIABLE *spawn (const REGU_VARIABLE *src);
      int spawn (const REGU_VARIABLE *src, REGU_VARIABLE *dest);

      DB_VALUE *spawn (const DB_VALUE *src);

      ARITH_TYPE *spawn (const ARITH_TYPE *src);
      struct drand48_data *spawn (const struct drand48_data *src);

      int spawn (const ATTR_DESCR *src, ATTR_DESCR *dest);
      HEAP_CACHE_ATTRINFO *spawn (const HEAP_CACHE_ATTRINFO *src);
      OR_CLASSREP *spawn (const OR_CLASSREP *src);	/* TODO: unsupported */
      int spawn (const HEAP_ATTRVALUE *src, HEAP_ATTRVALUE *dest);
      OR_ATTRIBUTE *spawn (const OR_ATTRIBUTE *src);	/* TODO: unsupported */

      int spawn (const QFILE_TUPLE_VALUE_POSITION *src, QFILE_TUPLE_VALUE_POSITION *dest);

      QFILE_SORTED_LIST_ID *spawn (const QFILE_SORTED_LIST_ID *src);
      QFILE_LIST_ID *spawn (const QFILE_LIST_ID *src);	/* TODO: unsupported */

      FUNCTION_TYPE *spawn (const FUNCTION_TYPE *src);
      function_tmp_obj *spawn (const function_tmp_obj *src);	/* TODO: unsupported */

      REGU_VALUE_LIST *spawn (const REGU_VALUE_LIST *src);
      REGU_VALUE_ITEM *spawn (const REGU_VALUE_ITEM *src);

      REGU_VARIABLE_LIST spawn (const REGU_VARIABLE_LIST src);

      SP_TYPE *spawn (const SP_TYPE *src);
      PL_SIGNATURE_TYPE *spawn (const PL_SIGNATURE_TYPE *src);	/* TODO: unsupported */

      /* (XASL_NODE *)->val_list */
      VAL_LIST *spawn (const VAL_LIST *src);
      int spawn (const QPROC_DB_VALUE_LIST src, QPROC_DB_VALUE_LIST dest);

      /* (XASL_STATE *)->vd */
      VAL_DESCR *spawn (const VAL_DESCR *src);

      template <typename T>
      T *
      find (const T *src);

      template <typename T>
      T *
      find (const T *src, int count);

      template <typename T>
      T *alloc (const T *src);

      template <typename T>
      T *alloc (const T *src, int count);

    private:
      struct cached_entry
      {
	void *ptr = nullptr;
	int count = 0;
	void (*deleter) (cubthread::entry *thread_p, void *ptr, int count) = nullptr;
      };

      cubthread::entry &m_thread_ref;
      std::unordered_map<const void *, cached_entry> m_cached_ptrs;

      template <typename T>
      static void cached_entry_deleter (cubthread::entry *thread_p, void *ptr, int count);

      bool is_valid_argument (const void *src, const void *dest);
  };
} /* namespace cubxasl */

/*
 * Function Definitions
 */

namespace cubxasl
{
  template <typename T>
  T *
  spawner::find (const T *src)
  {
    return spawner::find (src, 1);
  }

  template <typename T>
  T *
  spawner::find (const T *src, int count)
  {
    if (src == nullptr)
      {
	return nullptr;
      }

    auto old_it = m_cached_ptrs.find (src);
    if (old_it != m_cached_ptrs.end())
      {
	assert_release_error (old_it->second.count == count);
	return static_cast<T *> (old_it->second.ptr);
      }

    return nullptr;
  }

  template <typename T>
  T *
  spawner::alloc (const T *src)
  {
    return spawner::alloc (src, 1);
  }

  template <typename T>
  T *
  spawner::alloc (const T *src, int count)
  {
    int init_cnt = 0;

    if (src == nullptr)
      {
	return nullptr;
      }

    T *dest = static_cast<T *> (db_private_alloc (&m_thread_ref, count * sizeof (T)));
    if (dest == nullptr )
      {
	ASSERT_ERROR ();
	return nullptr;
      }

    for (int i = 0; i < count; i++)
      {
	T *item = dest + i;

	try
	  {
	    /* placement new */
	    new (item) T();

	    ++init_cnt;

	    cached_entry entry;
	    entry.ptr = item;
	    entry.count = count;
	    entry.deleter = (i == 0) ? &cached_entry_deleter<T> : nullptr;

	    auto [new_it, inserted] = m_cached_ptrs.try_emplace (src + i, std::move (entry));
	    if (!inserted)
	      {
		/* impossible case */
		assert_release_error (false);
		throw std::runtime_error ("spawner::alloc failed");
	      }
	  }
	catch (...)
	  {
	    auto it  = m_cached_ptrs.find (src);
	    if (it != m_cached_ptrs.end() && it->second.deleter != nullptr)
	      {
		if (it->second.deleter != nullptr)
		  {
		    it->second.deleter (&m_thread_ref, it->second.ptr, init_cnt);
		  }
		m_cached_ptrs.erase (it);
	      }

	    return nullptr;
	  }
      }

    ASSERT_NO_ERROR_OR_INTERRUPTED ();

    return dest;
  }

  template <typename T>
  inline void
  spawner::cached_entry_deleter (cubthread::entry *thread_p, void *ptr, int count)
  {
    if (ptr == nullptr)
      {
	return;
      }

    T *typed_ptr = static_cast<T *> (ptr);

    /*
     * When the spawner is extended to support a new type T:
     *   - Call pr_clear_value for all non-pointer DB_VALUE members
     *   - Refer to REGU_VARIABLE and DB_VALUE cases as examples
     */
    if constexpr (std::is_same_v<T, REGU_VARIABLE>)
      {
	if (typed_ptr->type == TYPE_DBVAL)
	  {
	    for (int i = 0; i < count; i++)
	      {
		pr_clear_value (&typed_ptr[i].value.dbval);
	      }
	  }
      }
    else if constexpr (std::is_same_v<T, DB_VALUE>)
      {
	for (int i = 0; i < count; i++)
	  {
	    pr_clear_value (&typed_ptr[i]);
	  }
      }
    else
      {
	/* fall through */
      }

    for (int i = 0; i < count; i++)
      {
	typed_ptr[i].~T();
      }

    db_private_free_and_init (thread_p, typed_ptr);
  }
} /* namespace cubxasl */
