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

#ifndef _MEMOIZE_HPP_
#define _MEMOIZE_HPP_

#include "thread_compat.hpp"
#include "memory_private_allocator.hpp"
#include "xasl.h"
#include <unordered_map>

namespace memoize
{

  template <typename T>
  class allocator : public cubmem::private_allocator<T>
  {
    public:
      typedef T *pointer;
      typedef size_t size_type;

      // rebind template to ensure proper allocator type
      template <typename U>
      struct rebind
      {
	typedef allocator<U> other;
      };

      allocator (cubthread::entry *thread_p)
	: cubmem::private_allocator<T> (thread_p)
      {
	m_size = 0;
      }

      // copy constructor
      allocator (const allocator &other)
	: cubmem::private_allocator<T> (other)
	, m_size (other.m_size)
      {
      }

      // conversion constructor from other allocator types
      template <typename U>
      allocator (const allocator<U> &other)
	: cubmem::private_allocator<T> (other)
	, m_size (other.get_size())
      {
      }

      pointer allocate (size_type count)
      {
	pointer p = cubmem::private_allocator<T>::allocate (count);
	m_size += count*sizeof (T);
	return p;
      }
      size_t get_size() const
      {
	return m_size;
      }
    private:
      size_t m_size;
  };

  template <typename T>
  using pvector = std::vector<T, allocator<T>>;
  enum class result_code
  {
    SUCCESS = 0,
    ENDED = 1,
    FAIL = 2,
    ERROR = 3,
  };

  class key
  {
    public:
      key () = delete;
      key (allocator<DB_VALUE> *allocator_p);
      ~key ();

      pvector<DB_VALUE> m_values;
      allocator<DB_VALUE> *m_allocator_p;

      struct hash
      {
	size_t operator() (const key *k) const;
      };
      struct equal
      {
	bool operator() (const key *k1, const key *k2) const;
      };
  };

  struct value
  {
    public:
      value () = delete;
      value (allocator<DB_VALUE> *allocator_p);
      ~value ();

      pvector<DB_VALUE> m_values;
      allocator<DB_VALUE> *m_allocator_p;
  };

  using iter = std::unordered_multimap<
	       key *,
	       value *,
	       key::hash,
	       key::equal,
	       cubmem::private_allocator<std::pair<key *const, value *>>
	       >::iterator;

  class storage
  {
    public:
      storage (THREAD_ENTRY *thread_p, size_t max_storage_size, int key_cnt, int value_cnt, VAL_LIST *val_list);
      ~storage();
      static storage *new_storage (THREAD_ENTRY *thread_p, size_t max_storage_size, ACCESS_SPEC_TYPE *spec,
				   VAL_LIST *val_list);
      void init (ACCESS_SPEC_TYPE *spec);
      result_code get ();
      result_code put();
      void set_key_changed()
      {
	key_changed = true;
      }
      size_t get_current_size () const;
      bool is_disabled () const
      {
	return disabled;
      }
    private:
      key *get_key();
      value *get_value();
      result_code set_value (value *value);


      const size_t m_max_storage_size;
      const int m_key_cnt;
      const int m_value_cnt;
      THREAD_ENTRY *m_thread_p;
      VAL_LIST *m_val_list;

      allocator<DB_VALUE *> m_dbval_p_allocator;
      allocator<DB_VALUE> m_dbval_allocator;
      allocator<std::pair<key *const, value *>> m_key_value_allocator;
      size_t m_key_sz;
      size_t m_value_sz;
      key *m_last_key;
      pvector<DB_VALUE *> m_keyptr_src;
      std::unordered_multimap<key *, value *, key::hash, key::equal, cubmem::private_allocator<std::pair<key *const, value *>>>
      m_key_value_map;
      bool disabled;
      iter cur_iter;
      iter cur_end;
      bool has_range;
      bool key_changed;
  };
}

extern "C"
{
  int new_memoize_storage (THREAD_ENTRY *thread_p, xasl_node *xasl);
  void clear_memoize_storage (THREAD_ENTRY *thread_p, xasl_node *xasl);
  int memoize_get (xasl_node *xasl, bool *success, bool *is_ended);
  int memoize_put (xasl_node *xasl, bool *success);
}

#endif /* _MEMOIZE_HPP_ */
