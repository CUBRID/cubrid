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
#include "xasl_predicate.hpp"
#include "tsc_timer.h"
#include <unordered_map>
#include "fixed_size_allocator.hpp"

namespace memoize
{
  constexpr size_t MEMOIZE_FREE_ITERATION_LIMIT = 1000;
  constexpr double MEMOIZE_HIT_RATIO_THRESHOLD = 0.5;

  template <typename T>
  using fixed_allocator = cubmem::fixed_size_alloc::allocator<T, false>;

  enum class result_code
  {
    SUCCESS = 0,
    ENDED = 1,
    NOT_FOUND = 2,
    FULL = 3,
    ERROR = 4,
  };

  class key
  {
    public:
      key ();
      ~key ();

      std::vector<DB_VALUE> m_values;
      size_t m_size;
      size_t get_size ();

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
      value ();
      ~value ();

      size_t get_size ();

      std::vector<DB_VALUE> m_values;
      size_t m_size;
  };

  constexpr size_t hash_entry_sz = sizeof (std::pair<key *const, value *>) + sizeof (void *)*3;

  class storage
  {
    public:
      storage (THREAD_ENTRY *thread_p, size_t max_storage_size, int key_cnt, int value_cnt, VAL_LIST *val_list);
      ~storage();
      static storage *new_storage (THREAD_ENTRY *thread_p, size_t max_storage_size, xasl_node *xasl);
      void init (std::vector<DB_VALUE *> &key_ptr_src);
      result_code get ();
      result_code put();
      void start_timer();
      void stop_timer();
      result_code put_nullptr();
      void set_key_changed()
      {
	key_changed = true;
	current_key_joined = false;
      }
      size_t get_current_size () const;
      bool is_disabled () const
      {
	return disabled;
      }
      size_t hit;
      size_t miss;
      struct timeval m_elapsed_time;
    private:
      key *get_key();
      value *get_value();
      result_code set_value (value *value);

      const size_t m_max_storage_size;
      const int m_key_cnt;
      const int m_value_cnt;
      THREAD_ENTRY *m_thread_p;
      VAL_LIST *m_val_list;

      fixed_allocator<key> m_key_fixed_allocator;
      size_t m_key_sz;
      size_t m_value_sz;
      size_t m_hash_sz;
      key *m_last_key;
      std::vector<DB_VALUE *> m_keyptr_src;
      std::unordered_multimap<key *, value *, key::hash, key::equal> m_key_value_map;
      std::vector<value *> m_current_value_list;
      bool disabled;
      bool has_range;
      bool key_changed;
      bool current_key_joined;
      TSC_TICKS m_start_tick;
  };
}

extern "C"
{
  int new_memoize_storage (THREAD_ENTRY *thread_p, xasl_node *xasl);
  void clear_memoize_storage (THREAD_ENTRY *thread_p, xasl_node *xasl);
  int memoize_get (THREAD_ENTRY *thread_p, xasl_node *xasl, bool *success, bool *is_ended);
  int memoize_put (THREAD_ENTRY *thread_p, xasl_node *xasl, bool *success);
  int memoize_put_nullptr (THREAD_ENTRY *thread_p, xasl_node *xasl, bool *success);
}

#endif /* _MEMOIZE_HPP_ */
