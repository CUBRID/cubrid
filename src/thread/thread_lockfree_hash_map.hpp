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

//
//  specialize lock-free hash map to thread manager and its entries
//

#ifndef _THREAD_LOCKFREE_HASH_MAP_HPP_
#define _THREAD_LOCKFREE_HASH_MAP_HPP_

#include "lock_free.h"  // old implementation
#include "lockfree_hashmap.hpp"
#include "lockfree_transaction_def.hpp"
#include "system_parameter.h"
#include "thread_entry.hpp"

namespace cubthread
{
  template <class Key, class T>
  class lockfree_hashmap
  {
    public:
      lockfree_hashmap ();

      class iterator;

      void init (lf_tran_system &transys, int entry_idx, int hash_size, int freelist_block_size,
		 int freelist_block_count, lf_entry_descriptor &edesc);

      void init_as_old (lf_tran_system &transys, int hash_size, int freelist_block_count, int freelist_block_size,
			lf_entry_descriptor &edesc, int entry_idx);
      void init_as_new (lockfree::tran::system &transys, size_t hash_size, size_t freelist_block_size,
			size_t freelist_block_count, lf_entry_descriptor &edesc);
      void destroy ();

      T *find (cubthread::entry *thread_p, Key &key);
      bool find_or_insert (cubthread::entry *thread_p, Key &key, T *&t);
      bool insert (cubthread::entry *thread_p, Key &key, T *&t);
      bool insert_given (cubthread::entry *thread_p, Key &key, T *&t);
      bool erase (cubthread::entry *thread_p, Key &key);
      bool erase_locked (cubthread::entry *thread_p, Key &key, T *&t);

      void unlock (cubthread::entry *thread_p, T *&t);

      void clear (cubthread::entry *thread_p);    // NOT LOCK-FREE

      T *freelist_claim (cubthread::entry *thread_p);
      void freelist_retire (cubthread::entry *thread_p, T *&t);

      void start_tran (cubthread::entry *thread_p);
      void end_tran (cubthread::entry *thread_p);

      size_t get_size () const;
      size_t get_element_count () const;
      size_t get_alloc_element_count () const;

    private:
      bool is_old_type () const;
      lf_tran_entry *get_tran_entry (cubthread::entry *thread_p);

      enum type
      {
	OLD,
	NEW,
	UNKNOWN
      };

      lf_hash_table_cpp<Key, T> m_old_hash;
      lockfree::hashmap<Key, T> m_new_hash;
      type m_type;
      int m_entry_idx;
  };

  template <class Key, class T>
  class lockfree_hashmap<Key, T>::iterator
  {
    public:
      iterator (cubthread::entry *thread_p, lockfree_hashmap &map);
      ~iterator () = default;

      T *iterate ();

      void restart ();

    private:
      // old stuff
      lockfree_hashmap &m_map;
      typename lf_hash_table_cpp<Key, T>::iterator m_old_iter;
      typename lockfree::hashmap<Key, T>::iterator m_new_iter;
  };

  lockfree::tran::system &get_thread_entry_lftransys ();
} // namespace cubthread

//
// implementation
//

namespace cubthread
{
  //
  //  lockfree_hashmap
  //
  template <class Key, class T>
  lockfree_hashmap<Key, T>::lockfree_hashmap ()
    : m_old_hash {}
    , m_new_hash {}
    , m_type (UNKNOWN)
  {
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::init (lf_tran_system &transys, int entry_idx, int hash_size, int freelist_block_size,
				  int freelist_block_count, lf_entry_descriptor &edesc)
  {
    if (prm_get_bool_value (PRM_ID_ENABLE_NEW_LFHASH))
      {
	init_as_new (get_thread_entry_lftransys (), (size_t) hash_size, (size_t) freelist_block_size,
		     (size_t) freelist_block_count, edesc);
      }
    else
      {
	init_as_old (transys, hash_size, freelist_block_count, freelist_block_size, edesc, entry_idx);
      }
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::init_as_old (lf_tran_system &transys, int hash_size, int freelist_block_count,
					 int freelist_block_size, lf_entry_descriptor &edesc, int entry_idx)
  {
    m_type = OLD;
    m_old_hash.init (transys, hash_size, freelist_block_count, freelist_block_size, edesc);
    m_entry_idx = entry_idx;
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::init_as_new (lockfree::tran::system &transys, size_t hash_size, size_t freelist_block_size,
					 size_t freelist_block_count, lf_entry_descriptor &edesc)
  {
    m_type = NEW;
    m_new_hash.init (transys, hash_size, freelist_block_size, freelist_block_count, edesc);
  }

#define lockfree_hashmap_forward_func(f_, tp_, ...) \
  is_old_type () ? \
  m_old_hash.f_ (get_tran_entry (tp_), __VA_ARGS__) : \
  m_new_hash.f_ ((tp_)->get_lf_tran_index (), __VA_ARGS__)
#define lockfree_hashmap_forward_func_noarg(f_, tp_) \
  is_old_type () ? \
  m_old_hash.f_ (get_tran_entry (tp_)) : \
  m_new_hash.f_ ((tp_)->get_lf_tran_index ())

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::destroy ()
  {
    if (m_type == UNKNOWN)
      {
	// was not initialized
	return;
      }
    if (is_old_type ())
      {
	m_old_hash.destroy ();
      }
    else
      {
	m_new_hash.destroy ();
      }
  }

  template <class Key, class T>
  T *
  lockfree_hashmap<Key, T>::find (cubthread::entry *thread_p, Key &key)
  {
    return lockfree_hashmap_forward_func (find, thread_p, key);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::find_or_insert (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return lockfree_hashmap_forward_func (find_or_insert, thread_p, key, t);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::insert (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return lockfree_hashmap_forward_func (insert, thread_p, key, t);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::insert_given (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return lockfree_hashmap_forward_func (insert_given, thread_p, key, t);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::erase (cubthread::entry *thread_p, Key &key)
  {
    return lockfree_hashmap_forward_func (erase, thread_p, key);
  }

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::erase_locked (cubthread::entry *thread_p, Key &key, T *&t)
  {
    return lockfree_hashmap_forward_func (erase_locked, thread_p, key, t);
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::unlock (cubthread::entry *thread_p, T *&t)
  {
    lockfree_hashmap_forward_func (unlock, thread_p, t);
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::clear (cubthread::entry *thread_p)
  {
    lockfree_hashmap_forward_func_noarg (clear, thread_p);
  }

  template <class Key, class T>
  T *
  lockfree_hashmap<Key, T>::freelist_claim (cubthread::entry *thread_p)
  {
    return lockfree_hashmap_forward_func_noarg (freelist_claim, thread_p);
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::freelist_retire (cubthread::entry *thread_p, T *&t)
  {
    lockfree_hashmap_forward_func (freelist_retire, thread_p, t);
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::start_tran (cubthread::entry *thread_p)
  {
    lockfree_hashmap_forward_func_noarg (start_tran, thread_p);
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::end_tran (cubthread::entry *thread_p)
  {
    lockfree_hashmap_forward_func_noarg (end_tran, thread_p);
  }

  template <class Key, class T>
  size_t
  lockfree_hashmap<Key, T>::get_size () const
  {
    return is_old_type () ? m_old_hash.get_size () : m_new_hash.get_size ();
  }

  template <class Key, class T>
  size_t
  lockfree_hashmap<Key, T>::get_element_count () const
  {
    return is_old_type () ? m_old_hash.get_element_count () : m_new_hash.get_element_count ();
  }

  template <class Key, class T>
  size_t
  lockfree_hashmap<Key, T>::get_alloc_element_count () const
  {
    return is_old_type () ? m_old_hash.get_alloc_element_count () : m_new_hash.get_alloc_element_count ();
  }

#undef lockfree_hashmap_forward_func
#undef lockfree_hashmap_forward_func_noarg

  template <class Key, class T>
  bool
  lockfree_hashmap<Key, T>::is_old_type () const
  {
    assert (m_type == OLD || m_type == NEW);
    return m_type == OLD;
  }

  template <class Key, class T>
  lf_tran_entry *
  lockfree_hashmap<Key, T>::get_tran_entry (cubthread::entry *thread_p)
  {
    return thread_p->tran_entries[m_entry_idx];
  }

  //
  // lockfree_hashmap::iterator
  //
  template <class Key, class T>
  lockfree_hashmap<Key, T>::iterator::iterator (cubthread::entry *thread_p, lockfree_hashmap &map)
    : m_map (map)
    , m_old_iter ()
    , m_new_iter ()
  {
    if (m_map.is_old_type ())
      {
	m_old_iter = { m_map.get_tran_entry (thread_p), m_map.m_old_hash };
      }
    else
      {
	m_new_iter = { thread_p->get_lf_tran_index (), m_map.m_new_hash };
      }
  }

  template <class Key, class T>
  T *
  lockfree_hashmap<Key, T>::iterator::iterate ()
  {
    if (m_map.is_old_type ())
      {
	return m_old_iter.iterate ();
      }
    else
      {
	return m_new_iter.iterate ();
      }
  }

  template <class Key, class T>
  void
  lockfree_hashmap<Key, T>::iterator::restart ()
  {
    if (m_map.is_old_type ())
      {
	m_old_iter.restart ();
      }
    else
      {
	m_new_iter.restart ();
      }
  }
}

#endif // !_THREAD_LOCKFREE_HASH_MAP_HPP_
